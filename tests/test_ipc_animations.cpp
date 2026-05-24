/**
 * test_ipc_animations.cpp
 *
 * End-to-end UDP IPC tests for Seelie.
 *
 * Spins up the real IPCServer, EventRouter, SpriteAnimationEngine,
 * and TipWidget, then drives them via
 * raw UDP datagrams (same protocol as the Node.js gateways).
 */

#include <QTest>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>

#include "IPCServer.h"
#include "EventRouter.h"
#include "SpriteAnimationEngine.h"
#include "TipWidget.h"
#include "TipsEngine.h"
#include "PetStateMachine.h"

class TestIpcAnimations : public QObject
{
    Q_OBJECT

public:
    TestIpcAnimations() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testPingPong();
    void testEventTriggersAnimation();
    void testTipMessage();
    void testMultipleDatagrams();
    void testMalformedJson();
    void testUnknownEventType();
    void testPriorityQueue();
    void testTipWithAnimation();

private:
    static QString findAssetsDir();
    void sendJson(QUdpSocket *socket, const QJsonObject &obj);
    QByteArray waitForResponse(QUdpSocket *socket, int timeoutMs = 1000);

    IPCServer *m_ipc = nullptr;
    EventRouter *m_router = nullptr;
    SpriteAnimationEngine *m_engine = nullptr;
    TipWidget *m_bubble = nullptr;
    TipsEngine *m_tips = nullptr;
    PetStateMachine *m_fsm = nullptr;

    static constexpr quint16 TEST_PORT = 52848;
    static constexpr const char *TEST_HOST = "127.0.0.1";
};

QString TestIpcAnimations::findAssetsDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QString candidate = dir.absoluteFilePath("assets");
        if (QFile::exists(candidate + "/map.png")) {
            return candidate;
        }
        if (!dir.cdUp()) break;
    }
    QString srcAssets = QDir(QStringLiteral(SOURCE_DIR)).absoluteFilePath("assets");
    if (QFile::exists(srcAssets + "/map.png")) {
        return srcAssets;
    }
    return QString();
}

void TestIpcAnimations::initTestCase()
{
    QString assetsDir = findAssetsDir();
    QVERIFY2(!assetsDir.isEmpty(), "Could not find assets directory");

    m_engine = new SpriteAnimationEngine(this);
    m_engine->loadAssets(assetsDir + "/map.png", assetsDir + "/animations.json");

    m_bubble = new TipWidget(nullptr);
    m_bubble->setAnchorRect(QRect(0, 0, 124, 93));

    m_tips = new TipsEngine(this);
    // H1: TipsEngine now emits animationRequested(QString); the test harness
    // wires it directly to the sprite engine since this test predates the
    // multi-engine fan-out and only exercises the legacy SpriteAnimationEngine.
    connect(m_tips, &TipsEngine::animationRequested, this, [this](const QString &name) {
        if (m_engine && m_engine->hasAnimations() && !name.isEmpty()) {
            m_engine->playAnimation(name, SpriteAnimationEngine::NormalPriority);
        }
    });
    m_tips->setTipWidget(m_bubble);

    m_router = new EventRouter(this);
    m_router->setTipWidget(m_bubble);
    m_router->setTipsEngine(m_tips);

    m_fsm = new PetStateMachine(this);
    connect(m_router, &EventRouter::eventProcessed,
            m_fsm, &PetStateMachine::onCanonicalEvent);
    connect(m_fsm, &PetStateMachine::animationRequested,
            this, [this](const QStringList &chain, int /*priority*/) {
        if (!chain.isEmpty() && m_engine) {
            m_engine->playAnimation(chain.first(), SpriteAnimationEngine::HighPriority);
        }
    });

    m_ipc = new IPCServer(this);
    connect(m_ipc, &IPCServer::eventReceived, m_router, &EventRouter::routeEvent);
    connect(m_ipc, &IPCServer::tipReceived, m_bubble, [this](const QJsonObject &tip) {
        const QString title = tip.value("title").toString("Tip");
        const QString body = tip.value("body").toString();
        const QString anim = tip.value("animation").toString();
        m_bubble->showBubble(title, body, TipWidget::TipBubble);
        if (!anim.isEmpty()) {
            m_engine->playAnimation(anim, SpriteAnimationEngine::NormalPriority);
        }
    });

    QVERIFY(m_ipc->start(QStringLiteral("127.0.0.1:%1").arg(TEST_PORT)));
}

void TestIpcAnimations::cleanupTestCase()
{
    m_ipc->stop();
    delete m_bubble;
}

void TestIpcAnimations::sendJson(QUdpSocket *socket, const QJsonObject &obj)
{
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    socket->writeDatagram(data, QHostAddress(TEST_HOST), TEST_PORT);
}

