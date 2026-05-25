#include "ConfigImporter.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

ConfigImporter::ConfigImporter(const QString &configDir)
    : m_configDir(configDir)
{
}

bool ConfigImporter::validateZip(const QString &zipPath, ExportManifest *outManifest,
                                  QString *errorMessage)
{
    if (!QFile::exists(zipPath)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Archive file not found.");
        return false;
    }

    // Extract just manifest.json to a temp dir for validation
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to create temporary directory.");
        return false;
    }

    QProcess unzip;
#if defined(Q_OS_WIN)
    const QString psCmd = QStringLiteral(
        "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"
    ).arg(zipPath, tempDir.path());
    unzip.start("powershell.exe", QStringList() << "-Command" << psCmd);
#else
    unzip.start("unzip", QStringList()
        << "-q"
        << "-o"
        << zipPath
        << "-d"
        << tempDir.path());
#endif

    if (!unzip.waitForStarted(5000)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to start unzip process. Is 'unzip' installed?");
        return false;
    }

    if (!unzip.waitForFinished(30000)) {
        unzip.kill();
        if (errorMessage)
            *errorMessage = QObject::tr("Unzip process timed out.");
        return false;
    }

    if (unzip.exitCode() != 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("Archive extraction failed: %1")
                .arg(QString::fromLocal8Bit(unzip.readAllStandardError()));
        return false;
    }

    // Read and validate manifest
    QFile manifestFile(tempDir.path() + QStringLiteral("/manifest.json"));
    if (!manifestFile.exists()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Invalid archive: manifest.json not found.");
        return false;
    }

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Cannot read manifest.json.");
        return false;
    }

    ExportManifest manifest = ExportManifest::fromJsonBytes(manifestFile.readAll());
    if (!manifest.isValid()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Invalid manifest: missing required fields.");
        return false;
    }

    if (!manifest.isSchemaCompatible()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Archive schema version %1 is not supported. "
                                        "Please update Seelie.")
                .arg(manifest.schemaVersion);
        return false;
    }

    // Check expected files exist in extracted temp dir
    const QString iniPath = tempDir.path() + QStringLiteral("/config/Seelie.ini");
    if (!QFile::exists(iniPath)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Invalid archive: config/Seelie.ini not found.");
        return false;
    }

    if (outManifest)
        *outManifest = manifest;

    return true;
}

bool ConfigImporter::isVersionMismatch(const ExportManifest &manifest) const
{
    const QString currentVersion = QStringLiteral(PROJECT_VERSION);
    const QStringList currentParts = currentVersion.split('.');
    const QStringList archiveParts = manifest.appVersion.split('.');

    if (currentParts.isEmpty() || archiveParts.isEmpty())
        return false;

    bool currentOk = false, archiveOk = false;
    const int currentMajor = currentParts.first().toInt(&currentOk);
    const int archiveMajor = archiveParts.first().toInt(&archiveOk);

    if (!currentOk || !archiveOk)
        return false;

    return currentMajor != archiveMajor;
}

bool ConfigImporter::importFromZip(const QString &zipPath, QString *errorMessage)
{
    // Step 1: Validate
    ExportManifest manifest;
    if (!validateZip(zipPath, &manifest, errorMessage))
        return false;

    // Step 2: Extract to temp directory
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to create temporary extraction directory.");
        return false;
    }

    if (!extractToTemp(zipPath, tempDir.path(), errorMessage))
        return false;

    // Step 3: Backup existing config
    if (!backupExisting(errorMessage))
        return false;

    // Step 4: Atomic replacement
    if (!atomicReplace(tempDir.path(), errorMessage))
        return false;

    return true;
}

bool ConfigImporter::extractToTemp(const QString &zipPath, const QString &tempDir,
                                    QString *errorMessage)
{
    QProcess unzip;
#if defined(Q_OS_WIN)
    const QString psCmd = QStringLiteral(
        "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"
    ).arg(zipPath, tempDir);
    unzip.start("powershell.exe", QStringList() << "-Command" << psCmd);
#else
    unzip.start("unzip", QStringList()
        << "-q"
        << "-o"
        << zipPath
        << "-d"
        << tempDir);
#endif

    if (!unzip.waitForStarted(5000)) {
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to start unzip process.");
        return false;
    }

    if (!unzip.waitForFinished(30000)) {
        unzip.kill();
        if (errorMessage)
            *errorMessage = QObject::tr("Unzip timed out.");
        return false;
    }

    if (unzip.exitCode() != 0) {
        if (errorMessage)
            *errorMessage = QObject::tr("Extraction failed: %1")
                .arg(QString::fromLocal8Bit(unzip.readAllStandardError()));
        return false;
    }

    return true;
}

