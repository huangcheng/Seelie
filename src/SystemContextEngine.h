#ifndef SYSTEMCONTEXTENGINE_H
#define SYSTEMCONTEXTENGINE_H

#include <QHash>
#include <QObject>
#include <QJsonObject>
#include <functional>

class ConfigManager;
class EventRouter;
class FullscreenWatcher;
class QTimer;

/**
 * SystemContextEngine (ContextSenses, Spec 2) senses system/session context
 * and emits synthetic `context.*` events into the normal pipeline by calling
 * EventRouter::routeEvent() with source "system" — identical wire shape to
 * gateway messages, so tips/stats/(future) persona all work unchanged.
 *
 * Two timers only (spec constraint): a 60 s clock tick (latenight /
 * longsession) and a shared 30 s tick (activity-idle, OS-away probe, battery
 * every second tick). Per-event cooldowns mirror Tips Engine's m_lastTriggered
 * pattern. Platform probes (OS idle, battery) are std::function seams —
 * production defaults live at the bottom of the .cpp; tests inject fakes.
 */
class SystemContextEngine : public QObject
{
    Q_OBJECT

public:
    explicit SystemContextEngine(EventRouter *router, ConfigManager *config,
                                 QObject *parent = nullptr);

    void start();
    void stop();
    bool isRunning() const;

    /** Shares MainWindow's FullscreenWatcher; context.gaming fires on stop. */
    void setFullscreenWatcher(FullscreenWatcher *watcher);

    // ── Test seams (EmbeddingService-style injectable fns) ────────────────
    using NowFn = std::function<qint64()>;   // ms since epoch
    struct PowerState {
        bool present = false;       // false → no battery / probe unsupported
        bool discharging = false;
        int percent = 100;
    };
    using OsIdleFn = std::function<int()>;   // seconds of OS input idle; -1 = unsupported
    using BatteryFn = std::function<PowerState()>;
    void setNowFn(NowFn fn) { m_nowFn = fn; }
    void setOsIdleProbe(OsIdleFn fn) { m_osIdleFn = fn; }
    void setBatteryProbe(BatteryFn fn) { m_batteryFn = fn; }

    // Public so tests can drive detectors without real waits.
    void clockTick();
    void sharedTick();
    /// Thin wrapper over the private emitContext for direct emission tests.
    void emitContextForTest(const QString &name) { emitContext(name); }

private slots:
    void onEventObserved(const QString &eventName, const QJsonObject &payload);
    void onFullscreenStopped();

private:
    void emitContext(const QString &name, const QJsonObject &payload = {});
    qint64 cooldownFor(const QString &name) const;
    qint64 nowMs() const;

    EventRouter *m_router;
    ConfigManager *m_config;  // reserved for Task 10: contextSensesEnabled gate
    FullscreenWatcher *m_watcher = nullptr;

    QTimer *m_clockTimer = nullptr;    // 60 s
    QTimer *m_sharedTimer = nullptr;   // 30 s
    int m_sharedTickCount = 0;

    QHash<QString, qint64> m_lastFired;  // event name → ms epoch (cooldown map)

    // Session tracking (observed, non-system events only)
    bool m_sessionActive = false;
    qint64 m_sessionStartMs = 0;
    bool m_timeofdaySent = false;

    qint64 m_lastActivityMs = 0;
    bool m_idleLatched = false;

    bool m_away = false;
    qint64 m_awayStartMs = 0;
    bool m_awayProbeDead = false;

    bool m_lowBattery = false;
    bool m_batteryProbeDead = false;

    NowFn m_nowFn;
    OsIdleFn m_osIdleFn;
    BatteryFn m_batteryFn;

    static constexpr int CLOCK_INTERVAL_MS = 60000;
    static constexpr int SHARED_INTERVAL_MS = 30000;

    static constexpr int LATENIGHT_HOUR = 23;
    static constexpr qint64 LATENIGHT_COOLDOWN_MS = 20LL * 60 * 60 * 1000;

    static constexpr qint64 LONGSESSION_MIN_MS = 3LL * 60 * 60 * 1000;
    static constexpr qint64 LONGSESSION_COOLDOWN_MS = 2LL * 60 * 60 * 1000;

    static constexpr qint64 IDLE_THRESHOLD_MS = 10LL * 60 * 1000;
    static constexpr qint64 IDLE_COOLDOWN_MS = 30LL * 60 * 1000;

    static constexpr int AWAY_THRESHOLD_SEC = 300;

    static constexpr int BATTERY_LOW_PERCENT = 20;
    static constexpr int BATTERY_REARM_PERCENT = 30;

    static constexpr qint64 GAMING_COOLDOWN_MS = 30LL * 60 * 1000;
};

#endif // SYSTEMCONTEXTENGINE_H
