#ifndef MODEL3D_ENGINE_H
#define MODEL3D_ENGINE_H

#include "AnimationEngine.h"
#include "Model3DTypes.h"
#include "GLSkinningRenderer.h"

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

class QPainter;
class QRect;
class CharacterPack;

// Skeletal 3D (glTF/GLB) animation engine. Mirrors Live2DAnimationEngine's
// structure: offscreen QOpenGLContext -> skinned FBO render -> QImage cache
// -> QPainter. Qt GL wrappers only — no GLEW (see GLSkinningRenderer).
class Model3DEngine : public QObject, public AnimationEngine
{
    Q_OBJECT

public:
    using AnimationEngine::Priority;
    using AnimationEngine::HighPriority;
    using AnimationEngine::NormalPriority;

    explicit Model3DEngine(QObject *parent = nullptr);
    ~Model3DEngine() override;

    bool loadFromCharacterPack(const CharacterPack *pack) override;
    void playAnimation(const QString &name, Priority priority = NormalPriority) override;
    void stop() override;
    void paint(QPainter *painter, const QRect &bounds) override;

    bool isPlaying() const override { return m_playing; }
    bool hasAnimations() const override { return m_modelLoaded; }
    bool lastPaintSuccessful() const override { return m_lastPaintSuccessful; }

    int renderWidth() const { return m_renderWidth; }
    int renderHeight() const { return m_renderHeight; }

signals:
    void frameChanged();
    void effectRequested(const QString &effectName);

private slots:
    void tick();
    void startIdleAnimation();

private:
    bool initOpenGL();
    void releaseOpenGL();
    bool recoverOpenGL();
    void releaseModel();
    void renderFrame();
    void startClip(int index, bool loop);
    void onClipFinished();
    void scheduleIdle();

    // --- GL state ---
    QOpenGLContext *m_glContext = nullptr;
    QOffscreenSurface *m_surface = nullptr;
    QOpenGLFramebufferObject *m_fbo = nullptr;
    GLSkinningRenderer *m_renderer = nullptr;
    QImage m_image;
    int m_renderWidth = 200;
    int m_renderHeight = 200;
    bool m_lastPaintSuccessful = false;

    // --- Model ---
    Model3DModel m_model;
    bool m_modelLoaded = false;

    // --- Playback ---
    bool m_playing = false;
    int m_currentClip = -1;          // index into m_model.clips, -1 = bind pose
    bool m_currentLoops = false;
    float m_clipTimeSec = 0.0f;
    QVector<int> m_queue;            // queued clip indices (one-shot)
    QTimer m_timer;                  // 16ms frame tick
    qint64 m_lastTickMs = 0;

    // --- Idle ---
    QVector<int> m_idleClips;        // clip indices (looped)
    QVector<int> m_idleWeights;
    QTimer m_idleTimer;
    QString m_lastIdleAnim;          // anti-repeat
    int m_idleTimeoutMs = 4000;
};

#endif // MODEL3D_ENGINE_H
