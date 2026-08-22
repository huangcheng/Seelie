#include "CharacterPackManager.h"
#include "CharacterPack.h"
#include "CharacterPackLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QRegularExpression>

#include "../thirdparty/miniz/miniz.h"

namespace {

// Maximum size we will allocate for any JSON-y entry (manifest.json,
// pet.json) extracted from an untrusted archive. mz_zip_reader_extract_to_heap
// returns size_t; legitimate manifests are < 100 KB. 10 MB is a comfortable
// upper bound that still rejects pathological / hostile entries before any
// allocation occurs.
constexpr size_t kMaxManifestBytes = 10 * 1024 * 1024;

// Returns true if the entry at `index` has a reasonable size for a manifest-
// like JSON file. Writes the size into `outSize` on success.
bool zipEntryFitsManifestCap(mz_zip_archive *zip, int index, size_t *outSize)
{
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(zip, static_cast<mz_uint>(index), &stat))
        return false;
    if (stat.m_uncomp_size > kMaxManifestBytes)
        return false;
    if (outSize) *outSize = static_cast<size_t>(stat.m_uncomp_size);
    return true;
}

// Reject ZIP entry names that would escape the staging directory. The
// archive is untrusted user input (drag-and-drop, downloaded .spk); a
// malicious entry like "../../Library/LaunchAgents/evil.plist" must NOT
// be allowed to overwrite files outside the install root.
//
// Returns the absolute destination path on success, or an empty string
// if the entry should be skipped.
QString safeZipDestination(const QString &stagingDir, const QString &entryName)
{
    if (entryName.isEmpty()) return QString();

    // Reject absolute paths and Windows drive-letter prefixes outright.
    // (miniz uses forward slashes per ZIP spec, but be defensive about
    // archives produced by misbehaving tools.)
    if (entryName.startsWith('/') || entryName.startsWith('\\'))
        return QString();
    if (entryName.size() >= 2 && entryName[1] == ':')
        return QString();

    // Reject any segment that is exactly ".." — catches both "../etc" and
    // "foo/../../etc". A plain "." segment is harmless (it's a no-op) but
    // we strip it via QDir::cleanPath below.
    const QStringList parts = entryName.split(QRegularExpression(R"([/\\])"),
                                              Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QStringLiteral(".."))
            return QString();
    }

    const QString candidate = QDir::cleanPath(stagingDir + "/" + entryName);
    const QString cleanStaging = QDir::cleanPath(stagingDir);

    // Defense in depth: after path normalization, the destination must
    // still live under the staging dir. Use '/' boundary to avoid the
    // "stagingDir-evil" sibling-prefix attack.
    if (candidate != cleanStaging &&
        !candidate.startsWith(cleanStaging + '/'))
        return QString();

    return candidate;
}

} // namespace

CharacterPackManager::CharacterPackManager(QObject *parent)
    : QObject(parent)
{
    m_hotReloadTimer = new QTimer(this);
    m_hotReloadTimer->setSingleShot(true);
    m_hotReloadTimer->setInterval(500);  // 500ms debounce
    connect(m_hotReloadTimer, &QTimer::timeout, this, &CharacterPackManager::onHotReloadTimer);
}

CharacterPackManager::~CharacterPackManager()
{
    cleanupFileWatcher();
    qDeleteAll(m_loadedPacks);
}

