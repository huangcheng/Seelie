#include "CharacterPack.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QTemporaryDir>
#include <QImage>
#include <QImageReader>
#include <QCryptographicHash>

#include "../thirdparty/miniz/miniz.h"

QString CharacterPack::Metadata::displayName(const QString &localeCode) const
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

bool CharacterPack::loadFromDirectory(const QString &packDir)
{
    QDir dir(packDir);
    if (!dir.exists()) {
        qWarning() << "CharacterPack: Directory does not exist:" << packDir;
        return false;
    }

    // Check for manifest.json
    const QString manifestPath = dir.absoluteFilePath("manifest.json");
    if (!QFile::exists(manifestPath)) {
        qWarning() << "CharacterPack: No manifest.json found in:" << packDir;
        return false;
    }

    // Read and parse manifest
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "CharacterPack: Failed to open manifest.json:" << manifestPath;
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &error);
    manifestFile.close();

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "CharacterPack: manifest.json parse error:" << error.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qWarning() << "CharacterPack: manifest.json is not an object";
        return false;
    }

    // Parse manifest
    m_rootPath = dir.absolutePath();
    if (!parseManifest(doc.object())) {
        qWarning() << "CharacterPack: Failed to parse manifest";
        return false;
    }

    m_valid = true;
    qDebug() << "CharacterPack: Loaded pack" << m_metadata.name << "from" << m_rootPath;
    return true;
}

CharacterPack::~CharacterPack()
{
    // Clean up the QTemporaryDir we kept alive past loadFromCodexPet by
    // calling setAutoRemove(false). Without this, every Codex-pet reload
    // (drag-drop, manifest hot reload, app restart loop) leaks ~3 MB to
    // /tmp until reboot. H4.
    if (!m_extractedTempPath.isEmpty()) {
        QDir(m_extractedTempPath).removeRecursively();
    }
}

