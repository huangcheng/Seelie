# MoodEngine & Proactive Companionship Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the pet a persistent mood state (valence/energy → 5 tiers) that reacts to events, decays over time, reuses MemoryManager's bond/affection for relationship stages, and drives four proactive behaviors (greeting, long-session nudge, missed-you, stage-up) surfaced via pool lines, on-demand LLM milestones, idle-animation bias, and ambient peek UI.

**Architecture:** New `MoodEngine` QObject singleton wired in `main.cpp`. It consumes `EventRouter::eventProcessed` (same hook PetStateMachine uses), emits synthetic `mood.*` events back through `EventRouter::routeEvent` (mirroring `SystemContextEngine::emitContext`), persists via `StatisticsManager::registerComponent`, and biases `PetStateMachine`'s idle chain tail via a new `setMoodIdleBias`. Relationship state stays in `MemoryManager` — no parallel affinity store.

**Tech Stack:** Qt6 (Core/Gui/Widgets/Test), C++17, QJsonObject stats persistence, SQLite-backed MemoryManager (existing).

**Spec:** `docs/superpowers/specs/2026-07-19-mood-engine-design.md`

---

### Task 1: MoodEngine core — scalars, deltas, decay, tier quantization

**Files:**
- Create: `src/MoodEngine.h`
- Create: `src/MoodEngine.cpp`
- Test: `tests/test_mood_engine.cpp`
- Modify: `tests/CMakeLists.txt` (add to `SEELIEPET_LIB_SOURCES` ~line 47 and `TEST_SOURCES` ~line 109)
- Modify: `CMakeLists.txt` (add next to `src/SystemContextEngine.cpp`, line ~417)

- [ ] **Step 1: Write the failing test**

`tests/test_mood_engine.cpp`:

```cpp
/**
 * test_mood_engine.cpp
 *
 * Unit tests for MoodEngine (mood vector + tiers):
 *   - delta application and clamping
 *   - 30s-tick decay toward time-of-day energy baseline
 *   - tier quantization with hysteresis
 */

#include <QTest>
#include <QSignalSpy>

#include "EventRouter.h"
#include "MoodEngine.h"

class TestMoodEngine : public QObject
{
    Q_OBJECT

private slots:
    void deltasApplyAndClamp();
    void decayTowardBaseline();
    void tierQuantization();
    void tierHysteresis();
};

void TestMoodEngine::deltasApplyAndClamp()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    QCOMPARE(mood.valence(), 0.0);
    QCOMPARE(mood.energy(), 0.0);

    mood.onEventProcessed(QStringLiteral("user.pet"), {});
    QVERIFY(mood.valence() > 0.0);

    mood.onEventProcessed(QStringLiteral("user.toss"), {});
    QVERIFY(mood.valence() < 0.0);

    // Clamp at [-1, 1]: repeated pets never exceed 1.0.
    for (int i = 0; i < 100; ++i)
        mood.onEventProcessed(QStringLiteral("user.pet"), {});
    QVERIFY(mood.valence() <= 1.0);
    QVERIFY(mood.energy() <= 1.0);
}

void TestMoodEngine::decayTowardBaseline()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T14:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });

    mood.onEventProcessed(QStringLiteral("user.toss"), {});   // valence -0.20
    const double v0 = mood.valence();
    mood.tickForTest();
    QVERIFY(mood.valence() > v0);   // decayed toward 0
    QVERIFY(mood.valence() < 0.0);  // but not past it in one tick
}

void TestMoodEngine::tierQuantization()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Content);

    mood.onEventProcessed(QStringLiteral("session.start"), {});
    // session.start after >12h absence is an excitement spike only when
    // absence is known; a fresh engine has no lastSeen, so force the vector:
    mood.setVectorForTest(0.5, 0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Excited);

    mood.setVectorForTest(-0.5, 0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Tense);

    mood.setVectorForTest(0.0, -0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Tired);
}

void TestMoodEngine::tierHysteresis()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    mood.setVectorForTest(0.5, 0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Excited);

    // Dropping to the stay-band (0.3 < v < 0.4) keeps Excited.
    mood.setVectorForTest(0.35, 0.35);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Excited);

    // Crossing fully below the stay threshold leaves Excited.
    mood.setVectorForTest(0.1, 0.1);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Content);
}

QTEST_GUILESS_MAIN(TestMoodEngine)
#include "test_mood_engine.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `tests/CMakeLists.txt`, add inside `SEELIEPET_LIB_SOURCES` (after the `SystemContextEngine` lines, ~line 47):

```cmake
    ${CMAKE_SOURCE_DIR}/src/MoodEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/MoodEngine.h
```

and inside `TEST_SOURCES` (after `test_system_context.cpp`, ~line 113):

```cmake
    test_mood_engine.cpp
```

Run:

```powershell
python scripts/build_release.py --skip-package; cd build; ./tests/test_mood_engine.exe
```

Expected: compile FAIL — `MoodEngine.h` not found.

- [ ] **Step 3: Write the implementation**

`src/MoodEngine.h`:

```cpp
#ifndef MOODENGINE_H
#define MOODENGINE_H

#include <QHash>
#include <QObject>
#include <QJsonObject>
#include <QVector>
#include <functional>

class EventRouter;
class MemoryManager;

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

signals:
    void moodTierChanged(MoodEngine::Tier tier);

private:
    void tick();
    void applyDelta(double dV, double dE);
    Tier quantize() const;
    void updateTier();
    void checkProactive();
    void emitMood(const QString &name, const QJsonObject &payload = {});
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
```

`src/MoodEngine.cpp`:

```cpp
#include "MoodEngine.h"
#include "CanonicalEvents.h"
#include "EventRouter.h"
#include "MemoryManager.h"
#include "StatisticsPersistence.h"
#include <QDateTime>
#include <QTimer>

