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

    mood.setVectorForTest(0.35, 0.35);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Excited);

    mood.setVectorForTest(0.1, 0.1);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Content);
}

QTEST_GUILESS_MAIN(TestMoodEngine)
#include "test_mood_engine.moc"
