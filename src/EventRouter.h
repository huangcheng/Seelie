#ifndef EVENTROUTER_H
#define EVENTROUTER_H

#include <QHash>
#include <QObject>
#include <QSet>

class TipWidget;
class TipsEngine;
class MemoryManager;

struct EventStats {
    int total = 0;
    QHash<QString, int> perEvent;
    qint64 lastEventMs   = 0;
    QString lastEventName;
};

/**
 * Validates incoming canonical events, fires tip-text bubbles, and emits
 * `eventProcessed` for downstream consumers (PetStateMachine). It no longer
 * dispatches animations directly — that responsibility moved to the FSM.
 */
class EventRouter : public QObject
{
    Q_OBJECT

public:
    explicit EventRouter(QObject *parent = nullptr);

    void setTipWidget(TipWidget *bubble) { m_tipWidget = bubble; }
    void setTipsEngine(TipsEngine *tips) { m_tips = tips; }
    void setMemoryManager(MemoryManager *mm) { m_memory = mm; }

    EventStats stats() const { return m_stats; }

    void loadStats(const QString &configDir);
    void saveStats(const QString &configDir);

public slots:
    void routeEvent(const QJsonObject &event);
    void retranslateUi() {}  // no-op — kept for signal-slot compatibility

signals:
    /** Emitted after any canonical event is validated and tip-routed. */
    void eventProcessed(const QString &eventName, const QJsonObject &payload);

private:
    bool validateEvent(const QJsonObject &event) const;

    TipWidget *m_tipWidget = nullptr;
    TipsEngine *m_tips = nullptr;
    MemoryManager *m_memory = nullptr;

    EventStats m_stats;

    static const QSet<QString> s_validEvents;
    static const QSet<QString> s_validSources;
};

#endif // EVENTROUTER_H