namespace CE = CanonicalEvents;

MoodEngine::MoodEngine(EventRouter *router, MemoryManager *memory, QObject *parent)
    : QObject(parent)
    , m_router(router)
    , m_memory(memory)
{
}

QString MoodEngine::tierName(Tier t)
{
    switch (t) {
    case Tier::Excited: return QStringLiteral("excited");
    case Tier::Tense:   return QStringLiteral("tense");
    case Tier::Tired:   return QStringLiteral("tired");
    case Tier::Lonely:  return QStringLiteral("lonely");
    case Tier::Content: break;
    }
    return QStringLiteral("content");
}

QString MoodEngine::stageName() const
{
    const int lvl = m_memory ? m_memory->bondLevel() : 0;
    if (lvl >= 4) return QStringLiteral("Partner");
    if (lvl >= 2) return QStringLiteral("Companion");
    return QStringLiteral("Stranger");
}

qint64 MoodEngine::nowMs() const
{
    return m_nowFn ? m_nowFn() : QDateTime::currentMSecsSinceEpoch();
}

void MoodEngine::start()
{
    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(TICK_MS);
        connect(m_tickTimer, &QTimer::timeout, this, &MoodEngine::tick);
    }
    m_tickTimer->start();
}

void MoodEngine::stop()
{
    if (m_tickTimer) m_tickTimer->stop();
}

void MoodEngine::applyDelta(double dV, double dE)
{
    m_valence = qBound(-1.0, m_valence + dV, 1.0);
    m_energy  = qBound(-1.0, m_energy + dE, 1.0);
    updateTier();
}

void MoodEngine::setVectorForTest(double v, double e)
{
    m_valence = qBound(-1.0, v, 1.0);
    m_energy  = qBound(-1.0, e, 1.0);
    updateTier();
}

double MoodEngine::energyBaseline() const
{
    const int hour = QDateTime::fromMSecsSinceEpoch(nowMs()).time().hour();
    return (hour >= 23 || hour < 6) ? NIGHT_BASELINE : 0.0;
}

void MoodEngine::tick()
{
    // Decay toward baselines (0 for valence, time-of-day for energy).
    const double eb = energyBaseline();
    m_valence -= qBound(-DECAY_PER_TICK, m_valence, DECAY_PER_TICK);
    const double eDiff = m_energy - eb;
    m_energy -= qBound(-DECAY_PER_TICK, eDiff, DECAY_PER_TICK);

    // Long-session energy drain once the session is past 2 h.
    if (m_sessionStartMs > 0
        && nowMs() - m_sessionStartMs >= LONG_SESSION_ENERGY_DRAIN_AGE_MS) {
        m_energy = qBound(-1.0, m_energy - LONG_SESSION_ENERGY_DRAIN, 1.0);
    }
    updateTier();
    checkProactive();
}

MoodEngine::Tier MoodEngine::quantize() const
{
    if (m_lonely) return Tier::Lonely;
    // Hysteresis: current tier keeps the 0.3 stay-threshold; a new tier
    // must beat the 0.4 enter-threshold. Prevents flapping at boundaries.
    constexpr double stay = 0.3, enter = 0.4;
    switch (m_tier) {
    case Tier::Excited:
        if (m_valence > stay && m_energy > stay) return Tier::Excited;
        break;
    case Tier::Tense:
        if (m_valence < -stay && m_energy > stay) return Tier::Tense;
        break;
    case Tier::Tired:
        if (m_energy < -stay) return Tier::Tired;
        break;
    default:
        break;
    }
    if (m_valence > enter && m_energy > enter) return Tier::Excited;
    if (m_valence < -enter && m_energy > enter) return Tier::Tense;
    if (m_energy < -enter) return Tier::Tired;
    return Tier::Content;
}

void MoodEngine::updateTier()
{
    const Tier t = quantize();
    if (t == m_tier) return;
    m_tier = t;
    emit moodTierChanged(t);
}

void MoodEngine::onEventProcessed(const QString &eventName, const QJsonObject &payload)
{
    Q_UNUSED(payload);
    const qint64 now = nowMs();

    if (eventName == QLatin1String("user.pet")) {
        applyDelta(0.08, 0.02);
    } else if (eventName == QLatin1String("user.toss")) {
        applyDelta(-0.20, 0.10);
    } else if (eventName == QLatin1String("todo.updated")) {
        applyDelta(0.05, 0.0);
    } else if (eventName == QLatin1String("session.error")) {
        applyDelta(-0.10, 0.05);
    } else if (eventName == QLatin1String("tool.failed")) {
        m_failTimes.append(now);
        while (!m_failTimes.isEmpty()
               && now - m_failTimes.first() > FAIL_BURST_WINDOW_MS)
            m_failTimes.removeFirst();
        if (m_failTimes.size() >= 3) {
            m_failTimes.clear();      // one burst delta per window
            applyDelta(-0.15, 0.10);
        }
    } else if (eventName == QLatin1String("session.start")) {
        const qint64 absence = m_lastSeenMs > 0 ? now - m_lastSeenMs : 0;
        m_sessionStartMs = now;
        if (m_lonely) {
            m_lonely = false;
            applyDelta(0.25, 0.25);   // excitement spike, clears Lonely tier
        } else if (absence > 12LL * 60 * 60 * 1000) {
            applyDelta(0.25, 0.25);
        }
        checkProactive();             // greeting / missed-you live here
    } else if (eventName == QLatin1String("session.end")) {
        m_sessionStartMs = 0;
    }
}

