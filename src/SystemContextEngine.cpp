#include "SystemContextEngine.h"

#include "CanonicalEvents.h"
#include "ConfigManager.h"
#include "EventRouter.h"
#include "FullscreenWatcher.h"

#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

namespace CE = CanonicalEvents;

SystemContextEngine::SystemContextEngine(EventRouter *router, ConfigManager *config,
                                         QObject *parent)
    : QObject(parent)
    , m_router(router)
    , m_config(config)
    , m_clockTimer(new QTimer(this))
    , m_sharedTimer(new QTimer(this))
{
    m_clockTimer->setInterval(CLOCK_INTERVAL_MS);
    m_clockTimer->setSingleShot(false);
    connect(m_clockTimer, &QTimer::timeout, this, &SystemContextEngine::clockTick);

    m_sharedTimer->setInterval(SHARED_INTERVAL_MS);
    m_sharedTimer->setSingleShot(false);
    connect(m_sharedTimer, &QTimer::timeout, this, &SystemContextEngine::sharedTick);

    // Always observe (even when stopped): keeps session/activity tracking warm
    // so enabling the toggle mid-run starts with correct state. Observation
    // never emits on its own — emissions only happen from detector ticks and
    // the time-of-day follow-up, all gated by isRunning().
    if (m_router) {
        connect(m_router, &EventRouter::eventProcessed,
                this, &SystemContextEngine::onEventObserved);
    }
}

void SystemContextEngine::start()
{
    if (isRunning()) return;
    m_lastActivityMs = nowMs();
    m_timeofdaySent = false;
    m_idleLatched = false;   // fresh state on restart (latches, unlike
    m_away = false;          // cooldowns, should not survive a stop→start
    m_lowBattery = false;    // cycle — user re-enabling expects a clean slate)
    m_clockTimer->start();
    m_sharedTimer->start();
}

void SystemContextEngine::stop()
{
    m_clockTimer->stop();
    m_sharedTimer->stop();
}

bool SystemContextEngine::isRunning() const
{
    return m_clockTimer->isActive();
}

qint64 SystemContextEngine::nowMs() const
{
    return m_nowFn ? m_nowFn() : QDateTime::currentMSecsSinceEpoch();
}

qint64 SystemContextEngine::cooldownFor(const QString &name) const
{
    if (name == QLatin1String(CE::ContextLateNight))   return LATENIGHT_COOLDOWN_MS;
    if (name == QLatin1String(CE::ContextLongSession)) return LONGSESSION_COOLDOWN_MS;
    if (name == QLatin1String(CE::ContextIdle))        return IDLE_COOLDOWN_MS;
    if (name == QLatin1String(CE::ContextGaming))      return GAMING_COOLDOWN_MS;
    return 0;  // away (per-episode), lowbattery (latch), timeofday (per-session)
}

void SystemContextEngine::emitContext(const QString &name, const QJsonObject &payload)
{
    if (!m_router || !isRunning()) return;
    const qint64 now = nowMs();
    const qint64 cd = cooldownFor(name);
    if (cd > 0 && m_lastFired.contains(name)
        && now - m_lastFired.value(name) < cd) {
        return;
    }
    // Cooldown recorded before routeEvent: emitContext always constructs
    // well-formed events (source "system", registered name), so validation
    // cannot fail here. If routeEvent ever gains a bool return, move this
    // insert after the call.
    m_lastFired.insert(name, now);
    QJsonObject ev = payload;
    ev.insert(QStringLiteral("type"), QStringLiteral("event"));
    ev.insert(QStringLiteral("source"), QStringLiteral("system"));
    ev.insert(QStringLiteral("event"), name);
    m_router->routeEvent(ev);
}

void SystemContextEngine::setFullscreenWatcher(FullscreenWatcher *watcher)
{
    if (m_watcher == watcher) return;
    if (m_watcher) {
        disconnect(m_watcher, &FullscreenWatcher::fullscreenAppStopped,
                   this, &SystemContextEngine::onFullscreenStopped);
    }
    m_watcher = watcher;
    if (m_watcher) {
        // Start is deliberately NOT connected: fullscreen start = silent
        // auto-hide (MainWindow owns that); a visible reaction while hiding
        // is pointless. Spec decision 2026-07-17.
        connect(m_watcher, &FullscreenWatcher::fullscreenAppStopped,
                this, &SystemContextEngine::onFullscreenStopped);
    }
}

