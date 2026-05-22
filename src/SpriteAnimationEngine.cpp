#include "SpriteAnimationEngine.h"
#include "CharacterPack.h"

#include <QPainter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QRandomGenerator>
#include <QDebug>
#include <QColor>
#include <limits>

SpriteAnimationEngine::SpriteAnimationEngine(QObject *parent)
    : QObject(parent)
    , m_timer(this)
    , m_idleTimer(this)
{
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &SpriteAnimationEngine::tick);

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(m_idleTimeoutMs);
    connect(&m_idleTimer, &QTimer::timeout, this, &SpriteAnimationEngine::startIdleAnimation);

    buildNameMap();
}

SpriteAnimationEngine::~SpriteAnimationEngine() = default;

void SpriteAnimationEngine::buildNameMap()
{
    // Canonical animation names (skin-agnostic)
    // These map to skin-specific animation names in animations.json
    m_nameMap["greet"]           = "Wave";
    m_nameMap["idle"]            = "Idle1_1";
    m_nameMap["think"]           = "Thinking";
    m_nameMap["work"]            = "Explain";
    m_nameMap["alert"]           = "Alert";
    m_nameMap["celebrate"]       = "Congratulate";
    m_nameMap["rest"]            = "RestPose";
    m_nameMap["send"]            = "SendMail";
    m_nameMap["attention"]       = "GetAttention";

    // Skin-specific aliases (legacy names kept for compatibility)
    m_nameMap["wave"]            = "Wave";
    m_nameMap["explain"]         = "Explain";
    m_nameMap["thinking"]        = "Thinking";
    m_nameMap["congratulate"]    = "Congratulate";
    m_nameMap["sendmail"]        = "SendMail";
    m_nameMap["getattentionyawn"]= "GetAttention";
    m_nameMap["greeting"]        = "Greeting";
    m_nameMap["goodbye"]         = "GoodBye";
    m_nameMap["processing"]      = "Processing";
    m_nameMap["writing"]         = "Writing";
    m_nameMap["searching"]       = "Searching";
    m_nameMap["print"]           = "Print";
    m_nameMap["save"]            = "Save";
    m_nameMap["hide"]            = "Hide";
    m_nameMap["show"]            = "Show";
    m_nameMap["gettechy"]        = "GetTechy";
    m_nameMap["getwizardy"]      = "GetWizardy";

    // Idle animations (for random selection)
    m_nameMap["idle1"]           = "Idle1_1";
    m_nameMap["idle2"]           = "IdleAtom";
    m_nameMap["idle3"]           = "IdleSideToSide";
    m_nameMap["click1"]          = "GestureUp";
    m_nameMap["click2"]          = "GestureRight";
    m_nameMap["doubleclick1"]    = "GetAttention";
    m_nameMap["doubleclick2"]    = "GetArtsy";
    m_nameMap["gesture_down"]    = "GestureDown";
    m_nameMap["gesture_up"]      = "GestureUp";
    m_nameMap["gesture_left"]    = "GestureLeft";
    m_nameMap["gesture_right"]   = "GestureRight";
    m_nameMap["lookdown"]        = "LookDown";
    m_nameMap["lookleft"]        = "LookLeft";
    m_nameMap["lookright"]       = "LookRight";
    m_nameMap["lookup"]          = "LookUp";
}