void MoodEngine::checkProactive()   { /* Task 4 */ }
void MoodEngine::emitMood(const QString &, const QJsonObject &) { /* Task 4 */ }

void MoodEngine::loadStats(const QString &)  { /* Task 4 */ }
void MoodEngine::saveStats(const QString &)  { /* Task 4 */ }
```

In `CMakeLists.txt`, add next to `src/SystemContextEngine.cpp` (~line 417):

```cmake
    src/MoodEngine.cpp
```

- [ ] **Step 4: Run the tests and verify they pass**

```powershell
python scripts/build_release.py --skip-package; cd build; ./tests/test_mood_engine.exe
```

Expected: `Totals: 4 passed, 0 failed`.

- [ ] **Step 5: Commit**

```powershell
git add src/MoodEngine.h src/MoodEngine.cpp tests/test_mood_engine.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(mood): MoodEngine core — valence/energy vector, decay, tier hysteresis"
```

---

### Task 2: Canonical `mood.*` events + EventRouter validation

**Files:**
- Modify: `src/CanonicalEvents.h` (after the `Context*` entries, ~line 54)
- Modify: `src/EventRouter.cpp` (`s_validEvents`, lines 15-31)
- Test: `tests/test_mood_engine.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_mood_engine.cpp` (add slot declaration too):

```cpp
void TestMoodEngine::moodEventsValidate()
{
    EventRouter router;
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                       {QStringLiteral("source"), QStringLiteral("system")},
                       {QStringLiteral("event"), QStringLiteral("mood.greeting")}});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("mood.greeting"));
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
python scripts/build_release.py --skip-package; cd build; ./tests/test_mood_engine.exe
```

Expected: FAIL — `spy.count()` is 0 (event rejected as invalid).

- [ ] **Step 3: Register the events**

In `src/CanonicalEvents.h`, after the `ContextTimeOfDay` line:

```cpp
inline constexpr const char *MoodGreeting    = "mood.greeting";
inline constexpr const char *MoodLongSession = "mood.long_session";
inline constexpr const char *MoodMissedYou   = "mood.missed_you";
inline constexpr const char *MoodStageUp     = "mood.stage_up";
```

In `src/EventRouter.cpp`, add inside the `s_validEvents` initializer (alongside the `CE::Context*` entries):

```cpp
    QStringLiteral(CE::MoodGreeting), QStringLiteral(CE::MoodLongSession),
    QStringLiteral(CE::MoodMissedYou), QStringLiteral(CE::MoodStageUp),
```

- [ ] **Step 4: Run to verify pass** — same command; Expected: all pass.

- [ ] **Step 5: Commit**

```powershell
git add src/CanonicalEvents.h src/EventRouter.cpp tests/test_mood_engine.cpp
git commit -m "feat(mood): register mood.* canonical events"
```

---

### Task 3: Relationship integration — stage-up emission + Lonely on launch

**Files:**
- Modify: `src/MoodEngine.h` (add slot + members)
- Modify: `src/MoodEngine.cpp`
- Test: `tests/test_mood_engine.cpp`

Note: `checkProactive`/`emitMood` bodies land in Task 4; this task wires the MemoryManager bond signal and absence→Lonely handling in `loadStats` (temporary minimal `loadStats` that only reads `lastSeenMs` is fine — Task 4 fleshes it out).

- [ ] **Step 1: Write the failing tests**

```cpp
#include <QTemporaryDir>
#include "MemoryManager.h"

void TestMoodEngine::stageUpOncePerLevel()
{
    EventRouter router;
    QTemporaryDir tmp;
    MemoryManager memory(tmp.filePath(QStringLiteral("m.db")));
    MoodEngine mood(&router, &memory);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);

    memory.addBondXP(100000);   // enough to cross several levels
    // mood.stage_up fired exactly once per newly reached level, milestone-guarded
    int stageUps = 0;
    for (int i = 0; i < spy.count(); ++i)
        if (spy.at(i).at(0).toString() == QLatin1String("mood.stage_up")) ++stageUps;
    QVERIFY(stageUps >= 1);
    const int lvl = memory.bondLevel();

    // Re-delivering the same level must NOT refire (milestone guard).
    spy.clear();
    mood.onBondLevelChanged(lvl);
    for (int i = 0; i < spy.count(); ++i)
        QVERIFY(spy.at(i).at(0).toString() != QLatin1String("mood.stage_up"));
}

void TestMoodEngine::lonelySetOnLongAbsence()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T14:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });

    // Simulate a persisted lastSeen 30 h ago via the JSON round-trip seam.
    QTemporaryDir tmp;
    {
        StatisticsPersistence p(tmp.path());
        p.saveSection(QStringLiteral("mood"),
                      {{QStringLiteral("lastSeenMs"), fakeNow - 30LL * 60 * 60 * 1000}});
    }
    mood.loadStats(tmp.path());
    QVERIFY(mood.isLonely());
    QCOMPARE(mood.tier(), MoodEngine::Tier::Lonely);

    // First session.start clears Lonely with an excitement spike.
    mood.onEventProcessed(QStringLiteral("session.start"), {});
    QVERIFY(!mood.isLonely());
    QVERIFY(mood.valence() > 0.0);
}
```

Add `moodEventsValidate` was Task 2; add these two slots to the class.

- [ ] **Step 2: Run to verify fail** — `onBondLevelChanged` doesn't exist; `isLonely()` false after load.

- [ ] **Step 3: Implement**

In `MoodEngine.h` add public slot + private member:

```cpp
public slots:
    void onBondLevelChanged(int newLevel);