bool CharacterPack::loadFromCodexPet(const QString &archivePath)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivePath.toUtf8().constData(), 0)) {
        qWarning() << "CharacterPack: Failed to open codex-pet archive:" << archivePath;
        return false;
    }

    // --- Read pet.json -------------------------------------------------------
    int petJsonIdx = mz_zip_reader_locate_file(&zip, "pet.json", nullptr, 0);
    if (petJsonIdx < 0) {
        qWarning() << "CharacterPack: No pet.json in codex-pet archive:" << archivePath;
        mz_zip_reader_end(&zip);
        return false;
    }

    // Refuse to allocate for pathological pet.json sizes. mz_zip's size_t is
    // attacker-controlled via the archive central directory; without a cap a
    // hostile archive could request a multi-gigabyte allocation.
    constexpr size_t kMaxPetJsonBytes = 10 * 1024 * 1024;
    mz_zip_archive_file_stat petStat;
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(petJsonIdx), &petStat) ||
        petStat.m_uncomp_size > kMaxPetJsonBytes) {
        qWarning() << "CharacterPack: pet.json missing or exceeds size cap in:" << archivePath;
        mz_zip_reader_end(&zip);
        return false;
    }
    size_t petJsonSize = 0;
    void *petJsonData = mz_zip_reader_extract_to_heap(&zip, petJsonIdx, &petJsonSize, 0);
    if (!petJsonData) {
        qWarning() << "CharacterPack: Failed to read pet.json from:" << archivePath;
        mz_zip_reader_end(&zip);
        return false;
    }

    QJsonDocument petDoc = QJsonDocument::fromJson(
        QByteArray(static_cast<const char *>(petJsonData), static_cast<int>(petJsonSize)));
    mz_free(petJsonData);

    if (!petDoc.isObject()) {
        qWarning() << "CharacterPack: Invalid pet.json in:" << archivePath;
        mz_zip_reader_end(&zip);
        return false;
    }

    const QJsonObject petObj = petDoc.object();

    // --- Extract spritesheet.webp to temp dir --------------------------------
    int sheetIdx = mz_zip_reader_locate_file(&zip, "spritesheet.webp", nullptr, 0);
    if (sheetIdx < 0) {
        qWarning() << "CharacterPack: No spritesheet.webp in codex-pet archive:" << archivePath;
        mz_zip_reader_end(&zip);
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qWarning() << "CharacterPack: Failed to create temp dir for codex-pet extraction";
        mz_zip_reader_end(&zip);
        return false;
    }

    QString sheetPath = tempDir.path() + "/spritesheet.webp";
    if (!mz_zip_reader_extract_to_file(&zip, sheetIdx, sheetPath.toUtf8().constData(), 0)) {
        qWarning() << "CharacterPack: Failed to extract spritesheet.webp from:" << archivePath;
        mz_zip_reader_end(&zip);
        return false;
    }

    mz_zip_reader_end(&zip);

    if (!QImageReader::supportedImageFormats().contains("webp")) {
        qWarning() << "CharacterPack: WEBP image format not supported. "
                      "Ensure the qwebp Qt image format plugin is installed and deployed. "
                      "On macOS: install via 'brew install qtimageformats' and run 'macdeployqt'. "
                      "The plugin should be at <bundle>/Contents/PlugIns/imageformats/libqwebp.dylib";
        return false;
    }

    QImage sheetImage(sheetPath);
    if (sheetImage.isNull()) {
        qWarning() << "CharacterPack: Failed to load spritesheet image from:" << sheetPath;
        return false;
    }
    if (sheetImage.width() != 1536 || sheetImage.height() != 1872) {
        qWarning() << "CharacterPack: Codex pet spritesheet has unexpected dimensions"
                   << sheetImage.size() << "expected 1536x1872";
        return false;
    }

    if (!sheetImage.hasAlphaChannel()) {
        QRgb topLeft = sheetImage.pixel(0, 0);
        QRgb topRight = sheetImage.pixel(sheetImage.width() - 1, 0);
        QRgb bottomLeft = sheetImage.pixel(0, sheetImage.height() - 1);
        QRgb bottomRight = sheetImage.pixel(sheetImage.width() - 1, sheetImage.height() - 1);

        if (topLeft == topRight && topLeft == bottomLeft && topLeft == bottomRight) {
            m_characterConfig.colorKey = QColor(topLeft);
            qDebug() << "CharacterPack: Sprite sheet lacks alpha, using color key:"
                     << m_characterConfig.colorKey.name();
        } else {
            qWarning() << "CharacterPack: Sprite sheet has no alpha and inconsistent corners,"
                       << "background may not be transparent";
        }
    }

    m_rootPath = tempDir.path();
    m_extractedTempPath = tempDir.path();
    tempDir.setAutoRemove(false);

    // --- Populate metadata ---------------------------------------------------
    m_metadata.formatVersion = "1.0.0";
    m_metadata.id = petObj.value("id").toString();
    m_metadata.name = petObj.value("displayName").toString();
    m_metadata.description = petObj.value("description").toString();
    m_metadata.author = "codex";
    m_metadata.version = "1.0.0";

    if (m_metadata.id.isEmpty() || m_metadata.name.isEmpty()) {
        qWarning() << "CharacterPack: Codex pet missing required fields (id/displayName)";
        return false;
    }

    // --- Populate character config -------------------------------------------
    m_characterConfig.engineType = EngineType::SpriteSheet;
    m_characterConfig.spriteSheet = "spritesheet.webp";
    m_characterConfig.frameWidth = 192;
    m_characterConfig.frameHeight = 208;
    // Render at 124px wide (matches the historical Clippy pet width) — the
    // SKILL.md 192×208 cell size has no host-display guidance, and rendering
    // at native resolution makes Codex pets visually dominate the desktop
    // next to other packs. Source rect stays 192×208, no cell bleed.
    m_characterConfig.displayScale = 124.0f / 192.0f;

    // --- Generate animation defs from the 8×9 atlas --------------------------
    // Codex pet format: fixed 8 columns × 9 rows, 192×208 cells
    constexpr int CODEX_COLUMNS = 8;
    constexpr int CODEX_ROWS = 9;
    constexpr int CELL_WIDTH = 192;
    constexpr int CELL_HEIGHT = 208;

    const QVector<int> frameCounts = {6, 8, 8, 4, 5, 8, 6, 6, 6};
    const QVector<QString> rowNames = {
        "idle", "running-right", "running-left", "waving",
        "jumping", "failed", "waiting", "running", "review"
    };
    const QVector<QVector<int>> frameDurations = {
        {280, 110, 110, 140, 140, 320},           // idle
        {120, 120, 120, 120, 120, 120, 120, 220}, // running-right
        {120, 120, 120, 120, 120, 120, 120, 220}, // running-left
        {140, 140, 140, 280},                     // waving
        {140, 140, 140, 140, 280},                // jumping
        {140, 140, 140, 140, 140, 140, 140, 240}, // failed
        {150, 150, 150, 150, 150, 260},           // waiting
        {120, 120, 120, 120, 120, 220},           // running
        {150, 150, 150, 150, 150, 280}            // review
    };

    for (int row = 0; row < CODEX_ROWS; ++row) {
        AnimationDef anim;
        anim.name = rowNames[row];
        anim.type = EngineType::SpriteSheet;
        anim.loop = true;

        int numFrames = frameCounts[row];
        for (int col = 0; col < numFrames; ++col) {
            FrameDef frame;
            frame.x = col * CELL_WIDTH;
            frame.y = row * CELL_HEIGHT;
            frame.w = CELL_WIDTH;
            frame.h = CELL_HEIGHT;
            frame.durationMs = frameDurations[row][col];
            anim.frames.append(frame);
            anim.totalDurationMs += frame.durationMs;
        }

        m_animations[anim.name] = anim;
    }

    // --- Idle pool: only idle for MVP ----------------------------------------
    IdleEntry idleEntry;
    idleEntry.animationName = "idle";
    idleEntry.weight = 1;
    m_idlePool.append(idleEntry);

    // --- Event map: minimal MVP mapping --------------------------------------
    m_eventMap["session.error"] = QStringList{"failed"};

    m_nameMap["greet"] = "waving";
    m_nameMap["idle"] = "idle";
    m_nameMap["think"] = "waiting";
    m_nameMap["work"] = "running";
    m_nameMap["alert"] = "failed";
    m_nameMap["celebrate"] = "jumping";
    m_nameMap["rest"] = "idle";
    m_nameMap["send"] = "waving";
    m_nameMap["attention"] = "waving";
    m_nameMap["wave"] = "waving";
    m_nameMap["explain"] = "running";
    m_nameMap["thinking"] = "waiting";
    m_nameMap["congratulate"] = "waving";
    m_nameMap["sendmail"] = "waving";
    m_nameMap["getattention"] = "waving";
    m_nameMap["greeting"] = "waving";
    m_nameMap["goodbye"] = "waving";
    m_nameMap["processing"] = "running";
    m_nameMap["writing"] = "running";
    m_nameMap["searching"] = "running";
    m_nameMap["print"] = "running";
    m_nameMap["save"] = "running";
    m_nameMap["hide"] = "running-left";
    m_nameMap["show"] = "running-right";
    m_nameMap["gettechy"] = "review";
    m_nameMap["getwizardy"] = "review";
    m_nameMap["idle1"] = "idle";
    m_nameMap["idle2"] = "idle";
    m_nameMap["idle3"] = "idle";
    m_nameMap["click1"] = "waving";
    m_nameMap["click2"] = "waving";
    m_nameMap["doubleclick1"] = "waving";
    m_nameMap["doubleclick2"] = "waving";
    m_nameMap["gesture_down"] = "waving";
    m_nameMap["gesture_up"] = "waving";
    m_nameMap["gesture_left"] = "running-left";
    m_nameMap["gesture_right"] = "running-right";
    m_nameMap["lookdown"] = "idle";
    m_nameMap["lookleft"] = "idle";
    m_nameMap["lookright"] = "idle";
    m_nameMap["lookup"] = "idle";

    m_valid = true;
    qDebug() << "CharacterPack: Loaded Codex pet" << m_metadata.name << "from" << archivePath;
    return true;
}

