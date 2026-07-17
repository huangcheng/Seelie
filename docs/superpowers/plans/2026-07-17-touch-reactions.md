# TouchReactions (Spec 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Direct mouse interaction with the pet — stroke-detected petting, grab, toss, and hover — with new FSM overlay states reusing existing pack animations, positive-only affection effects, and canned tip-bubble lines.

**Architecture:** Two pure C++ units (`StrokeDetector`, `VelocityTracker` — merged into one `StrokeDetector` class) decide stroke vs drag inside MainWindow's mouse handlers; `PetStateMachine` gains `Petted`/`Grabbed`/`Tossed` overlay states fed by new `user.*` synthetic events; memory + canned verbal reactions stay in MainWindow mirroring `tryRecordPoke`/`showRandomGreeting`. Spec: `docs/superpowers/specs/2026-07-17-touch-reactions-design.md`.

**Tech Stack:** C++17, Qt6, CMake. No new dependencies, no new assets.

**Conventions (binding):** `QStringLiteral` for literals; UPPERCASE acronyms in identifiers; dense reason-focused comments; conventional commits; test isolation via `QSettings::setPath` redirect in `initTestCase`. `ConfigManager` setters need BOTH the debounced `save()` call AND the `setValue` in `flush()` (Spec 2 Task 3 erratum — do not repeat that bug). Toggle-off path must be byte-identical to today's mouse behavior.

---

### Task 1: ConfigManager `touchReactionsEnabled` (default true)

**Files:**
- Modify: `src/ConfigManager.h` (getter/setter after `contextSensesEnabled` ~line 81; signal after `contextSensesEnabledChanged` ~line 147; member after `m_contextSensesEnabled` ~line 174)
- Modify: `src/ConfigManager.cpp` (load ~line 167; flush ~line 246; setter after `setContextSensesEnabled` ~line 386)
- Test: `tests/test_stroke_detector.cpp` (new — also hosts Task 2 detector tests)
- Modify: `tests/CMakeLists.txt` (`TEST_SOURCES`)

- [ ] **Step 1: Write the failing test**

Create `tests/test_stroke_detector.cpp`:

```cpp
/**
 * test_stroke_detector.cpp
 *
 * Unit tests for TouchReactions (Spec 3):
 *   - ConfigManager touchReactionsEnabled round-trip
 *   - StrokeDetector: reversal counting, jitter rejection, displacement budget,
 *     undecided→drag conversion, reset paths
 *   - VelocityTracker (inside StrokeDetector): EMA, toss threshold, parked decay
 */

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>

#include "ConfigManager.h"
#include "StrokeDetector.h"

class TestStrokeDetector : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Task 1: config key
    void testTouchReactionsDefaultTrue();
    void testTouchReactionsRoundTrip();
    void testTouchReactionsSignal();

private:
    QTemporaryDir m_tmpDir;
};

void TestStrokeDetector::initTestCase()
{
    // Redirect QSettings to a throw-away temp dir (mirrors test_gaming_mode).
    QVERIFY(m_tmpDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmpDir.path());
}

void TestStrokeDetector::testTouchReactionsDefaultTrue()
{
    ConfigManager cfg;
    cfg.load();
    QCOMPARE(cfg.touchReactionsEnabled(), true);
}

void TestStrokeDetector::testTouchReactionsRoundTrip()
{
    {   // Nested scopes: destructor's implicit flush() must persist before
        // the next instance loads (Spec 2 Task 3 erratum — same pattern).
        ConfigManager cfg;
        cfg.load();
        cfg.setTouchReactionsEnabled(false);
    }
    {
        ConfigManager cfg2;
        cfg2.load();
        QCOMPARE(cfg2.touchReactionsEnabled(), false);
        cfg2.setTouchReactionsEnabled(true);  // restore default for other tests
    }
}

void TestStrokeDetector::testTouchReactionsSignal()
{
    ConfigManager cfg;
    cfg.load();
    QSignalSpy spy(&cfg, &ConfigManager::touchReactionsEnabledChanged);
    cfg.setTouchReactionsEnabled(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    cfg.setTouchReactionsEnabled(true);
}

QTEST_MAIN(TestStrokeDetector)
#include "test_stroke_detector.moc"
```

Add to `tests/CMakeLists.txt` `TEST_SOURCES` (after `test_system_context.cpp`):

```cmake
    test_stroke_detector.cpp
```

Do NOT include `StrokeDetector.h` yet (Task 2 creates it — omit that include line in Task 1).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_stroke_detector` — Expected: COMPILE FAIL (`touchReactionsEnabled` missing).

- [ ] **Step 3: Implement**

`src/ConfigManager.h` — after the contextSenses block:

```cpp
    /** Whether TouchReactions mouse gestures (pet/grab/toss/hover) are active. Default true. */
    bool touchReactionsEnabled() const { return m_touchReactionsEnabled; }
    void setTouchReactionsEnabled(bool enabled);
```

Signals — after `contextSensesEnabledChanged`:

```cpp
    void touchReactionsEnabledChanged(bool enabled);
```

Members — after `m_contextSensesEnabled`:

```cpp
    bool m_touchReactionsEnabled = true;
```

`src/ConfigManager.cpp` — in `load()` after the contextSenses read:

```cpp
    m_touchReactionsEnabled = m_settings.value("touchReactions", true).toBool();
```

In `flush()` after the contextSenses `setValue` (REQUIRED — see header note):

```cpp
    m_settings.setValue("touchReactions", m_touchReactionsEnabled);