void CharacterPackManager::initialize(const QString &builtInDir, const QString &userDir,
                                       const QString &preferredId)
{
    m_builtInDir = builtInDir;
    m_userDir = userDir;

    // Ensure user directory exists
    QDir().mkpath(m_userDir);

    // Discover all packs (built-in stay in app bundle, user packs in config dir)
    discoverPacks();

    // Setup file watcher for hot-reload
    setupFileWatcher();

    // Load default pack: preferred ID from config (if still installed),
    // then the built-in Seelie mascot (the project's signature pack — the
    // initial default for first-launch users, before they pick anything),
    // then the alphabetically-first available pack as a final fallback.
    // If the preferred pack fails to load, keep trying others until one
    // succeeds — without this a broken user pack leaves the pet invisible.
    static constexpr const char *kSignaturePackId = "im.cheng.seelie.seelie";
    if (!m_packs.isEmpty()) {
        QStringList candidates;
        if (!preferredId.isEmpty() && m_packs.contains(preferredId)) {
            candidates.append(preferredId);
        }
        if (m_packs.contains(QString::fromLatin1(kSignaturePackId))
            && !candidates.contains(QString::fromLatin1(kSignaturePackId))) {
            candidates.append(QString::fromLatin1(kSignaturePackId));
        }
        for (const QString &id : m_packs.keys()) {
            if (!candidates.contains(id)) {
                candidates.append(id);
            }
        }

        bool loaded = false;
        for (const QString &packId : candidates) {
            if (switchPack(packId)) {
                loaded = true;
                break;
            }
            qWarning() << "CharacterPackManager: Failed to load pack" << packId
                       << "— trying next fallback.";
        }

        if (!loaded) {
            qWarning() << "CharacterPackManager: No packs could be loaded!";
        }
    } else {
        qWarning() << "CharacterPackManager: No sprite packs found!";
    }
}

QVector<CharacterPackManager::PackInfo> CharacterPackManager::availablePacks() const
{
    return m_packs.values().toVector();
}

QString CharacterPackManager::PackInfo::displayName(const QString &localeCode) const
{
    if (!localeCode.isEmpty()) {
        if (auto it = nameLocalized.constFind(localeCode); it != nameLocalized.constEnd()) {
            return it.value();
        }
        const QString lang = localeCode.section('_', 0, 0);
        for (auto it = nameLocalized.constBegin(); it != nameLocalized.constEnd(); ++it) {
            if (it.key().section('_', 0, 0) == lang) {
                return it.value();
            }
        }
    }
    return name;
}

bool CharacterPackManager::switchPack(const QString &packId)
{

    if (!m_packs.contains(packId)) {
        qWarning() << "CharacterPackManager: Pack not found in m_packs:" << packId;
        qWarning() << "  Available pack IDs:";
        for (const auto &key : m_packs.keys()) {
            qWarning() << "    -" << key;
        }
        return false;
    }

    if (packId == m_activePackId && m_activePack) {
        qDebug() << "CharacterPackManager: Pack already active:" << packId;
        return true;
    }

    const PackInfo info = m_packs.value(packId);
    qDebug() << "  Pack info: name=" << info.name << "path=" << info.path;

    // Load pack if not already loaded
    if (!m_loadedPacks.contains(packId)) {
        qDebug() << "  Loading pack from path:" << info.path;
        CharacterPack *pack = createAndLoadPack(info.path);
        if (!pack) {
            qWarning() << "CharacterPackManager: createAndLoadPack returned nullptr for:" << packId;
            return false;
        }
        qDebug() << "  Pack loaded successfully, isValid:" << pack->isValid()
                 << "engineType:" << static_cast<int>(pack->characterConfig().engineType);
        m_loadedPacks[packId] = pack;
    }

    // Switch active pack
    m_activePackId = packId;
    m_activePack = m_loadedPacks[packId];

    qDebug() << "CharacterPackManager: Switched to pack:" << info.name;
    emit activePackChanged(m_activePack);

    return true;
}