bool SpriteAnimationEngine::loadAssets(const QString &spriteSheetPath, const QString &animJsonPath)
{
    // Load sprite sheet
    if (!m_spriteSheet.load(spriteSheetPath)) {
        qWarning() << "Failed to load sprite sheet:" << spriteSheetPath;
        return false;
    }
    qDebug() << "Sprite sheet loaded:" << spriteSheetPath
             << "size:" << m_spriteSheet.size();

    // Determine frame dimensions from sprite sheet and animation data
    // Original Clippy: 27 cols x 34 rows, 124x93 per frame
    m_frameWidth = 124;
    m_frameHeight = 93;

    // Load animation definitions
    QFile file(animJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open animations.json:" << animJsonPath;
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "animations.json parse error:" << error.errorString();
        return false;
    }

    QJsonArray anims = doc.array();
    for (const QJsonValue &val : anims) {
        QJsonObject animObj = val.toObject();
        const QString name = animObj["Name"].toString();

        AnimationDef def;
        def.name = name;

        QJsonArray frames = animObj["Frames"].toArray();
        for (const QJsonValue &fval : frames) {
            QJsonObject frameObj = fval.toObject();
            QJsonObject offsets = frameObj["ImagesOffsets"].toObject();

            // Some frames have null/empty offsets (e.g., blank transition frames)
            if (offsets.isEmpty() || !offsets.contains("Column")) {
                // Use first frame as fallback
                offsets["Column"] = 0;
                offsets["Row"] = 0;
            }

            int col = offsets["Column"].toInt();
            int row = offsets["Row"].toInt();
            int duration = frameObj["Duration"].toInt();

            FrameDef frame;
            frame.sourceRect = QRect(col * m_frameWidth, row * m_frameHeight,
                                     m_frameWidth, m_frameHeight);
            frame.durationMs = duration;
            def.frames.append(frame);
            def.totalDurationMs += duration;
        }

        m_animations.insert(name, def);
    }

    qDebug() << "Loaded" << m_animations.size() << "animations";

    // Build idle pool from idle-type animations
    // {name, weight} — higher weight = more frequent
    const QVector<QPair<QString, int>> idlePool = {
        // Classic idle animations
        {"Idle1_1",           4},
        {"IdleAtom",          3},
        {"IdleSideToSide",    3},
        {"IdleRopePile",      2},
        {"IdleHeadScratch",   3},
        {"IdleFingerTap",     3},
        {"IdleEyeBrowRaise",  3},
        {"IdleSnooze",        1},
        // Look-around animations
        {"LookLeft",          2},
        {"LookRight",         2},
        {"LookUp",            2},
        {"LookDown",          2},
        {"LookUpLeft",        1},
        {"LookUpRight",       1},
        {"LookDownLeft",      1},
        {"LookDownRight",     1},
        // Personality animations
        {"GetArtsy",          1},
        {"GetTechy",          1},
        {"GetWizardy",        1},
        {"Hearing_1",         1},
        {"CheckingSomething", 1},
        {"Writing",           1},
        {"Searching",         1},
    };
    for (const auto &[name, weight] : idlePool) {
        if (m_animations.contains(name)) {
            m_idleAnims.append(name);
            m_idleWeights.append(weight);
        }
    }

    // Start with RestPose (non-looping — idle timer will cycle animations)
    if (m_animations.contains("RestPose")) {
        playAnimation("RestPose", HighPriority);
        // After RestPose finishes, idle timer kicks in
    }

    m_timer.start();
    // Start idle timer — first idle animation plays after 3 seconds
    m_idleTimer.start();
    return true;
}

