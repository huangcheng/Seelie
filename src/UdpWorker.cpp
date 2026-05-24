#include "UdpWorker.h"

#include <QUdpSocket>
#include <QHostAddress>
#include <QDebug>

UdpWorker::UdpWorker(QObject *parent)
    : QObject(parent)
{
}

UdpWorker::~UdpWorker()
{
    m_shuttingDown = true; // L1: prevent stop() from emitting stopped() during destruction
    stop();
}

void UdpWorker::start(const QString &endpoint)
{
    if (m_socket) {
        emit errorOccurred("UdpWorker already started");
        return;
    }

    int colonPos = endpoint.lastIndexOf(':');
    if (colonPos <= 0) {
        emit errorOccurred("Invalid endpoint format (expected host:port): " + endpoint);
        return;
    }

    QString host = endpoint.left(colonPos);
    bool portOk = false;
    quint16 port = endpoint.mid(colonPos + 1).toUShort(&portOk);
    if (!portOk || port == 0) {
        emit errorOccurred("Invalid port (must be 1-65535): " + endpoint.mid(colonPos + 1));
        return;
    }

    QHostAddress address;
    if (!address.setAddress(host)) {
        emit errorOccurred("Invalid host address: " + host);
        return;
    }

    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &UdpWorker::onReadyRead);

    // H1: pass ReuseAddressHint so a restarted process can rebind even if
    // a previous wedged thread hasn't fully released the port yet.
    if (!m_socket->bind(address, port, QAbstractSocket::ReuseAddressHint | QAbstractSocket::DefaultForPlatform)) {
        emit errorOccurred("Failed to bind UDP on " + endpoint + ": " + m_socket->errorString());
        delete m_socket;
        m_socket = nullptr;
        return;
    }

    qDebug() << "IPC: UDP worker listening on:" << endpoint;
    emit started();
}

void UdpWorker::stop()
{
    if (m_socket) {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
        // L1: suppress signal emission during destructor — receivers may be
        // destroyed already and Qt considers signals from destructors unsafe.
        if (!m_shuttingDown) {
            emit stopped();
        }
    }
}

void UdpWorker::sendDatagram(const QByteArray &data, const QHostAddress &host, quint16 port)
{
    if (m_socket) {
        // M1: check writeDatagram return value and emit error on failure
        qint64 written = m_socket->writeDatagram(data, host, port);
        if (written < 0) {
            emit errorOccurred("sendDatagram failed: " + m_socket->errorString());
        }
    }
}

void UdpWorker::onReadyRead()
{
    if (!m_socket) return;

    // Maximum theoretical UDP payload is 65507 bytes (65535 minus IPv4
    // header and UDP header). pendingDatagramSize() returns qint64; clamp
    // before resizing and discard anything pathological.
    constexpr qint64 kMaxDatagramBytes = 65535;

    // C2: cap datagrams per readyRead to bound main-thread event queue growth
    constexpr int kMaxDatagramsPerRead = 100;
    int processed = 0;

    while (m_socket->hasPendingDatagrams() && ++processed <= kMaxDatagramsPerRead) {
        const qint64 pending = m_socket->pendingDatagramSize();
        if (pending < 0 || pending > kMaxDatagramBytes) {
            qWarning() << "UdpWorker: dropping oversized/invalid datagram, size ="
                       << pending;
            // C1: drain with a real-sized buffer — readDatagram(sink, 0) is
            // undefined and may not advance the read pointer, causing infinite loop.
            QByteArray sink(qMin(pending > 0 ? pending : qint64(1), kMaxDatagramBytes), 0);
            m_socket->readDatagram(sink.data(), sink.size());
            continue;
        }
        QByteArray datagram;
        datagram.resize(pending);

        QHostAddress sender;
        quint16 senderPort;

        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        if (!datagram.isEmpty()) {
            emit packetReceived();
            emit datagramReceived(datagram.trimmed(), sender, senderPort);
        }
    }
}