bool CharacterPackManager::installPack(const QString &archivePath)
{
    m_lastError.clear();

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivePath.toUtf8().constData(), 0)) {
        qWarning() << "CharacterPackManager: Failed to open archive:" << archivePath;
        m_lastError = tr("Could not open the pack archive. It may be corrupt or unreadable.");
        return false;
    }

    // First pass: read manifest.json to get the pack ID
    int manifestIdx = mz_zip_reader_locate_file(&zip, "manifest.json", nullptr, 0);
    if (manifestIdx < 0) {
        qWarning() << "CharacterPackManager: No manifest.json in archive:" << archivePath;
        m_lastError = tr("Archive is missing manifest.json — not a valid Seelie pack.");
        mz_zip_reader_end(&zip);
        return false;
    }

    size_t manifestSize = 0;
    if (!zipEntryFitsManifestCap(&zip, manifestIdx, &manifestSize)) {
        qWarning() << "CharacterPackManager: manifest.json missing or exceeds size cap in:"
                   << archivePath;
        m_lastError = tr("manifest.json is unreadable or unreasonably large (>10 MB).");
        mz_zip_reader_end(&zip);
        return false;
    }
    void *manifestData = mz_zip_reader_extract_to_heap(&zip, manifestIdx, &manifestSize, 0);
    if (!manifestData) {
        qWarning() << "CharacterPackManager: Failed to read manifest.json from:" << archivePath;
        m_lastError = tr("Could not read manifest.json from the archive.");
        mz_zip_reader_end(&zip);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(static_cast<const char *>(manifestData),
                                                           static_cast<int>(manifestSize)));
    mz_free(manifestData);

    if (!doc.isObject()) {
        qWarning() << "CharacterPackManager: Invalid manifest.json in:" << archivePath;
        m_lastError = tr("manifest.json is not valid JSON.");
        mz_zip_reader_end(&zip);
        return false;
    }

    QString packId = doc.object().value("id").toString();
    if (packId.isEmpty()) {
        qWarning() << "CharacterPackManager: Pack manifest missing 'id' in:" << archivePath;
        m_lastError = tr("manifest.json is missing the required \"id\" field.");
        mz_zip_reader_end(&zip);
        return false;
    }

    // Extract to a sibling .tmp directory first, then atomically rename on
    // success. Without this, an extraction failure mid-way (disk full,
    // permission denied, corrupt entry) leaves a half-installed pack at the
    // final path that next launch will try to load and warn about.
    QString installDir = m_userDir + "/" + packId;
    QString stagingDir = installDir + ".tmp";

    // Wipe any leftover staging from a prior interrupted install.
    QDir(stagingDir).removeRecursively();
    if (!QDir().mkpath(stagingDir)) {
        qWarning() << "CharacterPackManager: Failed to create staging dir:" << stagingDir;
        m_lastError = tr("Could not create the staging directory at %1. "
                         "Check disk space and permissions.").arg(stagingDir);
        mz_zip_reader_end(&zip);
        return false;
    }

    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    bool extractOk = true;
    for (int i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;

        QString entryName = QString::fromUtf8(stat.m_filename);
        QString destPath = safeZipDestination(stagingDir, entryName);
        if (destPath.isEmpty()) {
            qWarning() << "CharacterPackManager: Rejecting unsafe archive entry:"
                       << entryName;
            m_lastError = tr("Archive contains an unsafe path (\"%1\") and was rejected.")
                              .arg(entryName);
            extractOk = false;
            break;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            QDir().mkpath(destPath);
        } else {
            // Ensure parent directory exists
            QDir().mkpath(QFileInfo(destPath).absolutePath());
            if (!mz_zip_reader_extract_to_file(&zip, i, destPath.toUtf8().constData(), 0)) {
                qWarning() << "CharacterPackManager: Failed to extract:" << entryName;
                m_lastError = tr("Failed to extract \"%1\" — disk full or permission denied.")
                                  .arg(entryName);
                extractOk = false;
                break;
            }
        }
    }

    mz_zip_reader_end(&zip);

    if (!extractOk) {
        QDir(stagingDir).removeRecursively();
        return false;
    }

    // Atomically replace the existing install (if any) with the staged copy.
    if (QDir(installDir).exists() && !QDir(installDir).removeRecursively()) {
        qWarning() << "CharacterPackManager: Failed to remove existing install:" << installDir;
        m_lastError = tr("Could not remove the previously installed copy at %1.").arg(installDir);
        QDir(stagingDir).removeRecursively();
        return false;
    }
    if (!QDir().rename(stagingDir, installDir)) {
        qWarning() << "CharacterPackManager: Failed to rename staging dir to:" << installDir;
        m_lastError = tr("Could not finalise the install at %1.").arg(installDir);
        QDir(stagingDir).removeRecursively();
        return false;
    }

    qDebug() << "CharacterPackManager: Installed pack" << packId << "to" << installDir;

    // Refresh pack list so UI updates immediately
    discoverPacks();
    emit packListChanged();

    return true;
}