// Task 5 fills these in.
void SystemContextEngine::clockTick()
{
    if (!isRunning()) return;
    const qint64 now = nowMs();
    // Local time, deliberately: latenight/timeofday are user-facing concepts,
    // not UTC ones. DST fall-back repeats an hour — the 20h cooldown absorbs it.
    const int hour = QDateTime::fromMSecsSinceEpoch(now).time().hour();

    // Late night: only meaningful while a session is active; the 20h cooldown
    // in emitContext makes it once-per-night.
    if (m_sessionActive && hour >= LATENIGHT_HOUR) {
        emitContext(QLatin1String(CE::ContextLateNight),
                    {{QStringLiteral("hour"), hour}});
    }

    // Long session: continuous session age; 2h cooldown spaces repeats.
    if (m_sessionActive && now - m_sessionStartMs >= LONGSESSION_MIN_MS) {
        emitContext(QLatin1String(CE::ContextLongSession),
                    {{QStringLiteral("hours"),  // integer hours, truncated
                      (now - m_sessionStartMs) / (60LL * 60 * 1000)}});
    }
}

void SystemContextEngine::onEventObserved(const QString &eventName,
                                          const QJsonObject &payload)
{
    // Synthetic system events must not count as "activity" — otherwise
    // context.idle would reset its own clock and never fire again.
    const QString source = payload.value(QStringLiteral("source")).toString();
    if (source != QLatin1String("system")) {
        m_lastActivityMs = nowMs();
        m_idleLatched = false;
    }

    if (eventName == QLatin1String(CE::SessionStart)) {
        // First-start wins: with multiple gateways overlapping, the
        // longsession clock tracks the session that opened the work period,
        // not the most recent tool to join it.
        if (!m_sessionActive) {
            m_sessionStartMs = nowMs();
        }
        m_sessionActive = true;
        if (!m_timeofdaySent && isRunning()) {
            m_timeofdaySent = true;
            const int h = QDateTime::fromMSecsSinceEpoch(nowMs()).time().hour();
            const QString bucket = (h >= 5 && h < 11)  ? QStringLiteral("morning")
                : (h >= 11 && h < 17)                  ? QStringLiteral("afternoon")
                : (h >= 17 && h < 22)                  ? QStringLiteral("evening")
                :                                        QStringLiteral("night");
            // Queued: onEventObserved runs inside routeEvent's signal emission;
            // re-entering routeEvent synchronously would nest stats/tip handling.
            // singleShot with `this` as context is cancelled if the engine is
            // destroyed first — stack-allocated test engines stay safe.
            QTimer::singleShot(0, this, [this, bucket] {
                emitContext(QLatin1String(CE::ContextTimeOfDay),
                            {{QStringLiteral("bucket"), bucket}});
            });
        }
    } else if (eventName == QLatin1String(CE::SessionEnd)
               || eventName == QLatin1String(CE::SessionIdle)) {
        m_sessionActive = false;
        m_timeofdaySent = false;  // next session gets a fresh bucket
    }
}

// Task 6/7/8 fill this in.
void SystemContextEngine::sharedTick()
{
    if (!isRunning()) return;
    ++m_sharedTickCount;
    const qint64 now = nowMs();

    // Activity idle (no IPC events for IDLE_THRESHOLD_MS). Latch prevents
    // refiring every tick; the latch releases on the next observed non-system
    // event, and IDLE_COOLDOWN_MS in emitContext spaces out episodes.
    // The latch is only set when emitContext will actually emit: a
    // cooldown-blocked attempt must not latch, or the next post-cooldown tick
    // would stay silent with no activity in between to release the latch.
    // (Header is frozen for this task, so emitContext can't return bool —
    // the cooldown gate is re-checked here against the same constant.)
    if (!m_idleLatched && now - m_lastActivityMs >= IDLE_THRESHOLD_MS) {
        const qint64 lastIdle = m_lastFired.value(QLatin1String(CE::ContextIdle), 0);
        if (now - lastIdle >= IDLE_COOLDOWN_MS) {
            m_idleLatched = true;
            emitContext(QLatin1String(CE::ContextIdle),
                        {{QStringLiteral("minutes"), (now - m_lastActivityMs) / 60000}});
        }
    }

    // Task 7: OS-away probe goes here.
    // Task 8: battery probe (every 2nd tick) goes here.
}

// Task 9 fills this in.
void SystemContextEngine::onFullscreenStopped() {}
