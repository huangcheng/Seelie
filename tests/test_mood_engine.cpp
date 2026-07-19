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
#include <QTemporaryDir>

#include "EventRouter.h"
#include "MoodEngine.h"
#include "MemoryManager.h"
#include "StatisticsPersistence.h"
#include "TipsCatalog.h"

class TestMoodEngine : public QObject
{
    Q_OBJECT

private slots:
    void deltasApplyAndClamp();
    void decayTowardBaseline();
    void tierQuantization();
    void tierHysteresis();
    void toolFailedBurst();
    void moodEventsValidate();
    void stageUpOncePerLevel();
    void lonelySetOnLongAbsence();
    void moodEventsValidateAllFour();
    void greetingOncePerDay();
    void missedYouAfter24h();
    void longSessionNudgeNeedsLowEnergy();
    void globalProactiveCap();
    void persistsAcrossReload();
    void corruptJsonRecovers();
    void moodEventsHaveCatalogFallback();
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
    // 0.08 pet − 0.20 toss = −0.12; assert magnitude with tolerance.
    QVERIFY(qAbs(mood.valence() - (-0.12)) < 1e-9);

    // Clamp at [-1, 1]: repeated pets never exceed 1.0.
    for (int i = 0; i < 100; ++i)
        mood.onEventProcessed(QStringLiteral("user.pet"), {});
    QVERIFY(mood.valence() <= 1.0);
    QVERIFY(mood.valence() > 0.9);   // converged at the upper clamp
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
    qRegisterMetaType<MoodEngine::Tier>("MoodEngine::Tier");
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    QSignalSpy spy(&mood, &MoodEngine::moodTierChanged);
    QVERIFY(spy.isValid());
    QCOMPARE(mood.tier(), MoodEngine::Tier::Content);

    mood.onEventProcessed(QStringLiteral("session.start"), {});
    mood.setVectorForTest(0.5, 0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Excited);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).value<MoodEngine::Tier>(), MoodEngine::Tier::Excited);

    mood.setVectorForTest(-0.5, 0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Tense);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).value<MoodEngine::Tier>(), MoodEngine::Tier::Tense);

    mood.setVectorForTest(0.0, -0.5);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Tired);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.last().at(0).value<MoodEngine::Tier>(), MoodEngine::Tier::Tired);

    // No emission when the tier doesn't change (still Tired).
    mood.setVectorForTest(0.0, -0.6);
    QCOMPARE(mood.tier(), MoodEngine::Tier::Tired);
    QCOMPARE(spy.count(), 3);
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

void TestMoodEngine::toolFailedBurst()
{
    EventRouter router;
    MoodEngine mood(&router, nullptr);
    qint64 fakeNow = QDateTime::fromString(QStringLiteral("2026-07-19T14:00:00"),
                                           Qt::ISODate).toMSecsSinceEpoch();
    mood.setNowFn([&]() { return fakeNow; });

    // Two failures: no burst delta yet.
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    QCOMPARE(mood.valence(), 0.0);

    // Third failure inside 60s: burst delta applies.
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    QVERIFY(qAbs(mood.valence() - (-0.15)) < 1e-9);
    QVERIFY(qAbs(mood.energy() - 0.10) < 1e-9);

    // Fourth failure in the same window: one-shot, no re-apply.
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    QVERIFY(qAbs(mood.valence() - (-0.15)) < 1e-9);

    // After the 60s window slides past, a fresh burst can build again.
    fakeNow += 61000;
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    mood.onEventProcessed(QStringLiteral("tool.failed"), {});
    QVERIFY(qAbs(mood.valence() - (-0.30)) < 1e-9);
}

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

void TestMoodEngine::stageUpOncePerLevel()
{
    EventRouter router;
    QTemporaryDir tmp;
    MemoryManager memory(tmp.filePath(QStringLiteral("m.db")));
    MoodEngine mood(&router, &memory);
    QSignalSpy spy(&router, &EventRouter::eventProcessed);

    memory.addBondXP(100000);   // enough to cross several levels
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

    QTemporaryDir tmp;
    {
        StatisticsPersistence p(tmp.path());
        p.saveSection(QStringLiteral("mood"),
                      {{QStringLiteral("lastSeenMs"), fakeNow - 30LL * 60 * 60 * 1000}});
    }
    mood.loadStats(tmp.path());
    QVERIFY(mood.isLonely());
    QCOMPARE(mood.tier(), MoodEngine::Tier::Lonely);

    mood.onEventProcessed(QStringLiteral("session.start"), {});
    QVERIFY(!mood.isLonely());
    QVERIFY(mood.valence() > 0.0);
}

void TestMoodEngine::moodEventsValidateAllFour()
{
    // Hardening from Task 2 review: all four mood.* names must validate.
    EventRouter router;
    QSignalSpy spy(&router, &EventRouter::eventProcessed);
    for (const char *name : {"mood.greeting", "mood.long_session",
                             "mood.missed_you", "mood.stage_up"}) {
        spy.clear();
        router.routeEvent({{QStringLiteral("type"), QStringLiteral("event")},
                           {QStringLiteral("source"), QStringLiteral("system")},
                           {QStringLiteral("event"), QString::fromLatin1(name)}});
        QCOMPARE(spy.count(), 1);
    }
}

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

    // High energy at the same session age: no nudge.
    MoodEngine fresh(&router, nullptr);
    qint64 now2 = fakeNow;   // reuse clock variable; keep lambda mutable
    fresh.setNowFn([&]() { return now2; });
    QSignalSpy spy2(&router, &EventRouter::eventProcessed);
    spy2.clear();
    fresh.onEventProcessed(QStringLiteral("session.start"), {});
    now2 += 160LL * 60 * 1000;
    fresh.setVectorForTest(0.5, 0.5);
    fresh.tickForTest();
    for (int i = 0; i < spy2.count(); ++i)
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

    QTemporaryDir tmp;
    {
        StatisticsPersistence p(tmp.path());
        p.saveSection(QStringLiteral("mood"),
                      {{QStringLiteral("lastSeenMs"), fakeNow - 30LL * 60 * 60 * 1000}});
    }
    mood.loadStats(tmp.path());

    // Pass 1: missed_you fires (checked first in checkProactive).
    mood.onEventProcessed(QStringLiteral("session.start"), {});
    // Pass 2, +30 min (still inside the 1h global cap): a fresh
    // session.start would qualify for greeting, but the cap blocks it.
    fakeNow += 30LL * 60 * 1000;
    mood.onEventProcessed(QStringLiteral("session.end"), {});
    mood.onEventProcessed(QStringLiteral("session.start"), {});

    int proactive = 0;
    for (int i = 0; i < spy.count(); ++i) {
        const QString n = spy.at(i).at(0).toString();
        if (n.startsWith(QLatin1String("mood."))) ++proactive;
    }
    QCOMPARE(proactive, 1);   // global 1/hour cap blocked the second bubble
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

void TestMoodEngine::moodEventsHaveCatalogFallback()
{
    // PersonaEngine::fallbackTip resolves mood.* through TipsCatalog::eventTip;
    // a missing JSON entry yields an empty body. Pin that all four exist.
    // TipsCatalog pre-loads the "en" bundle in its constructor, so the
    // default locale is already English here.
    for (const char *name : {"mood.greeting", "mood.long_session",
                             "mood.missed_you", "mood.stage_up"}) {
        const auto tip = TipsCatalog::instance().eventTip(QString::fromLatin1(name));
        QVERIFY2(!tip.body.isEmpty(), name);
    }
}

QTEST_GUILESS_MAIN(TestMoodEngine)
#include "test_mood_engine.moc"
