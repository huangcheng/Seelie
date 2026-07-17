# ContextSenses (Spec 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `SystemContextEngine` that emits 7 synthetic `context.*` events (latenight, longsession, idle, away, gaming, lowbattery, timeofday) into the existing event pipeline with per-event cooldowns, plus an X11 implementation of `FullscreenWatcher` for Linux Gaming Mode.

**Architecture:** One new QObject engine (`src/SystemContextEngine.h/.cpp`), constructed in `main.cpp`, observing `EventRouter::eventProcessed` and emitting back through `EventRouter::routeEvent()` with `source:"system"`. Platform probes (OS idle, battery) live in the engine's `.cpp` behind injectable `std::function` seams; tests drive detector slots directly with a fake clock. Spec: `docs/superpowers/specs/2026-07-17-context-senses-design.md`.

**Tech Stack:** C++17, Qt6 (Core/Gui/Widgets/Test), CMake. Platform APIs: macOS CoreGraphics + IOKit, Windows Win32, Linux X11 (+libXss optional).

**Conventions (binding):** `QStringLiteral` for literals; UPPERCASE acronyms in identifiers; dense reason-focused comments; conventional-commits messages; test port/working-dir isolation via `QSettings::setPath` redirect in `initTestCase`. All new user-visible tip strings go in `assets/i18n/tips.{en,zh_CN}.json` (NOT the `.ts` file).

---

### Task 1: Register the 7 `context.*` canonical events

**Files:**
- Modify: `src/CanonicalEvents.h` (after line 41, before the namespace close)
- Modify: `src/EventRouter.cpp:15-24` (`s_validEvents`)
- Test: `tests/test_system_context.cpp` (new)
- Modify: `tests/CMakeLists.txt:98-114` (`TEST_SOURCES`)

- [ ] **Step 1: Write the failing test**

Create `tests/test_system_context.cpp`. Note it intentionally uses **string literals** for event names (not the `CanonicalEvents` constants) so wire-format drift breaks the test:

```cpp
/**
 * test_system_context.cpp
 *
 * Unit tests for SystemContextEngine (ContextSenses, Spec 2):
 *   - context.* event registration / validation
 *   - tips JSON entries resolve in en + zh_CN
 *   - ConfigManager contextSensesEnabled round-trip
 *   - engine cooldowns, detectors (latenight/longsession/idle/away/battery/
 *     gaming/timeofday) driven via injected clock/probe seams
 */

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>

#include "EventRouter.h"
#include "ConfigManager.h"
#include "FullscreenWatcher.h"

class TestSystemContext : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Task 1: registration
    void testContextEventsAccepted();
    void testUnknownContextEventRejected();

private:
    QTemporaryDir m_tmpDir;
};

void TestSystemContext::initTestCase()
{
    // Redirect QSettings to a throw-away temp dir (mirrors test_gaming_mode).
    QVERIFY(m_tmpDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmpDir.path());
}

void TestSystemContext::testContextEventsAccepted()
{
    EventRouter router;
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    const QStringList names = {
        QStringLiteral("context.latenight"),
        QStringLiteral("context.longsession"),
        QStringLiteral("context.idle"),
        QStringLiteral("context.away"),
        QStringLiteral("context.gaming"),
        QStringLiteral("context.lowbattery"),
        QStringLiteral("context.timeofday"),
    };
    for (const QString &name : names) {
        router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                           {QStringLiteral("source"), QStringLiteral("system")},
                           {QStringLiteral("event"), name}});
    }
    QCOMPARE(spy.count(), names.size());
}

void TestSystemContext::testUnknownContextEventRejected()
{
    EventRouter router;
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("system")},
                       {QStringLiteral("event"), QStringLiteral("context.bogus")}});
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestSystemContext)
#include "test_system_context.moc"
```

Also add to `tests/CMakeLists.txt` inside `TEST_SOURCES` (after `test_gaming_mode.cpp`):

```cmake
    test_system_context.cpp
```

(The engine itself is added to `SEELIEPET_LIB_SOURCES` in Task 4, when the header exists — along with the test's `#include "SystemContextEngine.h"`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL — `testContextEventsAccepted` gets `spy.count() == 0` ("Unknown event name" warnings), because the names aren't in `s_validEvents`.

- [ ] **Step 3: Implement**

`src/CanonicalEvents.h` — add before the namespace close (line 42):

```cpp
inline constexpr const char *ContextLateNight     = "context.latenight";
inline constexpr const char *ContextLongSession   = "context.longsession";
inline constexpr const char *ContextIdle          = "context.idle";
inline constexpr const char *ContextAway          = "context.away";
inline constexpr const char *ContextGaming        = "context.gaming";
inline constexpr const char *ContextLowBattery    = "context.lowbattery";
inline constexpr const char *ContextTimeOfDay     = "context.timeofday";
```

`src/EventRouter.cpp` — extend `s_validEvents` (lines 15-24):

```cpp
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (2 tests)

- [ ] **Step 5: Commit**

```bash
git add src/CanonicalEvents.h src/EventRouter.cpp tests/test_system_context.cpp tests/CMakeLists.txt
git commit -m "feat(events): register 7 context.* canonical events for ContextSenses"
```

---

### Task 2: Tips JSON entries (en + zh_CN)

**Files:**
- Modify: `assets/i18n/tips.en.json` (inside `"events"`, after the `todo.updated` line)
- Modify: `assets/i18n/tips.zh_CN.json` (same position)
- Modify: `tests/CMakeLists.txt` (foreach loop — add tips resources to all test targets)
- Test: `tests/test_system_context.cpp`

Note: `context.timeofday` gets an intentionally **empty** entry (enrichment event for Spec 4, no bubble — mirrors `session.idle`).

- [ ] **Step 1: Write the failing test**

Add to `tests/test_system_context.cpp` (slot declarations + definitions). The test parses the JSON files directly via `SOURCE_DIR` (already defined for every test target) so it is robust to the empty `timeofday` values:

```cpp
    // Task 2: tips catalog entries
    void testTipsJsonEntriesEn();
    void testTipsJsonEntriesZhCn();
```

```cpp
static bool jsonHasEventKeys(const QString &path, const QStringList &keys)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject events = QJsonDocument::fromJson(f.readAll())
        .object().value(QStringLiteral("events")).toObject();
    for (const QString &k : keys) {
        if (!events.contains(k)) return false;
    }
    return true;
}

