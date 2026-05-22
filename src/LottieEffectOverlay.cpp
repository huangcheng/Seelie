#include "LottieEffectOverlay.h"
#include "CharacterPack.h"

#include <QPainter>
#include <QImage>
#include <QDir>
#include <QFileInfoList>
#include <QDebug>

LottieEffectOverlay::LottieEffectOverlay(QObject *parent)
    : QObject(parent)
    , m_timer(this)
{
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &LottieEffectOverlay::tick);
}

LottieEffectOverlay::~LottieEffectOverlay() = default;

void LottieEffectOverlay::loadEffects(const QString &effectsDir)
{
    QDir dir(effectsDir);
    if (!dir.exists()) {
        qWarning() << "Effects directory not found:" << effectsDir;
        return;
    }

    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        const QString name = fi.baseName();
        auto anim = rlottie::Animation::loadFromFile(fi.absoluteFilePath().toStdString());
        if (!anim) {
            qWarning() << "Failed to load effect:" << fi.fileName();
            continue;
        }

        m_effectTemplates.insert(name, std::move(anim));
        qDebug() << "Loaded effect:" << name;
    }

    setupDefaults();

    if (!m_effectTemplates.isEmpty()) {
        m_timer.start();
    }
}

bool LottieEffectOverlay::loadFromCharacterPack(const CharacterPack *pack)
{
    if (!pack || !pack->isValid()) {
        qWarning() << "LottieEffectOverlay: Invalid sprite pack";
        return false;
    }

    // Clear existing effects
    m_effectTemplates.clear();
    m_configs.clear();
    m_activeEffects.clear();

    // Get effects directory from pack
    const QString effectsDir = pack->assetPath("effects");
    if (effectsDir.isEmpty()) {
        qDebug() << "LottieEffectOverlay: No effects directory in pack";
        return true;  // Not an error, just no effects
    }

    QDir dir(effectsDir);
    if (!dir.exists()) {
        qDebug() << "LottieEffectOverlay: Effects directory does not exist:" << effectsDir;
        return true;  // Not an error, just no effects
    }

    // Load effect files
    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        const QString name = fi.baseName();
        auto anim = rlottie::Animation::loadFromFile(fi.absoluteFilePath().toStdString());
        if (!anim) {
            qWarning() << "LottieEffectOverlay: Failed to load effect:" << fi.fileName();
            continue;
        }

        m_effectTemplates.insert(name, std::move(anim));
        qDebug() << "LottieEffectOverlay: Loaded effect:" << name;
    }

    // Setup default configs (can be extended to load from pack)
    setupDefaults();

    if (!m_effectTemplates.isEmpty()) {
        m_timer.start();
    }

    qDebug() << "LottieEffectOverlay: Loaded" << m_effectTemplates.size() << "effects from sprite pack";
    return true;
}

void LottieEffectOverlay::triggerEffect(const QString &effectName)
{
    if (!m_effectTemplates.contains(effectName)) {
        qWarning() << "Effect not found:" << effectName;
        return;
    }

    ActiveEffect effect;
    effect.animation = m_effectTemplates.value(effectName);
    effect.name = effectName;
    effect.currentFrame = 0;
    effect.totalFrames = static_cast<int>(effect.animation->totalFrame());
    effect.frameRate = effect.animation->frameRate();
    effect.elapsedMs = 0.0;

    EffectConfig config = m_configs.value(effectName);
    effect.loop = config.loop;
    effect.offset = config.offset;

    effect.width = 200;
    effect.height = 200;
    // Align buffer to 32 bytes for SIMD compatibility
    effect.buffer.resize(effect.width * effect.height + 8);
    std::fill(effect.buffer.begin(), effect.buffer.end(), 0);

    // Stop any existing instance of this effect
    for (int i = m_activeEffects.size() - 1; i >= 0; --i) {
        if (m_activeEffects[i].name == effectName) {
            m_activeEffects.removeAt(i);
        }
    }

    m_activeEffects.append(effect);
}

void LottieEffectOverlay::stopEffect(const QString &effectName)
{
    for (int i = m_activeEffects.size() - 1; i >= 0; --i) {
        if (m_activeEffects[i].name == effectName) {
            m_activeEffects.removeAt(i);
        }
    }
}

void LottieEffectOverlay::paint(QPainter *painter, const QRect &petBounds)
{
    for (const ActiveEffect &effect : m_activeEffects) {
        if (effect.animation) {
            rlottie::Surface surface(
                const_cast<uint32_t*>(effect.buffer.data()),
                effect.width, effect.height,
                effect.width * sizeof(uint32_t));
            effect.animation->renderSync(effect.currentFrame, surface);

            QImage image(reinterpret_cast<const uchar*>(effect.buffer.data()),
                        effect.width, effect.height,
                        effect.width * sizeof(uint32_t),
                        QImage::Format_ARGB32_Premultiplied);

            // Position effect relative to pet
            QRect effectRect = petBounds;
            effectRect.translate(effect.offset.toPoint());
            painter->drawImage(effectRect, image);
        }
    }
}

void LottieEffectOverlay::tick()
{
    for (int i = m_activeEffects.size() - 1; i >= 0; --i) {
        ActiveEffect &effect = m_activeEffects[i];
        effect.elapsedMs += 16.0;

        // M8: Guard against zero/negative frameRate to avoid division by zero.
        if (effect.frameRate <= 0.0) {
            m_activeEffects.removeAt(i);
            continue;
        }
        const double frameDuration = 1000.0 / effect.frameRate;
        if (effect.elapsedMs >= frameDuration) {
            effect.elapsedMs -= frameDuration;
            effect.currentFrame++;

            if (effect.currentFrame >= effect.totalFrames) {
                if (effect.loop) {
                    effect.currentFrame = 0;
                } else {
                    // One-shot effect completed — remove it
                    m_activeEffects.removeAt(i);
                }
            }
        }
    }

    // Stop timer if no active effects
    if (m_activeEffects.isEmpty()) {
        m_timer.stop();
    }
}

void LottieEffectOverlay::setupDefaults()
{
    auto cfg = [](const QPointF &off, bool loop) {
        EffectConfig c;
        c.offset = off;
        c.loop = loop;
        return c;
    };

    m_configs["sparkles"]      = cfg(QPointF(0, 0),    false);
    m_configs["confetti"]      = cfg(QPointF(0, -50),  false);
    m_configs["alert-pulse"]   = cfg(QPointF(0, 0),    false);
    m_configs["thinking-dots"] = cfg(QPointF(30, -40), true);
    m_configs["wave-lines"]    = cfg(QPointF(0, 0),    false);
    m_configs["speech-pop"]    = cfg(QPointF(20, 20),  false);
}
