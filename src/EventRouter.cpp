#include "EventRouter.h"
#include "CanonicalEvents.h"
#include "MemoryManager.h"
#include "StatisticsPersistence.h"
#include "TipWidget.h"
#include "TipsEngine.h"
#include "TipsCatalog.h"

#include <QDateTime>
#include <QJsonObject>
#include <QDebug>

namespace CE = CanonicalEvents;

const QSet<QString> EventRouter::s_validEvents = {
    CE::SessionStart, CE::SessionEnd, CE::SessionIdle, CE::SessionError,
    CE::PromptSubmitted,
    CE::ToolBefore, CE::ToolAfter, CE::ToolFailed,
    CE::PermissionRequested, CE::PermissionDenied, CE::PermissionResponse,
    CE::SubagentStarted, CE::SubagentStopped,
    CE::NotificationSent,
    CE::FileEdited, CE::FileWatched,
    CE::TodoUpdated,
    // ContextSenses (Spec 2): synthetic events from SystemContextEngine.
    // source "system" needs no s_validSources entry — unknown sources are
    // accepted with a qDebug, unknown events are rejected.
    CE::ContextLateNight, CE::ContextLongSession,
    CE::ContextIdle, CE::ContextAway,
    CE::ContextGaming, CE::ContextLowBattery,
    CE::ContextTimeOfDay
};

const QSet<QString> EventRouter::s_validSources = {
    "opencode", "claude-code", "codex"
};

EventRouter::EventRouter(QObject *parent) : QObject(parent) {}

void EventRouter::routeEvent(const QJsonObject &event)
{
    if (!validateEvent(event)) return;

    const QString eventName = event.value("event").toString();

    // --- Stats bookkeeping ---------------------------------------------------
    ++m_stats.total;
    m_stats.perEvent[eventName] += 1;
    m_stats.lastEventMs   = QDateTime::currentMSecsSinceEpoch();
    m_stats.lastEventName = eventName;
    if (m_memory) {
        m_memory->increment(QStringLiteral("stats.events.total"));
        m_memory->increment(QStringLiteral("stats.events.") + eventName);
    }
    // -------------------------------------------------------------------------

    const QString source = event.value("source").toString();
    const QString session = event.value("session").toString();
    const QString sourceLabel = session.isEmpty() ? source : source + " · " + session;

    if (m_tips) {
        m_tips->processEvent(eventName, event);
    }

    emit eventProcessed(eventName, event);

    const TipsCatalog::Tip tip = TipsCatalog::instance().eventTip(eventName);
    if (!tip.title.isEmpty() && m_tipWidget) {
        m_tipWidget->showBubble(tip.title, tip.body, TipWidget::TipBubble, sourceLabel);
    }
}

bool EventRouter::validateEvent(const QJsonObject &event) const
{
    if (event.value("type").toString() != "event") {
        qWarning() << "EventRouter: Not an event message";
        return false;
    }
    const QString source = event.value("source").toString();
    if (source.isEmpty()) {
        qWarning() << "EventRouter: Missing source field";
        return false;
    }
    if (!s_validSources.contains(source)) {
        qDebug() << "EventRouter: Unknown source (accepted):" << source;
    }
    const QString eventName = event.value("event").toString();
    if (!s_validEvents.contains(eventName)) {
        qWarning() << "EventRouter: Unknown event name:" << eventName;
        return false;
    }
    return true;
}

void EventRouter::loadStats(const QString &configDir)
{
    StatisticsPersistence sp(configDir);
    QJsonObject section = sp.loadSection("events");

    if (section.isEmpty()) return;

    m_stats.total = section.value("total").toInt(0);
    m_stats.lastEventMs = static_cast<qint64>(section.value("lastEventMs").toVariant().toLongLong());
    m_stats.lastEventName = section.value("lastEventName").toString();

    const QJsonObject perEvent = section.value("perEvent").toObject();
    for (auto it = perEvent.constBegin(); it != perEvent.constEnd(); ++it) {
        m_stats.perEvent.insert(it.key(), it.value().toInt(0));
    }
}

void EventRouter::saveStats(const QString &configDir)
{
    StatisticsPersistence sp(configDir);

    QJsonObject section;
    section["total"] = m_stats.total;
    section["lastEventMs"] = QJsonValue(static_cast<qint64>(m_stats.lastEventMs));
    section["lastEventName"] = m_stats.lastEventName;

    QJsonObject perEvent;
    for (auto it = m_stats.perEvent.constBegin(); it != m_stats.perEvent.constEnd(); ++it) {
        perEvent[it.key()] = it.value();
    }
    section["perEvent"] = QJsonValue(perEvent);

    sp.saveSection("events", section);
}
