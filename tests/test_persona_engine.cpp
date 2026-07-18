#include "PersonaEngine.h"
#include "PersonaPool.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "CharacterPack.h"
#include "TipsCatalog.h"
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>

#ifdef SEELIE_HAS_QHTTPSERVER
#include <QHttpServer>
#include <QTcpServer>
#include <QJsonDocument>
#endif

class TestPersonaEngine : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("SeelieTest");
        QCoreApplication::setApplicationName("seelie_persona_engine_test");
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    "SeelieTest", "seelie_persona_engine_test");
        s.clear();
        s.sync();
    }

    void testFallbackWhenDisabled()
    {
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(false);
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("p");
        engine.setPersonaHash("h");

        auto r = engine.resolve("tool.before", {});
        // No upgrade ever arrives in the disabled path
        QCOMPARE(r.requestId, quint64(0));
    }

    void testPoolHitReturnsCachedText()
    {
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(true);
        cfg.setPersonaProfile("fake");
        cfg.setLLMProfiles({ { "fake", LLMProfile::Protocol::OpenAIChat,
                               "http://nope", "k", "m" } });
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("p");
        engine.setPersonaHash("h");

        // Seed pool directly via the test seam.
        engine.pool().insert("p", "tool.before", "h", "Cached line");

        auto r = engine.resolve("tool.before", {});
        QCOMPARE(r.text, QString("Cached line"));
        QCOMPARE(r.requestId, quint64(0));  // pool tier => no upgrade
    }

    void testTierClassification()
    {
        QVERIFY(PersonaEngine::tierFor("tool.before") == PersonaEngine::Tier::Pool);
        QVERIFY(PersonaEngine::tierFor("file.edited") == PersonaEngine::Tier::Pool);
        QVERIFY(PersonaEngine::tierFor("permission.response") == PersonaEngine::Tier::Pool);
        QVERIFY(PersonaEngine::tierFor("session.start") == PersonaEngine::Tier::OnDemand);
        QVERIFY(PersonaEngine::tierFor("session.error") == PersonaEngine::Tier::OnDemand);
        QVERIFY(PersonaEngine::tierFor("milestone.gaming_mode") == PersonaEngine::Tier::OnDemand);
        // Unknown event defaults to OnDemand (forward compat).
        QVERIFY(PersonaEngine::tierFor("future.unknown") == PersonaEngine::Tier::OnDemand);
        // Spec 4: context.* and user.pet/toss are pool-tier (auto-seeded).
        for (const char *name : {"context.latenight", "context.longsession",
                                 "context.idle", "context.away", "context.gaming",
                                 "context.lowbattery",
                                 "user.pet", "user.toss"}) {
            QVERIFY(PersonaEngine::tierFor(name) == PersonaEngine::Tier::Pool);
        }
        // Enrichment-only, never bubbles → NOT pool-tier (review: wasted refill).
        QVERIFY(PersonaEngine::tierFor("context.timeofday") == PersonaEngine::Tier::OnDemand);
        // hover stays out of the bubble pipeline entirely.
        QVERIFY(PersonaEngine::tierFor("user.hover") == PersonaEngine::Tier::OnDemand);
    }

    void testFallbackTouchEventsUseTouchPool()
    {
        // Persona disabled → resolve() returns {fallbackTip, 0}; user.* touch
        // events must fall back to the Spec-3 touch line pools (qrc-bundled).
        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(false);
        PersonaEngine engine(&mm, &cfg);

        QVERIFY(!engine.resolve("user.pet", {}).text.isEmpty());
        QVERIFY(!engine.resolve("user.toss", {}).text.isEmpty());
        // hover stays silent (spec: hover never bubbles)
        QVERIFY(engine.resolve("user.hover", {}).text.isEmpty());
    }

#ifdef SEELIE_HAS_QHTTPSERVER
    void testOnDemandUpgrade()
    {
        QHttpServer server;
        server.route("/chat/completions", [](const QHttpServerRequest &) {
            return QHttpServerResponse(QJsonDocument::fromJson(
                R"({"choices":[{"message":{"content":"Live line."}}],
                    "usage":{"prompt_tokens":1,"completion_tokens":1}})"
            ).object());
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(true);
        cfg.setLLMProfiles({ { "p", LLMProfile::Protocol::OpenAIChat,
                               QStringLiteral("http://127.0.0.1:%1").arg(port),
                               "k", "m" } });
        cfg.setPersonaProfile("p");
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("pack");
        engine.setPersonaHash("h");

        QSignalSpy spy(&engine, &PersonaEngine::tipUpgraded);
        auto r = engine.resolve("session.start", {});
        QVERIFY(r.requestId != 0);  // upgrade will arrive
        // Note: fallback text may be empty in tests (TipsCatalog has no qrc in test binary)

        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.first()[0].value<quint64>(), r.requestId);
        QCOMPARE(spy.first()[1].toString(), QString("Live line."));
    }

    void testPoolRefillFromBatchedLLM()
    {
        QHttpServer server;
        server.route("/chat/completions", [](const QHttpServerRequest &) {
            return QHttpServerResponse(QJsonDocument::fromJson(
                "{\"choices\":[{\"message\":{\"content\":\"[\\\"line one\\\",\\\"line two\\\",\\\"line three\\\"]\"}}]}"
            ).object());
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        MemoryManager mm(":memory:");
        ConfigManager cfg;
        cfg.load();
        cfg.setPersonaEnabled(true);
        cfg.setLLMProfiles({ { "p", LLMProfile::Protocol::OpenAIChat,
                               QStringLiteral("http://127.0.0.1:%1").arg(port),
                               "k","m" } });
        cfg.setPersonaProfile("p");
        PersonaEngine engine(&mm, &cfg);
        engine.setActivePackId("pack");
        engine.setPersonaHash("h");

        QCOMPARE(engine.pool().size("pack", "tool.before"), 0);
        engine.resolve("tool.before", {});  // triggers async refill

        QTRY_VERIFY_WITH_TIMEOUT(engine.pool().size("pack","tool.before") >= 3, 3000);
    }
#endif
};

QTEST_MAIN(TestPersonaEngine)
#include "test_persona_engine.moc"
