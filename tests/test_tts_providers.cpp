/**
 * test_tts_providers.cpp
 *
 * Adapter unit tests. Each adapter is exercised against an in-process
 * QHttpServer that stands in for the real provider API. We assert the
 * outgoing request shape (URL, headers, body) and the engine-side parsing
 * of canned responses.
 */

#include <QtTest>
#include <QHttpServer>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>

#include "tts/ITTSProvider.h"
#include "tts/ProviderConfig.h"
#include "tts/StepFunHTTPProvider.h"
#include "tts/MiniMaxHTTPProvider.h"
#include "tts/AzureSpeechProvider.h"
#include "tts/OpenAITTSProvider.h"

using namespace seelie::tts;

class TestTtsProviders : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void stepFun_buildsExpectedRequest();
    void stepFun_parsesMp3Response();
    void stepFun_mapsHappyEmotionToInstruction();
    void stepFun_returnsAuthFailedOn401();
    void miniMax_buildsExpectedRequest();
    void miniMax_decodesHexAudio();
    void miniMax_mapsHappyEmotionToEnum();
    void azure_buildsExpectedSsml();
    void azure_usesSubscriptionKeyHeader();
    void azure_mapsHappyEmotionToSsmlStyle();
    void openAi_buildsExpectedRequest();
    void openAi_dropsEmotionSilently();

private:
    QHttpServer*           m_server      = nullptr;
    QNetworkAccessManager* m_nam         = nullptr;
    quint16                m_port        = 0;

    // Mutable response state — set before each synthesize() call.
    int        m_nextStatus = 200;
    QByteArray m_nextBody;

    QJsonObject  m_lastRequestBody;
    QString      m_lastAuthHeader;
    QString      m_lastRequestPath;

    // Azure-specific capture fields
    QByteArray   m_lastRequestBodyRaw;
    QByteArray   m_lastSubscriptionKey;

    void setStepFunResponse(int httpStatus, const QByteArray& body);
    void setMiniMaxResponse(int httpStatus, const QByteArray& body);
    void setAzureResponse(int httpStatus, const QByteArray& body);
    void setOpenAiResponse(int httpStatus, const QByteArray& body);
};

void TestTtsProviders::initTestCase()
{
    m_nam = new QNetworkAccessManager(this);
    m_server = new QHttpServer(this);

    // Bind once; all tests share the same port.
    auto* tcpServer = new QTcpServer(m_server);
    QVERIFY(tcpServer->listen(QHostAddress::LocalHost, 0));
    m_port = tcpServer->serverPort();
    QVERIFY(m_port > 0);
    QVERIFY(m_server->bind(tcpServer));

    // Register the route once. The lambda reads m_nextStatus / m_nextBody
    // at request time, so each test can configure the desired response.
    m_server->route("/v1/audio/speech", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            m_lastRequestBody = QJsonDocument::fromJson(req.body()).object();
            m_lastAuthHeader  = QString::fromUtf8(req.headers().value("Authorization"));
            m_lastRequestPath = QStringLiteral("/v1/audio/speech");
            QHttpServerResponse resp(
                QByteArray("audio/mpeg"),
                m_nextBody,
                static_cast<QHttpServerResponse::StatusCode>(m_nextStatus));
            return resp;
        });

    // MiniMax route — same mutable state, different path.
    m_server->route("/v1/t2a_v2", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            m_lastRequestBody = QJsonDocument::fromJson(req.body()).object();
            m_lastAuthHeader  = QString::fromUtf8(req.headers().value("Authorization"));
            m_lastRequestPath = QStringLiteral("/v1/t2a_v2");
            QHttpServerResponse resp(
                QByteArray("application/json"),
                m_nextBody,
                static_cast<QHttpServerResponse::StatusCode>(m_nextStatus));
            return resp;
        });

    // Azure route — captures raw SSML body and subscription key header.
    m_server->route("/cognitiveservices/v1", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            m_lastRequestPath    = QStringLiteral("azure");
            m_lastRequestBodyRaw = req.body();
            m_lastSubscriptionKey =
                req.headers().value("Ocp-Apim-Subscription-Key").toByteArray();
            QHttpServerResponse resp(
                QByteArray("audio/mpeg"),
                m_nextBody,
                static_cast<QHttpServerResponse::StatusCode>(m_nextStatus));
            return resp;
        });

    // OpenAI route — distinct path avoids collision with the StepFun /v1/audio/speech route.
    m_server->route("/openai-v1/audio/speech", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            m_lastRequestPath = QStringLiteral("openai");
            m_lastRequestBody = QJsonDocument::fromJson(req.body()).object();
            m_lastAuthHeader  = QString::fromUtf8(
                req.headers().value("Authorization").toByteArray());
            QHttpServerResponse resp(
                QByteArray("audio/mpeg"),
                m_nextBody,
                static_cast<QHttpServerResponse::StatusCode>(m_nextStatus));
            return resp;
        });
}