bool SpriteAnimationEngine::loadFromCharacterPack(const CharacterPack *pack)
{
    if (!pack || !pack->isValid()) {
        qWarning() << "SpriteAnimationEngine: Invalid sprite pack";
        return false;
    }

    // Check if pack uses sprite sheet engine
    if (pack->characterConfig().engineType != CharacterPack::EngineType::SpriteSheet) {
        qWarning() << "SpriteAnimationEngine: Pack does not use sprite sheet engine";
        return false;
    }

    // C8/H10: Stop old playback before clearing animations and replacing the
    // sprite sheet. Without this, m_timer still fires and paint() accesses
    // stale FrameDef rectangles against the new (different) sprite sheet.
    stop();

    m_animations.clear();
    m_idleAnims.clear();
    m_idleWeights.clear();
    m_packNameMap = pack->nameMap();

    // Load sprite sheet
    const QString spriteSheetPath = pack->assetPath(pack->characterConfig().spriteSheet);
    qDebug() << "SpriteAnimationEngine: Loading sprite sheet from:" << spriteSheetPath;
    qDebug() << "SpriteAnimationEngine: File exists:" << QFile::exists(spriteSheetPath);
    if (!m_spriteSheet.load(spriteSheetPath)) {
        qWarning() << "SpriteAnimationEngine: Failed to load sprite sheet:" << spriteSheetPath;
        return false;
    }
    qDebug() << "SpriteAnimationEngine: Sprite sheet loaded successfully, size:" << m_spriteSheet.size();

    if (pack->characterConfig().colorKey.isValid()) {
        QImage image = m_spriteSheet.toImage().convertToFormat(QImage::Format_ARGB32);
        if (image.isNull()) {
            qWarning() << "SpriteAnimationEngine: color-key conversion produced null image";
            return false;
        }
        QColor keyColor = pack->characterConfig().colorKey;
        int keyR = keyColor.red();
        int keyG = keyColor.green();
        int keyB = keyColor.blue();
        int tolerance = 45;
        int toleranceSq = tolerance * tolerance;

        for (int y = 0; y < image.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                QRgb pixel = row[x];
                int diffR = qRed(pixel) - keyR;
                int diffG = qGreen(pixel) - keyG;
                int diffB = qBlue(pixel) - keyB;
                int distanceSq = diffR * diffR + diffG * diffG + diffB * diffB;

                if (distanceSq < toleranceSq) {
                    row[x] = qRgba(0, 0, 0, 0);
                }
            }
        }
        m_spriteSheet = QPixmap::fromImage(image);
        qDebug() << "SpriteAnimationEngine: Applied color key" << pack->characterConfig().colorKey.name()
                 << "with tolerance" << tolerance;
    }

    // Set frame dimensions from pack
    m_frameWidth = pack->characterConfig().frameWidth;
    m_frameHeight = pack->characterConfig().frameHeight;

    qDebug() << "SpriteAnimationEngine: Loaded sprite sheet:" << spriteSheetPath
             << "frame size:" << m_frameWidth << "x" << m_frameHeight;

    // Load animations from pack
    const auto &animations = pack->animations();
    for (auto it = animations.begin(); it != animations.end(); ++it) {
        const QString name = it.key();
        const CharacterPack::AnimationDef &animDef = it.value();

        // Only load sprite sheet animations
        if (animDef.type != CharacterPack::EngineType::SpriteSheet) {
            continue;
        }

        AnimationDef def;
        def.name = name;
        def.loop = animDef.loop;
        def.effect = animDef.effect;
        def.sound = animDef.sound;

        // Convert pack frames to engine frames
        const QRect sheetRect(0, 0, m_spriteSheet.width(), m_spriteSheet.height());
        for (const auto &packFrame : animDef.frames) {
            FrameDef frame;
            frame.durationMs = packFrame.durationMs;

            if (packFrame.isGridMode()) {
                // Grid mode: use col/row with frame dimensions. Widen to
                // qint64 before multiplication so a manifest with col=2^16
                // doesn't overflow signed int and produce a negative QRect
                // origin. Audit H9.
                const qint64 x64 = qint64(packFrame.col) * m_frameWidth;
                const qint64 y64 = qint64(packFrame.row) * m_frameHeight;
                if (x64 < 0 || y64 < 0 ||
                    x64 > std::numeric_limits<int>::max() ||
                    y64 > std::numeric_limits<int>::max()) {
                    qWarning() << "SpriteAnimationEngine: rejecting frame with"
                                  " out-of-range grid coords col=" << packFrame.col
                               << "row=" << packFrame.row;
                    continue;
                }
                frame.sourceRect = QRect(int(x64), int(y64),
                                         m_frameWidth, m_frameHeight);
            } else {
                // Atlas mode: use explicit x/y/w/h
                frame.sourceRect = QRect(packFrame.x, packFrame.y,
                                         packFrame.w, packFrame.h);
            }

            // Reject frames that lie wholly outside the sheet, or that have
            // a non-positive width/height. Clip overlapping rects to the
            // sheet so a manifest off-by-one doesn't crash the painter.
            // Audit M14.
            if (frame.sourceRect.width() <= 0 || frame.sourceRect.height() <= 0) {
                qWarning() << "SpriteAnimationEngine: rejecting empty frame rect"
                           << frame.sourceRect;
                continue;
            }
            const QRect clipped = frame.sourceRect.intersected(sheetRect);
            if (clipped.isEmpty()) {
                qWarning() << "SpriteAnimationEngine: rejecting frame outside sheet"
                           << frame.sourceRect << "sheet=" << sheetRect;
                continue;
            }
            frame.sourceRect = clipped;

            def.frames.append(frame);
            def.totalDurationMs += frame.durationMs;
        }

        m_animations.insert(name, def);
        qDebug() << "SpriteAnimationEngine: Loaded animation:" << name
                 << "frames:" << def.frames.size();
    }

    // Build idle pool from pack
    const auto &idlePool = pack->idlePool();
    for (const auto &entry : idlePool) {
        if (m_animations.contains(entry.animationName)) {
            m_idleAnims.append(entry.animationName);
            m_idleWeights.append(entry.weight);
        }
    }

    // Start with rest pose if available
    if (m_animations.contains("RestPose")) {
        playAnimation("RestPose", HighPriority);
    } else if (!m_animations.isEmpty()) {
        playAnimation(m_animations.firstKey(), HighPriority);
    }

    m_timer.start();
    m_idleTimer.start();

    qDebug() << "SpriteAnimationEngine: Loaded" << m_animations.size() << "animations from sprite pack";
    return true;
}