const CharacterPack::AnimationDef *CharacterPack::animation(const QString &name) const
{
    auto it = m_animations.find(name);
    if (it != m_animations.end()) {
        return &it.value();
    }
    return nullptr;
}

QString CharacterPack::assetPath(const QString &relativePath) const
{
    if (m_rootPath.isEmpty() || relativePath.isEmpty()) {
        return QString();
    }

    // The manifest is untrusted user input. A pack with
    //   "spriteSheet": "../../sensitive.png"
    // would otherwise let assetPath() resolve outside the pack root and
    // load attacker-controlled files. Verify the cleaned absolute path
    // still lives under m_rootPath.
    const QString candidate = QDir::cleanPath(
        QDir(m_rootPath).absoluteFilePath(relativePath));
    const QString cleanRoot = QDir::cleanPath(m_rootPath);
    if (candidate != cleanRoot &&
        !candidate.startsWith(cleanRoot + '/')) {
        qWarning() << "CharacterPack: refusing asset path outside pack root:"
                   << relativePath << "(resolves to" << candidate << ")";
        return QString();
    }
    return candidate;
}

QString CharacterPack::lottieAnimationPath(const QString &animationName) const
{
    const AnimationDef *anim = animation(animationName);
    if (!anim || anim->type != EngineType::Lottie) {
        return QString();
    }
    return assetPath(anim->lottieFile);
}

