#include "ExportManifest.h"
#include <QJsonDocument>
#include <QDateTime>

QJsonObject ExportManifest::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("appVersion")] = appVersion;
    obj[QStringLiteral("schemaVersion")] = schemaVersion;
    obj[QStringLiteral("exportTimestampMs")] = exportTimestampMs;
    obj[QStringLiteral("platform")] = platform;
    return obj;
}

ExportManifest ExportManifest::fromJson(const QJsonObject &obj)
{
    ExportManifest m;
    m.appVersion = obj.value(QStringLiteral("appVersion")).toString();
    m.schemaVersion = obj.value(QStringLiteral("schemaVersion")).toInt(1);
    m.exportTimestampMs = static_cast<qint64>(
        obj.value(QStringLiteral("exportTimestampMs")).toDouble(0));
    m.platform = obj.value(QStringLiteral("platform")).toString();
    return m;
}

ExportManifest ExportManifest::fromJsonBytes(const QByteArray &bytes)
{
    QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (doc.isObject())
        return fromJson(doc.object());
    return ExportManifest();
}

bool ExportManifest::isValid() const
{
    return !appVersion.isEmpty()
        && schemaVersion > 0
        && exportTimestampMs > 0
        && !platform.isEmpty();
}

bool ExportManifest::isSchemaCompatible() const
{
    // Current schema version is 1. We can import any archive with
    // schemaVersion <= current version. Future schema versions > 1
    // will require app updates.
    return schemaVersion <= 1;
}
