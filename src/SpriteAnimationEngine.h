#ifndef SPRITEANIMATIONENGINE_H
#define SPRITEANIMATIONENGINE_H

#include "AnimationEngine.h"

#include <QObject>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QPixmap>
#include <QRect>

class QPainter;
class CharacterPack;

class SpriteAnimationEngine : public QObject, public AnimationEngine
{
    Q_OBJECT

public:
    // Re-export the shared Priority enum so existing callers using
    // SpriteAnimationEngine::NormalPriority continue to resolve. H3.
    using AnimationEngine::Priority;
    using AnimationEngine::HighPriority;
    using AnimationEngine::NormalPriority;

    explicit SpriteAnimationEngine(QObject *parent = nullptr);
    ~SpriteAnimationEngine() override;

    /**
     * Load sprite sheet and animation definitions.
     * @param spriteSheet  Path to map.png (the sprite sheet image)
     * @param animJson     Path to animations.json (frame region definitions)
     */
    bool loadAssets(const QString &spriteSheet, const QString &animJson);

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

    // --- Test accessors ------------------------------------------------------
    QString currentAnimation() const { return m_current.name; }
    bool isPlaying() const override { return m_playing; }
    int queueSize() const { return m_queue.size(); }

    /** @brief True once loadAssets / loadFromCharacterPack has loaded animations. */
    bool hasAnimations() const override { return !m_animations.isEmpty(); }

    /** @brief True if a clip exists after pack/engine name resolution. */
    bool hasClip(const QString &name) const;

    /** @brief Play the first resolvable clip in the fallback chain. */
    void playAnimationChain(const QStringList &chain, Priority priority = NormalPriority);

    /** @brief Always true for sprite engine — no GPU context to lose. */
    bool lastPaintSuccessful() const override { return true; }

signals:
    void effectRequested(const QString &effectName);
    void frameChanged();

private slots:
    void tick();

private:
    void advanceFrame();
    void startNextAnimation();
    void startIdleAnimation();
    void checkEffectTrigger(const QString &animName);

    struct FrameDef {
        QRect sourceRect;  // Region in the sprite sheet
        int durationMs;
    };

    struct AnimationDef {
        QString name;
        QVector<FrameDef> frames;
        int totalDurationMs = 0;
        bool loop = false;
        QString effect;
        QString sound;
    };

    // All loaded animations
    QMap<QString, AnimationDef> m_animations;

    // The sprite sheet image
    QPixmap m_spriteSheet;
    int m_frameWidth = 120;
    int m_frameHeight = 120;

    // Current playback
    AnimationDef m_current;
    int m_currentFrameIndex = 0;
    int m_currentFrameElapsed = 0;
    bool m_playing = false;
    bool m_looping = false;

    // Timer (16ms ≈ 60fps)
    QTimer m_timer;

    // Idle pool
    QStringList m_idleAnims;
    QVector<int> m_idleWeights;
    QTimer m_idleTimer;
    int m_idleTimeoutMs = 3000;
    QString m_lastIdleAnim;   // anti-repeat for idle picks

    // Queue
    QStringList m_queue;

    // Name mapping: lowercase/snake_case → PascalCase from animations.json
    QMap<QString, QString> m_nameMap;
    QMap<QString, QString> m_packNameMap;
    void buildNameMap();
};

#endif // SPRITEANIMATIONENGINE_H