void TestSystemContext::testTipsJsonEntriesEn()
{
    const QStringList keys = {
        QStringLiteral("context.latenight"), QStringLiteral("context.longsession"),
        QStringLiteral("context.idle"), QStringLiteral("context.away"),
        QStringLiteral("context.gaming"), QStringLiteral("context.lowbattery"),
        QStringLiteral("context.timeofday"),
    };
    QVERIFY(jsonHasEventKeys(QStringLiteral(SOURCE_DIR) + QStringLiteral("/assets/i18n/tips.en.json"), keys));
}

void TestSystemContext::testTipsJsonEntriesZhCn()
{
    const QStringList keys = {
        QStringLiteral("context.latenight"), QStringLiteral("context.longsession"),
        QStringLiteral("context.idle"), QStringLiteral("context.away"),
        QStringLiteral("context.gaming"), QStringLiteral("context.lowbattery"),
        QStringLiteral("context.timeofday"),
    };
    QVERIFY(jsonHasEventKeys(QStringLiteral(SOURCE_DIR) + QStringLiteral("/assets/i18n/tips.zh_CN.json"), keys));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL — keys missing.

- [ ] **Step 3: Implement**

`assets/i18n/tips.en.json` — add inside `"events"` after the `todo.updated` line (mind the trailing comma on the previous entry):

```json
    "context.latenight":    {"title": "Late night",          "body": "It's getting late — consider wrapping up soon."},
    "context.longsession":  {"title": "Long session",        "body": "You've been at this for hours. Stretch break?"},
    "context.idle":         {"title": "Quiet…",              "body": "No activity for a while."},
    "context.away":         {"title": "Welcome back!",       "body": "Hope you had a good break!"},
    "context.gaming":       {"title": "Welcome back!",       "body": "GG! Hope the session went well."},
    "context.lowbattery":   {"title": "Low battery",         "body": "Battery is running low — time to plug in."},
    "context.timeofday":    {"title": "",                    "body": ""}
```

`assets/i18n/tips.zh_CN.json` — same position:

```json
    "context.latenight":    {"title": "夜深了",              "body": "很晚了，早点休息吧！"},
    "context.longsession":  {"title": "连续工作很久了",      "body": "已经好几个小时了，起来活动一下吧？"},
    "context.idle":         {"title": "好安静…",             "body": "有一会儿没有动静了。"},
    "context.away":         {"title": "欢迎回来！",           "body": "休息好了吗？继续加油吧！"},
    "context.gaming":       {"title": "欢迎回来！",           "body": "游戏打得怎么样？欢迎回来！"},
    "context.lowbattery":   {"title": "电量不足",             "body": "电池快没电了，记得插上电源。"},
    "context.timeofday":    {"title": "",                    "body": ""}
```

Also make the tips resources visible to tests (TipsCatalog reads `:/i18n/...` from compiled resources; test targets currently get none). In `tests/CMakeLists.txt`, inside the `foreach(test_src ...)` loop after `add_test(...)` (~line 152), add:

```cmake
    # TipsCatalog resource bundle — needed by tests that resolve tip text.
    qt_add_resources(${test_name} "tips_i18n"
        PREFIX "/i18n"
        BASE "${CMAKE_SOURCE_DIR}/assets"
        FILES
            ${CMAKE_SOURCE_DIR}/assets/i18n/tips.en.json
            ${CMAKE_SOURCE_DIR}/assets/i18n/tips.zh_CN.json
    )
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (4 tests). Also `cmake --build build` (full) must stay clean — the qrc addition touches all test targets.

- [ ] **Step 5: Commit**

```bash
git add assets/i18n/tips.en.json assets/i18n/tips.zh_CN.json tests/CMakeLists.txt tests/test_system_context.cpp
git commit -m "feat(i18n): add context.* tip entries (en + zh_CN), expose tips qrc to tests"
```

---

### Task 3: ConfigManager `contextSensesEnabled` (default true)

**Files:**
- Modify: `src/ConfigManager.h` (getter/setter near line 76-78; signal near line 141; member near line 167)
- Modify: `src/ConfigManager.cpp` (load near line 165; setter near line 369-375)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

Add slots + definitions (mirrors test_gaming_mode's ConfigManager cases):

```cpp
    // Task 3: config key
    void testContextSensesDefaultTrue();
    void testContextSensesRoundTrip();
    void testContextSensesSignal();
```

```cpp
void TestSystemContext::testContextSensesDefaultTrue()
{
    ConfigManager cfg;
    cfg.load();
    QCOMPARE(cfg.contextSensesEnabled(), true);
}

void TestSystemContext::testContextSensesRoundTrip()
{
    ConfigManager cfg;
    cfg.load();
    cfg.setContextSensesEnabled(false);
    ConfigManager cfg2;
    cfg2.load();
    QCOMPARE(cfg2.contextSensesEnabled(), false);
    cfg2.setContextSensesEnabled(true);  // restore for other tests
}

void TestSystemContext::testContextSensesSignal()
{
    ConfigManager cfg;
    cfg.load();
    QSignalSpy spy(&cfg, &ConfigManager::contextSensesEnabledChanged);
    cfg.setContextSensesEnabled(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    cfg.setContextSensesEnabled(true);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context` — Expected: COMPILE FAIL (no such members).

- [ ] **Step 3: Implement**

`src/ConfigManager.h` — after the gamingMode block (line 76-78):

```cpp
    /** Whether ContextSenses synthetic context events are emitted. Default true. */
    bool contextSensesEnabled() const { return m_contextSensesEnabled; }
    void setContextSensesEnabled(bool enabled);
```

Signals block — after `gamingModeEnabledChanged` (line 141):

```cpp
    void contextSensesEnabledChanged(bool enabled);
```

Members — after `m_gamingModeEnabled` (line 167):

```cpp
    bool m_contextSensesEnabled = true;
```

`src/ConfigManager.cpp` — in `load()` after the gamingMode line (line 165):

```cpp
    m_contextSensesEnabled = m_settings.value("contextSensesEnabled", true).toBool();
```

After `setGamingModeEnabled` (line 369-375):

```cpp
void ConfigManager::setContextSensesEnabled(bool enabled)
{
    if (m_contextSensesEnabled == enabled) return;
    m_contextSensesEnabled = enabled;
    save();
    emit contextSensesEnabledChanged(enabled);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (7 tests)

- [ ] **Step 5: Commit**

```bash
git add src/ConfigManager.h src/ConfigManager.cpp tests/test_system_context.cpp
git commit -m "feat(config): add contextSensesEnabled master toggle (default on)"
```

---

### Task 4: SystemContextEngine skeleton (emission + cooldowns)

**Files:**
- Create: `src/SystemContextEngine.h`
- Create: `src/SystemContextEngine.cpp`
- Modify: `CMakeLists.txt` (Seelie sources, near line 415)
- Modify: `tests/CMakeLists.txt` (`SEELIEPET_LIB_SOURCES`, near line 45-46)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

Add `#include "SystemContextEngine.h"` to the test file top (replacing the Task-1 omission). Add slots + definitions:

```cpp
    // Task 4: skeleton
    void testEngineEmitsThroughRouter();
    void testCooldownSuppressesSecondEmit();
    void testStoppedEngineIsSilent();
```

```cpp
void TestSystemContext::testEngineEmitsThroughRouter()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(12, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    QVERIFY(engine.isRunning());
    engine.emitContextForTest(QStringLiteral("context.latenight"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("context.latenight"));
}

void TestSystemContext::testCooldownSuppressesSecondEmit()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(23, 15)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    engine.emitContextForTest(QStringLiteral("context.latenight"));  // 20h cooldown
    QCOMPARE(spy.count(), 1);
    fakeNow += 60LL * 60 * 1000;  // +1h — still inside cooldown
    engine.emitContextForTest(QStringLiteral("context.latenight"));
    QCOMPARE(spy.count(), 1);     // suppressed
    fakeNow += 20LL * 60 * 60 * 1000;  // past the 20h cooldown
    engine.emitContextForTest(QStringLiteral("context.latenight"));
    QCOMPARE(spy.count(), 2);     // fires again
}

void TestSystemContext::testStoppedEngineIsSilent()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    // Never start() — detectors must not fire.
    engine.clockTick();
    engine.sharedTick();
    QCOMPARE(spy.count(), 0);
}
```

Note: `emitContextForTest` is a thin public wrapper so tests can emit without tripping detector state; `clockTick()`/`sharedTick()` are public for the same reason (mirrors `MockFullscreenWatcher::poll()`'s rationale).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context` — Expected: COMPILE FAIL (header missing).

- [ ] **Step 3: Implement**

Create `src/SystemContextEngine.h`:

```cpp
#ifndef SYSTEMCONTEXTENGINE_H
#define SYSTEMCONTEXTENGINE_H

#include <QHash>
#include <QObject>
#include <functional>

class ConfigManager;
class EventRouter;
class FullscreenWatcher;
class QJsonObject;
class QTimer;

/**
 * SystemContextEngine (ContextSenses, Spec 2) senses system/session context
 * and emits synthetic `context.*` events into the normal pipeline by calling
 * EventRouter::routeEvent() with source "system" — identical wire shape to
 * gateway messages, so tips/stats/(future) persona all work unchanged.
 *
 * Two timers only (spec constraint): a 60 s clock tick (latenight /
 * longsession) and a shared 30 s tick (activity-idle, OS-away probe, battery
 * every second tick). Per-event cooldowns mirror TipsEngine's m_lastTriggered
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
    ConfigManager *m_config;
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

    static constexpr qint64 LATENIGHT_HOUR = 23;
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
```

Create `src/SystemContextEngine.cpp` — skeleton portion (detectors arrive in Tasks 5-8; write the file exactly like this now, with `clockTick()`/`sharedTick()`/`onFullscreenStopped()` as empty bodies marked with task-forward comments):

```cpp
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
void SystemContextEngine::clockTick() {}
void SystemContextEngine::onEventObserved(const QString &, const QJsonObject &) {}

// Task 6/7/8 fill this in.
void SystemContextEngine::sharedTick() {}

// Task 9 fills this in.
void SystemContextEngine::onFullscreenStopped() {}
```

`CMakeLists.txt` — add to the Seelie sources (after `src/FullscreenWatcher.h`, line 416):

```cmake
    src/SystemContextEngine.cpp
    src/SystemContextEngine.h
```

`tests/CMakeLists.txt` — add to `SEELIEPET_LIB_SOURCES` (after the FullscreenWatcher lines, 45-46):

```cmake
    ${CMAKE_SOURCE_DIR}/src/SystemContextEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/SystemContextEngine.h
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (10 tests). Note `testStoppedEngineIsSilent` passes because `emitContext` and the (empty) ticks gate on `isRunning()`.

- [ ] **Step 5: Commit**

```bash
git add src/SystemContextEngine.h src/SystemContextEngine.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_system_context.cpp
git commit -m "feat(senses): SystemContextEngine skeleton — emission, cooldowns, test seams"
```

---

### Task 5: Clock detectors — latenight + longsession + session tracking

**Files:**
- Modify: `src/SystemContextEngine.cpp` (`clockTick`, `onEventObserved`)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    // Task 5: clock detectors
    void testLateNightFiresAfter23WithSession();
    void testLateNightSilentBefore23();
    void testLateNightSilentWithoutSession();
    void testLongSessionFiresAt3Hours();
```

```cpp
void TestSystemContext::testLateNightFiresAfter23WithSession()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(23, 15)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("codex")},
                       {QStringLiteral("event"), QStringLiteral("session.start")}});
    spy.clear();
    engine.clockTick();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("context.latenight"));
}

void TestSystemContext::testLateNightSilentBefore23()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(22, 15)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("codex")},
                       {QStringLiteral("event"), QStringLiteral("session.start")}});
    spy.clear();
    engine.clockTick();
    QCOMPARE(spy.count(), 0);
}

void TestSystemContext::testLateNightSilentWithoutSession()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(23, 15)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();  // no session.start observed
    engine.clockTick();
    QCOMPARE(spy.count(), 0);
}

void TestSystemContext::testLongSessionFiresAt3Hours()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(14, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("codex")},
                       {QStringLiteral("event"), QStringLiteral("session.start")}});
    fakeNow += (3LL * 60 + 1) * 60 * 1000;  // +3h01m
    spy.clear();
    engine.clockTick();
    bool sawLong = false;
    for (const auto &args : spy) {
        if (args.at(0).toString() == QStringLiteral("context.longsession")) sawLong = true;
    }
    QVERIFY(sawLong);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL (empty `clockTick`/`onEventObserved`).

- [ ] **Step 3: Implement**

Replace the Task-4 empty bodies in `src/SystemContextEngine.cpp`:

```cpp
void SystemContextEngine::clockTick()
{
    if (!isRunning()) return;
    const qint64 now = nowMs();
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
                    {{QStringLiteral("hours"), (now - m_sessionStartMs) / (60LL * 60 * 1000)}});
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
        m_sessionActive = true;
        m_sessionStartMs = nowMs();
    } else if (eventName == QLatin1String(CE::SessionEnd)
               || eventName == QLatin1String(CE::SessionIdle)) {
        m_sessionActive = false;
        m_timeofdaySent = false;  // next session gets a fresh bucket
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (14 tests)

- [ ] **Step 5: Commit**

```bash
git add src/SystemContextEngine.cpp tests/test_system_context.cpp
git commit -m "feat(senses): latenight + longsession clock detectors with session tracking"
```

---

### Task 6: Shared tick — activity idle + time-of-day

**Files:**
- Modify: `src/SystemContextEngine.cpp` (`sharedTick`, `onEventObserved` session.start branch)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    // Task 6: idle + time-of-day
    void testIdleFiresAfter10QuietMinutes();
    void testIdleLatchAndCooldown();
    void testTimeOfDayMorningBucket();
    void testTimeOfDayNightBucket();
    void testTimeOfDayOncePerSession();
```

```cpp
void TestSystemContext::testIdleFiresAfter10QuietMinutes()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(15, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();  // activity clock starts now
    fakeNow += 11LL * 60 * 1000;  // +11 min, nothing observed
    engine.sharedTick();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("context.idle"));
}

void TestSystemContext::testIdleLatchAndCooldown()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(15, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    fakeNow += 11LL * 60 * 1000;
    engine.sharedTick();
    QCOMPARE(spy.count(), 1);          // fired
    engine.sharedTick();
    QCOMPARE(spy.count(), 1);          // latched — no refire without activity
    // Activity resumes (non-system source resets the clock)
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("codex")},
                       {QStringLiteral("event"), QStringLiteral("file.edited")}});
    spy.clear();
    fakeNow += 11LL * 60 * 1000;       // quiet again, but within 30min cooldown
    engine.sharedTick();
    QCOMPARE(spy.count(), 0);          // cooldown blocks
    fakeNow += 20LL * 60 * 1000;       // cooldown elapsed
    engine.sharedTick();
    QCOMPARE(spy.count(), 1);          // fires again
}

void TestSystemContext::testTimeOfDayMorningBucket()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(9, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("codex")},
                       {QStringLiteral("event"), QStringLiteral("session.start")}});
    QCoreApplication::processEvents();  // timeofday is queued (singleShot 0)
    bool sawMorning = false;
    for (const auto &args : spy) {
        if (args.at(0).toString() == QStringLiteral("context.timeofday")) {
            sawMorning = args.at(1).toJsonObject()
                .value(QStringLiteral("bucket")).toString() == QStringLiteral("morning");
        }
    }
    QVERIFY(sawMorning);
}

