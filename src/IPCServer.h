#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include <QObject>
#include <QJsonDocument>
#include <QHostAddress>

class QThread;
class UDPWorker;

// NOTE: IPC packet counters are in-memory only (no MemoryManager persistence).
// UDPWorker lives on a worker thread; marshalling SQLite writes from there
// would violate the single-connection thread invariant. Per-session packet
// counts are sufficient for the StatisticsDialog.
struct IpcStats {
    qint64 packets      = 0;
    qint64 decodeErrors = 0;
    qint64 startedAtMs  = 0;
};

class IPCServer : public QObject
{
    Q_OBJECT

public:
    explicit IPCServer(QObject *parent = nullptr);
    ~IPCServer() override;

    bool start(const QString &endpoint);
    void stop();
    bool restart(const QString &endpoint);

    IpcStats stats() const { return m_stats; }

    void loadStats(const QString &configDir);
    void saveStats(const QString &configDir);

signals:
    void eventReceived(const QJsonObject &event);
    void tipReceived(const QJsonObject &tip);
    void pingReceived(const QHostAddress &sender, quint16 port);

private slots:
    void onDatagramReceived(const QByteArray &data, const QHostAddress &sender, quint16 port);
    void onWorkerError(const QString &message);
    void onPacketReceived();

private:
    void parseMessage(const QByteArray &data, const QHostAddress &sender, quint16 port);

    QThread *m_thread = nullptr;
    UDPWorker *m_worker = nullptr;
    IpcStats m_stats;
};

#endif // IPC_SERVER_H
