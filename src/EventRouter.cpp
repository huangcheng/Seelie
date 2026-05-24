#include "EventRouter.h"
#include "CanonicalEvents.h"
#include "MemoryManager.h"
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
    CE::TodoUpdated
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