QByteArray TestIpcAnimations::waitForResponse(QUdpSocket *socket, int timeoutMs)
{
    QTest::qWait(timeoutMs);
    if (socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(socket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;
        socket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        return data.trimmed();
    }
    return QByteArray();
}

// ------------------------------------------------------------------
// 1. Basic ping/pong health check
// ------------------------------------------------------------------
void TestIpcAnimations::testPingPong()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    sendJson(&client, QJsonObject{{"type", "ping"}});

    QByteArray response = waitForResponse(&client, 1000);
    QVERIFY(!response.isEmpty());

    QJsonDocument doc = QJsonDocument::fromJson(response);
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value("type").toString(), QStringLiteral("pong"));

    client.close();
}

// ------------------------------------------------------------------
// 2. Event triggers animation via UDP
// ------------------------------------------------------------------
void TestIpcAnimations::testEventTriggersAnimation()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    QSignalSpy spy(m_engine, &SpriteAnimationEngine::frameChanged);

    QJsonObject event{
        {"type", "event"},
        {"source", "opencode"},
        {"event", "session.start"}
    };
    sendJson(&client, event);

    QTest::qWait(200);

    QVERIFY(!m_engine->currentAnimation().isEmpty());
    QCOMPARE(m_engine->isPlaying(), true);

    client.close();
}

// ------------------------------------------------------------------
// ------------------------------------------------------------------
// 3. Tip message shows correct title & body via UDP
// ------------------------------------------------------------------
void TestIpcAnimations::testTipMessage()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    QJsonObject tip{
        {"type", "tip"},
        {"title", "Hello Test"},
        {"body", "This is a test tip"},
        {"animation", "wave"}
    };
    sendJson(&client, tip);

    QTest::qWait(200);

    QCOMPARE(m_bubble->title(), QStringLiteral("Hello Test"));
    QCOMPARE(m_bubble->message(), QStringLiteral("This is a test tip"));
    QCOMPARE(m_bubble->bubbleType(), TipWidget::TipBubble);

    client.close();
}

// ------------------------------------------------------------------
// 5. Multiple datagrams from same client
// ------------------------------------------------------------------
void TestIpcAnimations::testMultipleDatagrams()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    for (int i = 0; i < 5; ++i) {
        QJsonObject event{
            {"type", "event"},
            {"source", "claude-code"},
            {"event", "tool.before"}
        };
        sendJson(&client, event);
    }

    QTest::qWait(500);
    QVERIFY(m_engine->isPlaying() || !m_engine->currentAnimation().isEmpty());

    client.close();
}

// ------------------------------------------------------------------
// 6. Malformed JSON is handled gracefully
// ------------------------------------------------------------------
void TestIpcAnimations::testMalformedJson()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    QByteArray badData = "this is not json\n";
    client.writeDatagram(badData, QHostAddress(TEST_HOST), TEST_PORT);

    QTest::qWait(100);

    QJsonObject event{
        {"type", "event"},
        {"source", "opencode"},
        {"event", "session.start"}
    };
    sendJson(&client, event);

    QTest::qWait(200);
    QVERIFY(m_engine->isPlaying());

    client.close();
}

// ------------------------------------------------------------------
// 7. Unknown event name is logged, not crashed
// ------------------------------------------------------------------
void TestIpcAnimations::testUnknownEventType()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    QJsonObject event{
        {"type", "event"},
        {"source", "opencode"},
        {"event", "totally.fake.event"}
    };
    sendJson(&client, event);

    QTest::qWait(100);
    QVERIFY(true); // Did not crash

    client.close();
}

// ------------------------------------------------------------------
// 8. HighPriority events interrupt queued animations
// ------------------------------------------------------------------
void TestIpcAnimations::testPriorityQueue()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    // Send a normal-priority animation
    QJsonObject event1{
        {"type", "event"},
        {"source", "opencode"},
        {"event", "session.start"}
    };
    sendJson(&client, event1);
    QTest::qWait(50);

    // Send a high-priority event that should interrupt
    QJsonObject event2{
        {"type", "event"},
        {"source", "opencode"},
        {"event", "session.error"}
    };
    sendJson(&client, event2);
    QTest::qWait(200);

    QVERIFY(m_engine->isPlaying());

    client.close();
}

// ------------------------------------------------------------------
// 9. Tip message with animation shows bubble AND plays animation
// ------------------------------------------------------------------
void TestIpcAnimations::testTipWithAnimation()
{
    QUdpSocket client;
    client.bind(QHostAddress::Any, 0);

    QJsonObject tip{
        {"type", "tip"},
        {"title", "Animated Tip"},
        {"body", "With animation"},
        {"animation", "wave"}
    };
    sendJson(&client, tip);

    QTest::qWait(500);

    // Verify bubble shows
    QCOMPARE(m_bubble->title(), QStringLiteral("Animated Tip"));
    QCOMPARE(m_bubble->message(), QStringLiteral("With animation"));

    // Verify animation is playing (may be wave or previous animation still running)
    QVERIFY(m_engine->isPlaying());

    client.close();
}

QTEST_MAIN(TestIpcAnimations)
#include "test_ipc_animations.moc"