bool CharacterPackManager::uninstallPack(const QString &packId)
{
    m_lastError.clear();

    if (!m_packs.contains(packId)) {
        qWarning() << "CharacterPackManager: Pack not found:" << packId;
        m_lastError = tr("Pack \"%1\" is not installed.").arg(packId);
        return false;
    }

    const PackInfo info = m_packs.value(packId);

    // Only user packs can be uninstalled
    if (info.source != PackSource::User) {
        qWarning() << "CharacterPackManager: Cannot uninstall built-in pack:" << packId;
        m_lastError = tr("Cannot uninstall built-in pack \"%1\".").arg(info.name);
        return false;
    }

    // Cannot uninstall the currently active pack
    if (packId == m_activePackId) {
        qWarning() << "CharacterPackManager: Cannot uninstall active pack:" << packId;
        m_lastError = tr("Switch to a different pack before uninstalling \"%1\".").arg(info.name);
        return false;
    }

    QFileInfo pathInfo(info.path);
    if (pathInfo.isFile()) {
        if (!QFile::remove(info.path)) {
            qWarning() << "CharacterPackManager: Failed to remove pack file:" << info.path;
            m_lastError = tr("Could not delete %1 — permission denied or file in use.").arg(info.path);
            return false;
        }
    } else if (pathInfo.isDir()) {
        QDir packDir(info.path);
        if (!packDir.removeRecursively()) {
            qWarning() << "CharacterPackManager: Failed to remove pack directory:" << info.path;
            m_lastError = tr("Could not delete %1 — some files may be in use or read-only.").arg(info.path);
            return false;
        }
    }

    // Remove from loaded packs
    if (m_loadedPacks.contains(packId)) {
        delete m_loadedPacks[packId];
        m_loadedPacks.remove(packId);
    }

    // Remove from packs map
    m_packs.remove(packId);

    qDebug() << "CharacterPackManager: Uninstalled pack:" << packId;
    emit packListChanged();

    return true;
}

bool CharacterPackManager::isPackInstalled(const QString &packId) const
{
    return m_packs.contains(packId);
}

CharacterPackManager::PackInfo CharacterPackManager::packInfo(const QString &packId) const
{
    return m_packs.value(packId);
}

void CharacterPackManager::setHotReloadEnabled(bool enabled)
{
    m_hotReloadEnabled = enabled;
    if (enabled) {
        setupFileWatcher();
    } else {
        cleanupFileWatcher();
    }
}

void CharacterPackManager::reloadCurrentPack()
{
    if (m_activePackId.isEmpty() || !m_activePack) {
        return;
    }

    const PackInfo info = m_packs.value(m_activePackId);

    // Reload pack
    CharacterPack *newPack = createAndLoadPack(info.path);
    if (!newPack) {
        qWarning() << "CharacterPackManager: Failed to reload pack:" << m_activePackId;
        return;
    }

    // Replace loaded pack. Update m_activePack before deleting the old
    // object so no signal emitted between the two (future-proofing against
    // a use-after-free if a refactor inserts emit/slot dispatch here).
    CharacterPack *oldPack = m_loadedPacks[m_activePackId];
    m_loadedPacks[m_activePackId] = newPack;
    m_activePack = newPack;
    delete oldPack;

    qDebug() << "CharacterPackManager: Reloaded pack:" << info.name;
    emit packReloaded(m_activePack);
}

