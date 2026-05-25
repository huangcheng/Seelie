#ifndef STATISTICS_PERSISTENCE_H
#define STATISTICS_PERSISTENCE_H

#include <QJsonObject>
#include <QString>

/**
 * @brief Helper for atomic read/write of statistics.json in the config directory.
 *
 * Uses QSaveFile for atomic writes to avoid corrupting the file if the app
 * crashes mid-write. Each component (TTSEngine, PersonaEngine, EventRouter,
 * IPCServer) gets its own JSON object key under the root.
 */
class StatisticsPersistence
{
public:
    explicit StatisticsPersistence(const QString &configDir);

    /// Load the entire JSON object from disk. Returns empty object if file
    /// doesn't exist or is unreadable.
    QJsonObject load() const;

    /// Save the entire JSON object atomically using QSaveFile.
    bool save(const QJsonObject &data) const;

    /// Convenience: load a sub-object by key (e.g. "tts", "persona").
    /// Returns empty object if key not present.
    QJsonObject loadSection(const QString &key) const;

    /// Convenience: update a sub-object and save the whole file.
    /// Returns true on success.
    bool saveSection(const QString &key, const QJsonObject &sectionData);

    /// Full path to the statistics.json file.
    QString filePath() const { return m_filePath; }

private:
    QString m_filePath;
};

#endif // STATISTICS_PERSISTENCE_H