```

Setter after `setContextSensesEnabled`:

```cpp
void ConfigManager::setTouchReactionsEnabled(bool enabled)
{
    if (m_touchReactionsEnabled == enabled) return;
    m_touchReactionsEnabled = enabled;
    save();
    emit touchReactionsEnabledChanged(enabled);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_stroke_detector && ./build/tests/test_stroke_detector`
Expected: PASS (3 slots + init/cleanup)

- [ ] **Step 5: Commit**

```bash
git add src/ConfigManager.h src/ConfigManager.cpp tests/test_stroke_detector.cpp tests/CMakeLists.txt
git commit -m "feat(config): add touchReactionsEnabled master toggle (default on)"
```

---

### Task 2: StrokeDetector (with VelocityTracker) pure unit

**Files:**
- Create: `src/StrokeDetector.h`
- Create: `src/StrokeDetector.cpp`
- Modify: `CMakeLists.txt` (Seelie sources, after `src/SystemContextEngine.h`)
- Modify: `tests/CMakeLists.txt` (`SEELIEPET_LIB_SOURCES`, after SystemContextEngine lines)
- Test: `tests/test_stroke_detector.cpp`

- [ ] **Step 1: Write the failing tests**

Add `#include "StrokeDetector.h"` to the test file top. Add slots + definitions:

```cpp
    // Task 2: stroke detector
    void testStrokeBasicTwoReversals();
    void testStrokeJitterRejected();
    void testDragConversionOnDisplacement();
    void testStrokePulsesAccumulate();
    void testCancelResetsEverything();
    void testReleaseUndecidedMeansClick();
    void testTossSpeedAboveThreshold();
    void testSlowReleaseIsNotToss();
    void testParkedCursorDecaysSpeed();
```

```cpp
// Helper: feed a horizontal stroke pattern with 10ms spacing.
static void feedMoves(StrokeDetector &d, const QVector<int> &xs, int y, qint64 &nowMs)
{
    for (int x : xs) {
        nowMs += 10;
        d.move(QPoint(x, y), nowMs);
    }
}

void TestStrokeDetector::testStrokeBasicTwoReversals()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    // Right 12px (dir established), left 12px (reversal 1), right 12px (reversal 2 → stroke)
    feedMoves(d, {106, 112, 106, 100, 106, 112}, 100, now);
    QCOMPARE(d.phase(), StrokeDetector::Phase::Stroking);
    QCOMPARE(d.takeStrokePulses(), 1);   // entering Stroking pulses once
    QCOMPARE(d.takeStrokePulses(), 0);   // consumed
}

void TestStrokeDetector::testStrokeJitterRejected()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    // ±4px wiggle — below the 8px reversal threshold, never a stroke, never a drag
    feedMoves(d, {104, 100, 104, 100, 104, 100, 104}, 100, now);
    QCOMPARE(d.phase(), StrokeDetector::Phase::Undecided);
    QCOMPARE(d.takeStrokePulses(), 0);
}

void TestStrokeDetector::testDragConversionOnDisplacement()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    now += 10; d.move(QPoint(110, 100), now);
    QCOMPARE(d.phase(), StrokeDetector::Phase::Undecided);
    now += 10; d.move(QPoint(116, 100), now);   // displacement 16 ≥ 15
    QCOMPARE(d.phase(), StrokeDetector::Phase::Dragging);
    QCOMPARE(d.takeDragEngaged(), true);        // one-shot conversion flag
    QCOMPARE(d.takeDragEngaged(), false);
    QCOMPARE(d.takeStrokePulses(), 0);          // a drag is never a stroke
}

void TestStrokeDetector::testStrokePulsesAccumulate()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    // reversals at: 112→100 (1), 100→112 (2, pulse), 112→100 (3, pulse), 100→112 (4, pulse)
    feedMoves(d, {106, 112, 106, 100, 106, 112, 106, 100, 106, 112}, 100, now);
    QCOMPARE(d.phase(), StrokeDetector::Phase::Stroking);
    QCOMPARE(d.takeStrokePulses(), 3);   // 2nd, 3rd, 4th reversals each pulse
}

void TestStrokeDetector::testCancelResetsEverything()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    feedMoves(d, {106, 112, 106, 100, 106, 112}, 100, now);
    QCOMPARE(d.phase(), StrokeDetector::Phase::Stroking);
    d.cancel();
    QCOMPARE(d.phase(), StrokeDetector::Phase::Idle);
    QCOMPARE(d.takeStrokePulses(), 0);
    QVERIFY(d.releaseSpeedPxPerSec() < 1.0);
}

void TestStrokeDetector::testReleaseUndecidedMeansClick()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    now += 10; d.move(QPoint(102, 101), now);   // tiny movement, no decision
    const StrokeDetector::Phase result = d.release(QPoint(102, 101), now);
    QCOMPARE(result, StrokeDetector::Phase::Undecided);   // caller treats as click
    QCOMPARE(d.phase(), StrokeDetector::Phase::Idle);
}

void TestStrokeDetector::testTossSpeedAboveThreshold()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    // Convert to drag first (displacement ≥ 15), then whip: 10ms per 30px = 3000px/s
    now += 10; d.move(QPoint(116, 100), now);
    for (int i = 1; i <= 10; ++i) { now += 10; d.move(QPoint(116 + i * 30, 100), now); }
    const StrokeDetector::Phase result = d.release(QPoint(416, 100), now);
    QCOMPARE(result, StrokeDetector::Phase::Dragging);
    QVERIFY(d.releaseSpeedPxPerSec() > StrokeDetector::TOSS_SPEED_PX_PER_SEC);
}

void TestStrokeDetector::testSlowReleaseIsNotToss()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    // Drag at 10ms per 2px = 200px/s
    now += 10; d.move(QPoint(116, 100), now);
    for (int i = 1; i <= 10; ++i) { now += 10; d.move(QPoint(116 + i * 2, 100), now); }
    d.release(QPoint(136, 100), now);
    QVERIFY(d.releaseSpeedPxPerSec() < StrokeDetector::TOSS_SPEED_PX_PER_SEC);
}

void TestStrokeDetector::testParkedCursorDecaysSpeed()
{
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    now += 10; d.move(QPoint(116, 100), now);
    for (int i = 1; i <= 10; ++i) { now += 10; d.move(QPoint(116 + i * 30, 100), now); }
    QVERIFY(d.releaseSpeedPxPerSec() > StrokeDetector::TOSS_SPEED_PX_PER_SEC);  // fast now
    now += 500;   // cursor parked half a second before release
    d.release(QPoint(416, 100), now);
    QVERIFY(d.releaseSpeedPxPerSec() < StrokeDetector::TOSS_SPEED_PX_PER_SEC);  // decayed
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: COMPILE FAIL (header missing).

- [ ] **Step 3: Implement**

Create `src/StrokeDetector.h`:

```cpp
#ifndef STROKEDETECTOR_H
#define STROKEDETECTOR_H

#include <QPoint>

/**
 * StrokeDetector (TouchReactions, Spec 3) distinguishes petting strokes from
 * window drags inside one left-button press, and measures release velocity
 * for toss detection. Pure C++ (no QObject) so it is unit-testable without
 * widgets; MainWindow feeds it global cursor positions with timestamps.
 *
 * Lifecycle per press: press() → move()* → release() | cancel().
 *
 * Decision rules (spec §1):
 *  - Undecided→Dragging when displacement from press ≥ 15 px (budget spent
 *    before 2 reversals). takeDragEngaged() reports the conversion once so
 *    MainWindow can start moving the window retroactively at full delta.
 *  - Undecided→Stroking at the 2nd x-direction reversal (each reversal ≥ 8 px
 *    from the last extremum, rejecting jitter). The 2nd and every further
 *    reversal queue one pet pulse each, consumed via takeStrokePulses().
 *  - release() returns the final Phase (Undecided == "it was a click") and
 *    keeps releaseSpeedPxPerSec() readable for the toss check; cancel()
 *    discards everything (leaveEvent / double-click).
 */
class StrokeDetector
{
public:
    enum class Phase { Idle, Undecided, Stroking, Dragging };

    void press(const QPoint &globalPos, qint64 nowMs);
    void move(const QPoint &globalPos, qint64 nowMs);
    Phase release(const QPoint &globalPos, qint64 nowMs);
    void cancel();

    Phase phase() const { return m_phase; }
    bool takeDragEngaged();
    int takeStrokePulses();
    double releaseSpeedPxPerSec() const { return m_speedEma; }

    static constexpr qreal REVERSAL_MIN_PX = 8.0;
    static constexpr qreal DISPLACEMENT_BUDGET_PX = 15.0;
    static constexpr double TOSS_SPEED_PX_PER_SEC = 1500.0;

private:
    void trackSpeed(const QPoint &p, qint64 nowMs);
    void onReversal();

    Phase m_phase = Phase::Idle;
    QPoint m_pressPos;
    QPoint m_lastPos;
    qint64 m_lastMs = 0;

    // Reversal tracking on x (strokes are predominantly horizontal)
    int m_dir = 0;          // +1 rightward, -1 leftward, 0 unestablished
    int m_extremumX = 0;    // farthest x in the current direction
    int m_reversals = 0;
    int m_pendingPulses = 0;
    bool m_dragEngaged = false;

    // Velocity EMA, 100 ms time constant
    double m_speedEma = 0.0;
    static constexpr double SPEED_TAU_MS = 100.0;
};

#endif // STROKEDETECTOR_H
```

Create `src/StrokeDetector.cpp`:

```cpp
#include "StrokeDetector.h"

#include <QLineF>
#include <QtMath>

void StrokeDetector::press(const QPoint &globalPos, qint64 nowMs)
{
    m_phase = Phase::Undecided;
    m_pressPos = globalPos;
    m_lastPos = globalPos;
    m_lastMs = nowMs;
    m_dir = 0;
    m_extremumX = globalPos.x();
    m_reversals = 0;
    m_pendingPulses = 0;
    m_dragEngaged = false;
    m_speedEma = 0.0;
}

void StrokeDetector::move(const QPoint &globalPos, qint64 nowMs)
{
    if (m_phase == Phase::Idle) return;
    trackSpeed(globalPos, nowMs);

    if (m_phase != Phase::Dragging) {
        const QPoint totalDelta = globalPos - m_pressPos;
        if (m_phase == Phase::Undecided
            && totalDelta.manhattanLength() >= DISPLACEMENT_BUDGET_PX) {
            // Budget spent before a stroke formed: it's a drag. MainWindow
            // converts and moves the window at the FULL delta, so the motion
            // is positionally identical to a drag from the start.
            m_phase = Phase::Dragging;
            m_dragEngaged = true;
            m_lastPos = globalPos;
            m_lastMs = nowMs;
            return;
        }

        // Count x-direction reversals with jitter rejection via extremum.
        // Direction establishment is PRESS-RELATIVE (not per-move delta):
        // slow 6px steps must still establish a direction over several moves.
        const int x = globalPos.x();
        if (m_dir == 0) {
            const int fromPress = x - m_pressPos.x();
            if (qAbs(fromPress) >= REVERSAL_MIN_PX) {
                m_dir = (fromPress > 0) ? 1 : -1;
                m_extremumX = x;
            }
        } else if (m_dir > 0) {
            if (x >= m_extremumX) {
                m_extremumX = x;
            } else if (m_extremumX - x >= REVERSAL_MIN_PX) {
                ++m_reversals;
                m_dir = -1;
                m_extremumX = x;
                onReversal();
            }
        } else {
            if (x <= m_extremumX) {
                m_extremumX = x;
            } else if (x - m_extremumX >= REVERSAL_MIN_PX) {
                ++m_reversals;
                m_dir = 1;
                m_extremumX = x;
                onReversal();
            }
        }
    }

    m_lastPos = globalPos;
    m_lastMs = nowMs;
}

StrokeDetector::Phase StrokeDetector::release(const QPoint &globalPos, qint64 nowMs)
{
    const Phase result = m_phase;
    if (m_phase != Phase::Idle) {
        // Zero-distance sample: a cursor parked ≥200ms before release decays
        // the EMA toward 0, so a slow end to a fast drag is never a false toss.
        trackSpeed(m_lastPos, nowMs);
        if (result == Phase::Undecided) m_speedEma = 0.0;
    }
    // Reset detection state but KEEP m_speedEma for the caller's toss check.
    m_phase = Phase::Idle;
    m_dir = 0;
    m_reversals = 0;
    m_pendingPulses = 0;
    m_dragEngaged = false;
    return result;
}

void StrokeDetector::cancel()
{
    m_phase = Phase::Idle;
    m_dir = 0;
    m_reversals = 0;
    m_pendingPulses = 0;
    m_dragEngaged = false;
    m_speedEma = 0.0;
}

bool StrokeDetector::takeDragEngaged()
{
    const bool v = m_dragEngaged;
    m_dragEngaged = false;
    return v;
}

int StrokeDetector::takeStrokePulses()
{
    const int v = m_pendingPulses;
    m_pendingPulses = 0;
    return v;
}

void StrokeDetector::onReversal()
{
    // The 2nd reversal confirms stroking; spec: that reversal and every
    // further one is one stroke endpoint → one pet pulse each.
    if (m_reversals >= 2) {
        if (m_phase == Phase::Undecided) m_phase = Phase::Stroking;
        ++m_pendingPulses;
    }
}

void StrokeDetector::trackSpeed(const QPoint &p, qint64 nowMs)
{
    if (m_lastMs <= 0) return;
    const qint64 dt = nowMs - m_lastMs;
    if (dt <= 0) return;
    const double dist = QLineF(m_lastPos, p).length();
    const double inst = dist * 1000.0 / double(dt);   // px/s
    const double alpha = 1.0 - qExp(-double(dt) / SPEED_TAU_MS);
    m_speedEma = alpha * inst + (1.0 - alpha) * m_speedEma;
}
```

`CMakeLists.txt` — add after `src/SystemContextEngine.h`:

```cmake
    src/StrokeDetector.cpp
    src/StrokeDetector.h
```

`tests/CMakeLists.txt` — add to `SEELIEPET_LIB_SOURCES` after the SystemContextEngine lines:

```cmake
    ${CMAKE_SOURCE_DIR}/src/StrokeDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/StrokeDetector.h
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_stroke_detector && ./build/tests/test_stroke_detector`
Expected: PASS (12 slots + init/cleanup)

- [ ] **Step 5: Commit**

```bash
git add src/StrokeDetector.h src/StrokeDetector.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_stroke_detector.cpp
git commit -m "feat(touch): StrokeDetector — stroke/drag disambiguation + toss velocity EMA"
```

---

### Task 3: FSM touch states (Petted / Grabbed / Tossed)

**Files:**
- Modify: `src/PetStateMachine.h` (State enum ~line 29-37; private `enterSustainedOverlay` near line 86)
- Modify: `src/PetStateMachine.cpp` (ctor chains ~29-36; `onSyntheticEvent` ~130-141; `enterSustainedOverlay` new; `stateName` ~359; `rebuildChainsFromMaps` mappings ~297-305)
- Test: `tests/test_pet_state_machine.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_pet_state_machine.cpp` (slot decls + defs; mirror existing style — `initFsm()` helper exists, see the file):

```cpp
    // Task 3 (Spec 3): touch states
    void testPetOneShotReturnsToIdle();
    void testPetRetriggerRefreshesOneShot();
    void testGrabSustainedUntilGrabEnd();
    void testTossOneShot();
    void testPetOverlayFromWorkingRestoresWorking();
    void testTouchChainsResolveFallback();
```

```cpp
void TestPetStateMachine::testPetOneShotReturnsToIdle()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testPetRetriggerRefreshesOneShot()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTest::qWait(1000);                 // half the one-shot (NOTIFICATION_ONESHOT_MS=2000)
    m_fsm->onSyntheticEvent("user.pet");  // re-stroke → timer restarts
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTest::qWait(1000);                 // total 2000ms but only 1000ms since re-trigger
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testGrabSustainedUntilGrabEnd()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.grab");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Grabbed);
    QTest::qWait(2500);  // > NOTIFICATION_ONESHOT_MS — sustained: must NOT expire
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Grabbed);
    m_fsm->onSyntheticEvent("user.grabEnd");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testTossOneShot()
{
    initFsm();
    m_fsm->onSyntheticEvent("user.toss");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Tossed);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Idle);
}

