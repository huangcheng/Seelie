#ifndef CONFIG_IMPORTER_H
#define CONFIG_IMPORTER_H

#include "ExportManifest.h"
#include <QString>

/**
 * @brief Imports Seelie configuration from a ZIP archive.
 *
 * Validates the archive, extracts to a temp directory, backs up existing
 * config, and atomically replaces the current configuration.
 */
class ConfigImporter
{
public:
    explicit ConfigImporter(const QString &configDir);

    /// Imports configuration from the given ZIP path. Returns true on success.
    bool importFromZip(const QString &zipPath, QString *errorMessage = nullptr);

    /// Validates a ZIP archive without extracting. Returns true if valid.
    bool validateZip(const QString &zipPath, ExportManifest *outManifest = nullptr,
                     QString *errorMessage = nullptr);

    /// Checks if the manifest's app version differs from current (major version mismatch).
    bool isVersionMismatch(const ExportManifest &manifest) const;

    /// Returns the last error message from an import operation.
    QString lastError() const { return m_lastError; }

private:
    bool extractToTemp(const QString &zipPath, const QString &tempDir,
                       QString *errorMessage);
    bool backupExisting(QString *errorMessage);
    bool atomicReplace(const QString &tempDir, QString *errorMessage);

    QString m_configDir;
    QString m_lastError;
};

#endif // CONFIG_IMPORTER_H
