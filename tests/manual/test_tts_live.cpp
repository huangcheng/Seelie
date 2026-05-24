/**
 * test_tts_live.cpp — Manual live-API smoke test.
 *
 * Skipped unless SEELIE_LIVE_TTS=1 is set in the environment. Reads
 * credentials from per-provider env vars and exercises each adapter
 * against the real API. Run before releases.
 *
 *   SEELIE_LIVE_TTS=1 \
 *     SEELIE_STEPFUN_TOKEN=... \
 *     SEELIE_MINIMAX_TOKEN=... SEELIE_MINIMAX_GROUP=... \
 *     SEELIE_AZURE_KEY=...    SEELIE_AZURE_REGION=eastus \
 *     SEELIE_OPENAI_TOKEN=... \
 *     ./test_tts_live
 */

#include <QtTest>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <cstdlib>

#include "tts/StepFunHTTPProvider.h"
#include "tts/MiniMaxHTTPProvider.h"
#include "tts/AzureSpeechProvider.h"
#include "tts/OpenAITTSProvider.h"

using namespace seelie::tts;

namespace {
QString env(const char* name) { return QString::fromLocal8Bit(qgetenv(name)); }
} // namespace

class TestTtsLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        if (env("SEELIE_LIVE_TTS") != "1")
            QSKIP("Set SEELIE_LIVE_TTS=1 to run live-API tests");
        m_nam = new QNetworkAccessManager(this);
    }

    void stepFun() {
        const QString token = env("SEELIE_STEPFUN_TOKEN");
        if (token.isEmpty()) QSKIP("SEELIE_STEPFUN_TOKEN not set");
        ProviderConfig cfg{{
            {"baseUrl", "https://api.stepfun.com"},
            {"token", token},
            {"voice", "cixingnansheng"},
        }};
        StepFunHTTPProvider provider(cfg, m_nam);
        runOne(provider);
    }

    void miniMax() {
        const QString token = env("SEELIE_MINIMAX_TOKEN");
        const QString group = env("SEELIE_MINIMAX_GROUP");
        if (token.isEmpty() || group.isEmpty())
            QSKIP("SEELIE_MINIMAX_TOKEN and SEELIE_MINIMAX_GROUP required");
        ProviderConfig cfg{{
            {"baseUrl", "https://api.minimaxi.com"},
            {"token", token}, {"groupId", group},
            {"voice", "female-shaonv"},
        }};
        MiniMaxHTTPProvider provider(cfg, m_nam);
        runOne(provider);
    }

    void azure() {
        const QString key = env("SEELIE_AZURE_KEY");
        const QString region = env("SEELIE_AZURE_REGION");
        if (key.isEmpty() || region.isEmpty())
            QSKIP("SEELIE_AZURE_KEY and SEELIE_AZURE_REGION required");
        ProviderConfig cfg{{
            {"key", key}, {"region", region},
            {"voice", "en-US-JennyNeural"},
        }};
        AzureSpeechProvider provider(cfg, m_nam);
        runOne(provider);
    }

    void openAi() {
        const QString token = env("SEELIE_OPENAI_TOKEN");
        if (token.isEmpty()) QSKIP("SEELIE_OPENAI_TOKEN not set");
        ProviderConfig cfg{{
            {"baseUrl", "https://api.openai.com/v1"},
            {"token", token}, {"voice", "nova"},
        }};
        OpenAITTSProvider provider(cfg, m_nam);
        runOne(provider);
    }

private:
    void runOne(ITTSProvider& provider) {
        QByteArray audio;
        QString err;
        QEventLoop loop;
        provider.synthesize({QStringLiteral("Seelie live test."), {}},
            [&](SynthesisResult r){ audio = r.audio; loop.quit(); },
            [&](TTSError e){ err = e.message; loop.quit(); });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(audio.size() > 1024);
    }

    QNetworkAccessManager* m_nam = nullptr;
};

QTEST_MAIN(TestTtsLive)
#include "test_tts_live.moc"
