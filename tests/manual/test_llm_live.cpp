/**
 * test_llm_live.cpp — Manual live-API smoke test for LLMProvider.
 *
 * Skipped unless SEELIE_LIVE_LLM=1 is set in the environment. Reads the
 * profile from env vars and prints the raw result of one generate() call.
 * Used to debug provider/endpoint mismatches (e.g. DashScope verification).
 *
 *   SEELIE_LIVE_LLM=1 \
 *     SEELIE_LLM_BASEURL=https://dashscope.aliyuncs.com/compatible-mode/v1 \
 *     SEELIE_LLM_APIKEY=sk-... \
 *     SEELIE_LLM_MODEL=qwen3.7-plus \
 *     ./test_llm_live
 */

#include <QtTest>
#include <QEventLoop>
#include <QTimer>
#include <cstdlib>

#include "llm/LLMProvider.h"
#include "ConfigManager.h"

class TestLlmLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        if (QString::fromLocal8Bit(qgetenv("SEELIE_LIVE_LLM")) != "1")
            QSKIP("Set SEELIE_LIVE_LLM=1 to run live LLM test");
    }

    /// Mirrors the app's production path: load the ACTIVE persona profile
    /// from the real user config (same ini the app reads) and fire one
    /// generate() with the production default timeout. Prints everything
    /// needed to diagnose profile-parsing vs endpoint issues.
    void profileFromConfig() {
        // QTEST_GUILESS_MAIN names the app after the test binary, which would
        // make ConfigManager read a different roaming ini than the real app.
        QCoreApplication::setApplicationName(QStringLiteral("Seelie"));
        ConfigManager config;
        config.load();   // ctor = defaults only; user-scope ini applies here
        qInfo() << "personaEnabled:" << config.personaEnabled()
                << "personaProfile:" << config.personaProfile();
        LLMProfile active;
        bool found = false;
        for (const auto &p : config.llmProfiles()) {
            qInfo() << "profile:" << p.name << "protocol:" << int(p.protocol)
                    << "baseUrl:" << p.baseUrl << "model:" << p.model
                    << "apiKey(len):" << p.apiKey.length()
                    << "apiKey(head):" << p.apiKey.left(6);
            if (p.name == config.personaProfile()) { active = p; found = true; }
        }
        QVERIFY2(found, "active persona profile not found in llmProfiles()");

        LLMProvider provider;
        provider.setProfile(active);
        QVERIFY2(provider.isConfigured(), "provider reports not configured");

        LLMResult result;
        bool gotReply = false;
        QEventLoop loop;
        QTimer::singleShot(35000, &loop, &QEventLoop::quit);
        provider.generate(QStringLiteral("You are a connectivity probe."),
                          QStringLiteral("Reply with the single word: ok"),
                          [&](LLMResult r) { result = r; gotReply = true; loop.quit(); });
        loop.exec();

        qInfo() << "gotReply:" << gotReply
                << "ok:" << result.ok
                << "error:" << result.error
                << "text:" << result.text.left(120)
                << "tokensIn:" << result.tokensIn
                << "tokensOut:" << result.tokensOut;
        QVERIFY2(gotReply && result.ok, qPrintable(result.error));
    }

    void openAiChat() {
        LLMProfile p;
        p.name = QStringLiteral("live");
        p.protocol = LLMProfile::Protocol::OpenAIChat;
        p.baseUrl = QString::fromLocal8Bit(qgetenv("SEELIE_LLM_BASEURL"));
        p.apiKey  = QString::fromLocal8Bit(qgetenv("SEELIE_LLM_APIKEY"));
        p.model   = QString::fromLocal8Bit(qgetenv("SEELIE_LLM_MODEL"));
        QVERIFY2(!p.baseUrl.isEmpty() && !p.apiKey.isEmpty() && !p.model.isEmpty(),
                 "SEELIE_LLM_BASEURL/APIKEY/MODEL required");

        LLMProvider provider;
        provider.setProfile(p);
        // Live endpoints (DashScope first-call latency) can exceed the 5 s
        // production default; give the probe room so we measure reachability,
        // not timeout tuning.
        provider.setTimeoutMs(30000);

        LLMResult result;
        bool gotReply = false;
        QEventLoop loop;
        QTimer::singleShot(35000, &loop, &QEventLoop::quit);
        provider.generate(QStringLiteral("You are a connectivity probe."),
                          QStringLiteral("Reply with the single word: ok"),
                          [&](LLMResult r) { result = r; gotReply = true; loop.quit(); });
        loop.exec();

        qInfo() << "gotReply:" << gotReply
                << "ok:" << result.ok
                << "error:" << result.error
                << "text:" << result.text.left(120)
                << "tokensIn:" << result.tokensIn
                << "tokensOut:" << result.tokensOut;
        QVERIFY2(gotReply && result.ok, qPrintable(result.error));
    }
};

QTEST_GUILESS_MAIN(TestLlmLive)
#include "test_llm_live.moc"
