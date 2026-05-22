#ifndef UDPWORKER_H
#define UDPWORKER_H

#include <QObject>
#include <QHostAddress>

class QUdpSocket;

class UdpWorker : public QObject
{
    Q_OBJECT

public:
    explicit UdpWorker(QObject *parent = nullptr);
    ~UdpWorker() override;

public slots:
    void start(const QString &endpoint);
    void stop();
    void sendDatagram(const QByteArray &data, const QHostAddress &host, quint16 port);

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &message);
    void datagramReceived(const QByteArray &data, const QHostAddress &sender, quint16 port);

private slots:
    void onReadyRead();

private:
    QUdpSocket *m_socket = nullptr;
    bool m_shuttingDown = false; // L1: guard signal emission during destructor
};

#endif // UDPWORKER_H
