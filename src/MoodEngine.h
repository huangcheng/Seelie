#ifndef MOODENGINE_H
#define MOODENGINE_H

#include <QHash>
#include <QObject>
#include <QJsonObject>
#include <QVector>
#include <functional>

class EventRouter;
class MemoryManager;
class QTimer;

/**
 * @brief Owns the pet's ephemeral mood: a valence/energy vector that
 * quantizes into discrete tiers (Content/Excited/Tense/Tired/Lonely).
 *
 * Relationship state (affection, bondLevel, milestones) deliberately lives
 * in MemoryManager — this engine reads it, never duplicates it.
 * Proactive behaviors emit synthetic mood.* events through EventRouter,
 * mirroring SystemContextEngine::emitContext.
 */
class MoodEngine : public QObject
{
    Q_OBJECT

public:
    enum class Tier { Content, Excited, Tense, Tired, Lonely };
    Q_ENUM(Tier)

    explicit MoodEngine(EventRouter *router, MemoryManager *memory,
                        QObject *parent = nullptr);

    Tier tier() const { return m_tier; }
    double valence() const { return m_valence; }
    double energy() const { return m_energy; }
    bool isLonely() const { return m_lonely; }

    /// Stage band over MemoryManager::bondLevel(): L0-1 Stranger,
    /// L2-3 Companion, L4-5 Partner. Null memory → Stranger.
    QString stageName() const;

    static QString tierName(Tier t);   // "content"/"excited"/"tense"/"tired"/"lonely"

    void start();
    void stop();

    /// Test seam: injectable clock (mirrors SystemContextEngine).
    void setNowFn(std::function<qint64()> fn) { m_nowFn = fn; }
    /// Test seam: run one decay/proactive tick synchronously.
    void tickForTest() { tick(); }
    /// Test seam: set the vector directly (applies tier re-quantization).
    void setVectorForTest(double v, double e);

    void loadStats(const QString &configDir);
    void saveStats(const QString &configDir);

public slots:
    void onEventProcessed(const QString &eventName, const QJsonObject &payload);
    void onBondLevelChanged(int newLevel);

signals:
    void moodTierChanged(MoodEngine::Tier tier);

private:
    void tick();
    void applyDelta(double dV, double dE);
    Tier quantize() const;
    void updateTier();
    void checkProactive();
    bool emitMood(const QString &name, const QJsonObject &payload = {});
    qint64 nowMs() const;
    double energyBaseline() const;

    EventRouter *m_router = nullptr;
    MemoryManager *m_memory = nullptr;

    double m_valence = 0.0;
    double m_energy = 0.0;
    Tier m_tier = Tier::Content;
    bool m_lonely = false;

    QTimer *m_tickTimer = nullptr;
    qint64 m_sessionStartMs = 0;      // 0 = no active session
    QVector<qint64> m_failTimes;      // tool.failed burst window (60 s)
    qint64 m_lastSeenMs = 0;          // stamped on save; absence source
    QString m_lastGreetingDate;       // "yyyy-MM-dd" local
    QHash<QString, qint64> m_lastFired;   // per-type proactive cooldowns
    qint64 m_lastProactiveMs = 0;     // global 1/hour cap

    std::function<qint64()> m_nowFn;

    static constexpr qint64 TICK_MS = 30000;
    static constexpr double DECAY_PER_TICK = 0.01;
    static constexpr double NIGHT_BASELINE = -0.2;
    static constexpr qint64 FAIL_BURST_WINDOW_MS = 60000;
    static constexpr qint64 LONG_SESSION_ENERGY_DRAIN_AGE_MS = 2LL * 60 * 60 * 1000;
    static constexpr double LONG_SESSION_ENERGY_DRAIN = 0.05;
    static constexpr qint64 NUDGE_SESSION_AGE_MS = 150LL * 60 * 1000;  // 2.5 h
    static constexpr qint64 MISS_ABSENCE_MS = 24LL * 60 * 60 * 1000;
    static constexpr qint64 GLOBAL_PROACTIVE_COOLDOWN_MS = 60LL * 60 * 1000;
};

#endif // MOODENGINE_H
