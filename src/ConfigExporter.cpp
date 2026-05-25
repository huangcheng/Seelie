#include "ConfigExporter.h"
#include "ExportManifest.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

ConfigExporter::ConfigExporter(const QString &configDir)
    : m_configDir(configDir)
{
}

QStringList ConfigExporter::collectFiles() const
{
    QStringList files;
    const QString iniPath = m_configDir + QStringLiteral("/Seelie.ini");
    const QString dbPath  = m_configDir + QStringLiteral("/memory.db");

    if (QFile::exists(iniPath))
        files << iniPath;
    if (QFile::exists(dbPath))
        files << dbPath;

    return files;
}

bool ConfigExporter::exportToZip(const QString &destinationPath, QString *errorMessage)
{
    const QStringList files = collectFiles();
    if (files.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("No configuration files found to export.");
        return false;
    }

    // Build manifest
    ExportManifest manifest;
    manifest.appVersion = QStringLiteral(PROJECT_VERSION);
    manifest.schemaVersion = 1;
    manifest.exportTimestampMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

#if defined(Q_OS_MACOS)
    manifest.platform = QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    manifest.platform = QStringLiteral("windows");
#else
    manifest.platform = QStringLiteral("linux");
#endif

    // Stage files in a temp directory with the ZIP-internal structure
    QTemporaryDir stagingDir;
    if (!stagingDir.isValid()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to create temporary staging directory.");
        return false;
    }

    const QString configStaging = stagingDir.path() + QStringLiteral("/config");
    const QString memoryStaging = stagingDir.path() + QStringLiteral("/memory");
    QDir().mkpath(configStaging);
    QDir().mkpath(memoryStaging);

    // Copy files to staging with preserved names
    for (const QString &srcPath : files) {
        QFileInfo fi(srcPath);
        QString destDir;
        if (fi.fileName().endsWith(QStringLiteral(".ini")))
            destDir = configStaging;
        else
            destDir = memoryStaging;

        if (!QFile::copy(srcPath, destDir + "/" + fi.fileName())) {
            if (errorMessage)
                *errorMessage = QObject::tr("Failed to copy %1 to staging area.").arg(fi.fileName());
            return false;
        }
    }

    // Write manifest
    QJsonDocument doc(manifest.toJson());
    QFile manifestFile(stagingDir.path() + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to write manifest.json.");
        return false;
    }
    manifestFile.write(doc.toJson(QJsonDocument::Indented));
    manifestFile.close();

    // Create ZIP using system zip command
    // Remove existing file first
    if (QFile::exists(destinationPath))
        QFile::remove(destinationPath);

    QProcess zip;
    zip.setWorkingDirectory(stagingDir.path());

#if defined(Q_OS_WIN)
    // PowerShell Compress-Archive on Windows
    const QString psCmd = QStringLiteral(
        "Compress-Archive -Path * -DestinationPath '%1' -Force"
    ).arg(destinationPath);
    zip.start("powershell.exe", QStringList() << "-Command" << psCmd);
#else
    // zip command on macOS/Linux
    zip.start("zip", QStringList()
        << "-r"
        << "-q"
        << destinationPath
        << QStringLiteral("."));
#endif

    if (!zip.waitForStarted(5000)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to start ZIP process. Is 'zip' installed?");
        return false;
    }

    if (!zip.waitForFinished(30000)) {
        zip.kill();
        if (errorMessage)
            *errorMessage = QObject::tr("ZIP process timed out.");
        return false;
    }

    if (zip.exitCode() != 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("ZIP creation failed: %1").arg(QString::fromLocal8Bit(zip.readAllStandardError()));
        return false;
    }

    return QFile::exists(destinationPath);
}

QString ConfigExporter::generateFilename()
{
    const QDateTime now = QDateTime::currentDateTime();
    return QStringLiteral("Seelie-%1-%2-%3-%4-%5-%6.zip")
        .arg(now.date().year())
        .arg(now.date().month(),  2, 10, QLatin1Char('0'))
        .arg(now.date().day(),    2, 10, QLatin1Char('0'))
        .arg(now.time().hour(),   2, 10, QLatin1Char('0'))
        .arg(now.time().minute(), 2, 10, QLatin1Char('0'))
        .arg(now.time().second(), 2, 10, QLatin1Char('0'));
}