```

In `MoodEngine.cpp` constructor, wire bond levels:

```cpp
    if (m_memory) {
        connect(m_memory, &MemoryManager::bondLevelChanged,
                this, &MoodEngine::onBondLevelChanged);
    }
```

Implement:

```cpp
void MoodEngine::onBondLevelChanged(int newLevel)
{
    if (!m_memory) return;
    const QString key = QStringLiteral("mood.stage_up.L%1").arg(newLevel);
    if (m_memory->hasMilestone(key)) return;
    m_memory->setMilestone(key);
    emitMood(QStringLiteral("mood.stage_up"),
             {{QStringLiteral("bondLevel"), newLevel},
              {QStringLiteral("stage"), stageName()}});
}
```

Minimal `loadStats` (Task 4 extends it):

```cpp
void MoodEngine::loadStats(const QString &configDir)
{
    StatisticsPersistence p(configDir);
    const QJsonObject o = p.loadSection(QStringLiteral("mood"));
    m_lastSeenMs = static_cast<qint64>(o.value(QStringLiteral("lastSeenMs")).toDouble());
    if (m_lastSeenMs > 0 && nowMs() - m_lastSeenMs > MISS_ABSENCE_MS) {
        m_lonely = true;
        m_valence = 0.0;
        m_energy = 0.0;
    }
    updateTier();
}
```

- [ ] **Step 4: Run to verify pass.** Note `stageUpOncePerLevel` needs `emitMood` to actually route — Task 4 implements it; for this task an interim `emitMood` that calls `m_router->routeEvent` without cooldowns is acceptable and will be extended:

```cpp
void MoodEngine::emitMood(const QString &name, const QJsonObject &payload)
{
    if (!m_router) return;
    QJsonObject ev = payload;
    ev.insert(QStringLiteral("type"), QStringLiteral("event"));
    ev.insert(QStringLiteral("source"), QStringLiteral("system"));
    ev.insert(QStringLiteral("event"), name);
    m_router->routeEvent(ev);
}
```

- [ ] **Step 5: Commit**

```powershell
git add src/MoodEngine.h src/MoodEngine.cpp tests/test_mood_engine.cpp
git commit -m "feat(mood): bond-level stage-up emission + launch Lonely from lastSeen"
```

---

### Task 4: Proactive behaviors + cooldowns + persistence

**Files:**
- Modify: `src/MoodEngine.cpp` (fill `checkProactive`, cooldown-aware `emitMood`, full `loadStats`/`saveStats`)
- Test: `tests/test_mood_engine.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
void TestMoodEngine::greetingOncePerDay()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T09:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });
    QSignalSpy spy(&router, &EventRouter::eventProcessed);

    mood.onEventProcessed(QStringLiteral("session.start"), {});
    fakeNow += 3600LL * 1000;   // +1 h: second session same day
    mood.onEventProcessed(QStringLiteral("session.start"), {});

    int greetings = 0;
    for (int i = 0; i < spy.count(); ++i)
        if (spy.at(i).at(0).toString() == QLatin1String("mood.greeting")) ++greetings;
    QCOMPARE(greetings, 1);
}

void TestMoodEngine::missedYouAfter24h()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T14:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });
    QTemporaryDir tmp;
    {
        StatisticsPersistence p(tmp.path());
        p.saveSection(QStringLiteral("mood"),
                      {{QStringLiteral("lastSeenMs"), fakeNow - 30LL * 60 * 60 * 1000}});
    }
    mood.loadStats(tmp.path());
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    mood.onEventProcessed(QStringLiteral("session.start"), {});

    bool fired = false;
    for (int i = 0; i < spy.count(); ++i)
        if (spy.at(i).at(0).toString() == QLatin1String("mood.missed_you")) {
            fired = true;
            QCOMPARE(spy.at(i).at(1).toJsonObject()
                         .value(QStringLiteral("hoursAbsent")).toInt(), 30);
        }
    QVERIFY(fired);
}

void TestMoodEngine::longSessionNudgeNeedsLowEnergy()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T14:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });
    QSignalSpy spy(&router, &EventRouter::eventProcessed);

    mood.onEventProcessed(QStringLiteral("session.start"), {});
    fakeNow += 160LL * 60 * 1000;   // +2 h 40 m continuous session
    mood.setVectorForTest(0.0, -0.5);   // tired
    mood.tickForTest();

    bool fired = false;
    for (int i = 0; i < spy.count(); ++i)
        if (spy.at(i).at(0).toString() == QLatin1String("mood.long_session")) fired = true;
    QVERIFY(fired);

    // High energy → no nudge.
    spy.clear();
    MoodEngine fresh(&router, nullptr);
    fresh.setNowFn([&]() { return fakeNow; });
    QSignalSpy spy2(&router, &EventRouter::eventProcessed);
    fresh.onEventProcessed(QStringLiteral("session.start"), {});
    fakeNow += 160LL * 60 * 1000;
    fresh.setVectorForTest(0.5, 0.5);
    fresh.tickForTest();
    const int before = spy2.count();
    fresh.tickForTest();
    for (int i = before; i < spy2.count(); ++i)
        QVERIFY(spy2.at(i).at(0).toString() != QLatin1String("mood.long_session"));
}

