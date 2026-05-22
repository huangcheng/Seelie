#ifndef TIPSENGINE_H
#define TIPSENGINE_H

#include <QObject>
#include <QVector>
#include <QDateTime>
#include <QMap>
#include <QJsonObject>

class TipWidget;

/**
 * Pattern-matches recent events into contextual tips ("Working on tests?",
 * "Permission issues?") with a 5-minute per-pattern cooldown.
 *
 * Threading: processEvent() is invoked from the main GUI thread only (via
 * EventRouter, which runs on the IPC server's queued connection to the
 * main thread). Not re-entrant — a slot in the same thread that triggers
 * another processEvent() would corrupt m_eventWindow. Document via main-
 * thread-only invariant rather than guarding because the alternative
 * (nested event loops) is the kind of thing we'd want to crash loudly
 * on, not silently tolerate. L5.
 */
class TipsEngine : public QObject
{
    Q_OBJECT

public:
    explicit TipsEngine(QObject *parent = nullptr);

    void setTipWidget(TipWidget *bubble) { m_tipWidget = bubble; }

public slots:
    void processEvent(const QString &eventName, const QJsonObject &eventData);
    void retranslateUi();

signals:
    /// Emitted when a matched pattern wants to play a named animation.
    /// MainWindow fans this out across Live2D / Lottie / Sprite engines —
    /// TipsEngine itself stays engine-agnostic. Was previously a direct
    /// call into SpriteAnimationEngine which silently no-op'd for Lottie
    /// and Live2D packs (audit H1).
    void animationRequested(const QString &animationName);

private:
    struct PatternMatcher {
        QString name;
        std::function<bool(const QVector<QPair<QString, QDateTime>>&)> matcher;
        QString tipTitle;
        QString tipBody;
        QString animation;
    };

    void initMatchers();
    bool isInCooldown(const QString &patternName) const;

    struct EventEntry {
        QString name;
        QDateTime timestamp;
        QJsonObject data;
    };

    QVector<EventEntry> m_eventWindow;
    int m_windowSize = 20;
    int m_windowDurationSec = 30;

    QMap<QString, qint64> m_lastTriggered;  // M4: monotonic msecs (QDateTime::currentMSecsSinceEpoch)
    int m_cooldownMinMs = 5 * 60 * 1000; // 5 minutes

    QVector<PatternMatcher> m_matchers;

    TipWidget *m_tipWidget = nullptr;
};

#endif // TIPSENGINE_H