QString CharacterPack::modelJsonPath() const
{
    if (m_characterConfig.engineType != EngineType::Live2D || m_characterConfig.modelJson.isEmpty()) {
        return QString();
    }
    return assetPath(m_characterConfig.modelJson);
}

QString CharacterPack::effectPath(const QString &effectName) const
{
    // Look in effects directory
    const QString effectsDir = assetPath("effects");
    if (effectsDir.isEmpty()) {
        return QString();
    }

    const QString effectFile = QDir(effectsDir).absoluteFilePath(effectName + ".json");
    if (QFile::exists(effectFile)) {
        return effectFile;
    }
    return QString();
}

QString CharacterPack::soundPath(const QString &soundName) const
{
    // Look in sounds directory
    const QString soundsDir = assetPath("sounds");
    if (soundsDir.isEmpty()) {
        return QString();
    }

    const QString soundFile = QDir(soundsDir).absoluteFilePath(soundName);
    if (QFile::exists(soundFile)) {
        return soundFile;
    }
    return QString();
}

QStringList CharacterPack::availableLottieAnimations() const
{
    QStringList result;
    if (m_characterConfig.engineType != EngineType::Lottie) {
        return result;
    }

    const QString animDir = assetPath(m_characterConfig.animDirectory);
    if (animDir.isEmpty()) {
        return result;
    }

    QDir dir(animDir);
    if (!dir.exists()) {
        return result;
    }

    // Scan for .json files
    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        result.append(fi.baseName());
    }

    return result;
}

QStringList CharacterPack::availableEffects() const
{
    QStringList result;
    const QString effectsDir = assetPath("effects");
    if (effectsDir.isEmpty()) {
        return result;
    }

    QDir dir(effectsDir);
    if (!dir.exists()) {
        return result;
    }

    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        result.append(fi.baseName());
    }

    return result;
}