void TestTtsProviders::setStepFunResponse(int httpStatus, const QByteArray& body)
{
    m_nextStatus = httpStatus;
    m_nextBody   = body;
}

void TestTtsProviders::setMiniMaxResponse(int httpStatus, const QByteArray& body)
{
    m_nextStatus = httpStatus;
    m_nextBody   = body;
}

void TestTtsProviders::setAzureResponse(int httpStatus, const QByteArray& body)
{
    m_nextStatus = httpStatus;
    m_nextBody   = body;
}

void TestTtsProviders::setOpenAiResponse(int httpStatus, const QByteArray& body)
{
    m_nextStatus = httpStatus;
    m_nextBody   = body;
}

void TestTtsProviders::stepFun_buildsExpectedRequest()
{
    setStepFunResponse(200, QByteArray("MP3DATA"));

    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/v1/audio/speech").arg(m_port)},
        {"token",   "TKN"},
        {"model",   "stepaudio-2.5-tts"},
        {"voice",   "cixingnansheng"},
    }};
    StepFunHTTPProvider provider(cfg, m_nam);

    QByteArray gotAudio;
    bool gotError = false;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{ QStringLiteral("hello"), {} },
        [&](SynthesisResult r){ gotAudio = r.audio; loop.quit(); },
        [&](TTSError){ gotError = true; loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(!gotError);
    QCOMPARE(gotAudio, QByteArray("MP3DATA"));
    QCOMPARE(m_lastAuthHeader, QStringLiteral("Bearer TKN"));
    QCOMPARE(m_lastRequestBody.value("model").toString(), QStringLiteral("stepaudio-2.5-tts"));
    QCOMPARE(m_lastRequestBody.value("input").toString(), QStringLiteral("hello"));
    QCOMPARE(m_lastRequestBody.value("voice").toString(), QStringLiteral("cixingnansheng"));
    QCOMPARE(m_lastRequestBody.value("response_format").toString(), QStringLiteral("mp3"));
}

void TestTtsProviders::stepFun_parsesMp3Response()
{
    setStepFunResponse(200, QByteArray("\xFF\xFB\x10\xC4MP3DATA"));
    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/v1/audio/speech").arg(m_port)},
        {"token", "T"}, {"voice", "cixingnansheng"},
    }};
    StepFunHTTPProvider provider(cfg, m_nam);

    QByteArray gotAudio;
    QString gotMime;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), {}},
        [&](SynthesisResult r){ gotAudio = r.audio; gotMime = r.mimeType; loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QCOMPARE(gotAudio.size(), 11);
    QCOMPARE(gotMime, QStringLiteral("audio/mpeg"));
}

void TestTtsProviders::stepFun_mapsHappyEmotionToInstruction()
{
    setStepFunResponse(200, QByteArray("X"));
    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/v1/audio/speech").arg(m_port)},
        {"token", "T"}, {"voice", "cixingnansheng"},
    }};
    StepFunHTTPProvider provider(cfg, m_nam);

    SpeakOptions opts;
    opts.emotion = Emotion::Happy;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), opts},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(m_lastRequestBody.value("instruction").toString(),
             QString::fromUtf8("\xE8\xAF\xAD\xE6\xB0\x94\xE6\x84\x89\xE5\xBF\xAB"));  // 语气愉快
}

void TestTtsProviders::stepFun_returnsAuthFailedOn401()
{
    setStepFunResponse(401, QByteArray("{\"error\":\"bad token\"}"));
    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/v1/audio/speech").arg(m_port)},
        {"token", "T"}, {"voice", "cixingnansheng"},
    }};
    StepFunHTTPProvider provider(cfg, m_nam);

    TTSErrorKind gotKind = TTSErrorKind::Unknown;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), {}},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError e){ gotKind = e.kind; loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(gotKind, TTSErrorKind::AuthFailed);
}

void TestTtsProviders::miniMax_buildsExpectedRequest()
{
    QJsonObject envelope;
    envelope["data"] = QJsonObject{
        {"audio", QStringLiteral("48656c6c6f")},  // "Hello" in hex
        {"status", 2}
    };
    envelope["base_resp"] = QJsonObject{{"status_code", 0}};
    QByteArray respBody = QJsonDocument(envelope).toJson(QJsonDocument::Compact);

    setMiniMaxResponse(200, respBody);

    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/v1/t2a_v2").arg(m_port)},
        {"token",   "TKN"},
        {"groupId", "GRP123"},
        {"model",   "speech-02-hd"},
        {"voice",   "female-shaonv"},
    }};
    MiniMaxHTTPProvider provider(cfg, m_nam);

    QByteArray gotAudio;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), {}},
        [&](SynthesisResult r){ gotAudio = r.audio; loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(gotAudio, QByteArray("Hello"));
    QCOMPARE(m_lastAuthHeader, QStringLiteral("Bearer TKN"));
    QCOMPARE(m_lastRequestBody.value("text").toString(), QStringLiteral("hi"));
    // voice_id is nested under voice_setting:
    QJsonObject vs = m_lastRequestBody.value("voice_setting").toObject();
    QCOMPARE(vs.value("voice_id").toString(), QStringLiteral("female-shaonv"));
    QCOMPARE(m_lastRequestBody.value("model").toString(), QStringLiteral("speech-02-hd"));
}

