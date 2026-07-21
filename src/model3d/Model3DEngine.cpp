#include "Model3DEngine.h"

#include "GltfLoader.h"
#include "AnimationEvaluator.h"
#include "CharacterPack.h"

#include <QPainter>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QDebug>

// Render the FBO at 2x the window size; paint() downscales with
// SmoothPixmapTransform, giving cheap full-scene anti-aliasing (incl. alpha
// edges, which MSAA wouldn't fix).
static constexpr int kSupersample = 2;

Model3DEngine::Model3DEngine(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &Model3DEngine::tick);
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &Model3DEngine::startIdleAnimation);
}

Model3DEngine::~Model3DEngine()
{
    stop();
    releaseModel();
    releaseOpenGL();
}

bool Model3DEngine::initOpenGL()
{
    if (m_glContext) return true;
    auto bail = [this](const char *msg) {
        qWarning() << "Model3D:" << msg;
        releaseOpenGL();
        return false;
    };

    // Same format as Live2D (GL 2.1 compat, 24/8 depth-stencil): no driver
    // surprises, and per-engine context + offscreen surface with no share
    // groups keeps the two GL engines fully independent.
    m_surface = new QOffscreenSurface(nullptr);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setMajorVersion(2);
    fmt.setMinorVersion(1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    m_surface->setFormat(fmt);
    m_surface->create();

    m_glContext = new QOpenGLContext(this);
    m_glContext->setFormat(fmt);
    if (!m_glContext->create())
        return bail("Failed to create OpenGL context");
    if (!m_glContext->makeCurrent(m_surface))
        return bail("Failed to make context current");

    m_renderer = new GLSkinningRenderer;
    QString err;
    if (!m_renderer->initialize(&err))
        return bail(qPrintable(QStringLiteral("renderer init failed: %1").arg(err)));

    m_fbo = new QOpenGLFramebufferObject(m_renderWidth * kSupersample,
                                         m_renderHeight * kSupersample,
                                         QOpenGLFramebufferObject::CombinedDepthStencil);
    if (!m_fbo->isValid())
        return bail("FBO creation failed");

    m_glContext->doneCurrent();
    return true;
}

void Model3DEngine::releaseOpenGL()
{
    if (m_glContext && m_surface) {
        m_glContext->makeCurrent(m_surface);
        if (m_renderer) m_renderer->release();
        m_glContext->doneCurrent();
    }
    delete m_renderer;   m_renderer = nullptr;
    if (m_fbo) m_fbo->release();
    delete m_fbo;        m_fbo = nullptr;
    delete m_glContext;  m_glContext = nullptr;
    delete m_surface;    m_surface = nullptr;
}

bool Model3DEngine::recoverOpenGL()
{
    // Mirrors Live2DAnimationEngine::recoverOpenGL: rebuild context + GL
    // resources after GPU power-state change / DWM restart, then resume.
    const int savedClip = m_currentClip;
    const bool savedLoops = m_currentLoops;
    const bool wasPlaying = m_playing;
    Model3DModel model = std::move(m_model);

    releaseOpenGL();
    if (!initOpenGL()) return false;

    m_model = std::move(model);
    if (!m_glContext->makeCurrent(m_surface)) {
        qWarning() << "Model3D: recoverOpenGL: makeCurrent failed after init";
        return false;
    }
    m_renderer->upload(m_model, float(m_renderWidth) / float(m_renderHeight));
    m_glContext->doneCurrent();

    m_lastPaintSuccessful = true;
    if (wasPlaying && savedClip >= 0)
        startClip(savedClip, savedLoops);
    else if (wasPlaying)
        startIdleAnimation();
    return true;
}

bool Model3DEngine::loadFromCharacterPack(const CharacterPack *pack)
{
    if (!pack || pack->characterConfig().engineType != CharacterPack::EngineType::Model3D)
        return false;

    stop();
    releaseModel();

    m_renderWidth = qBound(1, pack->characterConfig().frameWidth, 4096);
    m_renderHeight = qBound(1, pack->characterConfig().frameHeight, 4096);

    if (!initOpenGL())
        return false;

    const QString glbPath = pack->assetPath(pack->characterConfig().modelFile);
    QString err;
    if (!GltfLoader::loadFromFile(glbPath, m_model, &err)) {
        qWarning() << "Model3D: load failed:" << err;
        releaseModel();
        return false;
    }

    // Coordinate/unit normalization via a model matrix (manifest overrides;
    // glTF is Y-up meters by default).
    QMatrix4x4 modelMat;
    modelMat.scale(pack->characterConfig().unitScale);
    if (pack->characterConfig().upAxis == QStringLiteral("z"))
        modelMat.rotate(-90.0f, 1.0f, 0.0f, 0.0f);
    m_renderer->setModelTransform(modelMat);
    m_renderer->setCameraOverrides(pack->characterConfig().cameraDistance,
                                   pack->characterConfig().cameraHeight,
                                   pack->characterConfig().cameraYaw);

    m_glContext->makeCurrent(m_surface);
    delete m_fbo;
    m_fbo = new QOpenGLFramebufferObject(m_renderWidth * kSupersample,
                                         m_renderHeight * kSupersample,
                                         QOpenGLFramebufferObject::CombinedDepthStencil);
    m_renderer->upload(m_model, float(m_renderWidth) / float(m_renderHeight));
    m_glContext->doneCurrent();

    // Filter pack mappings against clips present in the model (missing names
    // logged + skipped — never crash on a user-imported pack).
    m_idleClips.clear();
    m_idleWeights.clear();
    for (const auto &entry : pack->idlePool()) {
        const auto it = m_model.clipIndexByName.constFind(entry.animationName);
        if (it == m_model.clipIndexByName.constEnd()) {
            qWarning() << "Model3D: idle clip" << entry.animationName << "not in model, skipped";
            continue;
        }
        m_idleClips.append(it.value());
        m_idleWeights.append(qMax(1, entry.weight));
    }
    // Fallback idle = first clip in the model.
    if (m_idleClips.isEmpty() && !m_model.clips.isEmpty()) {
        m_idleClips.append(0);
        m_idleWeights.append(1);
    }

    m_modelLoaded = true;
    m_playing = true;
    m_lastPaintSuccessful = true;
    renderFrame();           // first frame within one tick of load
    emit frameChanged();
    scheduleIdle();
    return true;
}

void Model3DEngine::playAnimation(const QString &name, Priority priority)
{
    if (!m_modelLoaded) return;
    const auto it = m_model.clipIndexByName.constFind(name);
    if (it == m_model.clipIndexByName.constEnd()) {
        qWarning() << "Model3D: clip" << name << "not found";
        return;
    }
    const int index = it.value();
    const bool loops = m_idleClips.contains(index);
    if (priority == HighPriority) {
        m_queue.clear();
        startClip(index, loops);
    } else if (m_currentClip < 0 || m_currentLoops) {
        // Preempt looping clips (idle) with NormalPriority event clips.
        startClip(index, loops);
    } else {
        m_queue.append(index);
    }
}

void Model3DEngine::stop()
{
    m_timer.stop();
    m_idleTimer.stop();
    m_playing = false;
    m_queue.clear();
    m_currentClip = -1;
    m_image = QImage();
    m_lastPaintSuccessful = false;
}

void Model3DEngine::startClip(int index, bool loop)
{
    if (index < 0 || index >= m_model.clips.size()) return;
    m_currentClip = index;
    m_currentLoops = loop;
    m_clipTimeSec = 0.0f;
    m_playing = true;
    m_idleTimer.stop();
    if (!m_timer.isActive()) {
        m_lastTickMs = 0;
        m_timer.start();
    }
}

void Model3DEngine::onClipFinished()
{
    if (!m_queue.isEmpty()) {
        const int next = m_queue.takeFirst();
        startClip(next, m_idleClips.contains(next));
        return;
    }
    m_currentClip = -1;      // idle timer will pick the next idle clip
    scheduleIdle();
}

void Model3DEngine::scheduleIdle()
{
    if (m_idleClips.isEmpty() || !m_modelLoaded) return;
    // Jittered 3-6s, matching Live2D's idle cadence.
    const int jitter = QRandomGenerator::global()->bounded(3000);
    m_idleTimer.start(m_idleTimeoutMs + jitter);
}

void Model3DEngine::startIdleAnimation()
{
    if (!m_modelLoaded || m_idleClips.isEmpty() || m_currentClip >= 0) return;
    // Weighted pick with anti-repeat.
    int total = 0;
    for (int w : m_idleWeights) total += w;
    int pick = QRandomGenerator::global()->bounded(total);
    int chosen = 0;
    for (int i = 0; i < m_idleClips.size(); ++i) {
        pick -= m_idleWeights[i];
        if (pick < 0) { chosen = i; break; }
    }
    if (m_idleClips.size() > 1 &&
        m_model.clips[m_idleClips[chosen]].name == m_lastIdleAnim) {
        chosen = (chosen + 1) % m_idleClips.size();
    }
    m_lastIdleAnim = m_model.clips[m_idleClips[chosen]].name;
    startClip(m_idleClips[chosen], true);
}

void Model3DEngine::tick()
{
    if (!m_modelLoaded || !m_glContext) return;

    if (!m_glContext->isValid()) {
        qWarning() << "Model3D: GL context lost — attempting recovery";
        if (!recoverOpenGL()) {
            qWarning() << "Model3D: GL recovery failed — stopping engine";
            stop();
            return;
        }
    }

    static QElapsedTimer clock;   // engine lives on the GUI thread only
    if (!clock.isValid()) clock.start();
    const qint64 now = clock.elapsed();
    const float dt = m_lastTickMs > 0 ? float(now - m_lastTickMs) / 1000.0f : 0.016f;
    m_lastTickMs = now;

    if (m_currentClip >= 0) {
        m_clipTimeSec += dt;
        const Model3DClip &clip = m_model.clips[m_currentClip];
        if (!m_currentLoops && m_clipTimeSec >= clip.duration + oneShotHoldSec(clip.duration))
            onClipFinished();
    }
    m_swayTimeSec += dt;

    renderFrame();
    emit frameChanged();
}

void Model3DEngine::renderFrame()
{
    if (!m_glContext->makeCurrent(m_surface)) {
        m_lastPaintSuccessful = false;
        return;
    }
    const Model3DClip *clip = (m_currentClip >= 0) ? &m_model.clips[m_currentClip] : nullptr;
    QVector<QMatrix4x4> palette, globals;
    AnimationEvaluator::evaluate(m_model, clip, m_clipTimeSec, m_currentLoops, palette, &globals);
    // Procedural sway while idling (bind pose or looping idle clip) — gives
    // life even to packs whose idle clips are static poses.
    m_renderer->setSway(m_swayTimeSec, m_currentClip < 0 || m_currentLoops);
    m_renderer->render(m_fbo, palette, globals);
    m_image = m_fbo->toImage();
    m_lastPaintSuccessful = !m_image.isNull();
    m_glContext->doneCurrent();

    if (!m_lastPaintSuccessful && m_playing) {
        qWarning() << "Model3D: frame render failed, attempting GL recovery";
        recoverOpenGL();
    }
}

void Model3DEngine::paint(QPainter *painter, const QRect &bounds)
{
    if (!m_playing || m_image.isNull()) return;
    painter->drawImage(bounds, m_image);
}

void Model3DEngine::releaseModel()
{
    if (m_renderer && m_glContext && m_surface) {
        m_glContext->makeCurrent(m_surface);
        m_renderer->releaseModelResources();   // keep the shader program
        m_glContext->doneCurrent();
    }
    m_model = Model3DModel{};
    m_modelLoaded = false;
    m_idleClips.clear();
    m_idleWeights.clear();
    m_lastIdleAnim.clear();
    m_currentClip = -1;
}