bool ConfigImporter::backupExisting(QString *errorMessage)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString backupName = QStringLiteral("config-backup-%1-%2-%3-%4-%5-%6.zip")
        .arg(now.date().year())
        .arg(now.date().month(),  2, 10, QLatin1Char('0'))
        .arg(now.date().day(),    2, 10, QLatin1Char('0'))
        .arg(now.time().hour(),   2, 10, QLatin1Char('0'))
        .arg(now.time().minute(), 2, 10, QLatin1Char('0'))
        .arg(now.time().second(), 2, 10, QLatin1Char('0'));
    const QString backupPath = m_configDir + "/" + backupName;

    // Create backup ZIP of current config dir
    QProcess zip;
    zip.setWorkingDirectory(m_configDir);

#if defined(Q_OS_WIN)
    const QString psCmd = QStringLiteral(
        "Compress-Archive -Path * -DestinationPath '%1' -Force"
    ).arg(backupPath);
    zip.start("powershell.exe", QStringList() << "-Command" << psCmd);
#else
    zip.start("zip", QStringList()
        << "-r"
        << "-q"
        << backupPath
        << QStringLiteral("."));
#endif

    if (!zip.waitForStarted(5000) || !zip.waitForFinished(30000)) {
        if (zip.state() != QProcess::NotRunning)
            zip.kill();
        // Non-fatal: log but don't fail import if backup fails
        qWarning() << "ConfigImporter: backup creation failed (non-fatal)";
        return true; // Continue with import even if backup fails
    }

    return true;
}

bool ConfigImporter::atomicReplace(const QString &tempDir, QString *errorMessage)
{
    // Move existing config files to .old, then copy from temp
    const QString iniSrc = tempDir + QStringLiteral("/config/Seelie.ini");
    const QString dbSrc  = tempDir + QStringLiteral("/memory/memory.db");
    const QString iniDst = m_configDir + QStringLiteral("/Seelie.ini");
    const QString dbDst  = m_configDir + QStringLiteral("/memory.db");

    // Rename existing files to .old (atomic)
    const QString iniOld = iniDst + QStringLiteral(".old");
    const QString dbOld  = dbDst  + QStringLiteral(".old");

    bool hasExistingIni = QFile::exists(iniDst);
    bool hasExistingDb  = QFile::exists(dbDst);

    if (hasExistingIni) {
        QFile::remove(iniOld);
        if (!QFile::rename(iniDst, iniOld)) {
            if (errorMessage)
                *errorMessage = QObject::tr("Failed to backup existing config file.");
            return false;
        }
    }

    if (hasExistingDb) {
        QFile::remove(dbOld);
        if (!QFile::rename(dbDst, dbOld)) {
            // Rollback ini rename
            if (hasExistingIni)
                QFile::rename(iniOld, iniDst);
            if (errorMessage)
                *errorMessage = QObject::tr("Failed to backup existing memory database.");
            return false;
        }
    }

    // Copy new files
    bool iniCopied = QFile::copy(iniSrc, iniDst);
    bool dbCopied  = QFile::exists(dbSrc) ? QFile::copy(dbSrc, dbDst) : true;

    if (!iniCopied || !dbCopied) {
        // Rollback
        QFile::remove(iniDst);
        QFile::remove(dbDst);
        if (hasExistingIni)
            QFile::rename(iniOld, iniDst);
        if (hasExistingDb)
            QFile::rename(dbOld, dbDst);
        if (errorMessage)
            *errorMessage = QObject::tr("Failed to copy new configuration files.");
        return false;
    }

    // Clean up .old files on success
    QFile::remove(iniOld);
    QFile::remove(dbOld);

    return true;
}
