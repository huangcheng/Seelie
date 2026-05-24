#include "IPCServer.h"
#include "UDPWorker.h"

#include <QDateTime>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

IPCServer::IPCServer(QObject *parent)
    : QObject(parent)
{
    m_stats.startedAtMs = QDateTime::currentMSecsSinceEpoch();
}

IPCServer::~IPCServer()
{
    stop();
}

bool IPCServer::start(const QString &endpoint)
{
    if (m_thread) {
        qWarning() << "IPC: Server already running";
        return false;
    }

    m_thread = new QThread(this);
    m_worker = new UDPWorker();
    m_worker->moveToThread(m_thread);

    // M2: capture m_worker by value (copy of pointer) instead of [this]
    // to avoid cross-thread read of a member that the main thread may mutate.
    UDPWorker *worker = m_worker;
    connect(m_thread, &QThread::started, m_worker, [worker, endpoint]() {
        worker->start(endpoint);
    });

    connect(m_worker, &UDPWorker::started, this, [endpoint]() {
        qDebug() << "IPC: UDP server listening on:" << endpoint;
    });

    connect(m_worker, &UDPWorker::stopped, this, []() {
        qDebug() << "IPC: UDP worker stopped";
    });

    connect(m_worker, &UDPWorker::errorOccurred, this, &IPCServer::onWorkerError);

    connect(m_worker, &UDPWorker::datagramReceived, this, &IPCServer::onDatagramReceived);

    // Stats: worker-thread signals → main-thread slots via queued connection.
    // Qt picks QueuedConnection automatically (different threads), so we don't
    // need to specify it explicitly, but being explicit documents the intent.
    connect(m_worker, &UDPWorker::packetReceived, this, &IPCServer::onPacketReceived,
            Qt::QueuedConnection);

    // H2: avoid deleteLater on finished thread — the worker thread's event
    // loop has already stopped, so deleteLater can never be dispatched and
    // the worker leaks. Instead we delete explicitly after wait() succeeds.
    // Do NOT connect QThread::finished → deleteLater here.

    m_thread->start();
    return true;
}

void IPCServer::stop()
{
    if (m_thread) {
        // Queue stop() on the worker's thread (the socket is thread-affine —
        // QSocketNotifier must close on the same thread it was created on).
        // Use QueuedConnection rather than BlockingQueuedConnection: if the
        // worker thread is wedged for any reason, a blocking invoke from the
        // main thread waits forever on QLatch and the GUI hangs at quit.
        // Queued FIFO ordering ensures stop() runs before the event loop
        // exits via quit().
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
        m_thread->quit();
        // Bound the wait so a wedged worker can't freeze the main thread on
        // shutdown. 5s is generous — UDP socket close is normally instant.
        bool finishedCleanly = m_thread->wait(5000);
        if (finishedCleanly) {
            // H2: explicitly delete the worker from the main thread now that
            // the worker thread has exited. The thread itself is safe to delete.
            delete m_worker;
            m_worker = nullptr;
            delete m_thread;
            m_thread = nullptr;
        } else {
            // Do NOT terminate(). UDPWorker has worker-thread affinity and
            // owns a QUdpSocket / QSocketNotifier; killing the thread mid-
            // syscall leaks the OS file descriptor and can leave the port
            // bound until the process exits. Cross-thread delete from the
            // main thread is just as bad — it destroys the socket on the
            // wrong thread and triggers UB on Qt's socket-notifier cleanup.
            //
            // Instead: detach and intentionally leak the thread + worker.
            // We are on the shutdown path; the OS reclaims everything at
            // process exit. Better a deterministic exit than a flaky crash.
            qWarning() << "IPC: worker thread did not stop within 5s; "
                          "leaking rather than terminating to keep socket "
                          "and Qt object cleanup on the right thread";
            m_thread->setParent(nullptr);
            m_thread = nullptr;
            m_worker = nullptr;
        }
    }
}

bool IPCServer::restart(const QString &endpoint)
{
    stop();
    return start(endpoint);
}

void IPCServer::onDatagramReceived(const QByteArray &data, const QHostAddress &sender, quint16 port)
{
    parseMessage(data, sender, port);
}

void IPCServer::onWorkerError(const QString &message)
{
    qWarning() << "IPC:" << message;
}

void IPCServer::parseMessage(const QByteArray &data, const QHostAddress &sender, quint16 port)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "IPC: Malformed JSON:" << error.errorString()
                    << "data:" << data.left(200);
        ++m_stats.decodeErrors;
        return;
    }

    if (!doc.isObject()) {
        qWarning() << "IPC: Expected JSON object, got:" << data.left(200);
        ++m_stats.decodeErrors;
        return;
    }

    QJsonObject msg = doc.object();
    const QString type = msg.value("type").toString();

    if (type == "event") {
        emit eventReceived(msg);
    } else if (type == "tip") {
        emit tipReceived(msg);
    } else if (type == "ping") {
        if (!m_worker) {
            // Server is shutting down; the worker has already been torn
            // down and the queued invoke below would target a destroyed
            // object. Silently drop the pong — the caller has no recovery
            // path, and emitting pingReceived here would let callers fire
            // their own writes against the same dead socket. M3.
            return;
        }
        QJsonObject pong;
        pong["type"] = "pong";
        QJsonDocument pongDoc(pong);
        QByteArray response = pongDoc.toJson(QJsonDocument::Compact) + "\n";
        // M3: check invokeMethod return value — if the worker thread's event
        // loop has already quit, the call is silently dropped and the caller
        // never gets a pong.
        bool invoked = QMetaObject::invokeMethod(m_worker, "sendDatagram",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, response),
                                  Q_ARG(QHostAddress, sender),
                                  Q_ARG(quint16, port));
        if (!invoked) {
            qWarning() << "IPC: failed to queue pong sendDatagram — worker thread may have exited";
        }
        emit pingReceived(sender, port);
    } else {
        qWarning() << "IPC: Unknown message type:" << type;
    }
}

void IPCServer::onPacketReceived()
{
    ++m_stats.packets;
}
