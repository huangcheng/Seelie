#include "StatisticsPersistence.h"

#include <QJsonDocument>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QDebug>

StatisticsPersistence::StatisticsPersistence(const QString &configDir)
    : m_filePath(configDir + QStringLiteral("/statistics.json"))
{
}

QJsonObject StatisticsPersistence::load() const
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // No file yet — return empty object
        return QJsonObject();
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "StatisticsPersistence: failed to parse" << m_filePath
                 << ":" << parseError.errorString();
        return QJsonObject();
    }

    return doc.object();
}

bool StatisticsPersistence::save(const QJsonObject &data) const
{
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "StatisticsPersistence: failed to open" << m_filePath
                 << "for writing:" << file.errorString();
        return false;
    }

    const QJsonDocument doc(data);
    file.write(doc.toJson(QJsonDocument::Indented));

    if (!file.commit()) {
        qWarning() << "StatisticsPersistence: failed to commit" << m_filePath
                 << ":" << file.errorString();
        return false;
    }

    return true;
}

QJsonObject StatisticsPersistence::loadSection(const QString &key) const
{
    const QJsonObject root = load();
    return root.value(key).toObject();
}

bool StatisticsPersistence::saveSection(const QString &key, const QJsonObject &sectionData)
{
    QJsonObject root = load();
    root.insert(key, sectionData);
    return save(root);
}