void TestPetStateMachine::testPetOverlayFromWorkingRestoresWorking()
{
    initFsm();
    m_fsm->onCanonicalEvent("tool.before");
    QCOMPARE(m_fsm->baseState(), PetStateMachine::State::Working);
    m_fsm->onSyntheticEvent("user.pet");
    QCOMPARE(m_fsm->activeState(), PetStateMachine::State::Petted);
    QTRY_COMPARE(m_fsm->activeState(), PetStateMachine::State::Working);
}

void TestPetStateMachine::testTouchChainsResolveFallback()
{
    initFsm();
    QSignalSpy spy(m_fsm, &PetStateMachine::animationRequested);
    m_fsm->onSyntheticEvent("user.pet");
    QVERIFY(spy.count() >= 1);
    const QStringList chain = spy.takeFirst().at(0).toStringList();
    QVERIFY(!chain.isEmpty());
    // Default chain ends with the idle fallback appended by emitChainFor.
    QCOMPARE(chain.last(), QStringLiteral("idle"));
}
```

NOTE: check the file's `initFsm()` helper first — if it builds the FSM with a custom nameMap via `rebuildChainsFromNameMap`, the last test's `idle` fallback expectation may need adjusting to that helper's idle name. Adapt minimally and report the deviation.

- [ ] **Step 2: Run test to verify it fails**

Expected: COMPILE FAIL (`State::Petted` etc. missing) — red.

- [ ] **Step 3: Implement**

`src/PetStateMachine.h` — State enum (add at the END to keep existing values stable):

```cpp
    enum class State {
        Idle,
        Greeting,
        Thinking,
        Working,
        Reviewing,
        Failed,
        Celebrating,
        // Spec 3 (TouchReactions): touch overlay states.
        Petted,
        Grabbed,
        Tossed,
    };
