#include "UpdateChecker.h"
#include "ConfigManager.h"

#include <QUdpSocket>
#include <QTimer>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QtEndian>
#include <QDebug>

namespace {

/// Compare two dotted version strings (e.g. "1.2.3" vs "1.3.0").
/// Returns true if \a a is strictly less than \a b.
/// Non-numeric segments are compared lexicographically as a tiebreaker.
bool versionLessThan(const QString &a, const QString &b)
{
    const QStringList aParts = a.split(QLatin1Char('.'));
    const QStringList bParts = b.split(QLatin1Char('.'));
    const int count = qMax(aParts.size(), bParts.size());
    for (int i = 0; i < count; ++i) {
        bool aOk = false, bOk = false;
        const int aNum = (i < aParts.size()) ? aParts[i].toInt(&aOk) : 0;
        const int bNum = (i < bParts.size()) ? bParts[i].toInt(&bOk) : 0;
        if (aOk && bOk) {
            if (aNum != bNum) return aNum < bNum;
        } else {
            // Fallback: lexicographic comparison of non-numeric segments.
            const QString &aSeg = (i < aParts.size()) ? aParts[i] : QString();
            const QString &bSeg = (i < bParts.size()) ? bParts[i] : QString();
            if (aSeg != bSeg) return aSeg < bSeg;
        }
    }
    return false; // equal
}

} // namespace

// SECURITY NOTE (audit C5): The update check uses unencrypted UDP with CRC-16
// integrity only. This is vulnerable to on-path spoofing. Defense-in-depth
// measures applied: version strings validated as semver (M16), download URLs
// must use https://, sequence numbers prevent replay (H12). A future migration
// to HTTPS is recommended for production-grade security.

UpdateChecker::UpdateChecker(ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_socket(new QUdpSocket(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(CHECK_TIMEOUT_MS);
    connect(m_timeout, &QTimer::timeout, this, &UpdateChecker::onTimeout);
    connect(m_socket, &QUdpSocket::readyRead, this, &UpdateChecker::onReadyRead);
    // Bind to an ephemeral local port; replies come back to it.
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
        qWarning() << "UpdateChecker: failed to bind UDP socket:" << m_socket->errorString();
    }
}

QString UpdateChecker::currentVersion()
{
    return QStringLiteral(PROJECT_VERSION);
}

QString UpdateChecker::platformTag()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MAC)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

