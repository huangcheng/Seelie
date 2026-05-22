#include "LottieAnimationEngine.h"
#include "CharacterPack.h"

#include <QPainter>
#include <QImage>
#include <QDir>
#include <QFileInfoList>
#include <QRandomGenerator>
#include <QDebug>

LottieAnimationEngine::LottieAnimationEngine(QObject *parent)
    : QObject(parent)
    , m_timer(this)
    , m_idleTimer(this)
{
    m_buffer.resize(m_bufferWidth * m_bufferHeight);
    std::fill(m_buffer.begin(), m_buffer.end(), 0);

    m_timer.setInterval(16); // ~60fps
    connect(&m_timer, &QTimer::timeout, this, &LottieAnimationEngine::tick);

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(m_idleTimeoutMs);
    connect(&m_idleTimer, &QTimer::timeout, this, &LottieAnimationEngine::startIdleAnimation);
}

LottieAnimationEngine::~LottieAnimationEngine() = default;

void LottieAnimationEngine::loadAnimations(const QString &characterDir)
{
    QDir dir(characterDir);
    if (!dir.exists()) {
        qWarning() << "Character animation directory not found:" << characterDir;
        return;
    }

    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        const QString name = fi.baseName();
        auto anim = rlottie::Animation::loadFromFile(fi.absoluteFilePath().toStdString());
        if (!anim) {
            qWarning() << "Failed to load Lottie animation:" << fi.fileName();
            continue;
        }

        AnimationState state;
        state.animation = std::move(anim);
        state.name = name;
        state.totalFrames = static_cast<int>(state.animation->totalFrame());
        state.frameRate = state.animation->frameRate();

        m_animations.insert(name, state);
        qDebug() << "Loaded animation:" << name << "frames:" << state.totalFrames;

        // Add to idle pool if name starts with "idle"
        if (name.startsWith("idle")) {
            m_idleAnims.append(name);
            m_idleWeights.append(3); // equal weight
        }
    }

    // Start with rest pose
    if (m_animations.contains("rest")) {
        playAnimation("rest", HighPriority);
    }

    m_timer.start();
}

bool LottieAnimationEngine::loadFromCharacterPack(const CharacterPack *pack)
{
    if (!pack || !pack->isValid()) {
        qWarning() << "LottieAnimationEngine: Invalid sprite pack";
        return false;
    }

    // Check if pack uses Lottie engine
    if (pack->characterConfig().engineType != CharacterPack::EngineType::Lottie) {
        qWarning() << "LottieAnimationEngine: Pack does not use Lottie engine";
        return false;
    }

    // H9: Stop old playback before clearing animations. Without this, m_timer
    // still fires and m_current references a destroyed animation.
    stop();

    // Clear existing animations
    m_animations.clear();
    m_idleAnims.clear();
    m_idleWeights.clear();

    // Load animations from pack
    const auto &animations = pack->animations();
    for (auto it = animations.begin(); it != animations.end(); ++it) {
        const QString name = it.key();
        const CharacterPack::AnimationDef &animDef = it.value();

        // Only load Lottie animations
        if (animDef.type != CharacterPack::EngineType::Lottie) {
            continue;
        }

        // Get absolute path to Lottie file
        const QString lottiePath = pack->lottieAnimationPath(name);
        if (lottiePath.isEmpty()) {
            qWarning() << "LottieAnimationEngine: Lottie file not found for animation:" << name;
            continue;
        }

        // Load Lottie animation
        auto anim = rlottie::Animation::loadFromFile(lottiePath.toStdString());
        if (!anim) {
            qWarning() << "LottieAnimationEngine: Failed to load Lottie animation:" << lottiePath;
            continue;
        }

        // Create animation state
        AnimationState state;
        state.animation = std::move(anim);
        state.name = name;
        state.totalFrames = static_cast<int>(state.animation->totalFrame());
        state.frameRate = state.animation->frameRate();
        state.loop = animDef.loop;
        state.effect = animDef.effect;
        state.sound = animDef.sound;

        m_animations.insert(name, state);
        qDebug() << "LottieAnimationEngine: Loaded animation:" << name << "frames:" << state.totalFrames;

        // Add to idle pool if in pack's idle pool
        const auto &idlePool = pack->idlePool();
        for (const auto &entry : idlePool) {
            if (entry.animationName == name) {
                m_idleAnims.append(name);
                m_idleWeights.append(entry.weight);
                break;
            }
        }
    }

    // Start with rest pose if available
    if (m_animations.contains("rest")) {
        playAnimation("rest", HighPriority);
    } else if (!m_animations.isEmpty()) {
        // Fall back to first animation
        playAnimation(m_animations.firstKey(), HighPriority);
    }

    m_timer.start();

    qDebug() << "LottieAnimationEngine: Loaded" << m_animations.size() << "animations from sprite pack";
    return true;
}