```

Private methods — after `void enterOneShot(State s, int durationMs);`:

```cpp
    /// Sustained overlay with NO timer (Grabbed during window drag). Same
    /// save/emit shape as enterOneShot; exited explicitly via grabEnd →
    /// onOneShotFinished (shared restore path, grace re-arm included).
    void enterSustainedOverlay(State s);
```

`src/PetStateMachine.cpp` — ctor, after the Celebrating chain (line 36):

```cpp
    // Spec 3 touch overlays: semantic-first candidates; packs without touch
    // frames fall back to attention/celebration/running animations.
    m_chains[State::Petted]  = {"pat", "happy", "celebrate", "Congratulate", "TouchHead", "Tap"};
    m_chains[State::Grabbed] = {"grab", "alert", "Alert", "GetAttention", "TouchBody", "Tap"};
    m_chains[State::Tossed]  = {"toss", "running", "running-right", "GestureUp", "TouchBody", "Tap"};
```

`onSyntheticEvent` — add after the click/doubleclick block (before the hover comment):

```cpp
    if (eventName == "user.pet") {
        // No Idle gate (unlike click→Greeting): a physical interaction
        // interrupts whatever the pet is doing; restore machinery returns
        // to the saved sustained state afterwards.
        enterOneShot(State::Petted, NOTIFICATION_ONESHOT_MS);
        return;
    }
    if (eventName == "user.toss") {
        enterOneShot(State::Tossed, NOTIFICATION_ONESHOT_MS);
        return;
    }
    if (eventName == "user.grab") {
        enterSustainedOverlay(State::Grabbed);
        return;
    }
    if (eventName == "user.grabEnd") {
        onOneShotFinished();  // shared restore: overlay clear + grace re-arm
        return;
    }
```

New method after `enterOneShot`:

```cpp
void PetStateMachine::enterSustainedOverlay(State s)
{
    // Sustained overlay: enterOneShot's save/emit shape without the timer —
    // the caller exits explicitly (user.grabEnd → onOneShotFinished).
    if (m_overlayState == State::Idle) {
        m_savedSustained = m_baseState;
    }
    m_overlayState = s;
    emit stateChanged(activeState());
    emitChainFor(s, HighPriority);
}
```

`stateName` — add cases:

```cpp
        case State::Petted: return "Petted";
        case State::Grabbed: return "Grabbed";
        case State::Tossed: return "Tossed";
```

`rebuildChainsFromMaps` mappings vector — add:

```cpp
        {State::Petted,      "Petted",      {"pat", "happy", "celebrate", "congratulate"}},
        {State::Grabbed,     "Grabbed",     {"grab", "alert", "getattention", "attention"}},
        {State::Tossed,      "Tossed",      {"toss", "running", "gesture_up"}},
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_pet_state_machine && ./build/tests/test_pet_state_machine`
Expected: PASS (all existing 21 + 6 new). Full `ctest --test-dir build 2>&1 | tail -3` green.

- [ ] **Step 5: Commit**

```bash
git add src/PetStateMachine.h src/PetStateMachine.cpp tests/test_pet_state_machine.cpp
git commit -m "feat(fsm): Petted/Grabbed/Tossed touch overlay states + user.* events"
```

---

### Task 4: TipsCatalog touch lines + JSON pools

**Files:**
- Modify: `src/TipsCatalog.h` (accessor + Bundle member)
- Modify: `src/TipsCatalog.cpp` (loadBundle touch parsing + accessor)
- Modify: `assets/i18n/tips.en.json`, `assets/i18n/tips.zh_CN.json` (new "touch" section)
- Test: `tests/test_stroke_detector.cpp`

- [ ] **Step 1: Write the failing tests**

Add slots + defs to `tests/test_stroke_detector.cpp`:

```cpp
    // Task 4: touch line pools
    void testTipsJsonTouchPools();
    void testTouchLineAccessor();
    void testTouchLineUnknownGestureEmpty();
