#ifndef CONFIG_EXPORTER_H
#define CONFIG_EXPORTER_H

#include "ExportManifest.h"
#include <QString>
#include <QStringList>

/**
 * @brief Exports Seelie configuration to a ZIP archive.
 *
 * Collects Seelie.ini and memory.db from the config directory,
 * writes a manifest.json, and packages them into a ZIP file.
 */
class ConfigExporter
{
public:
    explicit ConfigExporter(const QString &configDir);

    /// Exports config to the given file path. Returns true on success.
    bool exportToZip(const QString &destinationPath, QString *errorMessage = nullptr);

    /// Generates a default filename: Seelie-YYYY-MM-DD-HH-MM-SS.zip
    static QString generateFilename();

    /// Returns the list of files that would be exported.
    QStringList collectFiles() const;

private:
    QString m_configDir;
};

#endif // CONFIG_EXPORTER_H
