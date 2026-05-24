#ifndef UDP_WORKER_H
#define UDP_WORKER_H

#include <QObject>
#include <QHostAddress>

class QUdpSocket;

class UDPWorker : public QObject
{
    Q_OBJECT

public:
    explicit UDPWorker(QObject *parent = nullptr);
    ~UDPWorker() override;

public slots:
    void start(const QString &endpoint);
    void stop();
    void sendDatagram(const QByteArray &data, const QHostAddress &host, quint16 port);

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &message);
    void datagramReceived(const QByteArray &data, const QHostAddress &sender, quint16 port);
    /// Emitted from the worker thread once per successfully read datagram.
    void packetReceived();

private slots:
    void onReadyRead();

private:
    QUdpSocket *m_socket = nullptr;
    bool m_shuttingDown = false; // L1: guard signal emission during destructor
};

#endif // UDP_WORKER_H