void CharacterPackManager::discoverPacks()
{
    m_packs.clear();

    auto scanDir = [&](const QString &dirPath, PackSource source) {
        QDir dir(dirPath);
        if (!dir.exists()) return;

        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            loadPackFromDirectory(entry.absoluteFilePath(), source);
        }

        const QStringList codexFilters = {"*.codex-pet", "*.codex-pet.zip"};
        const QFileInfoList codexFiles = dir.entryInfoList(codexFilters, QDir::Files);
        for (const QFileInfo &entry : codexFiles) {
            loadPackFromCodexPet(entry.absoluteFilePath(), source);
        }
    };

    // Scan user dir first, then built-in. This ensures that any stale
    // built-in pack copies left in the user directory get overridden by
    // the real built-in entry, keeping their source as BuiltIn so they
    // are filtered out of the Models Management dialog.
    if (!m_userDir.isEmpty()) {
        scanDir(m_userDir, PackSource::User);
    }

    if (!m_builtInDir.isEmpty()) {
        scanDir(m_builtInDir, PackSource::BuiltIn);
    }

    qDebug() << "CharacterPackManager: Discovered" << m_packs.size() << "packs:";
    for (auto it = m_packs.constBegin(); it != m_packs.constEnd(); ++it) {
        qDebug() << "  -" << it.key()
                 << "source=" << (it->source == PackSource::BuiltIn ? "BuiltIn" : "User")
                 << "path=" << it->path;
    }
    emit packListChanged();
}

QString CharacterPackManager::extractPackIdFromSpk(const QString &spkPath)
{
    // Shared archive-probe helper, lives in CharacterPackLoader.h. H18.
    return CharacterPackLoader::readSpkPackId(spkPath);
}

void CharacterPackManager::loadPackFromDirectory(const QString &packDir, PackSource source)
{
    // Check for manifest.json
    const QString manifestPath = QDir(packDir).absoluteFilePath("manifest.json");
    if (!QFile::exists(manifestPath)) {
        return;
    }

    // Read manifest to get pack ID
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "CharacterPackManager: Failed to open manifest:" << manifestPath;
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
    manifestFile.close();

    if (!doc.isObject()) {
        qWarning() << "CharacterPackManager: Invalid manifest:" << manifestPath;
        return;
    }

    const QJsonObject manifest = doc.object();
    const QString packId = manifest.value("id").toString();

    if (packId.isEmpty()) {
        qWarning() << "CharacterPackManager: Pack missing ID:" << manifestPath;
        return;
    }

#if !SEELIE_LOTTIE_ENABLED
    const QJsonObject character = manifest.value(QStringLiteral("character")).toObject();
    if (character.value(QStringLiteral("type")).toString() == QStringLiteral("lottie")) {
        qDebug() << "CharacterPackManager: Skipping Lottie pack (SEELIE_LOTTIE_ENABLED=OFF):"
                 << packId;
        return;
    }
#endif

    // Create pack info
    PackInfo info;
    info.id = packId;
    info.name = manifest.value("name").toString();
    const QJsonObject locNames = manifest.value("nameLocalized").toObject();
    for (auto it = locNames.begin(); it != locNames.end(); ++it) {
        const QString locale = it.key();
        const QString value = it.value().toString();
        if (!locale.isEmpty() && !value.isEmpty()) {
            info.nameLocalized.insert(locale, value);
        }
    }
    info.author = manifest.value("author").toString();
    info.version = manifest.value("version").toString();
    info.description = manifest.value("description").toString();
    info.preview = manifest.value("preview").toString();
    info.path = packDir;
    info.category = manifest.value("category").toString();
    // Legacy fallback: pre-`category` Azur Lane manifests can be detected
    // by the import-script-stamped author string.
    if (info.category.isEmpty()) {
        info.category = info.author.startsWith(QStringLiteral("Imported from github.com/"))
                            ? QStringLiteral("azur_lane")
                            : QStringLiteral("originals");
    }
    info.source = source;

    // Add to packs map (user packs override built-in with same ID)
    m_packs[packId] = info;

    qDebug() << "CharacterPackManager: Found pack:" << info.name << "(" << packId << ")";
}