void TestSystemContext::testTimeOfDayNightBucket()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(23, 30)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("codex")},
                       {QStringLiteral("event"), QStringLiteral("session.start")}});
    QCoreApplication::processEvents();
    bool sawNight = false;
    for (const auto &args : spy) {
        if (args.at(0).toString() == QStringLiteral("context.timeofday")) {
            sawNight = args.at(1).toJsonObject()
                .value(QStringLiteral("bucket")).toString() == QStringLiteral("night");
        }
    }
    QVERIFY(sawNight);
}

void TestSystemContext::testTimeOfDayOncePerSession()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(9, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.start();
    const QJsonObject startEv{{QStringLiteral("type"), QStringLiteral("event")},
                              {QStringLiteral("source"), QStringLiteral("codex")},
                              {QStringLiteral("event"), QStringLiteral("session.start")}};
    router.routeEvent(startEv);
    router.routeEvent(startEv);  // duplicate start in same session
    QCoreApplication::processEvents();
    int count = 0;
    for (const auto &args : spy) {
        if (args.at(0).toString() == QStringLiteral("context.timeofday")) ++count;
    }
    QCOMPARE(count, 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL.

- [ ] **Step 3: Implement**

In `src/SystemContextEngine.cpp`, replace the empty `sharedTick()`:

```cpp
void SystemContextEngine::sharedTick()
{
    if (!isRunning()) return;
    ++m_sharedTickCount;
    const qint64 now = nowMs();

    // Activity idle (no IPC events for IDLE_THRESHOLD_MS). Latch prevents
    // refiring every tick; the latch releases on the next observed non-system
    // event, and IDLE_COOLDOWN_MS in emitContext spaces out episodes.
    if (!m_idleLatched && now - m_lastActivityMs >= IDLE_THRESHOLD_MS) {
        m_idleLatched = true;
        emitContext(QLatin1String(CE::ContextIdle),
                    {{QStringLiteral("minutes"), (now - m_lastActivityMs) / 60000}});
    }

    // Task 7: OS-away probe goes here.
    // Task 8: battery probe (every 2nd tick) goes here.
}
```

And extend the `SessionStart` branch of `onEventObserved` (full replacement of that branch):

```cpp
    if (eventName == QLatin1String(CE::SessionStart)) {
        m_sessionActive = true;
        m_sessionStartMs = nowMs();
        if (!m_timeofdaySent && isRunning()) {
            m_timeofdaySent = true;
            const int h = QDateTime::fromMSecsSinceEpoch(nowMs()).time().hour();
            const QString bucket = (h >= 5 && h < 11)  ? QStringLiteral("morning")
                : (h >= 11 && h < 17)                  ? QStringLiteral("afternoon")
                : (h >= 17 && h < 22)                  ? QStringLiteral("evening")
                :                                        QStringLiteral("night");
            // Queued: onEventObserved runs inside routeEvent's signal emission;
            // re-entering routeEvent synchronously would nest stats/tip handling.
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (19 tests)

- [ ] **Step 5: Commit**

```bash
git add src/SystemContextEngine.cpp tests/test_system_context.cpp
git commit -m "feat(senses): activity-idle detector + time-of-day bucket on session.start"
```

---

### Task 7: OS-away probe (platform impls + seam)

**Files:**
- Modify: `src/SystemContextEngine.cpp` (`sharedTick` away block + platform section at file bottom)
- Modify: `CMakeLists.txt` (Seelie: nothing new on macOS — CoreGraphics already linked; Windows: nothing new)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    // Task 7: away
    void testAwayFiresOnReturn();
    void testAwaySilentWhileAway();
    void testAwayProbeUnsupportedDisables();
```

```cpp
void TestSystemContext::testAwayFiresOnReturn()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(16, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    int osIdleSec = 0;
    engine.setOsIdleProbe([&osIdleSec] { return osIdleSec; });
    engine.start();
    fakeNow += 6LL * 60 * 1000;  // user walks away
    osIdleSec = 360;
    engine.sharedTick();         // enters away state — no event yet
    QCOMPARE(spy.count(), 0);
    fakeNow += 60 * 1000;
    osIdleSec = 0;               // user returns
    engine.sharedTick();
    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("context.away"));
    QVERIFY(args.at(1).toJsonObject().value(QStringLiteral("awayMinutes")).toInt() >= 6);
}

void TestSystemContext::testAwaySilentWhileAway()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(16, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    int osIdleSec = 400;
    engine.setOsIdleProbe([&osIdleSec] { return osIdleSec; });
    engine.start();
    engine.sharedTick();
    engine.sharedTick();  // still away — must not fire per tick
    QCOMPARE(spy.count(), 0);
}

void TestSystemContext::testAwayProbeUnsupportedDisables()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    engine.setOsIdleProbe([] { return -1; });  // e.g. Wayland
    engine.start();
    engine.sharedTick();
    engine.sharedTick();  // probe dead after first failure — no crash, no event
    QCOMPARE(spy.count(), 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL.

- [ ] **Step 3: Implement**

In `sharedTick()`, replace the `// Task 7:` comment with:

```cpp
    // OS-away: fires on RETURN, not while away (a bubble for an absent user
    // is pointless — mirrors the gaming quiet-hide decision). Probe returning
    // -1 means unsupported (Wayland); disable after a single warning.
    if (!m_awayProbeDead) {
        const int idleSec = m_osIdleFn ? m_osIdleFn() : platformOsIdleSeconds();
        if (idleSec < 0) {
            m_awayProbeDead = true;
            qWarning() << "SystemContextEngine: OS idle probe unsupported — context.away disabled";
        } else if (!m_away && idleSec >= AWAY_THRESHOLD_SEC) {
            m_away = true;
            m_awayStartMs = now - static_cast<qint64>(idleSec) * 1000;
        } else if (m_away && idleSec < AWAY_THRESHOLD_SEC) {
            const qint64 awayMs = now - m_awayStartMs;
            m_away = false;
            emitContext(QLatin1String(CE::ContextAway),
                        {{QStringLiteral("awayMinutes"), awayMs / 60000}});
        }
    }
```

At the bottom of `src/SystemContextEngine.cpp` (before the final `sharedTick` is fine — these are statics; place them after the includes, before the ctor, matching FullscreenWatcher.cpp's platform-section style):

```cpp
// ── Platform probes ─────────────────────────────────────────────────────────

#if defined(Q_OS_MAC)
#include <CoreGraphics/CoreGraphics.h>
static int platformOsIdleSeconds()
{
    return static_cast<int>(CGEventSourceSecondsSinceLastEventType(
        kCGEventSourceStateHIDSystemState, kCGAnyInputEventType));
}
#elif defined(Q_OS_WIN)
#include <windows.h>
static int platformOsIdleSeconds()
{
    LASTINPUTINFO lii{sizeof(lii)};
    if (!GetLastInputInfo(&lii)) return -1;
    return static_cast<int>((GetTickCount() - lii.dwTime) / 1000);
}
#elif defined(Q_OS_LINUX) && defined(SEELIE_HAS_XSS)
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>
static int platformOsIdleSeconds()
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return -1;  // Wayland — unsupported
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    int idle = -1;
    if (XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info)) {
        idle = static_cast<int>(info->idle / 1000);
    }
    XFree(info);
    XCloseDisplay(dpy);
    return idle;
}
#else
static int platformOsIdleSeconds()
{
    return -1;  // unsupported platform — detector self-disables
}
#endif
```

(Forward declaration near the top of the file, after the includes: `static int platformOsIdleSeconds();` — or define the platform block before its first use; either is fine as long as it compiles. Pick: define the block immediately after the includes/`namespace CE` line.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (22 tests)

- [ ] **Step 5: Commit**

```bash
git add src/SystemContextEngine.cpp tests/test_system_context.cpp
git commit -m "feat(senses): OS-away probe with platform impls (mac/win/x11) + return-event"
```

---

### Task 8: Battery probe (platform impls + seam)

**Files:**
- Modify: `src/SystemContextEngine.cpp` (`sharedTick` battery block + platform section)
- Modify: `CMakeLists.txt` (macOS: add IOKit to the framework link at line 789-790)
- Modify: `tests/CMakeLists.txt` (macOS: add IOKit in the foreach APPLE block, line 138-142)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    // Task 8: battery
    void testLowBatteryFires();
    void testLowBatteryLatchAndRearm();
    void testNoBatteryDisablesProbe();
```

```cpp
void TestSystemContext::testLowBatteryFires()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(16, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.setOsIdleProbe([] { return 0; });
    engine.setBatteryProbe([] {
        return SystemContextEngine::PowerState{true, true, 15};
    });
    engine.start();
    engine.sharedTick();   // tick 1 — battery not evaluated
    QCOMPARE(spy.count(), 0);
    engine.sharedTick();   // tick 2 — battery evaluated
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("context.lowbattery"));
}

void TestSystemContext::testLowBatteryLatchAndRearm()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    qint64 fakeNow = QDateTime(QDate(2026, 7, 17), QTime(16, 0)).toMSecsSinceEpoch();
    engine.setNowFn([&fakeNow] { return fakeNow; });
    engine.setOsIdleProbe([] { return 0; });
    SystemContextEngine::PowerState ps{true, true, 15};
    engine.setBatteryProbe([&ps] { return ps; });
    engine.start();
    engine.sharedTick(); engine.sharedTick();  // fires (tick 2)
    QCOMPARE(spy.count(), 1);
    engine.sharedTick(); engine.sharedTick();  // latched — no refire at 15%
    QCOMPARE(spy.count(), 1);
    ps.discharging = false;                     // AC attached → re-arm
    engine.sharedTick(); engine.sharedTick();
    QCOMPARE(spy.count(), 1);
    ps = {true, true, 18};                      // unplugged again, still low
    engine.sharedTick(); engine.sharedTick();
    QCOMPARE(spy.count(), 2);
}

void TestSystemContext::testNoBatteryDisablesProbe()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    engine.setOsIdleProbe([] { return 0; });
    engine.setBatteryProbe([] { return SystemContextEngine::PowerState{}; });  // desktop
    engine.start();
    for (int i = 0; i < 4; ++i) engine.sharedTick();
    QCOMPARE(spy.count(), 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL.

- [ ] **Step 3: Implement**

In `sharedTick()`, replace the `// Task 8:` comment with:

```cpp
    // Battery: every second shared tick (≈60 s). Latch + re-arm: fire once
    // when crossing ≤20% discharging; re-arm on AC attach or charge > 30%.
    if (!m_batteryProbeDead && (m_sharedTickCount % 2 == 0)) {
        const PowerState ps = m_batteryFn ? m_batteryFn() : platformPowerState();
        if (!ps.present) {
            m_batteryProbeDead = true;  // desktop / no battery — silent
        } else if (ps.discharging && ps.percent <= BATTERY_LOW_PERCENT && !m_lowBattery) {
            m_lowBattery = true;
            emitContext(QLatin1String(CE::ContextLowBattery),
                        {{QStringLiteral("percent"), ps.percent}});
        } else if (!ps.discharging || ps.percent > BATTERY_REARM_PERCENT) {
            m_lowBattery = false;
        }
    }
```

Add to the platform-probe section of `src/SystemContextEngine.cpp`:

```cpp
#if defined(Q_OS_MAC)
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
static SystemContextEngine::PowerState platformPowerState()
{
    SystemContextEngine::PowerState ps;
    CFTypeRef info = IOPSGetPowerSourceInfo();
    if (!info) return ps;
    CFArrayRef list = IOPSCopyPowerSourceList(info);
    if (!list) return ps;
    const CFIndex n = CFArrayGetCount(list);
    for (CFIndex i = 0; i < n; ++i) {
        CFDictionaryRef src = static_cast<CFDictionaryRef>(
            CFArrayGetValueAtIndex(list, i));
        if (!src) continue;
        // Only consider internal batteries (skip UPS etc. absent Type check)
        CFStringRef type = static_cast<CFStringRef>(
            CFDictionaryGetValue(src, CFSTR(kIOPSTypeKey)));
        if (!type || CFStringCompare(type, CFSTR(kIOPSInternalBatteryType), 0)
                != kCFCompareEqualTo) continue;
        CFNumberRef cur = static_cast<CFNumberRef>(
            CFDictionaryGetValue(src, CFSTR(kIOPSCurrentCapacityKey)));
        CFBooleanRef charging = static_cast<CFBooleanRef>(
            CFDictionaryGetValue(src, CFSTR(kIOPSIsChargingKey)));
        CFStringRef state = static_cast<CFStringRef>(
            CFDictionaryGetValue(src, CFSTR(kIOPSPowerSourceStateKey)));
        int pct = 100;
        if (cur) CFNumberGetValue(cur, kCFNumberIntType, &pct);
        ps.present = true;
        ps.percent = pct;
        const bool onAc = state && CFStringCompare(state, CFSTR(kIOPSACPowerValue), 0)
                              == kCFCompareEqualTo;
        ps.discharging = !onAc && !(charging && CFBooleanGetValue(charging));
        break;  // first internal battery wins
    }
    CFRelease(list);
    return ps;
}
#elif defined(Q_OS_WIN)
static SystemContextEngine::PowerState platformPowerState()
{
    SystemContextEngine::PowerState ps;
    SYSTEM_POWER_STATUS sps = {};
    if (!GetSystemPowerStatus(&sps)) return ps;
    if (sps.BatteryFlag & 128) return ps;      // no system battery
    if (sps.BatteryLifePercent > 100) return ps; // 255 = unknown
    ps.present = true;
    ps.discharging = (sps.ACLineStatus == 0);
    ps.percent = sps.BatteryLifePercent;
    return ps;
}
#elif defined(Q_OS_LINUX)
#include <QDir>
#include <QFile>
static SystemContextEngine::PowerState platformPowerState()
{
    SystemContextEngine::PowerState ps;
    const QDir base(QStringLiteral("/sys/class/power_supply"));
    const QStringList bats = base.entryList({QStringLiteral("BAT*")}, QDir::Dirs);
    if (bats.isEmpty()) return ps;
    QFile cap(base.absoluteFilePath(bats.first() + QStringLiteral("/capacity")));
    QFile st(base.absoluteFilePath(bats.first() + QStringLiteral("/status")));
    if (!cap.open(QIODevice::ReadOnly) || !st.open(QIODevice::ReadOnly)) return ps;
    ps.present = true;
    ps.percent = QString::fromUtf8(cap.readAll()).trimmed().toInt();
    ps.discharging = QString::fromUtf8(st.readAll()).trimmed()
        == QLatin1String("Discharging");
    return ps;
}
#else
static SystemContextEngine::PowerState platformPowerState()
{
    return {};
}
#endif
```

Also add `static SystemContextEngine::PowerState platformPowerState();` visibility: define this block in the same platform-probe area as Task 7's function (it references `SystemContextEngine::PowerState`, so it must come AFTER the class definition is visible — it is, same file, after the header include).

`CMakeLists.txt` — macOS frameworks (line 789-790):

```cmake
    # CoreGraphics: FullscreenWatcher + SystemContextEngine idle probe.
    # IOKit: SystemContextEngine battery probe (IOPSCopyPowerSourceInfo).
    target_link_libraries(Seelie PRIVATE "-framework AppKit" "-framework CoreGraphics" "-framework IOKit")
```

`tests/CMakeLists.txt` — foreach APPLE block (lines 138-142): add `"-framework IOKit"` to the list.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (25 tests). Also full `cmake --build build` clean (IOKit link).

- [ ] **Step 5: Commit**

```bash
git add src/SystemContextEngine.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_system_context.cpp
git commit -m "feat(senses): battery probe (mac IOKit / win / linux sysfs) with latch+rearm"
```

---

### Task 9: Gaming event + Linux X11 FullscreenWatcher

**Files:**
- Modify: `src/SystemContextEngine.cpp` (`onFullscreenStopped`)
- Modify: `src/FullscreenWatcher.cpp` (replace the `#else` Linux stub, lines 125-134)
- Modify: `CMakeLists.txt` (Linux X11/Xss detection — new block; Seelie target)
- Modify: `tests/CMakeLists.txt` (Linux X11/Xss for test targets)
- Test: `tests/test_system_context.cpp`

- [ ] **Step 1: Write the failing test**

Add a mock watcher (same shape as test_gaming_mode.cpp's) at the top of the test file, after the includes:

```cpp
class MockFullscreenWatcher : public FullscreenWatcher
{
public:
    explicit MockFullscreenWatcher(QObject *parent = nullptr)
        : FullscreenWatcher(parent) {}
    void setNextResult(bool r) { m_next = r; }
    void poll() { QMetaObject::invokeMethod(this, "onPoll", Qt::DirectConnection); }
protected:
    bool checkFullscreen() override { return m_next; }
private:
    bool m_next = false;
};
```

Slots + definitions:

```cpp
    // Task 9: gaming
    void testGamingFiresOnFullscreenStop();
    void testGamingSilentOnFullscreenStart();
    void testGamingCooldown();
```

```cpp
void TestSystemContext::testGamingFiresOnFullscreenStop()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    MockFullscreenWatcher watcher;
    engine.setFullscreenWatcher(&watcher);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    engine.start();
    watcher.setNextResult(true);
    watcher.poll();    // fullscreen starts — silent
    QCOMPARE(spy.count(), 0);
    watcher.setNextResult(false);
    watcher.poll();    // fullscreen stops — welcome back
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("context.gaming"));
}

void TestSystemContext::testGamingSilentOnFullscreenStart()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    MockFullscreenWatcher watcher;
    engine.setFullscreenWatcher(&watcher);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    engine.start();
    watcher.setNextResult(true);
    watcher.poll();
    watcher.poll();    // steady state, still fullscreen
    QCOMPARE(spy.count(), 0);
}

void TestSystemContext::testGamingCooldown()
{
    EventRouter router;
    ConfigManager cfg; cfg.load();
    SystemContextEngine engine(&router, &cfg);
    MockFullscreenWatcher watcher;
    engine.setFullscreenWatcher(&watcher);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    engine.start();
    watcher.setNextResult(true);  watcher.poll();
    watcher.setNextResult(false); watcher.poll();  // fires
    QCOMPARE(spy.count(), 1);
    watcher.setNextResult(true);  watcher.poll();
    watcher.setNextResult(false); watcher.poll();  // within 30min cooldown
    QCOMPARE(spy.count(), 1);
}
```

Note: `testGamingCooldown` relies on real wall time for the cooldown map (no nowFn injected — emitContext uses `nowMs()`, and without a nowFn that's the real clock; two stops seconds apart are inside 30 min). This is intentional and fast (no waits).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: FAIL (`onFullscreenStopped` is empty).

- [ ] **Step 3a: Implement the gaming slot**

Replace the empty body in `src/SystemContextEngine.cpp`:

```cpp
void SystemContextEngine::onFullscreenStopped()
{
    // Welcome-back moment (start is the silent auto-hide). GATED on running:
    // the watcher is shared with MainWindow and can outlive an engine stop.
    if (!isRunning()) return;
    emitContext(QLatin1String(CE::ContextGaming));
}
```

- [ ] **Step 3b: Implement the X11 FullscreenWatcher**

In `src/FullscreenWatcher.cpp`, replace the `#else` stub block (lines 125-134) with:

```cpp
#elif defined(Q_OS_LINUX) && defined(SEELIE_HAS_X11)

// Linux/X11: focused window carries _NET_WM_STATE_FULLSCREEN. Wayland has no
// compositor-agnostic equivalent — XOpenDisplay fails there and we stay a
// no-op with a one-time warning (spec decision 2026-07-17).

#include <X11/Xlib.h>
#include <X11/Xatom.h>

static bool platformCheckFullscreen()
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning() << "FullscreenWatcher: X11 unavailable (Wayland?) — Gaming Mode disabled";
        }
        return false;
    }

    bool result = false;
    const Atom netActive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    const Atom netState = XInternAtom(dpy, "_NET_WM_STATE", True);
    const Atom netFullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", True);
    if (netActive == None || netState == None || netFullscreen == None) {
        XCloseDisplay(dpy);
        return false;
    }

    // Ask the root window for the currently active window
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nitems = 0, bytesAfter = 0;
    unsigned char *prop = nullptr;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), netActive, 0, 1, False,
                           XA_WINDOW, &actualType, &actualFormat, &nitems,
                           &bytesAfter, &prop) == Success && prop && nitems == 1) {
        const Window active = *reinterpret_cast<Window *>(prop);
        XFree(prop);
        prop = nullptr;

        unsigned char *stateProp = nullptr;
        if (XGetWindowProperty(dpy, active, netState, 0, 32, False,
                               XA_ATOM, &actualType, &actualFormat, &nitems,
                               &bytesAfter, &stateProp) == Success && stateProp) {
            auto *atoms = reinterpret_cast<Atom *>(stateProp);
            for (unsigned long i = 0; i < nitems; ++i) {
                if (atoms[i] == netFullscreen) { result = true; break; }
            }
            XFree(stateProp);
        }
    }
    if (prop) XFree(prop);
    XCloseDisplay(dpy);
    return result;
}

#else

// Other Unix / Linux without X11 dev files: fullscreen detection unsupported.
// Returns false so Gaming Mode is a harmless no-op on these platforms.
static bool platformCheckFullscreen()
{
    return false;
}

#endif
```

- [ ] **Step 3c: CMake — X11/Xss detection**

`CMakeLists.txt` — add a block after the macOS framework section (~line 791):

```cmake
if(UNIX AND NOT APPLE)
    # X11: FullscreenWatcher (Spec 2) and SystemContextEngine OS-idle probe.
    # Optional: absent X11 dev files keep both features as runtime no-ops.
    find_package(X11)
    if(X11_FOUND)
        target_compile_definitions(Seelie PRIVATE SEELIE_HAS_X11=1)
        target_include_directories(Seelie PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(Seelie PRIVATE ${X11_LIBRARIES})
        if(X11_Xss_FOUND)
            target_compile_definitions(Seelie PRIVATE SEELIE_HAS_XSS=1)
            target_link_libraries(Seelie PRIVATE ${X11_Xss_LIB})
        endif()
    endif()
endif()
```

`tests/CMakeLists.txt` — same block inside the `foreach` loop (after the APPLE block), with `${test_name}` instead of `Seelie`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_system_context && ./build/tests/test_system_context`
Expected: PASS (28 tests). On macOS/Windows the X11 code compiles out; on Linux CI verify the X11 branch compiles (`cmake --build build` with libx11-dev installed).

- [ ] **Step 5: Commit**

```bash
git add src/SystemContextEngine.cpp src/FullscreenWatcher.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_system_context.cpp
git commit -m "feat(senses): context.gaming welcome-back + X11 FullscreenWatcher for Linux"
```

---

### Task 10: Production wiring + full verification

**Files:**
- Modify: `src/mainwindow.h` (public getter near line 62-66)
- Modify: `src/main.cpp` (after line 407 area — after `setPersonaEngine` block)
- Modify: `TODO.md` (mark Spec 2 shipped)
- Test: whole suite

- [ ] **Step 1: MainWindow getter**

`src/mainwindow.h` — public section, next to `setEventRouter` (line 62):

```cpp
    /** Owned FullscreenWatcher (Gaming Mode) — shared with SystemContextEngine
        for the context.gaming welcome-back event. */
    FullscreenWatcher *fullscreenWatcher() const { return m_fullscreenWatcher; }
```

- [ ] **Step 2: main.cpp wiring**

After the persona-engine block (`w.setPersonaEngine(&personaEngine);`, ~line 407), insert:

```cpp
    // --- System context engine (ContextSenses, Spec 2) -----------------------
    // Emits synthetic context.* events through the same EventRouter pipeline.
    // start/stop follows the contextSensesEnabled toggle live, mirroring the
    // Gaming Mode wiring pattern in MainWindow.
    SystemContextEngine contextEngine(&eventRouter, &config);
    contextEngine.setFullscreenWatcher(w.fullscreenWatcher());
    if (config.contextSensesEnabled())
        contextEngine.start();
    QObject::connect(&config, &ConfigManager::contextSensesEnabledChanged,
                     &contextEngine, [&contextEngine](bool enabled) {
        if (enabled) contextEngine.start(); else contextEngine.stop();
    });
```

Add `#include "SystemContextEngine.h"` to main.cpp's include block.

- [ ] **Step 3: Build + full suite**

Run: `cmake --build build -j 10 && ctest --test-dir build`
Expected: build clean; 18/18 tests pass (17 existing + test_system_context).

- [ ] **Step 4: Manual smoke (macOS, optional but recommended)**

Run the app (`open build/Seelie.app`), then in a shell:
`seelie-gateway --source codex --event session.start` — expect the normal session tip AND, one event-loop turn later, no visible bubble for `context.timeofday` (empty entry). Check `~/.config/Seelie/config.json`-adjacent stats after a few minutes: `stats.events.context.timeofday` incremented (MemoryManager). Temporarily set fake early thresholds only if you want to see latenight/idle bubbles live — do NOT commit such changes.

- [ ] **Step 5: TODO.md + commit**

Update `TODO.md` §2: mark Spec 2 ✅ COMPLETE with a one-line summary + test count; remove the `FullscreenWatcher` Linux stub bullet from §3.

```bash
git add src/mainwindow.h src/main.cpp TODO.md
git commit -m "feat(senses): wire SystemContextEngine in main.cpp; mark Spec 2 complete"
```

---

## Self-Review Notes (filled at plan completion)

- **Spec coverage:** 7 events (T1) · tips en+zh (T2) · master toggle (T3) · cooldowns (T4) · latenight/longsession (T5) · idle/timeofday (T6) · away (T7) · battery (T8) · gaming + X11 (T9) · wiring + verification (T10). Wayland-stub documented in T9 code comments + spec. Presentation reduced to tips+stats per spec §3 (revised).
- **Type consistency:** `PowerState{present, discharging, percent}` used uniformly; `NowFn/OsIdleFn/BatteryFn` signatures match header; `emitContextForTest`/`clockTick`/`sharedTick` public in header and used by tests; `CE::Context*` constant names identical in CanonicalEvents.h, engine, and cooldownFor.
- **Known deliberate deviations from first-draft spec:** no EventAction/animation table (doesn't exist); `context.timeofday` empty tip; settings-panel checkbox deferred.

---

## Execution Errata (recorded 2026-07-17, after subagent-driven execution)

Bugs in THIS PLAN found by implementers' TDD red phases or review loops; all fixed in code. Kept as lessons for future plan authors.

1. **Task 3 — missing `flush()` write.** The plan's ConfigManager snippet called
   `save()` in the setter but never added `m_settings.setValue("contextSensesEnabled", …)`
   to `flush()`. `save()` only schedules a debounced timer; `flush()` is the sole
   persistence point. The round-trip test caught it. *Lesson: when mirroring a
   persistence pattern, trace the full write path, not just the setter.*
2. **Task 3 — round-trip test needed nested scopes.** The plan's inlined round-trip
   would read stale state (debounced flush hadn't fired). The test_gaming_mode donor's
   nested-scope shape was the correct one. *Lesson: copy the donor's structure, not
   just its assertions.*
3. **Task 6 — idle-latch/cooldown contradiction.** The plan's literal code set
   `m_idleLatched = true` unconditionally before `emitContext`; a cooldown-suppressed
   attempt then latched forever and the plan's OWN `testIdleLatchAndCooldown` could
   never pass its final step. Fixed by gating the latch on cooldown expiry.
   *Lesson: mentally execute plan tests against plan code — the plan wrote code and
   tests that contradicted each other.*
4. **Task 8 — wrong macOS API names + CF leak.** Plan snippet used
   `IOPSGetPowerSourceInfo`/`IOPSCopyPowerSourceList`; real SDK exports
   `IOPSCopyPowerSourcesInfo`/`IOPSCopyPowerSourcesList`. Snippet also leaked the
   `info` snapshot (Create rule) — fixed with `CFRelease(info)` (`6e5ecb6`).
   *Lesson: verify platform API names against the SDK, and always pair
   Create-rule releases.*
5. **Task 9 — `_NET_ACTIVE_WINDOW == 0` crash (review catch).** EWMH sets the
   property to `None` when nothing is focused; the plan's X11 code would have
   queried window 0 → BadWindow → fatal async X error. Guard added (`c2310ef`).
   *Lesson: X error handling is fatal and async — every id from a property reply
   needs a zero check before reuse.*
6. **Task 4 — header default arg needs complete type.** `emitContext(name,
   const QJsonObject &payload = {})` with only a forward declaration is ill-formed;
   header includes `<QJsonObject>` instead. *Lesson: `{}` default args force the
   include.*

Review-loop improvements beyond the plan (all reviewed and committed): first-start-wins
session clock (`b05853c`), latch resets on engine start (`4633523`), XScreenSaverAllocInfo
null guard (`3c2a5ea`), Linux battery transient-read sentinel (`4ee7e87`), JSON column
alignment (`c7367f7`), X11 absent-builder STATUS message (`c2310ef`).

**Final state:** 19 commits on `context-senses`, suite 18/18 (30 engine tests),
final whole-implementation review verdict READY TO MERGE.