void TestMoodEngine::globalProactiveCap()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T09:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });
    QSignalSpy spy(&router, &EventRouter::eventProcessed);

    // Greeting fires on session.start; a missed-you in the same hour must
    // be suppressed by the global 1/hour cap even though its own cooldown
    // has lapsed (lastSeen 30 h ago).
    QTemporaryDir tmp;
    {
        StatisticsPersistence p(tmp.path());
        p.saveSection(QStringLiteral("mood"),
                      {{QStringLiteral("lastSeenMs"), fakeNow - 30LL * 60 * 60 * 1000}});
    }
    mood.loadStats(tmp.path());
    mood.onEventProcessed(QStringLiteral("session.start"), {});

    int proactive = 0;
    for (int i = 0; i < spy.count(); ++i) {
        const QString n = spy.at(i).at(0).toString();
        if (n.startsWith(QLatin1String("mood."))) ++proactive;
    }
    QCOMPARE(proactive, 1);   // greeting won; missed_you suppressed this hour
}

void TestMoodEngine::persistsAcrossReload()
{
    QTemporaryDir tmp;
    EventRouter router;
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T14:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    {
        MoodEngine mood(&router, nullptr);
        mood.setNowFn([&]() { return fakeNow; });
        mood.setVectorForTest(0.42, -0.17);
        mood.saveStats(tmp.path());
    }
    MoodEngine mood2(&router, nullptr);
    mood2.setNowFn([&]() { return fakeNow; });
    mood2.loadStats(tmp.path());
    QVERIFY(qAbs(mood2.valence() - 0.42) < 0.001);
    QVERIFY(qAbs(mood2.energy() + 0.17) < 0.001);
}

void TestMoodEngine::corruptJsonRecovers()
{
    QTemporaryDir tmp;
    StatisticsPersistence p(tmp.path());
    p.saveSection(QStringLiteral("mood"),
                  {{QStringLiteral("valence"), QStringLiteral("garbage")}});
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    mood.loadStats(tmp.path());   // must not crash
    QCOMPARE(mood.valence(), 0.0);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Content);
}
```

- [ ] **Step 2: Run to verify fail** — greeting/missed-you/nudge don't fire; persistence absent.

- [ ] **Step 3: Implement**

Replace `checkProactive`, `emitMood`, `loadStats`, `saveStats` in `MoodEngine.cpp`:

```cpp
void MoodEngine::checkProactive()
{
    const qint64 now = nowMs();
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(now);
    const QString today = local.toString(QStringLiteral("yyyy-MM-dd"));
    const int hour = local.time().hour();

    // Morning greeting: first session.start of a new local date, after 06:00.
    if (m_sessionStartMs > 0 && hour >= 6 && m_lastGreetingDate != today) {
        if (emitMood(QStringLiteral("mood.greeting"), {}))
            m_lastGreetingDate = today;
        return;   // one proactive per check pass
    }

    // Missed-you: session.start after > 24 h absence.
    if (m_sessionStartMs == now && m_lastSeenMs > 0
        && now - m_lastSeenMs > MISS_ABSENCE_MS) {
        emitMood(QStringLiteral("mood.missed_you"),
                 {{QStringLiteral("hoursAbsent"),
                   static_cast<int>((now - m_lastSeenMs) / (60LL * 60 * 1000))}});
        return;
    }

    // Long-session nudge: > 2.5 h continuous AND low energy.
    if (m_sessionStartMs > 0 && now - m_sessionStartMs >= NUDGE_SESSION_AGE_MS
        && m_energy < -0.3) {
        emitMood(QStringLiteral("mood.long_session"),
                 {{QStringLiteral("hours"),
                   (now - m_sessionStartMs) / (60LL * 60 * 1000)}});
    }
}
```

Change `emitMood` to return bool and enforce per-type + global cooldowns:

```cpp
bool MoodEngine::emitMood(const QString &name, const QJsonObject &payload)
{
    if (!m_router) return false;
    const qint64 now = nowMs();

    static const QHash<QString, qint64> cooldowns = {
        {QStringLiteral("mood.greeting"),     20LL * 60 * 60 * 1000},
        {QStringLiteral("mood.long_session"),  3LL * 60 * 60 * 1000},
        {QStringLiteral("mood.missed_you"),   20LL * 60 * 60 * 1000},
        // mood.stage_up: milestone-guarded, no time cooldown
    };
    if (now - m_lastProactiveMs < GLOBAL_PROACTIVE_COOLDOWN_MS) return false;
    const qint64 cd = cooldowns.value(name, 0);
    if (cd > 0 && m_lastFired.contains(name)
        && now - m_lastFired.value(name) < cd) return false;

    m_lastFired.insert(name, now);
    m_lastProactiveMs = now;
    QJsonObject ev = payload;
    ev.insert(QStringLiteral("type"), QStringLiteral("event"));
    ev.insert(QStringLiteral("source"), QStringLiteral("system"));
    ev.insert(QStringLiteral("event"), name);
    m_router->routeEvent(ev);
    return true;
}
```

(Adjust the header declaration: `bool emitMood(const QString &name, const QJsonObject &payload = {});`)

Full persistence:

```cpp
void MoodEngine::loadStats(const QString &configDir)
{
    StatisticsPersistence p(configDir);
    const QJsonObject o = p.loadSection(QStringLiteral("mood"));
    m_valence = qBound(-1.0, o.value(QStringLiteral("valence")).toDouble(0.0), 1.0);
    m_energy  = qBound(-1.0, o.value(QStringLiteral("energy")).toDouble(0.0), 1.0);
    m_lastGreetingDate = o.value(QStringLiteral("lastGreetingDate")).toString();
    m_lastProactiveMs = static_cast<qint64>(
        o.value(QStringLiteral("lastProactiveMs")).toDouble());
    const QJsonObject fired = o.value(QStringLiteral("lastFired")).toObject();
    for (auto it = fired.begin(); it != fired.end(); ++it)
        m_lastFired.insert(it.key(), static_cast<qint64>(it.value().toDouble()));

    m_lastSeenMs = static_cast<qint64>(
        o.value(QStringLiteral("lastSeenMs")).toDouble());
    if (m_lastSeenMs > 0 && nowMs() - m_lastSeenMs > MISS_ABSENCE_MS) {
        m_lonely = true;          // cleared by first session.start
        m_valence = 0.0;
        m_energy = 0.0;
    }
    updateTier();
}

