#include "llm/LLMProvider.h"
#include "llm/LLMProfile.h"
#include <QtTest/QtTest>
#include <QSignalSpy>

#ifdef SEELIE_HAS_QHTTPSERVER
#include <QHttpServer>
#include <QTcpServer>
#endif

class TestLLMProvider : public QObject
{
    Q_OBJECT
private slots:
#ifdef SEELIE_HAS_QHTTPSERVER
    void testOpenAiChatHappyPath()
    {
        QHttpServer server;
        server.route("/chat/completions", [](const QHttpServerRequest &) {
            // Use QJsonObject constructor so QHttpServer sets Content-Type correctly.
            QJsonObject body;
            QJsonObject message; message["content"] = "Hello world.";
            QJsonObject choice;  choice["message"]  = message;
            QJsonArray choices;  choices.append(choice);
            body["choices"] = choices;
            QJsonObject usage;
            usage["prompt_tokens"]      = 12;
            usage["completion_tokens"]  = 3;
            body["usage"] = usage;
            return QHttpServerResponse(body);
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        LLMProfile profile;
        profile.protocol = LLMProfile::Protocol::OpenAIChat;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        profile.apiKey = "sk-test";
        profile.model = "gpt-4o-mini";

        LLMProvider provider;
        provider.setProfile(profile);

        QEventLoop loop;
        LLMResult got;
        provider.generate("system prompt", "user prompt", [&](LLMResult r) {
            got = std::move(r);
            loop.quit();
        });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();

        QVERIFY(got.ok);
        QCOMPARE(got.text, QString("Hello world."));
        QCOMPARE(got.tokensIn, 12);
        QCOMPARE(got.tokensOut, 3);
    }

    void testOpenAiChatHttpError()
    {
        QHttpServer server;
        server.route("/chat/completions", [](const QHttpServerRequest &) {
            return QHttpServerResponse("Unauthorized", QHttpServerResponder::StatusCode::Unauthorized);
        });
        auto tcp = std::make_unique<QTcpServer>();
        QVERIFY(tcp->listen(QHostAddress::LocalHost, 0));
        const quint16 port = tcp->serverPort();
        QVERIFY(server.bind(tcp.release()));

        LLMProfile profile;
        profile.protocol = LLMProfile::Protocol::OpenAIChat;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        profile.apiKey = "bad";
        profile.model = "x";

        LLMProvider provider;
        provider.setProfile(profile);
        QEventLoop loop;
        LLMResult got;
        provider.generate("s", "u", [&](LLMResult r) { got = std::move(r); loop.quit(); });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();

        QVERIFY(!got.ok);
        QVERIFY(!got.error.isEmpty());
    }
#else
    void skipNoHttpServer()
    {
        QSKIP("Qt6::HttpServer not found; LLMProvider tests skipped at compile time.");
    }
#endif
};

QTEST_MAIN(TestLLMProvider)
#include "test_llm_provider.moc"
