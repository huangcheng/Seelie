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
