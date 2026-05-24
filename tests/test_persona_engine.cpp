#include "PersonaEngine.h"
#include "PersonaPool.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "CharacterPack.h"
#include "TipsCatalog.h"
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSettings>

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
    }
};

QTEST_MAIN(TestPersonaEngine)
#include "test_persona_engine.moc"