quint16 UpdateChecker::crc16ccitt(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (auto byte : data) {
        crc ^= static_cast<quint8>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) crc = static_cast<quint16>((crc << 1) ^ 0x1021);
            else              crc = static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

QByteArray UpdateChecker::encodeCheck(quint16 seq) const
{
    QJsonObject payloadObj;
    payloadObj.insert(QStringLiteral("current_version"), currentVersion());
    payloadObj.insert(QStringLiteral("platform"), platformTag());
    const QByteArray payload = QJsonDocument(payloadObj).toJson(QJsonDocument::Compact);

    QByteArray header;
    header.reserve(10);
    header.append('H').append('C').append('H').append(static_cast<char>(0x01));   // magic
    header.append(static_cast<char>(0x01));                                        // protocol version
    header.append(static_cast<char>(CMD_CHECK));                                   // command
    char seqBytes[2]; qToBigEndian<quint16>(seq, seqBytes);  header.append(seqBytes, 2);
    char lenBytes[2]; qToBigEndian<quint16>(static_cast<quint16>(payload.size()), lenBytes); header.append(lenBytes, 2);

    QByteArray packet = header + payload;
    char crcBytes[2]; qToBigEndian<quint16>(crc16ccitt(packet), crcBytes);
    packet.append(crcBytes, 2);
    return packet;
}

void UpdateChecker::checkForUpdates(bool userTriggered)
{
    if (m_inFlight) {
        // A check is already on the wire. If THIS request is user-driven
        // but the in-flight one wasn't, upgrade the flag so the response
        // surfaces feedback to the user instead of being silent.
        if (userTriggered && !m_userTriggered) {
            m_userTriggered = true;
            qDebug() << "UpdateChecker: upgrading in-flight check to user-triggered";
        }
        return;
    }
    if (!m_config) {
        m_userTriggered = userTriggered;
        emit checkFailed(QStringLiteral("no ConfigManager"));
        return;
    }

    m_userTriggered = userTriggered;

    // Set m_inFlight immediately to prevent a second checkForUpdates() call
    // from overwriting m_pendingSeq before DNS resolves (audit H12).
    m_inFlight = true;

    const QString endpoint = m_config->updateServerEndpoint();

    // Basic endpoint validation (audit H17): reject if empty or obviously
    // malformed. The endpoint is a host:port pair, not a URL — no scheme.
    if (endpoint.isEmpty() || endpoint.contains(QLatin1Char('@'))) {
        m_inFlight = false;
        emit checkFailed(QStringLiteral("invalid updateServerEndpoint: %1").arg(endpoint));
        return;
    }

    const int colon = endpoint.lastIndexOf(':');
    if (colon <= 0) {
        m_inFlight = false;
        emit checkFailed(QStringLiteral("invalid updateServerEndpoint: %1").arg(endpoint));
        return;
    }
    const QString host = endpoint.left(colon);
    const quint16 port = endpoint.mid(colon + 1).toUShort();
    if (port == 0) {
        m_inFlight = false;
        emit checkFailed(QStringLiteral("invalid port in updateServerEndpoint: %1").arg(endpoint));
        return;
    }

    m_pendingSeq = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 0x10000));

    QHostAddress addr(host);
    if (!addr.isNull()) {
        // Literal IP — send immediately.
        sendCheckPacket(addr, port);
        return;
    }

    // Hostname — resolve asynchronously so a slow DNS query doesn't freeze
    // the GUI thread (audit M1). The callback fires on whichever thread
    // QHostInfo picks; capture by value and bounce through the QObject
    // receiver so we run on this thread.
    qDebug() << "UpdateChecker: resolving" << host << "asynchronously";
    QHostInfo::lookupHost(host, this, [this, host, port](const QHostInfo &info) {
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            m_inFlight = false;
            emit checkFailed(QStringLiteral("dns lookup failed for %1: %2")
                                 .arg(host, info.errorString()));
            return;
        }
        sendCheckPacket(info.addresses().first(), port);
    });
}

void UpdateChecker::sendCheckPacket(const QHostAddress &addr, quint16 port)
{
    const QByteArray packet = encodeCheck(m_pendingSeq);
    qDebug() << "UpdateChecker: CHECK to" << addr.toString() << ":" << port
             << "seq=" << m_pendingSeq << "current=" << currentVersion();
    const qint64 sent = m_socket->writeDatagram(packet, addr, port);
    if (sent != packet.size()) {
        m_inFlight = false;
        emit checkFailed(QStringLiteral("UDP send failed: %1").arg(m_socket->errorString()));
        return;
    }
    // m_inFlight was already set in checkForUpdates() before DNS resolution.
    m_timeout->start();
}

