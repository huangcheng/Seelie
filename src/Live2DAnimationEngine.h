#ifndef LIVE2DANIMATIONENGINE_H
#define LIVE2DANIMATIONENGINE_H

#ifdef SEELIE_LIVE2D_SUPPORT

#include "AnimationEngine.h"

#include <QObject>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <CubismFramework.hpp>
#include <Model/CubismUserModel.hpp>
#include <CubismModelSettingJson.hpp>
#include <Motion/CubismMotion.hpp>
#include <ICubismModelSetting.hpp>

class QPainter;
class QRect;
class CharacterPack;

/**
 * @brief Live2D Cubism animation engine.
 *
 * Loads a Live2D model from a sprite pack, renders it to an offscreen
 * OpenGL framebuffer, and exposes the result as a QImage for QPainter-based
 * compositing.  Follows the same public interface as the legacy
 * SpriteAnimationEngine so MainWindow / EventRouter can treat all engines
 * uniformly.
 */
class Live2DAnimationEngine : public QObject, public AnimationEngine
{
    Q_OBJECT

public:
    using AnimationEngine::Priority;
    using AnimationEngine::HighPriority;
    using AnimationEngine::NormalPriority;

    explicit Live2DAnimationEngine(QObject *parent = nullptr);
    ~Live2DAnimationEngine() override;

    /**
     * @brief Load a Live2D model from a sprite pack.
     * @param pack  Pack whose characterConfig().engineType == Live2D
     * @return true on success
     */
    bool loadFromCharacterPack(const CharacterPack *pack) override;

    /**
     * @brief Start playing a named motion group.
     *
     * Motion names correspond to groups in model3.json (e.g. "Idle", "TapBody").
     * They are also the keys used in the pack's eventMap.
     */
    void playAnimation(const QString &name, Priority priority = NormalPriority) override;

    /** @brief Stop playback and clear state (used when switching to a different engine). */
    void stop() override;

    /**
     * @brief Set the pointer target in normalized viewport coords (-1..+1).
     *        Live2D's drag manager smooths this into ParamAngleX/Y/Z,
     *        ParamBodyAngleX, and ParamEyeBallX/Y so the head/body/eyes follow
     *        the cursor. Pass (0, 0) to recenter when the pointer leaves.
     */
    void setPointerTarget(float x, float y);

    /**
     * @brief Play the first non-empty motion group in the chain.
     * The engine has zero knowledge of what specific group names mean —
     * the manifest's eventMap declares the chain, this is just the
     * dispatcher. No-op if every group in the chain is missing or empty.
     * Mouse clicks should route through EventRouter::triggerEvent("user.click")
     * so the manifest can declare the chain instead of the engine guessing.
     */
    void playAnimationChain(const QStringList &chain, Priority priority);

    /**
     * @brief Render the current frame into a QPainter target rect.
     */
    void paint(QPainter *painter, const QRect &bounds) override;

    /**
     * @brief Bounding box of the visible character in the FBO's coordinate
     *        space, derived from the alpha channel of the last rendered frame.
     *        Returns QRect() until at least one frame with non-transparent
     *        pixels has been rendered. Cached until the next stop() / load.
     */
    QRect characterBounds() const;

    bool isPlaying() const override { return m_playing; }
    bool hasAnimations() const override { return m_modelLoaded; }

    /** @brief Render dimensions (from manifest frameWidth/frameHeight). */
    int renderWidth() const { return m_renderWidth; }
    int renderHeight() const { return m_renderHeight; }

    /** @brief True if the last render produced a valid non-null image.
     *         Used by MainWindow to detect GL context loss and fall back
     *         to another engine. */
    bool lastPaintSuccessful() const override { return m_lastPaintSuccessful; }

signals:
    void frameChanged();
    void effectRequested(const QString &effectName);

private slots:
    void tick();

private:
    // --- Cubism helpers ---
    bool initOpenGL();
    void releaseOpenGL();
    bool recoverOpenGL();
    bool loadModel(const QString &modelJsonPath, const QString &modelDir);
    void releaseModel();
    void renderFrame();

    void startNextAnimation();
    void startIdleAnimation();

    // --- Cubism SDK objects ---
    // Using raw pointers because CubismUserModel hierarchy manages its own lifetime.
    class CubismModel;                     // forward-declared pimpl
    CubismModel *m_cubismModel = nullptr;  // pimpl hides Cubism headers from consumers

    // --- Render state ---
    QOpenGLContext *m_glContext = nullptr;
    QOffscreenSurface *m_surface = nullptr;
    QOpenGLFramebufferObject *m_fbo = nullptr;
    QImage m_image;
    mutable QRect m_characterBounds;  // alpha bbox, lazily computed from m_image
    int m_renderWidth = 200;
    int m_renderHeight = 200;
    bool m_lastPaintSuccessful = false;

    // --- Playback state ---
    bool m_modelLoaded = false;
    bool m_playing = false;
    QStringList m_motionGroups;     // available motion group names
    QString m_currentMotion;        // currently playing motion group
    QStringList m_queue;            // queued motion group names

    // --- Timing ---
    QTimer m_timer;
    qint64 m_lastTickMs = 0;

    // --- Idle pool ---
    QStringList m_idleAnims;
    QVector<int> m_idleWeights;
    QTimer m_idleTimer;
    int m_idleTimeoutMs = 3000;
    QString m_lastIdleAnim;   // anti-repeat for idle picks
};

#endif // SEELIE_LIVE2D_SUPPORT

#endif // LIVE2DANIMATIONENGINE_H