void SpriteAnimationEngine::playAnimation(const QString &name, Priority priority)
{
    QString actualName;

    if (m_packNameMap.contains(name)) {
        actualName = m_packNameMap.value(name);
    } else {
        actualName = m_nameMap.value(name, name);
        if (!m_animations.contains(actualName)) {
            actualName = name;
        }
    }

    if (!m_animations.contains(actualName)) {
        qWarning() << "Animation not found:" << name << "(resolved to" << actualName << ")";
        return;
    }

    // If the same animation is already playing, let it finish — don't restart.
    if (m_playing && m_current.name == actualName) {
        return;
    }

    m_idleTimer.stop();

    // Idle and RestPose animations should always be interruptible by events
    bool currentIsIdle = m_idleAnims.contains(m_current.name)
                         || m_current.name == "RestPose";

    if (priority == HighPriority || currentIsIdle) {
        m_current = m_animations.value(actualName);
        m_currentFrameIndex = 0;
        m_currentFrameElapsed = 0;
        m_playing = true;
        m_looping = false;  // No animation loops — idle timer handles cycling
        if (priority == HighPriority) {
            m_queue.clear();
        }
        checkEffectTrigger(actualName);
    } else {
        if (!m_playing) {
            m_current = m_animations.value(actualName);
            m_currentFrameIndex = 0;
            m_currentFrameElapsed = 0;
            m_playing = true;
            m_looping = false;
        } else {
            m_queue.append(actualName);
        }
    }
}

void SpriteAnimationEngine::stop()
{
    m_timer.stop();
    m_idleTimer.stop();
    m_playing = false;
    m_queue.clear();
    m_current = AnimationDef{};
    m_currentFrameIndex = 0;
    m_currentFrameElapsed = 0;
}

void SpriteAnimationEngine::paint(QPainter *painter, const QRect &bounds)
{
    if (m_playing && !m_current.frames.isEmpty()) {
        // Draw current animation frame
        const FrameDef &frame = m_current.frames.at(m_currentFrameIndex);
        painter->drawPixmap(bounds, m_spriteSheet, frame.sourceRect);
    } else if (!m_current.frames.isEmpty()) {
        // Show last frame of whatever animation just finished
        const FrameDef &frame = m_current.frames.first();
        painter->drawPixmap(bounds, m_spriteSheet, frame.sourceRect);
    }
}

void SpriteAnimationEngine::tick()
{
    if (!m_playing || m_current.frames.isEmpty()) {
        return;
    }

    advanceFrame();
    emit frameChanged();
}

void SpriteAnimationEngine::advanceFrame()
{
    m_currentFrameElapsed += 16; // 16ms per tick

    const FrameDef &frame = m_current.frames.at(m_currentFrameIndex);
    if (m_currentFrameElapsed >= frame.durationMs) {
        m_currentFrameElapsed -= frame.durationMs;
        m_currentFrameIndex++;

        if (m_currentFrameIndex >= m_current.frames.size()) {
            if (m_looping) {
                m_currentFrameIndex = 0;
            } else {
                startNextAnimation();
            }
        }
    }
}

void SpriteAnimationEngine::startNextAnimation()
{
    if (!m_queue.isEmpty()) {
        const QString nextName = m_queue.takeFirst();
        m_current = m_animations.value(nextName);
        m_currentFrameIndex = 0;
        m_currentFrameElapsed = 0;
        m_playing = true;
        m_looping = false;
        checkEffectTrigger(nextName);
    } else {
        m_playing = false;
        m_idleTimer.start();
    }
}

void SpriteAnimationEngine::checkEffectTrigger(const QString &animName)
{
    const QStringList effectTriggers = {"Congratulate", "Alert", "SendMail"};
    if (effectTriggers.contains(animName)) {
        QString effectName;
        if (animName == "Congratulate") effectName = "confetti";
        else if (animName == "Alert") effectName = "alert-pulse";
        else if (animName == "SendMail") effectName = "sparkles";

        if (!effectName.isEmpty()) {
            emit effectRequested(effectName);
        }
    }
}

void SpriteAnimationEngine::startIdleAnimation()
{
    if (m_idleAnims.isEmpty()) {
        // Try RestPose, then resolve "rest" via nameMap, then first animation
        if (m_animations.contains("RestPose")) {
            playAnimation("RestPose", HighPriority);
        } else if (!m_animations.isEmpty()) {
            playAnimation(m_animations.firstKey(), HighPriority);
        }
        return;
    }

    int totalWeight = 0;
    for (int w : m_idleWeights) totalWeight += w;

    int roll = QRandomGenerator::global()->bounded(totalWeight);
    int cumulative = 0;
    for (int i = 0; i < m_idleAnims.size(); ++i) {
        cumulative += m_idleWeights.at(i);
        if (roll < cumulative) {
            playAnimation(m_idleAnims.at(i), HighPriority);
            return;
        }
    }

    playAnimation(m_idleAnims.first(), HighPriority);
}