void LottieAnimationEngine::playAnimation(const QString &name, Priority priority)
{
    if (!m_animations.contains(name)) {
        qWarning() << "Animation not found:" << name;
        return;
    }

    // Reset idle timer on any animation request
    m_idleTimer.stop();

    if (priority == HighPriority) {
        // Interrupt current animation immediately
        m_current = m_animations.value(name);
        m_currentFrame = 0;
        m_elapsedMs = 0.0;
        m_playing = true;
        m_looping = (name == "rest");
        m_queue.clear(); // Clear queued animations
    } else {
        // Queue after current
        if (!m_playing) {
            m_current = m_animations.value(name);
            m_currentFrame = 0;
            m_elapsedMs = 0.0;
            m_playing = true;
            m_looping = false;
        } else {
            m_queue.append(name);
        }
    }
}

void LottieAnimationEngine::stop()
{
    m_timer.stop();
    m_idleTimer.stop();
    m_playing = false;
    m_queue.clear();
    m_current = AnimationState{};
    m_currentFrame = 0;
    m_elapsedMs = 0.0;
}

void LottieAnimationEngine::paint(QPainter *painter, const QRect &bounds)
{
    if (!m_playing || !m_current.animation) {
        return;
    }

    rlottie::Surface surface(m_buffer.data(), m_bufferWidth, m_bufferHeight,
                              m_bufferWidth * sizeof(uint32_t));
    m_current.animation->renderSync(m_currentFrame, surface);

    QImage image(reinterpret_cast<const uchar*>(m_buffer.data()),
                 m_bufferWidth, m_bufferHeight,
                 m_bufferWidth * sizeof(uint32_t),
                 QImage::Format_ARGB32_Premultiplied);

    painter->drawImage(bounds, image);
}

void LottieAnimationEngine::tick()
{
    if (!m_playing || !m_current.animation) {
        return;
    }

    advanceFrame();
    emit frameChanged();
}

void LottieAnimationEngine::advanceFrame()
{
    m_elapsedMs += 16.0; // 16ms per tick

    // Malformed Lottie files can report frameRate=0; m_speedMultiplier
    // could also be 0 if a future caller forgets to validate. Either way,
    // dividing yields inf/NaN and corrupts the elapsed-time accounting.
    const double rate = m_current.frameRate * m_speedMultiplier;
    if (rate <= 0.0) {
        return;
    }
    const double frameDuration = 1000.0 / rate;
    if (m_elapsedMs < frameDuration) {
        return; // Not enough time elapsed for next frame
    }

    m_elapsedMs -= frameDuration;
    m_currentFrame++;

    if (m_currentFrame >= m_current.totalFrames) {
        if (m_looping) {
            m_currentFrame = 0;
        } else {
            startNextAnimation();
        }
    }
}

void LottieAnimationEngine::startNextAnimation()
{
    if (!m_queue.isEmpty()) {
        const QString nextName = m_queue.takeFirst();
        m_current = m_animations.value(nextName);
        m_currentFrame = 0;
        m_elapsedMs = 0.0;
        m_playing = true;
        m_looping = m_current.loop;

        // Trigger effect if defined
        if (!m_current.effect.isEmpty()) {
            emit effectRequested(m_current.effect);
        }
    } else {
        // No more animations — go to rest pose
        m_playing = false;
        m_idleTimer.start();
    }
}

void LottieAnimationEngine::startIdleAnimation()
{
    if (m_idleAnims.isEmpty()) {
        playAnimation("rest", HighPriority);
        return;
    }

    // Weighted random selection
    int totalWeight = 0;
    for (int w : m_idleWeights) {
        totalWeight += w;
    }

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