```

```cpp
void TestStrokeDetector::testTipsJsonTouchPools()
{
    for (const QString &file : {QStringLiteral("/assets/i18n/tips.en.json"),
                                QStringLiteral("/assets/i18n/tips.zh_CN.json")}) {
        QFile f(QStringLiteral(SOURCE_DIR) + file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject touch = QJsonDocument::fromJson(f.readAll())
            .object().value(QStringLiteral("touch")).toObject();
        QVERIFY(touch.contains(QStringLiteral("pet")));
        QVERIFY(touch.contains(QStringLiteral("toss")));
        QVERIFY(touch.value(QStringLiteral("pet")).toArray().size() >= 3);
        QVERIFY(touch.value(QStringLiteral("toss")).toArray().size() >= 3);
    }
}

void TestStrokeDetector::testTouchLineAccessor()
{
    // TipsCatalog singleton reads the qrc bundle (all test targets carry it).
    const auto tip = TipsCatalog::instance().touchLine(QStringLiteral("pet"));
    QVERIFY(!tip.body.isEmpty());
}

void TestStrokeDetector::testTouchLineUnknownGestureEmpty()
{
    const auto tip = TipsCatalog::instance().touchLine(QStringLiteral("backflip"));
    QVERIFY(tip.body.isEmpty());
}
```

(Also `#include "TipsCatalog.h"` and `<QJsonDocument>`/`<QFile>` as needed.)

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL (no "touch" section; no `touchLine` method — compile fail on the accessor first).

- [ ] **Step 3: Implement**

`src/TipsCatalog.h` — after `randomGreeting()`:

```cpp
    /// Pick one touch-reaction line at random for `gesture` ("pet", "toss").
    /// Empty Tip if the gesture has no lines in active or fallback locale.
    Tip touchLine(const QString &gesture) const;
```

Bundle struct — after `messages`:

```cpp
        QHash<QString, QVector<Tip>> touch;   // gesture → line pool
```

`src/TipsCatalog.cpp` — accessor after `randomGreeting`:

```cpp
TipsCatalog::Tip TipsCatalog::touchLine(const QString &gesture) const
{
    const Bundle &active = activeBundle();
    if (auto it = active.touch.constFind(gesture);
        it != active.touch.constEnd() && !it.value().isEmpty()) {
        return substitute(it.value().at(
            QRandomGenerator::global()->bounded(it.value().size())));
    }
    const Bundle &fb = fallbackBundle();
    if (auto it = fb.touch.constFind(gesture);
        it != fb.touch.constEnd() && !it.value().isEmpty()) {
        return substitute(it.value().at(
            QRandomGenerator::global()->bounded(it.value().size())));
    }
    return {};
}
```

`loadBundle` — after the messages loop (before the qDebug):

```cpp
    const QJsonObject touch = root.value(QStringLiteral("touch")).toObject();
    for (auto it = touch.begin(); it != touch.end(); ++it) {
        QVector<Tip> lines;
        const QJsonArray arr = it.value().toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject t = v.toObject();
            lines.append(Tip{t.value(QStringLiteral("title")).toString(),
                             t.value(QStringLiteral("body")).toString()});
        }
        b.touch.insert(it.key(), lines);
    }
```

`assets/i18n/tips.en.json` — add a `"touch"` section after `"messages"` (trailing-comma care):

```json
  "touch": {
    "pet": [
      {"title": "Mmm…",          "body": "That feels nice."},
      {"title": "Purrr~",        "body": "More, more!"},
      {"title": "Hey!",          "body": "That tickles!"},
      {"title": "Happy wiggle",  "body": "I like being petted."}
    ],
    "toss": [
      {"title": "Wheee!",        "body": "I'm flying!"},
      {"title": "Whoa!",         "body": "Put me down!"},
      {"title": "Ack!",          "body": "What was that for?"},
      {"title": "Dizzy…",        "body": "The room is spinning."}
    ]
  }
```

`assets/i18n/tips.zh_CN.json` — same position:

```json
  "touch": {
    "pet": [
      {"title": "嗯…",            "body": "好舒服呀。"},
      {"title": "呼噜呼噜~",      "body": "再来一下！"},
      {"title": "嘿嘿",           "body": "有点痒！"},
      {"title": "开心",           "body": "喜欢被摸摸。"}
    ],
    "toss": [
      {"title": "哇——",           "body": "我飞起来了！"},
      {"title": "哇！",           "body": "快放我下来！"},
      {"title": "啊！",           "body": "干嘛扔我呀？"},
      {"title": "晕…",            "body": "天旋地转……"}
    ]
  }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_stroke_detector && ./build/tests/test_stroke_detector`
Expected: PASS (15 slots). Note: the qrc resource rebuild picks up the JSON changes automatically (tips qrc is in every test target since Spec 2).

- [ ] **Step 5: Commit**

```bash
git add src/TipsCatalog.h src/TipsCatalog.cpp assets/i18n/tips.en.json assets/i18n/tips.zh_CN.json tests/test_stroke_detector.cpp
git commit -m "feat(touch): TipsCatalog touchLine pools (en + zh_CN)"
```

---

### Task 5: MainWindow mouse integration (detector wiring)

**Files:**
- Modify: `src/mainwindow.h` (members: `StrokeDetector m_strokeDetector;` `bool m_strokeSession = false;` + private methods `void onPetStroke();` `void onTossDetected();` near `tryRecordPoke`; include)
- Modify: `src/mainwindow.cpp` (mousePressEvent, mouseMoveEvent, mouseReleaseEvent, leaveEvent)
- Test: existing suites only (wiring is widget-level; verified via build + suite + manual smoke)

This is the highest-integration-risk task. The **toggle-off path must remain byte-identical to today**.

- [ ] **Step 1: Read the current handlers first**

Re-read `src/mainwindow.cpp:388-529` before editing. The current behavior is the baseline for the toggle-off path.

- [ ] **Step 2: Implement — header**

`src/mainwindow.h` — add include at top:

```cpp
#include "StrokeDetector.h"
```

Private methods — after `void tryRecordPoke();` (locate it; near onEventForMemory):

```cpp
    /// Spec 3: one pet pulse from the StrokeDetector. Task 5 fires the FSM
    /// overlay; Task 6 adds memory + canned lines.
    void onPetStroke();
    /// Spec 3: release velocity exceeded TOSS_SPEED_PX_PER_SEC at drag end.
    void onTossDetected();
```

Members — after the drag-state block (`m_dragging`/`DRAG_THRESHOLD`):

```cpp
    // Spec 3 (TouchReactions): stroke/drag disambiguation. m_strokeSession is
    // true while a petRect press is being classified (toggle on only).
    StrokeDetector m_strokeDetector;
    bool m_strokeSession = false;
```

- [ ] **Step 3: Implement — mousePressEvent**

```cpp
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isInPetRect(event->pos())) {
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragWindowPos = pos();
        m_dragging = false;
        if (m_config->touchReactionsEnabled()) {
            m_strokeDetector.press(m_dragStartPos, QDateTime::currentMSecsSinceEpoch());
            m_strokeSession = true;
        }
    }
    QWidget::mousePressEvent(event);
}
```

- [ ] **Step 4: Implement — mouseMoveEvent**

Keep the Live2D pointer-tracking block at the top UNCHANGED. Replace the drag block:

```cpp
    if (event->buttons() & Qt::LeftButton) {
        const QPoint globalNow = event->globalPosition().toPoint();
        const QPoint delta = globalNow - m_dragStartPos;

        if (m_strokeSession) {
            m_strokeDetector.move(globalNow, QDateTime::currentMSecsSinceEpoch());
            // Stroke pulses (2nd+ reversal) → pet reaction per stroke endpoint.
            for (int pulses = m_strokeDetector.takeStrokePulses(); pulses > 0; --pulses) {
                onPetStroke();
            }
            // Drag conversion: the window starts moving now, at the FULL delta
            // (positionally identical to a drag from press — no jump).
            if (m_strokeDetector.takeDragEngaged()) {
                m_dragging = true;
                if (m_stateMachine) {
                    m_stateMachine->onSyntheticEvent(QStringLiteral("user.grab"));
                }
                // Only sprite packs ship a 'gesture_down' animation (unchanged).
                if (m_engine->hasAnimations()) {
                    m_engine->playAnimation("gesture_down", SpriteAnimationEngine::HighPriority);
                }
            }
        } else if (!m_dragging && delta.manhattanLength() > DRAG_THRESHOLD) {
            // Toggle-off / press-outside-petRect path: today's behavior,
            // byte-identical (no user.grab, no detector).
            m_dragging = true;
            if (m_engine->hasAnimations()) {
                m_engine->playAnimation("gesture_down", SpriteAnimationEngine::HighPriority);
            }
        }

        if (m_dragging) {
            move(m_dragWindowPos + delta);
        }
    }
```

- [ ] **Step 5: Implement — mouseReleaseEvent**

Replace the left-button block:

```cpp
    if (event->button() == Qt::LeftButton) {
        if (m_strokeSession) {
            m_strokeSession = false;
            const StrokeDetector::Phase phase = m_strokeDetector.release(
                event->globalPosition().toPoint(), QDateTime::currentMSecsSinceEpoch());

            if (phase == StrokeDetector::Phase::Dragging) {
                m_dragging = false;
                if (m_stateMachine) {
                    m_stateMachine->onSyntheticEvent(QStringLiteral("user.grabEnd"));
                }
                if (m_strokeDetector.releaseSpeedPxPerSec()
                        > StrokeDetector::TOSS_SPEED_PX_PER_SEC) {
                    onTossDetected();
                } else if (m_engine->hasAnimations()) {
                    m_engine->playAnimation("lookdown", SpriteAnimationEngine::HighPriority);
                    m_engine->playAnimation("rest", SpriteAnimationEngine::NormalPriority);
                }
                emit positionChanged(pos());
                QWidget::mouseReleaseEvent(event);
                return;
            }
            if (phase == StrokeDetector::Phase::Stroking) {
                // Stroke session over: no click, no drag-release effects.
                QWidget::mouseReleaseEvent(event);
                return;
            }
            // Phase::Undecided → it was a click: fall through to today's path.
        }

        if (m_dragging) {
            m_dragging = false;
            if (m_engine->hasAnimations()) {
                m_engine->playAnimation("lookdown", SpriteAnimationEngine::HighPriority);
                m_engine->playAnimation("rest", SpriteAnimationEngine::NormalPriority);
            }
            emit positionChanged(pos());
        } else if (isInPetRect(event->pos())) {
            // Poke write (Task 9 → Task 10 Rider B): throttle extracted to
            // tryRecordPoke() (2s cooldown shared with dblclick). Placed before
            // the m_stateMachine split so both the FSM path and the legacy
            // fallback path count as a poke.
            tryRecordPoke();
            // Route mouse-click through FSM so the state machine handles
            // user interaction and can trigger the appropriate animation chain.
            if (m_stateMachine) {
                m_stateMachine->onSyntheticEvent(QStringLiteral("user.click"));
                showRandomGreeting();
                QWidget::mouseReleaseEvent(event);
                return;
            }
            // Fallback for sprite packs without an event router wired.
            const QStringList clickAnims = {"click1", "click2"};
            const QString anim = clickAnims.at(QRandomGenerator::global()->bounded(clickAnims.size()));
            m_engine->playAnimation(anim, SpriteAnimationEngine::HighPriority);
            showRandomGreeting();
        }
    }
```

(The two blocks above are today's code verbatim — keep them EXACTLY as-is; they are shown here so there is no ambiguity about what "unchanged" means.)

- [ ] **Step 6: Implement — leaveEvent + onPetStroke/onTossDetected stubs**

`leaveEvent` — add before the existing hoverLeave emission:

```cpp
    if (m_strokeSession) {
        // Leaving mid-stroke cancels it (spec §6); a converted drag is
        // unaffected — the window keeps following the cursor.
        if (m_strokeDetector.phase() != StrokeDetector::Phase::Dragging) {
            m_strokeSession = false;
            m_strokeDetector.cancel();
        }
    }
```

Stubs after `tryRecordPoke` (Task 6 fills the bodies; Task 5 needs them to link):

```cpp
void MainWindow::onPetStroke()
{
    if (m_stateMachine) {
        m_stateMachine->onSyntheticEvent(QStringLiteral("user.pet"));
    }
}

void MainWindow::onTossDetected()
{
    if (m_stateMachine) {
        m_stateMachine->onSyntheticEvent(QStringLiteral("user.toss"));
    }
}
```

- [ ] **Step 7: Build + verify**

Run: `cmake --build build -j 10` (clean) && `ctest --test-dir build 2>&1 | tail -3` (18/18 — no new automated tests this task; FSM/detector behavior is covered by Tasks 2-3's suites).
Also sanity: `./build/tests/test_pet_state_machine | grep Totals`.

- [ ] **Step 8: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp
git commit -m "feat(touch): wire StrokeDetector into MainWindow mouse handlers"
```

---

### Task 6: Touch memory effects + canned bubbles

**Files:**
- Modify: `src/mainwindow.h` (members: `qint64 m_lastPetWriteMs = 0;` `qint64 m_lastHoverWriteMs = 0;` + `void showTouchBubble(const QString &gesture);`)
- Modify: `src/mainwindow.cpp` (`onPetStroke`, `onTossDetected`, `enterEvent`, `showTouchBubble`)
- Test: none new (throttle/bubble logic is timing/UI; covered by build + suite + smoke)

- [ ] **Step 1: Implement — header additions**

After `m_lastPokeWriteMs`:

```cpp
    qint64 m_lastPetWriteMs = 0;    // pet affection throttle (2s, spec §3)
    qint64 m_lastHoverWriteMs = 0;  // hover affection throttle (60s, spec §3)
```

After `showTouchBubble` declaration spot (near `showRandomGreeting` decl):

```cpp
    /// Random canned line for a touch gesture ("pet"/"toss") via TipsCatalog.
    void showTouchBubble(const QString &gesture);
```

- [ ] **Step 2: Implement — onPetStroke (full body, replacing the Task-5 stub)**

```cpp
void MainWindow::onPetStroke()
{
    if (m_stateMachine) {
        m_stateMachine->onSyntheticEvent(QStringLiteral("user.pet"));
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_memory && m_memory->isValid() && nowMs - m_lastPetWriteMs >= 2000) {
        m_lastPetWriteMs = nowMs;
        m_memory->increment(QStringLiteral("stats.pets"));
        m_memory->addAffection(2);
        m_memory->checkMilestone(QStringLiteral("first_pet"),
            tr("First pet!"),
            tr("You petted Seelie for the first time."));
    }

    // Spam control: bubble on ~1 of 3 strokes (spec §4).
    if (QRandomGenerator::global()->bounded(3) == 0) {
        showTouchBubble(QStringLiteral("pet"));
    }
}
```

- [ ] **Step 3: Implement — onTossDetected (full body)**

```cpp
void MainWindow::onTossDetected()
{
    if (m_stateMachine) {
        m_stateMachine->onSyntheticEvent(QStringLiteral("user.toss"));
    }
    if (m_memory && m_memory->isValid()) {
        m_memory->increment(QStringLiteral("stats.tosses"));
        m_memory->checkMilestone(QStringLiteral("first_toss"),
            tr("First toss!"),
            tr("You threw Seelie across the screen."));
    }
    showTouchBubble(QStringLiteral("toss"));  // always (spec §4)
}
```

- [ ] **Step 4: Implement — enterEvent hover effect**

In `enterEvent`, after the existing hoverLeave/hoverEnter emission block:

```cpp
    if (m_config->touchReactionsEnabled()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_memory && m_memory->isValid() && nowMs - m_lastHoverWriteMs >= 60000) {
            m_lastHoverWriteMs = nowMs;
            m_memory->increment(QStringLiteral("stats.hover"));
            m_memory->addAffection(1);
        }
    }
```

- [ ] **Step 5: Implement — showTouchBubble**

After `showRandomGreeting` (mirror its shape, minus the dedup/substitution):

```cpp
void MainWindow::showTouchBubble(const QString &gesture)
{
    if (!m_tipWidget) return;
    const auto tip = TipsCatalog::instance().touchLine(gesture);
    if (tip.title.isEmpty()) return;
    m_tipWidget->showBubble(tip.title, tip.body, TipWidget::TipBubble);
}
```

Check `showRandomGreeting`'s exact `showBubble` call signature first (it may pass a source label as 4th arg) — match it.

- [ ] **Step 6: Implement — grab stats**

In `mouseMoveEvent`'s drag-conversion branch (the `takeDragEngaged()` one), add after the `user.grab` emission:

```cpp
                if (m_memory && m_memory->isValid()) {
                    m_memory->increment(QStringLiteral("stats.grabs"));
                }
```

- [ ] **Step 7: Build + verify**

Run: `cmake --build build -j 10` (clean) && `ctest --test-dir build 2>&1 | tail -3` (18/18).

- [ ] **Step 8: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp
git commit -m "feat(touch): affection/stats/milestones + canned touch bubbles"
```

---

### Task 7: Settings checkbox + zh_CN.ts entries

**Files:**
- Modify: `src/SettingsPanelWidget.h` (members `m_touchReactionsLabel`, `m_touchReactionsCheck`; slot `onTouchReactionsToggled`)
- Modify: `src/SettingsPanelWidget.cpp` (Interaction group row; slot impl; retranslateUi if it re-applies label texts — CHECK how m_tipBubblesLabel is handled there and mirror)
- Modify: `Seelie_zh_CN.ts` (add `<message>` entries for "Touch Reactions", "First pet!", "You petted Seelie for the first time.", "First toss!", "You threw Seelie across the screen.")

- [ ] **Step 1: Implement — panel row (mirror the tipBubbles row exactly)**

In the Interaction group after the tipBubbles row (grid row 2):

```cpp
    m_touchReactionsLabel = new QLabel(tr("Touch Reactions"), m_contentWidget);
    m_touchReactionsLabel->setFont(harmonyFont(10));
    m_touchReactionsLabel->setStyleSheet("color: black; background: transparent;");
    m_touchReactionsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_touchReactionsCheck = new CheckMarkBox(m_contentWidget);
    m_touchReactionsCheck->setFixedSize(16, 16);
    m_touchReactionsCheck->setChecked(m_config->touchReactionsEnabled());
    m_touchReactionsCheck->setStyleSheet(m_autoStartCheck->styleSheet());
    connect(m_touchReactionsCheck, &QCheckBox::toggled,
            this, &SettingsPanelWidget::onTouchReactionsToggled);
    connect(m_config, &ConfigManager::touchReactionsEnabledChanged,
            this, [this](bool enabled) {
        QSignalBlocker blocker(m_touchReactionsCheck);
        m_touchReactionsCheck->setChecked(enabled);
    });

    interactGrid->addWidget(m_touchReactionsLabel, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    interactGrid->addWidget(m_touchReactionsCheck, 2, 1, Qt::AlignLeft | Qt::AlignVCenter);
```

Slot (mirror `onTipBubblesToggled` — read it first and copy its shape):

```cpp
void SettingsPanelWidget::onTouchReactionsToggled(bool checked)
{
    m_config->setTouchReactionsEnabled(checked);
}
```

Header: declare `m_touchReactionsLabel`/`m_touchReactionsCheck` (match sibling types: `QLabel *`, `CheckMarkBox *`) and the slot. Check `retranslateUi()`/`onLanguageChanged` — if labels' texts are re-set there, add the new label the same way.

- [ ] **Step 2: Implement — zh_CN.ts entries**

In the `SettingsPanelWidget` context near "Event Tips":

```xml
    <message>
        <source>Touch Reactions</source>
        <translation>触摸互动</translation>
    </message>
```

In the `MainWindow` context (locate it; milestone strings come from `tr()` in MainWindow):

```xml
    <message>
        <source>First pet!</source>
        <translation>第一次抚摸！</translation>
    </message>
    <message>
        <source>You petted Seelie for the first time.</source>
        <translation>你第一次抚摸了 Seelie。</translation>
    </message>
    <message>
        <source>First toss!</source>
        <translation>第一次抛接！</translation>
    </message>
    <message>
        <source>You threw Seelie across the screen.</source>
        <translation>你把 Seelie 扔了出去。</translation>
    </message>
```

- [ ] **Step 3: Verify**

Run: `cmake --build build -j 10` — clean AND no new lrelease duplicate/missing warnings (watch the lrelease lines). `ctest --test-dir build 2>&1 | tail -3` (18/18).

- [ ] **Step 4: Commit**

```bash
git add src/SettingsPanelWidget.h src/SettingsPanelWidget.cpp Seelie_zh_CN.ts
git commit -m "feat(settings): Touch Reactions checkbox + zh_CN touch strings"
```

---

### Task 8: Full verification + TODO.md

**Files:**
- Modify: `TODO.md`
- Test: whole suite

- [ ] **Step 1: Full build + suite**

Run: `cmake --build build -j 10 && ctest --test-dir build`
Expected: build clean; all green (18/18 pre-existing + test_stroke_detector as #19, plus 6 new FSM slots inside test_pet_state_machine).

- [ ] **Step 2: Manual smoke checklist for the user (report only)**

- Launch `open build/Seelie.app`: stroke the pet slowly (short back-and-forth without moving the window) → Petted animation + occasional bubble; `stats.pets` increments (memory.db at `~/Library/Preferences/memory.db`).
- Drag the window slowly → moves exactly as before; release slowly → lookdown/rest (no toss).
- Fling the window fast → Tossed animation + toss bubble; `stats.tosses` increments.
- Settings → Interaction → Touch Reactions off → all behavior reverts to pre-Spec-3.

- [ ] **Step 3: TODO.md + commit**

§2 Spec 3 bullet → `[x]` SHIPPED 2026-07-17 on branch `touch-reactions` (list: stroke detector, 3 FSM states, memory effects, canned lines, settings toggle; pending: merge + manual smoke + momentum glide parked).

```bash
git add TODO.md
git commit -m "docs: mark Spec 3 (TouchReactions) shipped"
```

---

## Self-Review Notes (filled at plan completion)

- **Spec coverage:** toggle (T1) · stroke/velocity detection (T2) · FSM states+chains (T3) · canned lines (T4) · mouse wiring (T5) · memory+bubbles (T6) · settings+i18n (T7) · verification (T8). Hover memory-only per spec §1 ✓ (T6). Milestones first_pet/first_toss ✓ (T6). Toggle-off byte-identical ✓ (T5 constraint).
- **Type consistency:** `StrokeDetector::Phase{Idle,Undecided,Stroking,Dragging}` uniform; `takeStrokePulses/takeDragEngaged/releaseSpeedPxPerSec` match header; FSM `State::Petted/Grabbed/Tossed` added at enum END (Q_ENUM/value stability); `touchLine(const QString &)` returns `TipsCatalog::Tip`.
- **Deliberate deviations from first-draft spec:** VelocityTracker folded into StrokeDetector (one class, fewer files — spec mentioned two units; the class doc comment records it); pet/toss have NO Idle gate on the FSM overlay (unlike click→Greeting) — spec §2 says overlays interrupt; recorded here.
- **Known untested-by-automation areas:** T5/T6 widget wiring (detector purity + FSM suites cover logic; manual smoke assigned to user in T8).

---

## Execution Errata (recorded 2026-07-17, after subagent-driven execution)

Issues found by implementers' TDD red phases or the per-task review loops; all fixed
in code. Kept as lessons, per the pet-memory-2/context-senses precedent.

1. **T3 — one-shot timer raced the sustained overlay (review catch).** `user.grab`
   during an active Petted/Tossed one-shot left `m_oneShotTimer` running; its
   timeout cleared Grabbed mid-drag. Fixed with `m_oneShotTimer.stop()` in
   `enterSustainedOverlay` + `testGrabDuringOneShotCancelsTimer` (`a7a8606`).
   *Lesson: a shared single timer across overlay kinds must be cancelled on
   every kind transition.*
2. **T1 — signal test missed the early-return path.** Strengthened to verify
   no-emit-on-same-value (`7bbdeb6`), mirroring the test_gaming_mode donor.
3. **T2 — invariants unpinned.** Added tests for vertical→drag conversion,
   stroke-persistence-beyond-budget, and move-before-press Idle guard
   (`bccc2b9`). *Lesson: deliberate design constraints need tests, or a future
   maintainer "fixes" them.*
4. **T6 — milestone bubble overwritten by canned bubble.** First-ever pet/toss
   showed the canned line instead of the one-time milestone. Guard added via
   `hasMilestone`-before-`checkMilestone` capture (`95b99be`).
5. **Chain candidates adjusted for Live2D fallbacks.** The ctor default chains
   use `TouchHead`/`TouchBody`/`Tap` (Live2D names) where the spec table had
   `Greeting`/`running-left`/`attention`; the `rebuildChainsFromMaps` lowercase
   canonical candidates match the spec. Pragmatic for current packs; true
   touch frames in future packs resolve first either way.
6. **i18n tone alignment.** zh first-toss title aligned to the body's verb
   (抛接→抛出, `136afc0`); zh pet title tonal fix (开心→开心~, `2006584`);
   catalog load log gained the touch-pool count (`2006584`).
7. **Known edge (no fix planned):** drag → cursor leaves window (`grabEnd`
   delivered) → re-enters holding the button: the window keeps moving
   correctly, but the Grabbed overlay does not re-engage until the next grab.
   Harmless; documented here.
8. **`mouseDoubleClickEvent` detector reset is implicit.** Qt's
   press→release→doubleClick sequence means the detector is already reset via
   the release; no explicit cancel in the handler (comment added post-review).

**Final state:** 14 commits on `touch-reactions`, suite 19/19 (12 detector +
7 FSM touch tests), final whole-implementation review verdict READY TO MERGE.
