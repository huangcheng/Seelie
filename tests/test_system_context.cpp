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

QTEST_MAIN(TestSystemContext)
#include "test_system_context.moc"
