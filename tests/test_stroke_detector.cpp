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
#include <QPoint>

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
    void testVerticalMovementConvertsToDrag();
    void testStrokingPersistsBeyondBudget();
    void testMoveBeforePressIsNoop();

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
    cfg.setTouchReactionsEnabled(true);
    QCOMPARE(spy.count(), 2);
    cfg.setTouchReactionsEnabled(true);  // same value — early-return, no emit
    QCOMPARE(spy.count(), 2);
}

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
    QCOMPARE(d.releaseSpeedPxPerSec(), 0.0);   // never a false toss candidate
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

void TestStrokeDetector::testVerticalMovementConvertsToDrag()
{
    // Design pin: x-only reversal counting means pure-vertical movement can
    // never form a stroke — it hits the displacement budget and becomes a
    // drag. Correct UX: vertical pulls on the pet ARE drags (spec §1:
    // "strokes are predominantly horizontal").
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    now += 10; d.move(QPoint(100, 116), now);   // pure vertical, manhattan 16 ≥ 15
    QCOMPARE(d.phase(), StrokeDetector::Phase::Dragging);
}

void TestStrokeDetector::testStrokingPersistsBeyondBudget()
{
    // Design pin: once a stroke session forms, the displacement budget no
    // longer applies — vigorous petting that wanders must not suddenly yank
    // the window. (The budget gate checks phase == Undecided only.)
    StrokeDetector d;
    qint64 now = 1000;
    d.press(QPoint(100, 100), now);
    feedMoves(d, {106, 112, 106, 100, 106, 112}, 100, now);
    QCOMPARE(d.phase(), StrokeDetector::Phase::Stroking);
    feedMoves(d, {120, 130, 140, 150}, 100, now);   // 50px from press
    QCOMPARE(d.phase(), StrokeDetector::Phase::Stroking);
}

void TestStrokeDetector::testMoveBeforePressIsNoop()
{
    StrokeDetector d;
    d.move(QPoint(100, 100), 1000);   // no press() — Idle guard must swallow it
    QCOMPARE(d.phase(), StrokeDetector::Phase::Idle);
}

QTEST_MAIN(TestStrokeDetector)
#include "test_stroke_detector.moc"