void MoodEngine::saveStats(const QString &configDir)
{
    StatisticsPersistence p(configDir);
    QJsonObject fired;
    for (auto it = m_lastFired.begin(); it != m_lastFired.end(); ++it)
        fired.insert(it.key(), static_cast<double>(it.value()));
    p.saveSection(QStringLiteral("mood"),
                  {{QStringLiteral("valence"), m_valence},
                   {QStringLiteral("energy"), m_energy},
                   {QStringLiteral("lastGreetingDate"), m_lastGreetingDate},
                   {QStringLiteral("lastProactiveMs"),
                    static_cast<double>(m_lastProactiveMs)},
                   {QStringLiteral("lastFired"), fired},
                   {QStringLiteral("lastSeenMs"),
                    static_cast<double>(nowMs())}});
}
```

Note: `m_sessionStartMs == now` in the missed-you check works because `onEventProcessed` sets `m_sessionStartMs = now` immediately before calling `checkProactive()`.

- [ ] **Step 4: Run to verify pass**

```powershell
python scripts/build_release.py --skip-package; cd build; ./tests/test_mood_engine.exe
```

Expected: `Totals: 11 passed, 0 failed`.

- [ ] **Step 5: Commit**

```powershell
git add src/MoodEngine.h src/MoodEngine.cpp tests/test_mood_engine.cpp
git commit -m "feat(mood): proactive behaviors, cooldowns, stats persistence"
```

---

### Task 5: Persona surfacing — pool keys, catalog fallbacks, milestone prompt

**Files:**
- Modify: `src/PersonaEngine.cpp` (`poolTierEvents()` ~line 12; `resolveOnDemand` ~line 170)
- Modify: `assets/i18n/tips.en.json`
- Modify: `assets/i18n/tips.zh_CN.json`
- Test: `tests/test_mood_engine.cpp` (router-level fallback pin)

- [ ] **Step 1: Write the failing test**

```cpp
void TestMoodEngine::moodEventsHaveCatalogFallback()
{
    // PersonaEngine::fallbackTip resolves mood.* through TipsCatalog::eventTip;
    // a missing JSON entry yields an empty body. Pin that all four exist.
    for (const char *name : {"mood.greeting", "mood.long_session",
                             "mood.missed_you", "mood.stage_up"}) {
        const auto tip = TipsCatalog::instance().eventTip(QString::fromLatin1(name));
        QVERIFY2(!tip.body.isEmpty(), name);
    }
}
```

(Include `TipsCatalog.h` at the top of the test file.)

- [ ] **Step 2: Run to verify fail** — bodies empty for `mood.*`.

- [ ] **Step 3: Implement**

`assets/i18n/tips.en.json` — add inside `"events"`:

```json
    "mood.greeting":     {"title": "Good morning",       "body": "New day, new bugs. Let's go!"},
    "mood.long_session": {"title": "Break time?",        "body": "You've been at this a while — stretch a little?"},
    "mood.missed_you":   {"title": "Welcome back",       "body": "I missed you! What are we building today?"},
    "mood.stage_up":     {"title": "We're closer now",   "body": "It feels like we really know each other now."},
```

`assets/i18n/tips.zh_CN.json` — same keys:

```json
    "mood.greeting":     {"title": "早上好",       "body": "新的一天，开始写代码吧！"},
    "mood.long_session": {"title": "休息一下？",   "body": "你已经忙了很久了，起来活动一下吧？"},
    "mood.missed_you":   {"title": "欢迎回来",     "body": "好想你！今天做点什么？"},
    "mood.stage_up":     {"title": "更亲近了",     "body": "感觉我们越来越有默契了。"},
```

`src/PersonaEngine.cpp` — add to `poolTierEvents()` (after the `user.pet`/`user.toss` lines, with a comment):

```cpp
        // Mood engine: routine proactive bubbles are pool-tier; stage_up and
        // long-absence missed_you stay on-demand (rare, high-value moments).
        QStringLiteral("mood.greeting"), QStringLiteral("mood.long_session"),
```

`resolveOnDemand` — replace `Q_UNUSED(payload);` with mood-context enrichment:

```cpp
    // Mood milestones carry tier/stage context for the prompt.
    QString moodLine;
    if (eventName.startsWith(QLatin1String("mood."))) {
        const QString stage = payload.value(QStringLiteral("stage")).toString();
        const int hoursAbsent = payload.value(QStringLiteral("hoursAbsent")).toInt();
        if (!stage.isEmpty())
            moodLine = QStringLiteral("\nRelationship stage: %1").arg(stage);
        if (hoursAbsent > 0)
            moodLine += QStringLiteral("\nHours since user was last seen: %1")
                            .arg(hoursAbsent);
    }
```

and append `moodLine` to `userPrompt` right after construction (both branches — simplest: after the if/else that builds `userPrompt`):

```cpp
    if (!moodLine.isEmpty()) userPrompt += moodLine;
