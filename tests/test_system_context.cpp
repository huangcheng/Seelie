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
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDateTime>

#include "EventRouter.h"
#include "ConfigManager.h"
#include "FullscreenWatcher.h"
#include "SystemContextEngine.h"

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

class TestSystemContext : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Task 1: registration
    void testContextEventsAccepted();
    void testUnknownContextEventRejected();

    // Task 2: tips catalog entries
    void testTipsJsonEntriesEn();
    void testTipsJsonEntriesZhCn();

    // Task 3: config key
    void testContextSensesDefaultTrue();
    void testContextSensesRoundTrip();
    void testContextSensesSignal();

    // Task 4: skeleton
    void testEngineEmitsThroughRouter();
    void testCooldownSuppressesSecondEmit();
    void testStoppedEngineIsSilent();

    // Task 5: clock detectors
    void testLateNightFiresAfter23WithSession();
    void testLateNightSilentBefore23();
    void testLateNightSilentWithoutSession();
    void testLongSessionFiresAt3Hours();

    // Task 6: idle + time-of-day
    void testIdleFiresAfter10QuietMinutes();
    void testIdleLatchAndCooldown();
    void testTimeOfDayMorningBucket();
    void testTimeOfDayNightBucket();
    void testTimeOfDayOncePerSession();

    // Task 7: away
    void testAwayFiresOnReturn();
    void testAwaySilentWhileAway();
    void testAwayProbeUnsupportedDisables();

    // Task 8: battery
    void testLowBatteryFires();
    void testLowBatteryLatchAndRearm();
    void testNoBatteryDisablesProbe();

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
    QCOMPARE(spy.count(), static_cast<int>(names.size()));
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

void TestSystemContext::testContextSensesDefaultTrue()
{
    ConfigManager cfg;
    cfg.load();
    QCOMPARE(cfg.contextSensesEnabled(), true);
}

void TestSystemContext::testContextSensesRoundTrip()
{
    // Mirror test_gaming_mode: nested scopes so each ConfigManager's
    // destructor flushes its debounced save() before the next one loads.
    {
        ConfigManager cfg;
        cfg.load();
        cfg.setContextSensesEnabled(false);
    }
    {
        ConfigManager cfg2;
        cfg2.load();
        QCOMPARE(cfg2.contextSensesEnabled(), false);
    }
    // Restore default for any test that runs after this one.
    {
        ConfigManager cfg3;
        cfg3.load();
        cfg3.setContextSensesEnabled(true);
    }
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

QTEST_MAIN(TestSystemContext)
#include "test_system_context.moc"
