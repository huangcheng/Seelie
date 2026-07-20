#ifndef LOTTIEANIMATIONENGINE_H
#define LOTTIEANIMATIONENGINE_H

#include "AnimationEngine.h"

#include <QObject>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <memory>
#include <rlottie.h>

class QPainter;
class QRect;
class QImage;
class CharacterPack;

class LottieAnimationEngine : public QObject, public AnimationEngine
{
    Q_OBJECT

public:
    using AnimationEngine::Priority;
    using AnimationEngine::HighPriority;
    using AnimationEngine::NormalPriority;

    explicit LottieAnimationEngine(QObject *parent = nullptr);
    ~LottieAnimationEngine() override;

    // Load all Lottie JSON files from assets
    void loadAnimations(const QString &characterDir);

    /**
     * @brief Load animations from a sprite pack
     * @param pack Sprite pack to load from
     * @return true if loaded successfully
     */
    bool loadFromCharacterPack(const CharacterPack *pack) override;

    // Play named animation with given priority
    void playAnimation(const QString &name, Priority priority = NormalPriority) override;

    // Stop playback and clear state (used when switching to a different engine).
    void stop() override;

    // Render current frame onto painter
    void paint(QPainter *painter, const QRect &bounds) override;

    // Check if animation is playing
    bool isPlaying() const override { return m_playing; }

    // Check if engine has any animations loaded
    bool hasAnimations() const override { return !m_animations.isEmpty(); }

    /** @brief Always true for Lottie — software renderer, no GPU context to lose. */
    bool lastPaintSuccessful() const override { return true; }

signals:
    void effectRequested(const QString &effectName);
    void frameChanged(); // emitted every tick so parent widget can repaint

private slots:
    void tick();

private:
    void advanceFrame();
    void startNextAnimation();
    void startIdleAnimation();

    struct AnimationState {
        std::shared_ptr<rlottie::Animation> animation;
        QString name;
        int totalFrames = 0;
        double frameRate = 30.0;
        bool loop = false;
        QString effect;
        QString sound;
    };

    // Loaded animations (name → animation)
    QMap<QString, AnimationState> m_animations;

    // Current playback
    AnimationState m_current;
    int m_currentFrame = 0;
    double m_speedMultiplier = 1.0;
    double m_elapsedMs = 0.0;
    bool m_playing = false;
    bool m_looping = false;

    // Render buffer (shared, reusable)
    std::vector<uint32_t> m_buffer;
    int m_bufferWidth = 200;
    int m_bufferHeight = 200;

    // Timer for frame updates (16ms ≈ 60fps)
    QTimer m_timer;

    // Idle pool
    QStringList m_idleAnims;
    QVector<int> m_idleWeights;
    QTimer m_idleTimer;
    int m_idleTimeoutMs = 3000;
    QString m_lastIdleAnim;   // anti-repeat for idle picks

    // Queue for normal priority animations
    QStringList m_queue;
};

#endif // LOTTIEANIMATIONENGINE_H