void TestTtsProviders::miniMax_decodesHexAudio()
{
    QSKIP("Verified by miniMax_buildsExpectedRequest");
}

void TestTtsProviders::miniMax_mapsHappyEmotionToEnum()
{
    QJsonObject envelope;
    envelope["data"] = QJsonObject{{"audio", QStringLiteral("00")}, {"status", 2}};
    envelope["base_resp"] = QJsonObject{{"status_code", 0}};
    QByteArray respBody = QJsonDocument(envelope).toJson();
    setMiniMaxResponse(200, respBody);

    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/v1/t2a_v2").arg(m_port)},
        {"token", "T"}, {"groupId", "G"}, {"voice", "v"},
    }};
    MiniMaxHTTPProvider provider(cfg, m_nam);

    SpeakOptions opts;
    opts.emotion = Emotion::Happy;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("x"), opts},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(m_lastRequestBody.value("emotion").toString(), QStringLiteral("happy"));
}

void TestTtsProviders::azure_buildsExpectedSsml()
{
    setAzureResponse(200, QByteArray("MP3"));

    ProviderConfig cfg{{
        {"azureEndpointOverride", QString("http://127.0.0.1:%1").arg(m_port)},
        {"key", "KEY"},
        {"region", "eastus"},
        {"voice", "en-US-JennyNeural"},
    }};
    AzureSpeechProvider provider(cfg, m_nam);

    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi there"), {}},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(m_lastRequestBodyRaw.contains("<speak"));
    QVERIFY(m_lastRequestBodyRaw.contains("name=\"en-US-JennyNeural\""));
    QVERIFY(m_lastRequestBodyRaw.contains(">hi there<"));
}

void TestTtsProviders::azure_usesSubscriptionKeyHeader()
{
    setAzureResponse(200, QByteArray("MP3"));

    ProviderConfig cfg{{
        {"azureEndpointOverride", QString("http://127.0.0.1:%1").arg(m_port)},
        {"key", "SECRETKEY"}, {"region", "eastus"},
        {"voice", "en-US-JennyNeural"},
    }};
    AzureSpeechProvider provider(cfg, m_nam);

    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("x"), {}},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(m_lastSubscriptionKey, QByteArray("SECRETKEY"));
}

void TestTtsProviders::azure_mapsHappyEmotionToSsmlStyle()
{
    setAzureResponse(200, QByteArray("MP3"));

    ProviderConfig cfg{{
        {"azureEndpointOverride", QString("http://127.0.0.1:%1").arg(m_port)},
        {"key", "K"}, {"region", "eastus"},
        {"voice", "en-US-JennyNeural"},
    }};
    AzureSpeechProvider provider(cfg, m_nam);

    SpeakOptions opts;
    opts.emotion = Emotion::Happy;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), opts},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(m_lastRequestBodyRaw.contains("<mstts:express-as style=\"cheerful\""));
}

void TestTtsProviders::openAi_buildsExpectedRequest()
{
    setOpenAiResponse(200, QByteArray("MP3"));

    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/openai-v1/audio/speech").arg(m_port)},
        {"token", "sk-XYZ"},
        {"model", "gpt-4o-mini-tts"},
        {"voice", "nova"},
    }};
    OpenAITTSProvider provider(cfg, m_nam);

    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), {}},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(m_lastAuthHeader, QStringLiteral("Bearer sk-XYZ"));
    QCOMPARE(m_lastRequestBody.value("model").toString(), QStringLiteral("gpt-4o-mini-tts"));
    QCOMPARE(m_lastRequestBody.value("input").toString(), QStringLiteral("hi"));
    QCOMPARE(m_lastRequestBody.value("voice").toString(), QStringLiteral("nova"));
    QCOMPARE(m_lastRequestBody.value("response_format").toString(), QStringLiteral("mp3"));
}

void TestTtsProviders::openAi_dropsEmotionSilently()
{
    setOpenAiResponse(200, QByteArray("MP3"));

    ProviderConfig cfg{{
        {"baseUrl", QString("http://127.0.0.1:%1/openai-v1/audio/speech").arg(m_port)},
        {"token", "T"}, {"voice", "nova"},
    }};
    OpenAITTSProvider provider(cfg, m_nam);

    SpeakOptions opts;
    opts.emotion = Emotion::Happy;
    QEventLoop loop;
    provider.synthesize(SynthesisRequest{QStringLiteral("hi"), opts},
        [&](SynthesisResult){ loop.quit(); },
        [&](TTSError){ loop.quit(); });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(!m_lastRequestBody.contains("emotion"));
    QVERIFY(!m_lastRequestBody.contains("instruction"));
}

QTEST_MAIN(TestTtsProviders)
#include "test_tts_providers.moc"
