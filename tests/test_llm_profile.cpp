#include "ConfigManager.h"
#include "llm/LLMProfile.h"
#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSettings>

class TestLLMProfile : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("SeelieTest");
        QCoreApplication::setApplicationName("seelie_llm_profile_test");
        // ConfigManager uses IniFormat+UserScope; clear the same backend.
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("SeelieTest"),
                    QStringLiteral("seelie_llm_profile_test"));
        s.clear();
        s.sync();
    }

    void testRoundTrip()
    {
        ConfigManager cfg;
        cfg.load();

        QVector<LLMProfile> profiles;
        profiles.append({ "fast", LLMProfile::Protocol::OpenAIChat,
                          "https://api.openai.com/v1", "sk-test", "gpt-4o-mini" });
        profiles.append({ "smart", LLMProfile::Protocol::AnthropicMessages,
                          "https://api.anthropic.com", "sk-ant-test", "claude-haiku" });
        cfg.setLLMProfiles(profiles);
        cfg.setPersonaProfile("fast");
        cfg.setPersonaEnabled(true);
        cfg.setShareMemoryWithAi(false);
        cfg.flush();

        ConfigManager cfg2;
        cfg2.load();
        const auto got = cfg2.llmProfiles();
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0].name, QString("fast"));
        QCOMPARE(got[0].protocol, LLMProfile::Protocol::OpenAIChat);
        QCOMPARE(got[0].baseUrl, QString("https://api.openai.com/v1"));
        QCOMPARE(got[0].apiKey, QString("sk-test"));
        QCOMPARE(got[0].model, QString("gpt-4o-mini"));
        QCOMPARE(got[1].protocol, LLMProfile::Protocol::AnthropicMessages);
        QCOMPARE(cfg2.personaProfile(), QString("fast"));
        QCOMPARE(cfg2.personaEnabled(), true);
        QCOMPARE(cfg2.shareMemoryWithAi(), false);
    }

    void testEmptyOnFirstRun()
    {
        {
            QSettings s(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("SeelieTest"),
                        QStringLiteral("seelie_llm_profile_test"));
            s.clear();
            s.sync();
        }
        ConfigManager cfg;
        cfg.load();
        QVERIFY(cfg.llmProfiles().isEmpty());
        QCOMPARE(cfg.personaProfile(), QString());
        QCOMPARE(cfg.personaEnabled(), false);
        QCOMPARE(cfg.shareMemoryWithAi(), false);
    }
};

QTEST_MAIN(TestLLMProfile)
#include "test_llm_profile.moc"
