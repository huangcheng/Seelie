#ifndef EXPORT_MANIFEST_H
#define EXPORT_MANIFEST_H

#include <QJsonObject>
#include <QString>

/**
 * @brief Metadata manifest embedded in exported configuration archives.
 *
 * The manifest enables version validation, schema checking, and future
 * extensibility when importing archives created by different app versions.
 */
struct ExportManifest
{
    QString appVersion;       /// Seelie application version (e.g. "1.0.0")
    int schemaVersion = 1;    /// Manifest schema version for forward compatibility
    qint64 exportTimestampMs; /// UTC milliseconds since epoch
    QString platform;         /// Target platform: "macos", "windows", "linux"

    QJsonObject toJson() const;
    static ExportManifest fromJson(const QJsonObject &obj);
    static ExportManifest fromJsonBytes(const QByteArray &bytes);

    /// Validates that the manifest has all required fields.
    bool isValid() const;

    /// Checks schema compatibility. Returns true if this app can import
    /// an archive with the given manifest schema version.
    bool isSchemaCompatible() const;
};

#endif // EXPORT_MANIFEST_H