```

Missed-you escalation: in `MoodEngine::checkProactive`, the missed-you emission stays `mood.missed_you`; PersonaEngine routes it on-demand automatically since it is NOT in `poolTierEvents()`. The spec's ">72 h → LLM, ≤72 h → pool" nuance: simplest honest rule — `mood.missed_you` is always on-demand (it fires at most once per 20 h and needs absence context anyway). **Deviation from spec accepted:** no pool variant of missed_you; document in the commit message.

- [ ] **Step 4: Run to verify pass** — same command; Expected: 12 passed.

- [ ] **Step 5: Commit**

```powershell
git add src/PersonaEngine.cpp assets/i18n/tips.en.json assets/i18n/tips.zh_CN.json tests/test_mood_engine.cpp
git commit -m "feat(mood): pool keys + catalog fallbacks + milestone prompt context"
```

---

### Task 6: Peek UI — tray status line + pet tooltip

**Files:**
- Modify: `src/SystemTray.h` / `src/SystemTray.cpp`
- Modify: `src/mainwindow.h` / `src/mainwindow.cpp`
- Modify: `Seelie_zh_CN.ts` (2 new strings)
- Test: none (UI glue; verified manually in Task 8). If a pin is desired, assert `SystemTray::setMoodStatus` stores text on the action — keep manual.

- [ ] **Step 1: SystemTray status line**

`src/SystemTray.h` — add public method + member:

```cpp
    /// Ambient peek: disabled menu line showing current mood + stage.
    void setMoodStatus(const QString &text);
```

```cpp
    QAction *m_moodStatusAction = nullptr;
```

`src/SystemTray.cpp` — in `setupMenu()`, right after `m_trayMenu = new QMenu();` and the font set, insert as the first item:

```cpp
    m_moodStatusAction = m_trayMenu->addAction(QString());
    m_moodStatusAction->setEnabled(false);   // informational, not clickable
    m_moodStatusAction->setVisible(false);   // shown once mood text arrives
    m_trayMenu->addSeparator();
```

Implement:

```cpp
void SystemTray::setMoodStatus(const QString &text)
{
    if (!m_moodStatusAction) return;
    m_moodStatusAction->setText(text);
    m_moodStatusAction->setVisible(!text.isEmpty());
}
```

- [ ] **Step 2: MainWindow tooltip**

`src/mainwindow.h` — add public method:

```cpp
    /// Ambient peek: hover tooltip mirroring the tray mood line.
    void setMoodPeekText(const QString &text);
```

`src/mainwindow.cpp`:

```cpp
void MainWindow::setMoodPeekText(const QString &text)
{
    setToolTip(text);   // empty string clears — Qt hides empty tooltips
}
```

- [ ] **Step 3: i18n strings** (added where the composer lives — Task 7's wiring lambda in main.cpp uses `QObject::tr`; add these two `<message>` blocks to `Seelie_zh_CN.ts` under a `<context><name>MoodEngine</name>`):

```xml
    <message>
        <source>Seelie feels %1 · %2</source>
        <translation>Seelie 现在%1 · %2</translation>
    </message>
    <message>
        <source>%1 (tier)</source>
        <comment>tier names: content/excited/tense/tired/lonely — translate via mapping in code</comment>
        <translation>%1</translation>
    </message>
```

Simpler and more robust: translate tier/stage via a small helper in main.cpp wiring (Task 7) using `QObject::tr` on each of the 5 tier names and 3 stage names — 8 short messages. Add all 8 to the ts:

```xml
    <message><source>content</source><translation>心情不错</translation></message>
    <message><source>excited</source><translation>很兴奋</translation></message>
    <message><source>tense</source><translation>有点烦躁</translation></message>
    <message><source>tired</source><translation>有点累</translation></message>
    <message><source>lonely</source><translation>有点孤单</translation></message>
    <message><source>Stranger</source><translation>初识</translation></message>
    <message><source>Companion</source><translation>伙伴</translation></message>
    <message><source>Partner</source><translation>挚友</translation></message>
```

- [ ] **Step 4: Commit**

```powershell
git add src/SystemTray.h src/SystemTray.cpp src/mainwindow.h src/mainwindow.cpp Seelie_zh_CN.ts
git commit -m "feat(mood): peek UI — tray status line + pet hover tooltip"
```

---

### Task 7: Animation bias — PetStateMachine::setMoodIdleBias + full main.cpp wiring

**Files:**
- Modify: `src/PetStateMachine.h` / `src/PetStateMachine.cpp`
- Modify: `src/main.cpp`
- Test: `tests/test_pet_state_machine.cpp` (add one test)

- [ ] **Step 1: Write the failing test**

Append to `tests/test_pet_state_machine.cpp`:

```cpp
void TestPetStateMachine::moodIdleBiasReplacesIdleTail()
{
    PetStateMachine sm;
    QSignalSpy spy(&sm, &PetStateMachine::animationRequested);
    sm.rebuildChainsFromNameMap({{QStringLiteral("idle"), QStringLiteral("idle_anim")}});
    sm.setMoodIdleBias(QStringLiteral("sleepy_anim"));
    sm.onSyntheticEvent(QStringLiteral("session.start"));   // emits a chain
    QVERIFY(spy.count() > 0);
    const QStringList chain = spy.last().at(0).toStringList();
    QVERIFY(chain.contains(QStringLiteral("sleepy_anim")));
    QVERIFY(!chain.contains(QStringLiteral("idle_anim"))
            || chain.indexOf(QStringLiteral("sleepy_anim"))
               < chain.indexOf(QStringLiteral("idle_anim")));

    sm.setMoodIdleBias(QString());   // clear → original idle tail returns
    sm.onSyntheticEvent(QStringLiteral("session.start"));
    QVERIFY(spy.last().at(0).toStringList().contains(QStringLiteral("idle_anim")));
}
```

(Adjust the trigger event to whatever the existing test file uses to force a chain emission — mirror an existing test's first lines.)

- [ ] **Step 2: Run to verify fail** — `setMoodIdleBias` doesn't exist.

- [ ] **Step 3: Implement**

`src/PetStateMachine.h` — add public method + member:

```cpp
    /// Mood engine hook: when non-empty, appended to emitted chains in place
    /// of m_idleFallback. Caller guarantees the name exists in the active
    /// pack's nameMap. Empty restores the default idle tail.
    void setMoodIdleBias(const QString &animName) { m_moodIdleBias = animName; }