QStringList CharacterPack::availableSounds() const
{
    QStringList result;
    const QString soundsDir = assetPath("sounds");
    if (soundsDir.isEmpty()) {
        return result;
    }

    QDir dir(soundsDir);
    if (!dir.exists()) {
        return result;
    }

    const QFileInfoList files = dir.entryInfoList({"*.wav", "*.mp3", "*.ogg"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        result.append(fi.fileName());
    }

    return result;
}

bool CharacterPack::parseManifest(const QJsonObject &manifest)
{
    // Parse format version
    m_metadata.formatVersion = manifest.value("formatVersion").toString();
    if (m_metadata.formatVersion != "1.0.0") {
        qWarning() << "CharacterPack: Unsupported format version:" << m_metadata.formatVersion;
        return false;
    }

    // Parse required fields
    m_metadata.id = manifest.value("id").toString();
    m_metadata.name = manifest.value("name").toString();
    m_metadata.author = manifest.value("author").toString();
    m_metadata.version = manifest.value("version").toString();

    // Optional locale-keyed display names: { "zh_CN": "拉菲", "ja_JP": "ラフィー" }
    const QJsonObject locNames = manifest.value("nameLocalized").toObject();
    for (auto it = locNames.begin(); it != locNames.end(); ++it) {
        const QString locale = it.key();
        const QString value = it.value().toString();
        if (!locale.isEmpty() && !value.isEmpty()) {
            m_metadata.nameLocalized.insert(locale, value);
        }
    }

    if (m_metadata.id.isEmpty() || m_metadata.name.isEmpty() ||
        m_metadata.author.isEmpty() || m_metadata.version.isEmpty()) {
        qWarning() << "CharacterPack: Missing required metadata fields";
        return false;
    }

    // Parse optional fields
    m_metadata.description = manifest.value("description").toString();
    m_metadata.preview = manifest.value("preview").toString();
    m_metadata.license = manifest.value("license").toString();
    m_metadata.category = manifest.value("category").toString();
    m_metadata.minAppVersion = manifest.value("minAppVersion").toString();

    // Parse tags
    const QJsonArray tagsArray = manifest.value("tags").toArray();
    for (const QJsonValue &tag : tagsArray) {
        m_metadata.tags.append(tag.toString());
    }

    // Parse character configuration
    const QJsonObject characterObj = manifest.value("character").toObject();
    if (!parseCharacter(characterObj)) {
        qWarning() << "CharacterPack: Failed to parse character configuration";
        return false;
    }

    // idlePool / eventMap / effectTriggers live at the top level of the manifest
    // in the v1.0 schema. Historical sprite packs (Clippy / ClippyJS agents)
    // shipped them nested inside the "character" object, so fall back to that
    // location for backwards compatibility — top level wins when both exist.
    auto pickArray = [&](const char *key) {
        const QJsonArray top = manifest.value(key).toArray();
        if (!top.isEmpty()) return top;
        return manifest.value("character").toObject().value(key).toArray();
    };
    auto pickObject = [&](const char *key) {
        const QJsonObject top = manifest.value(key).toObject();
        if (!top.isEmpty()) return top;
        return manifest.value("character").toObject().value(key).toObject();
    };

    if (!parseIdlePool(pickArray("idlePool"))) {
        qWarning() << "CharacterPack: Failed to parse idle pool";
        return false;
    }

    if (!parseEventMap(pickObject("eventMap"))) {
        qWarning() << "CharacterPack: Failed to parse event map";
        return false;
    }

    if (!parseEffectTriggers(pickObject("effectTriggers"))) {
        qWarning() << "CharacterPack: Failed to parse effect triggers";
        return false;
    }

    if (!parseStateMap(pickObject("stateMap"))) {
        qWarning() << "CharacterPack: Failed to parse state map";
        return false;
    }

    // Parse optional persona block (AI persona layer §5)
    if (manifest.contains(QStringLiteral("persona")) &&
        manifest.value(QStringLiteral("persona")).isObject()) {
        if (!parsePersona(manifest.value(QStringLiteral("persona")).toObject())) {
            qWarning() << "CharacterPack: persona block failed to parse; continuing with empty persona";
            m_persona = Persona{};
        }
    }

    return true;
}

bool CharacterPack::parseCharacter(const QJsonObject &character)
{
    // Parse engine type
    const QString typeStr = character.value("type").toString();
    if (typeStr == "lottie") {
        m_characterConfig.engineType = EngineType::Lottie;
    } else if (typeStr == "spriteSheet") {
        m_characterConfig.engineType = EngineType::SpriteSheet;
    } else if (typeStr == "live2d") {
        m_characterConfig.engineType = EngineType::Live2D;
    } else if (typeStr == "model3d") {
        m_characterConfig.engineType = EngineType::Model3D;
    } else {
        qWarning() << "CharacterPack: Unknown character type:" << typeStr;
        return false;
    }

    // Parse type-specific configuration
    if (m_characterConfig.engineType == EngineType::Lottie) {
        m_characterConfig.animDirectory = character.value("directory").toString("character");
    } else if (m_characterConfig.engineType == EngineType::Live2D) {
        m_characterConfig.modelJson = character.value("model").toString();
        m_characterConfig.frameWidth = character.value("frameWidth").toInt(200);
        m_characterConfig.frameHeight = character.value("frameHeight").toInt(200);
    } else if (m_characterConfig.engineType == EngineType::Model3D) {
        m_characterConfig.modelFile = character.value("model").toString();
        m_characterConfig.frameWidth = character.value("frameWidth").toInt(200);
        m_characterConfig.frameHeight = character.value("frameHeight").toInt(200);
        m_characterConfig.cameraDistance = float(character.value("cameraDistance").toDouble(0.0));
        m_characterConfig.cameraHeight = float(character.value("cameraHeight").toDouble(0.0));
        m_characterConfig.cameraYaw = float(character.value("cameraYaw").toDouble(0.0));
        m_characterConfig.unitScale = float(character.value("unitScale").toDouble(1.0));
        m_characterConfig.upAxis = character.value("upAxis").toString(QStringLiteral("y"));
    } else {
        m_characterConfig.spriteSheet = character.value("spriteSheet").toString();
        m_characterConfig.frameWidth = character.value("frameWidth").toInt();
        m_characterConfig.frameHeight = character.value("frameHeight").toInt();
        m_characterConfig.definitions = character.value("definitions").toString();

        if (m_characterConfig.spriteSheet.isEmpty() ||
            m_characterConfig.frameWidth <= 0 ||
            m_characterConfig.frameHeight <= 0) {
            qWarning() << "CharacterPack: Invalid sprite sheet configuration";
            return false;
        }
    }

    // Optional display-size multiplier. Lets a sprite pack ship at its native
    // cell resolution (which determines how the engine reads the sheet) but
    // render larger in the pet window — useful for low-res ClippyJS agents
    // next to 300×300 Live2D packs.
    const double scale = character.value("displayScale").toDouble(1.0);
    m_characterConfig.displayScale = (scale > 0.0) ? static_cast<float>(scale) : 1.0f;

    // Parse animations (from definitions file or inline)
    if (!m_characterConfig.definitions.isEmpty()) {
        // Load animations from external definitions file (original animations.json format)
        const QString definitionsPath = assetPath(m_characterConfig.definitions);
        if (!loadAnimationsFromDefinitions(definitionsPath)) {
            qWarning() << "CharacterPack: Failed to load animations from definitions:" << definitionsPath;
            return false;
        }
    } else {
        // Load animations from inline definitions
        const QJsonObject animationsObj = character.value("animations").toObject();
        if (!parseAnimations(animationsObj)) {
            qWarning() << "CharacterPack: failed to parse animations";
            return false;
        }
    }

    return true;
}

bool CharacterPack::parseAnimations(const QJsonObject &animations)
{
    for (auto it = animations.begin(); it != animations.end(); ++it) {
        const QString name = it.key();
        const QJsonObject animObj = it.value().toObject();

        AnimationDef anim;
        anim.name = name;

        if (!parseAnimationDef(name, animObj, anim)) {
            qWarning() << "CharacterPack: Failed to parse animation:" << name;
            return false;
        }

        m_animations[name] = anim;
    }

    return true;
}

bool CharacterPack::parseAnimationDef(const QString &name, const QJsonObject &def, AnimationDef &out)
{
    // Parse type
    const QString typeStr = def.value("type").toString();
    if (typeStr == "lottie") {
        out.type = EngineType::Lottie;
        out.lottieFile = def.value("file").toString();

        if (out.lottieFile.isEmpty()) {
            qWarning() << "CharacterPack: Lottie animation missing 'file' field:" << name;
            return false;
        }
    } else if (typeStr == "sprite") {
        out.type = EngineType::SpriteSheet;

        const QJsonArray framesArray = def.value("frames").toArray();
        if (!parseFrames(framesArray, out.frames)) {
            qWarning() << "CharacterPack: Failed to parse frames for animation:" << name;
            return false;
        }

        if (out.frames.isEmpty()) {
            qWarning() << "CharacterPack: Sprite animation has no frames:" << name;
            return false;
        }
    } else {
        qWarning() << "CharacterPack: Unknown animation type:" << typeStr << "for" << name;
        return false;
    }

    // Parse optional fields
    out.loop = def.value("loop").toBool(false);
    out.highPriority = def.value("priority").toString() == "high";
    out.effect = def.value("effect").toString();
    out.sound = def.value("sound").toString();

    return true;
}

bool CharacterPack::parseFrames(const QJsonArray &frames, QVector<FrameDef> &out)
{
    for (const QJsonValue &frameVal : frames) {
        const QJsonObject frameObj = frameVal.toObject();
        FrameDef frame;

        frame.durationMs = frameObj.value("duration").toInt(100);

        // Check if grid mode (col/row) or atlas mode (x/y/w/h)
        if (frameObj.contains("col") && frameObj.contains("row")) {
            frame.col = frameObj.value("col").toInt();
            frame.row = frameObj.value("row").toInt();
            frame.x = -1;  // Grid mode
            frame.y = -1;
            frame.w = -1;
            frame.h = -1;
        } else if (frameObj.contains("x") && frameObj.contains("y") && 
                   frameObj.contains("w") && frameObj.contains("h")) {
            frame.x = frameObj.value("x").toInt();
            frame.y = frameObj.value("y").toInt();
            frame.w = frameObj.value("w").toInt();
            frame.h = frameObj.value("h").toInt();
            frame.col = -1;  // Atlas mode
            frame.row = -1;
        } else {
            qWarning() << "CharacterPack: Frame missing position (col/row or x/y/w/h)";
            return false;
        }

        if (frame.durationMs <= 0) {
            qWarning() << "CharacterPack: Frame has invalid duration:" << frame.durationMs;
            return false;
        }

        out.append(frame);
    }

    return true;
}

bool CharacterPack::parseIdlePool(const QJsonArray &pool)
{
    for (const QJsonValue &entryVal : pool) {
        const QJsonObject entryObj = entryVal.toObject();
        IdleEntry entry;

        // An empty-string name is valid: Cubism models (e.g., kei, haru_greeter)
        // can group all motions under "". Reject only if the 'name' key is
        // entirely absent.
        if (!entryObj.contains("name")) {
            qWarning() << "CharacterPack: Idle pool entry missing 'name'";
            return false;
        }
        entry.animationName = entryObj.value("name").toString();
        entry.weight = entryObj.value("weight").toInt(1);

        if (entry.weight <= 0) {
            qWarning() << "CharacterPack: Idle pool entry has invalid weight:" << entry.weight;
            return false;
        }

        m_idlePool.append(entry);
    }

    return true;
}

bool CharacterPack::parseEventMap(const QJsonObject &map)
{
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QString eventName = it.key();
        const QJsonValue value = it.value();
        QStringList chain;
        // Accept either a string ("Tap") or an array (["Login","Tap","Idle"]).
        // The engine will try each in order and play the first non-empty one,
        // so a manifest can declare its own fallback semantics without the
        // engine knowing anything about specific group names.
        if (value.isArray()) {
            for (const QJsonValue &v : value.toArray()) {
                const QString s = v.toString();
                if (!s.isEmpty()) chain.append(s);
            }
        } else {
            const QString s = value.toString();
            if (!s.isEmpty()) chain.append(s);
        }
        m_eventMap[eventName] = chain;
    }
    return true;
}

