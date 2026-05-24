#include "TTSVoiceCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

Q_LOGGING_CATEGORY(lcTtsCache, "seelie.tts.cache")

namespace seelie::tts {

namespace {

QString optionsFingerprint(const SpeakOptions &opts)
{
    // Deterministic, compact representation of the option fields that affect
    // synthesis output. Missing optionals serialize as empty so default-
    // constructed options collapse to "||".
    const QString emotion = opts.emotion
        ? QString::number(static_cast<int>(*opts.emotion))
        : QString();
    const QString rate = opts.rate
        ? QString::number(*opts.rate, 'f', 3)
        : QString();
    return emotion + QStringLiteral("|") + rate + QStringLiteral("|") + opts.languageHint;
}

} // namespace

TTSVoiceCache::TTSVoiceCache(QObject *parent)
    : QObject(parent)
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_cacheDir = baseDir + QStringLiteral("/tts_voice_cache");
}

QString TTSVoiceCache::cacheKey(const QString &providerId,
                                const QString &voiceId,
                                const QString &modelId,
                                const SpeakOptions &options,
                                const QString &text)
{
    // simplified() collapses internal runs of whitespace and trims edges, so
    // "Hello", " Hello ", and "Hello\n" hash the same. Avoids inflating the
    // cache from incidental whitespace differences in tip strings.
    const QString normalizedText = text.simplified();
    const QString input = providerId + QStringLiteral("|") +
                          voiceId + QStringLiteral("|") +
                          modelId + QStringLiteral("|") +
                          optionsFingerprint(options) + QStringLiteral("|") +
                          normalizedText;
    return QString::fromLatin1(QCryptographicHash::hash(input.toUtf8(),
                                                         QCryptographicHash::Sha256).toHex());
}

bool TTSVoiceCache::ensureDir() const
{
    if (QDir().exists(m_cacheDir)) return true;
    if (!QDir().mkpath(m_cacheDir)) {
        qCWarning(lcTtsCache) << "failed to create cache dir:" << m_cacheDir;
        return false;
    }
    return true;
}

QString TTSVoiceCache::audioPath(const QString &key) const
{
    return m_cacheDir + QStringLiteral("/") + key + QStringLiteral(".bin");
}

QString TTSVoiceCache::mimePath(const QString &key) const
{
    return m_cacheDir + QStringLiteral("/") + key + QStringLiteral(".mime");
}

bool TTSVoiceCache::hasCachedAudio(const QString &key) const
{
    return QFile::exists(audioPath(key));
}

QByteArray TTSVoiceCache::getCachedAudio(const QString &key) const
{
    const QString path = audioPath(key);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(lcTtsCache) << "cache miss:" << key;
        return QByteArray();
    }
    qCInfo(lcTtsCache) << "cache hit:" << key << "size=" << file.size();
    return file.readAll();
}

QString TTSVoiceCache::getCachedMimeType(const QString &key) const
{
    QFile file(mimePath(key));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool TTSVoiceCache::writeCachedAudio(const QString &key,
                                     const QByteArray &audioData,
                                     const QString &mimeType)
{
    if (audioData.isEmpty()) {
        qCWarning(lcTtsCache) << "refusing to cache empty audio for key:" << key;
        return false;
    }

    if (!ensureDir()) return false;

    QFile audioFile(audioPath(key));
    if (!audioFile.open(QIODevice::WriteOnly)) {
        qCWarning(lcTtsCache) << "failed to write cache file:" << audioFile.fileName();
        return false;
    }
    audioFile.write(audioData);
    audioFile.close();

    if (!mimeType.isEmpty()) {
        QFile mimeFile(mimePath(key));
        if (mimeFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            mimeFile.write(mimeType.toUtf8());
            mimeFile.close();
        } else {
            qCWarning(lcTtsCache) << "failed to write mime sidecar:" << mimeFile.fileName();
        }
    }

    qCInfo(lcTtsCache) << "cached audio:" << key
                       << "size=" << audioData.size()
                       << "mime=" << mimeType;
    evictIfNeeded();
    return true;
}

void TTSVoiceCache::evictIfNeeded()
{
    if (m_maxBytes <= 0) return;
    QDir dir(m_cacheDir);
    if (!dir.exists()) return;

    QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::NoSort);

    // Only count .bin and .mime files toward the size limit. Other files
    // (e.g. stray thumbs.db, .DS_Store) should not inflate the total and
    // trigger premature eviction (audit M15).
    qint64 total = 0;
    for (const QFileInfo &fi : entries) {
        const QString suffix = fi.suffix();
        if (suffix == QStringLiteral("bin") || suffix == QStringLiteral("mime"))
            total += fi.size();
    }
    if (total <= m_maxBytes) return;

    // Group .bin and its .mime sidecar so eviction takes both atomically and
    // we don't end up with orphan sidecars.
    std::sort(entries.begin(), entries.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  return a.lastModified() < b.lastModified();
              });

    qint64 removed = 0;
    int removedCount = 0;
    for (const QFileInfo &fi : entries) {
        if (total - removed <= m_maxBytes) break;
        if (fi.suffix() != QStringLiteral("bin")) continue;
        const QString base = fi.completeBaseName();
        const QString sidecar = mimePath(base);

        // Count the .bin size and the .mime sidecar size (if any) toward
        // the freed total so the accounting is accurate.
        const qint64 sidecarSize = QFileInfo::exists(sidecar)
            ? QFileInfo(sidecar).size() : 0;
        const qint64 sz = fi.size() + sidecarSize;
        if (QFile::remove(fi.absoluteFilePath())) {
            removed += sz;
            ++removedCount;
            QFile::remove(sidecar); // best-effort
        }
    }
    if (removedCount > 0) {
        qCInfo(lcTtsCache) << "evicted" << removedCount << "entries,"
                           << removed << "bytes;"
                           << "now under" << m_maxBytes << "bytes";
    }
}

void TTSVoiceCache::wipeAll()
{
    if (!QDir().exists(m_cacheDir)) return;
    QDir dir(m_cacheDir);
    const QStringList files = dir.entryList(QDir::Files);
    int removed = 0;
    int failed = 0;
    for (const QString &file : files) {
        if (dir.remove(file)) {
            ++removed;
        } else {
            ++failed;
            qCWarning(lcTtsCache) << "failed to remove cache entry:" << file;
        }
    }
    qCInfo(lcTtsCache) << "cache wiped:" << removed << "removed,"
                       << failed << "failed (of" << files.size() << "files)";
}

} // namespace seelie::tts