void CharacterPackManager::loadPackFromCodexPet(const QString &archivePath, PackSource source)
{
    PackInfo info;
    if (!extractCodexPetInfo(archivePath, info)) {
        return;
    }

    info.path = archivePath;
    info.source = source;
    info.category = "codex";

    m_packs[info.id] = info;
    qDebug() << "CharacterPackManager: Found Codex pet:" << info.name << "(" << info.id << ")";
}

bool CharacterPackManager::extractCodexPetInfo(const QString &archivePath, PackInfo &outInfo)
{
    const QJsonObject obj = CharacterPackLoader::readJsonEntryFromArchive(
        archivePath, QStringLiteral("pet.json"),
        "CharacterPackManager::extractCodexPetInfo");
    if (obj.isEmpty()) {
        return false;
    }

    outInfo.id = obj.value("id").toString();
    outInfo.name = obj.value("displayName").toString();
    outInfo.description = obj.value("description").toString();
    outInfo.author = "codex";
    outInfo.version = "1.0.0";

    return !outInfo.id.isEmpty() && !outInfo.name.isEmpty();
}

CharacterPack *CharacterPackManager::createAndLoadPack(const QString &packPath)
{
    CharacterPack *pack = new CharacterPack();

    if (packPath.endsWith(".codex-pet", Qt::CaseInsensitive) ||
        packPath.endsWith(".codex-pet.zip", Qt::CaseInsensitive)) {
        if (!pack->loadFromCodexPet(packPath)) {
            qWarning() << "CharacterPackManager: Failed to load Codex pet from:" << packPath;
            delete pack;
            return nullptr;
        }
        return pack;
    }

    if (!pack->loadFromDirectory(packPath)) {
        qWarning() << "CharacterPackManager: Failed to load pack from:" << packPath;
        delete pack;
        return nullptr;
    }
    return pack;
}

void CharacterPackManager::setupFileWatcher()
{
    if (m_fileWatcher) {
        return;  // Already setup
    }

    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::directoryChanged,
            this, &CharacterPackManager::onDirectoryChanged);

    // Watch user packs directory
    if (!m_userDir.isEmpty() && QDir(m_userDir).exists()) {
        m_fileWatcher->addPath(m_userDir);
    }
}

void CharacterPackManager::cleanupFileWatcher()
{
    if (m_fileWatcher) {
        // Use deleteLater so any directoryChanged signal already queued from
        // the watcher's last fire doesn't land on a freed object during a
        // rapid setHotReloadEnabled(false) ↔ true toggle. L10.
        m_fileWatcher->disconnect(this);
        m_fileWatcher->deleteLater();
        m_fileWatcher = nullptr;
    }
}

void CharacterPackManager::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path);

    if (!m_hotReloadEnabled) {
        return;
    }

    // Debounce: restart timer
    m_hotReloadPending = true;
    m_hotReloadTimer->start();
}

void CharacterPackManager::onHotReloadTimer()
{
    if (!m_hotReloadPending) {
        return;
    }

    m_hotReloadPending = false;

    // Re-discover packs
    discoverPacks();

    // If active pack is still available, reload it
    if (m_packs.contains(m_activePackId)) {
        reloadCurrentPack();
    } else {
        // Active pack was removed, switch to first available
        if (!m_packs.isEmpty()) {
            switchPack(m_packs.firstKey());
        }
    }

    emit packListChanged();
}