bool CharacterPack::parseEffectTriggers(const QJsonObject &triggers)
{
    for (auto it = triggers.begin(); it != triggers.end(); ++it) {
        const QString animationName = it.key();
        const QString effectName = it.value().toString();

        if (effectName.isEmpty()) {
            qWarning() << "CharacterPack: Effect trigger has empty effect for animation:" << animationName;
            return false;
        }

        m_effectTriggers[animationName] = effectName;
    }

    return true;
}

bool CharacterPack::parseStateMap(const QJsonObject &map)
{
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QString stateName = it.key();
        const QJsonValue value = it.value();
        QStringList chain;
        if (value.isArray()) {
            for (const QJsonValue &v : value.toArray()) {
                const QString s = v.toString();
                if (!s.isEmpty()) chain.append(s);
            }
        } else {
            const QString s = value.toString();
            if (!s.isEmpty()) chain.append(s);
        }
        m_stateMap[stateName] = chain;
    }
    return true;
}

bool CharacterPack::parsePersona(const QJsonObject &persona)
{
    m_persona.system = persona.value(QStringLiteral("system")).toString();
    m_persona.language = persona.value(QStringLiteral("language")).toString();
    m_persona.styleExamples.clear();
    const QJsonArray ex = persona.value(QStringLiteral("style_examples")).toArray();
    for (const QJsonValue &v : ex) {
        if (v.isString()) m_persona.styleExamples << v.toString();
    }
    m_personaHashCache.clear();
    return true;
}

