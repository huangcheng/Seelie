#ifndef SEELIE_TTSVOICECACHE_H
#define SEELIE_TTSVOICECACHE_H

#include "ITtsProvider.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace seelie::tts {

class TtsVoiceCache : public QObject
{
    Q_OBJECT

public:
    explicit TtsVoiceCache(QObject *parent = nullptr);
    ~TtsVoiceCache() = default;

    // Compute a cache key from the provider/voice/model fingerprint, the
    // speak options that affect synthesis output, and the (whitespace-
    // normalized) text. Returns a 64-character hex SHA-256 string.
    static QString cacheKey(const QString &providerId,
                            const QString &voiceId,
                            const QString &modelId,
                            const SpeakOptions &options,
                            const QString &text);

    QString cacheDir() const { return m_cacheDir; }

    bool hasCachedAudio(const QString &key) const;
    QByteArray getCachedAudio(const QString &key) const;
    QString getCachedMimeType(const QString &key) const;

    // Writes audio + mime sidecar. Refuses to cache empty audio. Triggers
    // LRU eviction so the directory stays under maxBytes().
    bool writeCachedAudio(const QString &key,
                          const QByteArray &audioData,
                          const QString &mimeType);

    qint64 maxBytes() const { return m_maxBytes; }
    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }

public slots:
    void wipeAll();

private:
    bool ensureDir() const;
    QString audioPath(const QString &key) const;
    QString mimePath(const QString &key) const;
    void evictIfNeeded();

    mutable QString m_cacheDir;
    qint64 m_maxBytes = 100LL * 1024 * 1024;  // 100 MB
};

} // namespace seelie::tts

#endif // SEELIE_TTSVOICECACHE_H
