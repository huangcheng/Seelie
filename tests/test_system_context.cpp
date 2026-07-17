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

QTEST_MAIN(TestSystemContext)
#include "test_system_context.moc"