QString CharacterPack::personaHash() const
{
    if (!m_personaHashCache.isEmpty()) return m_personaHashCache;

    QJsonObject canonical;
    canonical.insert(QStringLiteral("system"), m_persona.system);
    canonical.insert(QStringLiteral("language"), m_persona.language);
    QJsonArray exArr;
    for (const QString &e : m_persona.styleExamples) exArr.append(e);
    canonical.insert(QStringLiteral("style_examples"), exArr);

    const QByteArray bytes = QJsonDocument(canonical).toJson(QJsonDocument::Compact);
    m_personaHashCache = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return m_personaHashCache;
}

bool CharacterPack::loadAnimationsFromDefinitions(const QString &definitionsPath)
{
    QFile file(definitionsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CharacterPack: Failed to open definitions file:" << definitionsPath;
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "CharacterPack: definitions parse error:" << error.errorString();
        return false;
    }

    if (!doc.isArray()) {
        qWarning() << "CharacterPack: definitions is not an array";
        return false;
    }

    // Parse original animations.json format
    // Each entry: {"Name": "...", "Frames": [{"Duration": ..., "ImagesOffsets": {"Column": ..., "Row": ...}}]}
    const QJsonArray anims = doc.array();
    for (const QJsonValue &val : anims) {
        QJsonObject animObj = val.toObject();
        const QString name = animObj["Name"].toString();

        AnimationDef def;
        def.name = name;
        def.type = EngineType::SpriteSheet;
        def.loop = false;  // Original format doesn't specify loop

        QJsonArray frames = animObj["Frames"].toArray();
        for (const QJsonValue &fval : frames) {
            QJsonObject frameObj = fval.toObject();
            QJsonObject offsets = frameObj["ImagesOffsets"].toObject();

            // Some frames have null/empty offsets (e.g., blank transition frames)
            if (offsets.isEmpty() || !offsets.contains("Column")) {
                offsets["Column"] = 0;
                offsets["Row"] = 0;
            }

            int col = offsets["Column"].toInt();
            int row = offsets["Row"].toInt();
            int duration = frameObj["Duration"].toInt();

            FrameDef frame;
            frame.col = col;
            frame.row = row;
            frame.durationMs = duration;
            def.frames.append(frame);
            def.totalDurationMs += duration;
        }

        m_animations.insert(name, def);
    }

    qDebug() << "CharacterPack: Loaded" << m_animations.size() << "animations from definitions:" << definitionsPath;
    return true;
}
