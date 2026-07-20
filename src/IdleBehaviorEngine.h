#ifndef IDLEBEHAVIORENGINE_H
#define IDLEBEHAVIORENGINE_H

#include <QObject>
#include <QTimer>
#include <functional>

#include "ConfigManager.h"
#include "SayingPool.h"

class PersonaEngine;

/**
 * Schedules idle sayings: canned lines from SayingPool by default, with an
 * occasional LLM quip via PersonaEngine ("idle.quip") when the user opted in.
 *
 * Sayings are the LOWEST bubble priority: a slot only fires when no event
 * occurred within the current interval AND the caller-supplied gate (pet
 * idle, bubble free, window visible) passes. Skipped slots are silent —
 * no catch-up bursts.
 *
 * Test seams mirror SystemContextEngine: injectable clock + RNG + tick().
 */
class IdleBehaviorEngine : public QObject
{
    Q_OBJECT
public:
    using NowFn  = std::function<qint64()>;
    using RngFn  = std::function<double()>;
    using GateFn = std::function<bool()>;

    // Non-owning. Caller must ensure config and persona outlive this engine
    // (in practice all three are created together in main() / MainWindow).
    IdleBehaviorEngine(ConfigManager *config, PersonaEngine *persona,
                       QObject *parent = nullptr);

    /** Combined "pet idle & bubble-free" gate, supplied by MainWindow.
     *  The lambda's captures must not outlive this engine. */
    void setCanShowGate(GateFn fn) { m_canShow = std::move(fn); }

    /** Call on every EventRouter::eventProcessed — resets the idle clock. */
    void onEventProcessed();

    /** Load sayings for a locale. false → sayings disabled until next load. */
    bool loadSayings(const QString &locale);

    /** Re-read frequency; starts/stops the scheduler. Call after loadSayings
     *  and on sayingFrequencyChanged. */
    void applyConfig();

    // --- Test seams ------------------------------------------------------
    void setNowFn(NowFn fn) { m_now = std::move(fn); }
    void setRngFn(RngFn fn) { m_rng = std::move(fn); }
    void tick();                       // drive one scheduler step manually
    SayingPool &pool() { return m_pool; }

signals:
    void sayingReady(const QString &title, const QString &body);

private slots:
    void onTimer() { attemptSlot(); }

private:
    void armTimer();
    int rollIntervalMs() const;
    void attemptSlot();
    void fireCanned();
    void fireQuip();
    void onQuipUpgraded(quint64 requestId, const QString &text);
    void onQuipFailed(quint64 requestId);

    ConfigManager *m_config;
    PersonaEngine *m_persona;
    SayingPool m_pool;
    GateFn m_canShow;
    NowFn m_now;
    RngFn m_rng;
    QTimer m_timer;
    qint64 m_lastEventAt = 0;
    int m_intervalMs = 0;
    quint64 m_pendingQuipId = 0;
    bool m_sayingsUsable = false;
};

#endif // IDLEBEHAVIORENGINE_H
