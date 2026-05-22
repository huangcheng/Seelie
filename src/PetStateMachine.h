#ifndef PETSTATEMACHINE_H
#define PETSTATEMACHINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QPoint>
#include <QMap>
#include <QTimer>
#include <QJsonObject>

class CharacterPack;

/**
 * @brief Owns "what is the pet doing right now."
 *
 * Seven abstract states map gateway events onto an animation chain that all
 * pack types resolve into their own animations. Sustained states (Working,
 * Thinking, Reviewing) hold across event bursts via grace timers; one-shot
 * states (Greeting, Failed, Celebrating) play once and return to the saved
 * sustained state. Walking is an overlay (not a state) driven by position
 * deltas, layered on top of whichever state is current.
 */
class PetStateMachine : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Greeting,
        Thinking,
        Working,
        Reviewing,
        Failed,
        Celebrating,
    };
    Q_ENUM(State)

    enum class WalkDir { None, Left, Right };
    Q_ENUM(WalkDir)

    enum Priority {
        HighPriority,
        NormalPriority,
    };

    explicit PetStateMachine(QObject *parent = nullptr);

    State baseState() const { return m_baseState; }
    State activeState() const;  // overlay if set, else baseState

public slots:
    void onCanonicalEvent(const QString &eventName, const QJsonObject &payload = {});
    void onSyntheticEvent(const QString &eventName);
    void onPositionChanged(const QPoint &oldPos, const QPoint &newPos, bool isUserDrag);
    void onActivePackChanged(const CharacterPack *pack);

    /// Test seam: rebuild chains directly from a (canonical → pack-actual)
    /// name map, the same shape as CharacterPack::nameMap().
    void rebuildChainsFromNameMap(const QMap<QString, QString> &nameMap);

    /// Test seam / production entry: `stateMap` (if non-empty) supplies the
    /// pack-author override for each State; `nameMap` provides canonical-name
    /// fallbacks as in rebuildChainsFromNameMap. Engine defaults are appended
    /// last so a partial override still degrades cleanly.
    void rebuildChainsFromMaps(const QMap<QString, QStringList> &stateMap,
                               const QMap<QString, QString> &nameMap);

signals:
    void animationRequested(const QStringList &chain, int priority);
    void stateChanged(PetStateMachine::State newState);

private slots:
    void onWorkingGraceExpired();
    void onThinkingTimeout();
    void onReviewingTimeout();
    void onOneShotFinished();
    /// Fires WALK_IDLE_MS after the last position change. Clears m_walking
    /// and re-emits the base state's chain so the walking overlay isn't
    /// visible forever after motion stops. M10.
    void onWalkIdleExpired();

private:
    void enterBase(State s, Priority priority);
    void enterOneShot(State s, int durationMs);
    void emitChainFor(State s, Priority priority);
    void rebuildChainsFromPack(const CharacterPack *pack);
    QStringList resolveChain(State s) const;

    static QString stateName(State s);

    State m_baseState = State::Idle;
    State m_overlayState = State::Idle;     // Idle == "no overlay"
    State m_savedSustained = State::Idle;
    bool m_walking = false;
    WalkDir m_walkDir = WalkDir::None;

    QTimer m_workingGrace;
    QTimer m_thinkingTimeout;
    QTimer m_reviewingTimeout;
    QTimer m_oneShotTimer;
    QTimer m_walkIdleTimer;     // resets m_walking when motion stops

    QMap<State, QStringList> m_chains;
    QMap<State, QStringList> m_engineDefaultChains;  // M6: immutable defaults
    QString m_idleFallback = QStringLiteral("idle");

    static constexpr int WORKING_GRACE_MS = 1500;
    static constexpr int THINKING_TIMEOUT_MS = 15000;
    static constexpr int REVIEWING_TIMEOUT_MS = 60000;
    static constexpr int NOTIFICATION_ONESHOT_MS = 2000;
    static constexpr int WALK_DELTA_THRESHOLD_PX = 32;
    static constexpr int WALK_IDLE_MS = 500;
};

#endif // PETSTATEMACHINE_H