void UpdateChecker::onReadyRead()
{
    // Cap to 65535 bytes (UDP payload max); pendingDatagramSize() is qint64
    // and a negative/huge value would cause QByteArray::resize() UB.
    constexpr qint64 kMaxDatagramBytes = 65535;

    while (m_socket->hasPendingDatagrams()) {
        const qint64 pending = m_socket->pendingDatagramSize();
        if (pending < 0 || pending > kMaxDatagramBytes) {
            qWarning() << "UpdateChecker: dropping oversized/invalid datagram, size ="
                       << pending;
            char sink[1];
            m_socket->readDatagram(sink, 0);
            continue;
        }
        QByteArray buf;
        buf.resize(static_cast<int>(pending));
        m_socket->readDatagram(buf.data(), buf.size());

        // Validate framing: 10-byte header + N-byte payload + 2-byte CRC trailer.
        if (buf.size() < 12) { qWarning() << "UpdateChecker: packet too short"; continue; }
        if (buf[0] != 'H' || buf[1] != 'C' || buf[2] != 'H' || buf[3] != 0x01) {
            qWarning() << "UpdateChecker: bad magic"; continue;
        }
        const quint8 cmd = static_cast<quint8>(buf[5]);
        const quint16 seq = qFromBigEndian<quint16>(buf.constData() + 6);
        const quint16 len = qFromBigEndian<quint16>(buf.constData() + 8);
        if (10 + len + 2 != static_cast<quint16>(buf.size())) {
            qWarning() << "UpdateChecker: length mismatch"; continue;
        }
        const quint16 wireCrc = qFromBigEndian<quint16>(buf.constData() + 10 + len);
        const quint16 calcCrc = crc16ccitt(buf.left(10 + len));
        if (wireCrc != calcCrc) {
            qWarning() << "UpdateChecker: CRC mismatch wire=" << Qt::hex << wireCrc
                       << "calc=" << calcCrc; continue;
        }
        // Drop replies that don't match our outstanding request.
        if (!m_inFlight || seq != m_pendingSeq) {
            qDebug() << "UpdateChecker: stale/unexpected seq" << seq << "expected" << m_pendingSeq;
            continue;
        }

        // Stop the timeout — we got a valid reply for our request.
        m_timeout->stop();
        m_inFlight = false;

        if (cmd != CMD_ANNOUNCE) {
            emit checkFailed(QStringLiteral("unexpected command in reply: %1").arg(cmd));
            return;
        }
        const QByteArray payload = buf.mid(10, len);
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit checkFailed(QStringLiteral("malformed ANNOUNCE: %1").arg(perr.errorString()));
            return;
        }
        const QJsonObject obj = doc.object();
        const bool available = obj.value(QStringLiteral("available")).toBool(false);
        const QString cur = currentVersion();
        if (available) {
            const QString latest = obj.value(QStringLiteral("latest_version")).toString();
            // Validate the version string looks like semver (audit M16).
            // Reject clearly malformed values to make spoofing harder.
            static const QRegularExpression semverRe(
                QStringLiteral(R"(^\d+\.\d+\.\d+.*)"));
            if (!semverRe.match(latest).hasMatch()) {
                qWarning() << "UpdateChecker: server sent invalid version string:" << latest;
                emit checkFailed(QStringLiteral("server returned invalid version format"));
                return;
            }
            // Only emit updateAvailable if the remote version is strictly newer.
            if (versionLessThan(cur, latest)) {
                // Validate download URL if present (audit C5): must be https://.
                QString downloadUrl = obj.value(
                    QStringLiteral("download_url")).toString();
                if (!downloadUrl.isEmpty() && !downloadUrl.startsWith(
                        QStringLiteral("https://"))) {
                    qWarning() << "UpdateChecker: rejecting non-HTTPS download URL";
                    downloadUrl.clear();
                }
                qDebug() << "UpdateChecker: update available — current=" << cur << "latest=" << latest;
                emit updateAvailable(cur, latest, downloadUrl);
            } else {
                qDebug() << "UpdateChecker: server version not newer — current=" << cur << "latest=" << latest;
                emit noUpdateAvailable(cur);
            }
        } else {
            qDebug() << "UpdateChecker: no update available";
            emit noUpdateAvailable(cur);
        }
        return;
    }
}

void UpdateChecker::onTimeout()
{
    if (!m_inFlight) return;
    m_inFlight = false;
    qWarning() << "UpdateChecker: no reply from update server within"
               << CHECK_TIMEOUT_MS << "ms";
    emit checkFailed(QStringLiteral("no reply from update server (timeout)"));
}