```

```cpp
    QString m_moodIdleBias;
```

`src/PetStateMachine.cpp` — in both chain-tail spots (line ~192 and ~323), replace the `m_idleFallback` tail logic:

```cpp
    const QString tail = m_moodIdleBias.isEmpty() ? m_idleFallback : m_moodIdleBias;
    if (!tail.isEmpty() && !chain.contains(tail)) {
        chain.append(tail);
    }
```

`src/main.cpp` — after the PetStateMachine wiring block (~line 584), add MoodEngine construction + all wiring:

```cpp
// --- Mood engine ---------------------------------------------------------
MoodEngine moodEngine(&eventRouter, &memory);
QObject::connect(&eventRouter, &EventRouter::eventProcessed,
                 &moodEngine, &MoodEngine::onEventProcessed);
moodEngine.loadStats(configDir());
moodEngine.start();

// Peek: tray line + hover tooltip, refreshed on tier/bond changes.
auto refreshMoodPeek = [&]() {
    const QString text = QObject::tr("Seelie feels %1 · %2")
        .arg(QObject::tr(qPrintable(MoodEngine::tierName(moodEngine.tier()))),
             QObject::tr(qPrintable(moodEngine.stageName())));
    w.setMoodPeekText(text);
    tray.setMoodStatus(text);
};
QObject::connect(&moodEngine, &MoodEngine::moodTierChanged,
                 &w, refreshMoodPeek);
QObject::connect(&memory, &MemoryManager::bondLevelChanged,
                 &w, refreshMoodPeek);
refreshMoodPeek();

// Idle animation bias: only when the active pack actually has the variant.
auto applyMoodIdle = [&]() {
    const CharacterPack *pack = packManager.activePack();
    const QString want = QStringLiteral("idle_")
        + MoodEngine::tierName(moodEngine.tier());
    stateMachine.setMoodIdleBias(
        pack ? pack->nameMap().value(want) : QString());
};
QObject::connect(&moodEngine, &MoodEngine::moodTierChanged,
                 &w, applyMoodIdle);
QObject::connect(&packManager, &CharacterPackManager::activePackChanged,
                 &w, applyMoodIdle);
applyMoodIdle();
```

(Adjust `tray` to the actual SystemTray variable name in main.cpp — check the existing construction site; it is built after `w`. If the tray is constructed later in the file, move the `refreshMoodPeek` block to after tray construction.)

Register stats persistence next to the persona registration (~line 617):

```cpp
StatisticsManager::instance()->registerComponent("mood",
    [&]() { moodEngine.loadStats(statsDir); },
    [&]() { moodEngine.saveStats(statsDir); });
```

(Use the same `statsDir` variable the persona registration uses.)

- [ ] **Step 4: Run the full test suite**

```powershell
python scripts/build_release.py --skip-package; cd build; ctest --output-on-failure
```

Expected: all tests pass, including the new PSM test and 12 mood tests.

- [ ] **Step 5: Commit**

```powershell
git add src/PetStateMachine.h src/PetStateMachine.cpp src/main.cpp tests/test_pet_state_machine.cpp
git commit -m "feat(mood): idle animation bias + main wiring + stats registration"
```

---

### Task 8: Manual verification

- [ ] **Step 1: Full release build + launch**

```powershell
python scripts/build_release.py --skip-package
Start-Process build/Seelie.exe -WorkingDirectory (Resolve-Path build)
```

- [ ] **Step 2: Peek check** — hover the pet → tooltip shows mood + stage; tray menu top line shows the same text (disabled).

- [ ] **Step 3: Event-driven mood check**

```powershell
seelie-gateway --source claude-code --event session.start
seelie-gateway --source claude-code --event tool.failed
```

Pet the pet (stroke) → tray line flips toward excited after several strokes.

- [ ] **Step 4: Missed-you check** — close the app, edit `%USERPROFILE%/.config/Seelie/statistics.json` `mood.lastSeenMs` to 30 h ago, relaunch, send `session.start` → missed-you bubble.

- [ ] **Step 5: LLM milestone check** — add a `qwen-plus` LLM profile in Settings → AI (base URL + token from the user's API note), enable persona, force a bond level-up (or temporarily lower the XP curve in a dev build) → `mood.stage_up` bubble shows a real LLM line.

- [ ] **Step 6: Commit any fixes surfaced** — then tag the feature complete in the spec (`Status: shipped`).

---

## Self-Review Notes

- **Spec coverage:** all 9 spec sections map to tasks: §1→T1, §2→T3, §3→T4, §4→T5, §5→T7, §6→T6, §7→T4, §8→T3/T5, §9→tests throughout + T8.
- **Deviation documented in T5:** `mood.missed_you` is always on-demand (no pool variant) — simpler, fires rarely, needs absence context for the prompt.
- **Type consistency:** `emitMood` returns `bool` from Task 4 on (header adjusted there); `setMoodIdleBias`/`m_moodIdleBias` naming consistent T7; `tierName()`/`stageName()` used identically in T6/T7 wiring.
