#ifdef SEELIE_LIVE2D_SUPPORT

// GLEW must be included BEFORE any Qt/OpenGL headers
#include <GL/glew.h>

#include "Live2DAnimationEngine.h"
#include "CharacterPack.h"

#include <QPainter>
#include <QDir>
#include <QFile>
#include <QByteArray>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QDebug>
#include <QOpenGLFunctions>
#include <cstring>

// Live2D Cubism SDK
#include <CubismFramework.hpp>
#include <CubismModelSettingJson.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismPose.hpp>
#include <Math/CubismTargetPoint.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Id/CubismIdManager.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Utils/CubismString.hpp>

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;

// ---------------------------------------------------------------------------
// CubismAllocator — required by the framework
// ---------------------------------------------------------------------------
class SeelieCubismAllocator : public Csm::ICubismAllocator
{
    void *Allocate(const Csm::csmSizeType size) override { return malloc(size); }
    void Deallocate(void *addr) override { free(addr); }
    void *AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 align) override
    {
        const size_t alignMask = ~(static_cast<size_t>(align) - 1);
        size_t offset = static_cast<size_t>(align) - 1 + sizeof(void *);
        void *raw = malloc(size + offset);
        if (!raw) return nullptr;
        void **aligned = reinterpret_cast<void **>((reinterpret_cast<size_t>(raw) + offset) & alignMask);
        aligned[-1] = raw;
        return aligned;
    }
    void DeallocateAligned(void *addr) override
    {
        if (addr) free(static_cast<void **>(addr)[-1]);
    }
};

static SeelieCubismAllocator s_allocator;
static bool s_frameworkInitialized = false;

// Cubism Framework loads shader source from disk via this callback.
// We ship the framework's shader files as Qt resources under :/live2d/
// with aliases that match the paths the framework requests
// (e.g. "FrameworkShaders/VertShaderSrc.vert").
static Csm::csmByte *seelieCubismLoadFile(const std::string filePath, Csm::csmSizeInt *outSize)
{
    const QString resourcePath = ":/live2d/" + QString::fromStdString(filePath);
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Live2D: framework file not found:" << resourcePath;
        if (outSize) *outSize = 0;
        return nullptr;
    }
    const QByteArray data = f.readAll();
    auto *buf = static_cast<Csm::csmByte *>(malloc(static_cast<size_t>(data.size())));
    if (!buf) {
        if (outSize) *outSize = 0;
        return nullptr;
    }
    std::memcpy(buf, data.constData(), static_cast<size_t>(data.size()));
    if (outSize) *outSize = static_cast<Csm::csmSizeInt>(data.size());
    return buf;
}

static void seelieCubismReleaseBytes(Csm::csmByte *buf)
{
    free(buf);
}

static void ensureFrameworkInitialized()
{
    // NOTE: CubismFramework::StartUp() stores the Option* pointer (no copy),
    // and GetLoadFileFunction()/GetReleaseBytesFunction() dereference it on
    // every lookup. The Option must therefore outlive the framework — using
    // a local on the stack causes use-after-return the first time the
    // renderer's shader init asks for the file loader. Make it static.
    static CubismFramework::Option s_option = [] {
        CubismFramework::Option o;
        o.LogFunction = [](const char *msg) { qDebug() << "[Live2D]" << msg; };
        o.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
        o.LoadFileFunction = &seelieCubismLoadFile;
        o.ReleaseBytesFunction = &seelieCubismReleaseBytes;
        return o;
    }();

    if (!s_frameworkInitialized) {
        CubismFramework::StartUp(&s_allocator, &s_option);
        CubismFramework::Initialize();
        s_frameworkInitialized = true;
    }
}

// ---------------------------------------------------------------------------
// Pimpl — wraps CubismUserModel and all SDK objects
// ---------------------------------------------------------------------------
class Live2DAnimationEngine::CubismModel : public Csm::CubismUserModel
{
public:
    CubismModel() : Csm::CubismUserModel() {}
    ~CubismModel() override { release(); }

    bool load(const QString &modelJsonPath, const QString &modelDir)
    {
        m_modelDir = modelDir.toStdString();
        if (!m_modelDir.empty() && m_modelDir.back() != '/') m_modelDir += '/';

        // --- model3.json ---
        qDebug() << "Live2D: Reading model3.json...";
        QByteArray jsonBuf = readFile(modelJsonPath);
        if (jsonBuf.isEmpty()) { qWarning() << "Live2D: model3.json is empty"; return false; }
        qDebug() << "Live2D: model3.json size:" << jsonBuf.size() << "bytes";
        m_setting = new CubismModelSettingJson(
            reinterpret_cast<const Csm::csmByte *>(jsonBuf.constData()), jsonBuf.size());
        qDebug() << "Live2D: CubismModelSettingJson created OK";

        // --- .moc3 ---
        {
            std::string path = m_modelDir + m_setting->GetModelFileName();
            qDebug() << "Live2D: Reading moc3:" << QString::fromStdString(path);
            QByteArray buf = readFile(QString::fromStdString(path));
            if (buf.isEmpty()) { qWarning() << "Live2D: moc3 file is empty"; return false; }
            qDebug() << "Live2D: moc3 size:" << buf.size() << "bytes, calling LoadModel...";
            LoadModel(reinterpret_cast<const Csm::csmByte *>(buf.constData()), buf.size());
            qDebug() << "Live2D: LoadModel OK";
        }

        // --- Physics ---
        if (strcmp(m_setting->GetPhysicsFileName(), "")) {
            std::string path = m_modelDir + m_setting->GetPhysicsFileName();
            qDebug() << "Live2D: Loading physics:" << QString::fromStdString(path);
            QByteArray buf = readFile(QString::fromStdString(path));
            if (!buf.isEmpty()) {
                LoadPhysics(reinterpret_cast<const Csm::csmByte *>(buf.constData()), buf.size());
                qDebug() << "Live2D: Physics loaded OK";
            }
        }

        // --- Pose ---
        if (strcmp(m_setting->GetPoseFileName(), "")) {
            std::string path = m_modelDir + m_setting->GetPoseFileName();
            qDebug() << "Live2D: Loading pose:" << QString::fromStdString(path);
            QByteArray buf = readFile(QString::fromStdString(path));
            if (!buf.isEmpty()) {
                LoadPose(reinterpret_cast<const Csm::csmByte *>(buf.constData()), buf.size());
                qDebug() << "Live2D: Pose loaded OK";
            }
        }

        // --- Eye blink ---
        if (m_setting->GetEyeBlinkParameterCount() > 0) {
            _eyeBlink = CubismEyeBlink::Create(m_setting);
        }

        // --- Drag / pointer tracking ---
        // Cubism's CubismTargetPoint smooths raw input into a moving target
        // that feeds the head/body/eye drag parameters every update.
        m_dragManager = CSM_NEW Csm::CubismTargetPoint();
        auto *idm = CubismFramework::GetIdManager();
        m_idParamAngleX     = idm->GetId(ParamAngleX);
        m_idParamAngleY     = idm->GetId(ParamAngleY);
        m_idParamAngleZ     = idm->GetId(ParamAngleZ);
        m_idParamBodyAngleX = idm->GetId(ParamBodyAngleX);
        m_idParamEyeBallX   = idm->GetId(ParamEyeBallX);
        m_idParamEyeBallY   = idm->GetId(ParamEyeBallY);

        // --- Breath ---
        {
            _breath = CubismBreath::Create();
            Csm::csmVector<CubismBreath::BreathParameterData> params;
            params.PushBack(CubismBreath::BreathParameterData(
                CubismFramework::GetIdManager()->GetId(ParamAngleX), 0.0f, 15.0f, 6.5345f, 0.5f));
            params.PushBack(CubismBreath::BreathParameterData(
                CubismFramework::GetIdManager()->GetId(ParamAngleY), 0.0f, 8.0f, 3.5345f, 0.5f));
            params.PushBack(CubismBreath::BreathParameterData(
                CubismFramework::GetIdManager()->GetId(ParamAngleZ), 0.0f, 10.0f, 5.5345f, 0.5f));
            params.PushBack(CubismBreath::BreathParameterData(
                CubismFramework::GetIdManager()->GetId(ParamBodyAngleX), 0.0f, 4.0f, 15.5345f, 0.5f));
            _breath->SetParameters(params);
        }

        // --- Preload motions ---
        qDebug() << "Live2D: Preloading motions, groups:" << m_setting->GetMotionGroupCount();
        for (Csm::csmInt32 g = 0; g < m_setting->GetMotionGroupCount(); ++g) {
            const char *group = m_setting->GetMotionGroupName(g);
            m_motionGroupNames.append(QString::fromUtf8(group));
            Csm::csmInt32 count = m_setting->GetMotionCount(group);
            for (Csm::csmInt32 i = 0; i < count; ++i) {
                std::string motionPath = m_modelDir + m_setting->GetMotionFileName(group, i);
                QByteArray buf = readFile(QString::fromStdString(motionPath));
                if (buf.isEmpty()) continue;
                Csm::csmString name = Csm::Utils::CubismString::GetFormatedString("%s_%d", group, i);
                CubismMotion *motion = static_cast<CubismMotion *>(
                    LoadMotion(reinterpret_cast<const Csm::csmByte *>(buf.constData()),
                               buf.size(), name.GetRawString()));
                if (motion) {
                    const Csm::csmFloat32 fadeIn = m_setting->GetMotionFadeInTimeValue(group, i);
                    const Csm::csmFloat32 fadeOut = m_setting->GetMotionFadeOutTimeValue(group, i);
                    if (fadeIn >= 0.0f) motion->SetFadeInTime(fadeIn);
                    if (fadeOut >= 0.0f) motion->SetFadeOutTime(fadeOut);
                    m_motions[name] = motion;
                }
            }
        }

        // --- Renderer ---
        // Will be created after GL context is current (in setupRenderer)

        m_modelLoaded = true;
        return true;
    }

    void setupRenderer(int width, int height)
    {
        if (!_model) { qWarning() << "Live2D: setupRenderer: no model"; return; }
        qDebug() << "Live2D: Creating renderer" << width << "x" << height;
        qDebug() << "Live2D: _model ptr:" << (void*)_model
                 << "canvas:" << _model->GetCanvasWidth() << "x" << _model->GetCanvasHeight()
                 << "drawables:" << _model->GetDrawableCount()
                 << "blendMode:" << _model->IsBlendModeEnabled()
                 << "masking:" << _model->IsUsingMasking();
        CreateRenderer(width, height);
        qDebug() << "Live2D: Renderer created OK, loading" << m_setting->GetTextureCount() << "textures...";
        // Textures
        for (Csm::csmInt32 t = 0; t < m_setting->GetTextureCount(); ++t) {
            std::string texPath = m_modelDir + m_setting->GetTextureFileName(t);
            qDebug() << "Live2D: Texture" << t << ":" << texPath.c_str();
            GLuint texId = loadTexture(QString::fromStdString(texPath));
            qDebug() << "Live2D: Texture" << t << "loaded, GL ID:" << texId;
            // M10: Reject texture 0 (loadTexture returns 0 on failure).
            // Without this check, BindTexture(0) makes the renderer draw
            // with an uninitialised texture slot, producing garbage.
            if (texId == 0) {
                qWarning() << "Live2D: Texture load failed for index" << t << ", aborting renderer setup";
                return;
            }
            GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->BindTexture(t, texId);
            m_textures.append(texId);
        }
        GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(false);
        qDebug() << "Live2D: Renderer setup complete";
    }

    Csm::CubismMotionQueueEntryHandle startMotion(const QString &group, int no, int priority)
    {
        Csm::csmString name = Csm::Utils::CubismString::GetFormatedString(
            "%s_%d", group.toUtf8().constData(), no);
        CubismMotion *motion = static_cast<CubismMotion *>(m_motions[name]);
        if (!motion) return Csm::InvalidMotionQueueEntryHandleValue;
        return _motionManager->StartMotionPriority(motion, false, priority);
    }

    bool isMotionFinished() const
    {
        return _motionManager ? _motionManager->IsFinished() : true;
    }

    void update(float deltaSeconds)
    {
        if (!_model) return;
        _model->LoadParameters();

        // Update motions
        _motionManager->UpdateMotion(_model, deltaSeconds);

        _model->SaveParameters();

        // Eye blink
        if (_eyeBlink) _eyeBlink->UpdateParameters(_model, deltaSeconds);
        // Breath
        if (_breath) _breath->UpdateParameters(_model, deltaSeconds);

        // Drag / pointer tracking — smoothed head/body/eye tracking.
        // Apply after motion/breath so it biases the base pose without being
        // overwritten by SaveParameters.
        if (m_dragManager) {
            m_dragManager->Update(deltaSeconds);
            const float dx = m_dragManager->GetX();
            const float dy = m_dragManager->GetY();
            if (m_idParamAngleX)     _model->AddParameterValue(m_idParamAngleX,     dx * 30.0f);
            if (m_idParamAngleY)     _model->AddParameterValue(m_idParamAngleY,     dy * 30.0f);
            if (m_idParamAngleZ)     _model->AddParameterValue(m_idParamAngleZ,     dx * dy * -30.0f);
            if (m_idParamBodyAngleX) _model->AddParameterValue(m_idParamBodyAngleX, dx * 10.0f);
            if (m_idParamEyeBallX)   _model->AddParameterValue(m_idParamEyeBallX,   dx);
            if (m_idParamEyeBallY)   _model->AddParameterValue(m_idParamEyeBallY,   dy);
        }

        // Physics
        if (_physics) _physics->Evaluate(_model, deltaSeconds);
        // Pose
        if (_pose) _pose->UpdateParameters(_model, deltaSeconds);

        _model->Update();
    }

    void setDragging(float x, float y)
    {
        if (m_dragManager) m_dragManager->Set(x, y);
    }

    void draw(int width, int height)
    {
        if (!_model) return;
        CubismMatrix44 proj;
        proj.LoadIdentity();

        // Aspect correction only. The model's CubismModelMatrix constructor
        // already calls SetHeight(2.0f) so the canvas fills NDC [-1, +1]
        // vertically — no extra scaling needed for a square-ish viewport.
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        if (aspect < 1.0f) {
            proj.Scale(1.0f, aspect);
        } else {
            proj.Scale(1.0f / aspect, 1.0f);
        }

        proj.MultiplyByMatrix(_modelMatrix);
        GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->SetMvpMatrix(&proj);
        GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->DrawModel();
    }

    int motionCount(const QString &group) const
    {
        return m_setting ? m_setting->GetMotionCount(group.toUtf8().constData()) : 0;
    }

    QStringList motionGroupNames() const { return m_motionGroupNames; }
    bool loaded() const { return m_modelLoaded; }

    void release()
    {
        for (auto iter = m_motions.Begin(); iter != m_motions.End(); ++iter) {
            Csm::ACubismMotion::Delete(iter->Second);
        }
        m_motions.Clear();
        for (GLuint tex : m_textures) {
            glDeleteTextures(1, &tex);
        }
        m_textures.clear();
        delete m_setting;
        m_setting = nullptr;
        if (m_dragManager) {
            CSM_DELETE(m_dragManager);
            m_dragManager = nullptr;
        }
        m_idParamAngleX = m_idParamAngleY = m_idParamAngleZ = nullptr;
        m_idParamBodyAngleX = nullptr;
        m_idParamEyeBallX = m_idParamEyeBallY = nullptr;
        m_modelLoaded = false;
        // NOTE: do NOT call any base-class destructor manually here — the
        // ~CubismModel() destructor chains into ~CubismUserModel(), which
        // already deletes _model, _renderer, _motionManager, _eyeBlink,
        // _breath, _physics, _pose. Audit C4 claimed this leaked; it does
        // not, as long as release() is reached via destruction (which is
        // the only call path today).
    }

private:
    static QByteArray readFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "Live2D: Cannot open file:" << path
                       << "exists:" << QFile::exists(path)
                       << "error:" << f.errorString();
            return {};
        }
        return f.readAll();
    }

    static GLuint loadTexture(const QString &path)
    {
        QImage img(path);
        if (img.isNull()) {
            qWarning() << "Live2D: Cannot load texture:" << path;
            return 0;
        }
        // Reject pathological dimensions before allocating GPU memory.
        // A 30000x30000 PNG would OOM during decode or crash the driver
        // inside glTexImage2D.
        //
        // Query GL_MAX_TEXTURE_SIZE at runtime instead of hardcoding 4096:
        // many real-world Cubism 4 packs ship 8K textures (atlas variants
        // suffixed `.8192/`), and modern GPUs handle 16K easily. The
        // conservative 4096 cap that landed with the audit's C14 fix
        // rejected legitimate packs (e.g. little_demon's 8192×4096
        // texture). Audit C14, with rectified bound.
        GLint maxTexSize = 4096;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
        if (maxTexSize < 4096) maxTexSize = 4096;  // sanity floor
        if (img.width() <= 0 || img.height() <= 0 ||
            img.width() > maxTexSize || img.height() > maxTexSize) {
            qWarning() << "Live2D: Refusing texture with out-of-range dimensions"
                       << img.size() << "(GL max =" << maxTexSize << ") from" << path;
            return 0;
        }
        img = img.convertToFormat(QImage::Format_RGBA8888);

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        if (__glewGenerateMipmap) __glewGenerateMipmap(GL_TEXTURE_2D);
        return tex;
    }

    std::string m_modelDir;
    CubismModelSettingJson *m_setting = nullptr;
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion *> m_motions;
    QStringList m_motionGroupNames;
    QVector<GLuint> m_textures;
    bool m_modelLoaded = false;

    // Drag / pointer tracking — see update() for how these are applied.
    Csm::CubismTargetPoint *m_dragManager = nullptr;
    const Csm::CubismId *m_idParamAngleX = nullptr;
    const Csm::CubismId *m_idParamAngleY = nullptr;
    const Csm::CubismId *m_idParamAngleZ = nullptr;
    const Csm::CubismId *m_idParamBodyAngleX = nullptr;
    const Csm::CubismId *m_idParamEyeBallX = nullptr;
    const Csm::CubismId *m_idParamEyeBallY = nullptr;
};

// ---------------------------------------------------------------------------
// Live2DAnimationEngine public implementation
// ---------------------------------------------------------------------------

Live2DAnimationEngine::Live2DAnimationEngine(QObject *parent)
    : QObject(parent)
    , m_timer(this)
    , m_idleTimer(this)
{
    ensureFrameworkInitialized();

    m_timer.setInterval(16); // ~60fps
    connect(&m_timer, &QTimer::timeout, this, &Live2DAnimationEngine::tick);

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(m_idleTimeoutMs);
    connect(&m_idleTimer, &QTimer::timeout, this, &Live2DAnimationEngine::startIdleAnimation);
}

Live2DAnimationEngine::~Live2DAnimationEngine()
{
    m_timer.stop();
    m_idleTimer.stop();

    // Must make context current to release GL resources
    if (m_glContext && m_surface) {
        m_glContext->makeCurrent(m_surface);
    }
    releaseModel();
    releaseOpenGL();
}

bool Live2DAnimationEngine::initOpenGL()
{
    if (m_glContext) return true;

    // Any failure below must release everything allocated so far; otherwise
    // a partial init (e.g. driver returns OK but makeCurrent fails) leaks
    // m_surface + m_glContext on every retry. Audit C5.
    auto bail = [this](const char *msg) {
        qWarning() << "Live2D:" << msg;
        releaseOpenGL();
        return false;
    };

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

    // Initialize GLEW (must be called after context is current)
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        qWarning() << "Live2D: GLEW init failed:" << reinterpret_cast<const char *>(glewGetErrorString(err));
        releaseOpenGL();
        return false;
    }
    qDebug() << "Live2D: GLEW initialized OK, GL version:"
             << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    // Create FBO
    m_fbo = new QOpenGLFramebufferObject(m_renderWidth, m_renderHeight,
                                          QOpenGLFramebufferObject::CombinedDepthStencil);
    if (!m_fbo->isValid())
        return bail("FBO creation failed");
    return true;
}

void Live2DAnimationEngine::releaseOpenGL()
{
    // H11: Unbind the FBO before deleting so the GL driver doesn't try to
    // reference a deleted buffer object if the context is still current.
    if (m_fbo) m_fbo->release();
    delete m_fbo;
    m_fbo = nullptr;
    delete m_glContext;
    m_glContext = nullptr;
    delete m_surface;
    m_surface = nullptr;
}

bool Live2DAnimationEngine::recoverOpenGL()
{
    QString savedMotion = m_currentMotion;
    bool wasPlaying = m_playing;

    // Drop the existing renderer before we tear down its GL context.
    // setupRenderer() below calls CreateRenderer() which overwrites the
    // base-class _renderer pointer; without an explicit Delete the old
    // one leaks on every recovery (GPU power-state change, DWM restart).
    // Audit C6.
    if (m_cubismModel) {
        m_cubismModel->DeleteRenderer();
    }

    releaseOpenGL();

    if (!initOpenGL()) {
        qWarning() << "Live2D: recoverOpenGL: initOpenGL failed";
        return false;
    }

    if (!m_glContext->makeCurrent(m_surface)) {
        qWarning() << "Live2D: recoverOpenGL: makeCurrent failed";
        return false;
    }

    m_cubismModel->setupRenderer(m_renderWidth, m_renderHeight);
    m_glContext->doneCurrent();

    // M11: Restart the saved motion so playback resumes after GL context
    // rebuild. Without this, recoverOpenGL() saves the current motion but
    // never restarts it, so the pet freezes after a GPU power-state change.
    m_currentMotion = savedMotion;
    m_playing = wasPlaying;
    m_lastPaintSuccessful = true;
    if (wasPlaying && !savedMotion.isEmpty() && m_cubismModel) {
        int count = m_cubismModel->motionCount(savedMotion);
        if (count > 0) {
            m_cubismModel->startMotion(savedMotion, 0, 3);
        }
    }

    qDebug() << "Live2D: OpenGL recovery complete";
    return true;
}

void Live2DAnimationEngine::releaseModel()
{
    delete m_cubismModel;
    m_cubismModel = nullptr;
    m_modelLoaded = false;
    m_motionGroups.clear();
    m_idleAnims.clear();
    m_idleWeights.clear();
    m_characterBounds = QRect();
}

bool Live2DAnimationEngine::loadFromCharacterPack(const CharacterPack *pack)
{
    if (!pack || !pack->isValid()) return false;
    if (pack->characterConfig().engineType != CharacterPack::EngineType::Live2D) return false;

    // Set render dimensions from pack. Clamp to a reasonable range:
    // a manifest claiming 0 or a negative size crashes inside the GPU
    // driver during FBO creation; a manifest claiming 30000x30000
    // exhausts GPU memory. 4096 is the conservative floor across modern
    // GPU max-texture limits.
    static constexpr int kMinDim = 1;
    static constexpr int kMaxDim = 4096;
    m_renderWidth  = qBound(kMinDim, pack->characterConfig().frameWidth,  kMaxDim);
    m_renderHeight = qBound(kMinDim, pack->characterConfig().frameHeight, kMaxDim);

    // Initialize OpenGL
    qDebug() << "Live2D: initOpenGL()...";
    if (!initOpenGL()) {
        qWarning() << "Live2D: initOpenGL() failed";
        return false;
    }
    m_glContext->makeCurrent(m_surface);
    qDebug() << "Live2D: GL context current, GLEW version:" << reinterpret_cast<const char *>(glewGetString(GLEW_VERSION));

    // Recreate FBO if size changed
    if (m_fbo && (m_fbo->width() != m_renderWidth || m_fbo->height() != m_renderHeight)) {
        delete m_fbo;
        m_fbo = new QOpenGLFramebufferObject(m_renderWidth, m_renderHeight,
                                              QOpenGLFramebufferObject::CombinedDepthStencil);
    }

    // Release previous model
    releaseModel();

    // Load model
    QString modelJsonPath = pack->modelJsonPath();
    if (modelJsonPath.isEmpty()) {
        qWarning() << "Live2D: No model JSON path in pack";
        return false;
    }

    qDebug() << "Live2D: Loading model from" << modelJsonPath;
    QString modelDir = QFileInfo(modelJsonPath).absolutePath();
    m_cubismModel = new CubismModel();
    if (!m_cubismModel->load(modelJsonPath, modelDir)) {
        qWarning() << "Live2D: Failed to load model from" << modelJsonPath;
        releaseModel();
        return false;
    }
    qDebug() << "Live2D: Model loaded, setting up renderer...";

    // Setup renderer (needs GL context)
    m_cubismModel->setupRenderer(m_renderWidth, m_renderHeight);

    // Populate motion group list
    m_motionGroups = m_cubismModel->motionGroupNames();
    m_modelLoaded = true;

    // Build idle pool from pack — but skip entries whose group has zero
    // motions in this model. Without this filter, an idlePool that lists
    // (say) Wedding wastes its weight roll on packs that don't ship a
    // Wedding group, producing dead air every Nth idle tick.
    const auto &idlePool = pack->idlePool();
    for (const auto &entry : idlePool) {
        if (!m_cubismModel || m_cubismModel->motionCount(entry.animationName) <= 0) {
            qDebug() << "Live2D: idlePool skipping missing group:" << entry.animationName;
            continue;
        }
        m_idleAnims.append(entry.animationName);
        m_idleWeights.append(entry.weight);
    }

    // If no idle pool entries survived (or none were declared), fall back
    // to whatever the model has so the pet doesn't sit motionless. Order:
    //   "Idle" → "Tap" → first non-empty group of any kind.
    // Sparse VTube Studio packs (yumi has only Tap) and Cubism Free Sample
    // packs (Idle only) both wind up with at least one cycling group.
    if (m_idleAnims.isEmpty()) {
        for (const QString &candidate : {QStringLiteral("Idle"), QStringLiteral("Tap")}) {
            if (m_motionGroups.contains(candidate)
                && m_cubismModel->motionCount(candidate) > 0) {
                m_idleAnims.append(candidate);
                m_idleWeights.append(1);
                break;
            }
        }
    }
    if (m_idleAnims.isEmpty()) {
        for (const QString &g : m_motionGroups) {
            if (!g.isEmpty() && m_cubismModel->motionCount(g) > 0) {
                m_idleAnims.append(g);
                m_idleWeights.append(1);
                break;
            }
        }
    }

    // Start idle
    m_playing = true;
    m_lastTickMs = 0;
    m_timer.start();
    startIdleAnimation();

    qDebug() << "Live2D: Loaded model with" << m_motionGroups.size() << "motion groups:"
             << m_motionGroups;
    return true;
}

void Live2DAnimationEngine::playAnimation(const QString &name, Priority priority)
{
    if (!m_modelLoaded || !m_cubismModel) return;

    m_idleTimer.stop();
    int count = m_cubismModel->motionCount(name);
    if (count <= 0) {
        qWarning() << "Live2D: Motion group not found:" << name;
        return;
    }

    // Don't re-trigger the same group while it's still playing. Without this,
    // a chatty event stream where every active event maps to "Tap" (or every
    // idle event maps to "Idle") restarts the motion every ~1.8s and the user
    // only ever sees the opening frames — the "small repeat movements" feel.
    // Different group requests still apply Cubism's priority rules normally.
    if (name == m_currentMotion && !m_cubismModel->isMotionFinished()) {
        return;
    }

    int no = QRandomGenerator::global()->bounded(count);
    int cubismPriority = (priority == HighPriority) ? 3 : 2;  // Force=3, Normal=2

    m_cubismModel->startMotion(name, no, cubismPriority);
    m_currentMotion = name;
    m_playing = true;

    if (priority == HighPriority) {
        m_queue.clear();
    }
}

void Live2DAnimationEngine::stop()
{
    m_timer.stop();
    m_idleTimer.stop();
    m_playing = false;
    m_queue.clear();
    m_currentMotion.clear();
    m_image = QImage();  // clear last rendered frame so paint() returns early
    m_characterBounds = QRect();
    m_lastPaintSuccessful = false;
}

void Live2DAnimationEngine::setPointerTarget(float x, float y)
{
    if (m_cubismModel) m_cubismModel->setDragging(x, y);
}

void Live2DAnimationEngine::playAnimationChain(const QStringList &chain, Priority priority)
{
    if (!m_modelLoaded || !m_cubismModel) return;
    // Try each group in order; play the first one that has motions for the
    // loaded model. The engine has zero knowledge of what specific group
    // names mean — the manifest's eventMap declares the chain, this is just
    // the dispatcher. If every group in the chain is missing, do nothing.
    for (const QString &name : chain) {
        if (name.isEmpty()) continue;
        if (m_cubismModel->motionCount(name) > 0) {
            playAnimation(name, priority);
            return;
        }
    }
}

QRect Live2DAnimationEngine::characterBounds() const
{
    if (!m_characterBounds.isNull() || m_image.isNull()) {
        return m_characterBounds;
    }
    // Alpha bounding box scan over the FBO-readback image.
    // m_image is Format_ARGB32_Premultiplied: 4 bytes/px, alpha is the top byte of each uint.
    const int w = m_image.width();
    const int h = m_image.height();
    int top = -1, bottom = -1, left = w, right = -1;
    for (int y = 0; y < h; ++y) {
        const auto *row = reinterpret_cast<const quint32 *>(m_image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if ((row[x] >> 24) != 0) {
                if (top < 0) top = y;
                bottom = y;
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
    }
    if (top < 0) return QRect();
    m_characterBounds = QRect(left, top, right - left + 1, bottom - top + 1);
    return m_characterBounds;
}

void Live2DAnimationEngine::paint(QPainter *painter, const QRect &bounds)
{
    if (!m_modelLoaded || m_image.isNull()) return;
    // Use the full render width (motion can swing arms / hair beyond the
    // static alpha bbox — cropping horizontally clips those frames) and only
    // crop the transparent top margin. A generous vertical pad keeps head-
    // bob, arm-raise, and any glow/lighting effect above the character
    // inside the kept region.
    QRect src = m_image.rect();
    if (!m_characterBounds.isNull()) {
        const int padTop = std::max(32, m_characterBounds.height() / 4);
        const int y = std::max(0, m_characterBounds.y() - padTop);
        const int h = std::min(m_image.height() - y,
                               m_characterBounds.height() + padTop);
        src = QRect(0, y, m_image.width(), h);
    }
    painter->drawImage(bounds, m_image, src);
}

void Live2DAnimationEngine::tick()
{
    if (!m_modelLoaded || !m_cubismModel || !m_glContext || !m_fbo) return;

    // GPU power-state changes, display adapter resets, GPU switches (eGPU
    // attach/detach on macOS), and DWM restarts on Windows can all
    // invalidate the OpenGL context. The recovery path was originally
    // Windows-only, but macOS triggers context loss too — most reliably
    // on lid-close / display sleep / external-display unplug. Run the
    // validity check on every platform; recoverOpenGL() is a no-op when
    // the context is fine because it tears down and rebuilds GL state
    // unconditionally (so guard with the explicit isValid() check). M11.
    if (!m_glContext->isValid()) {
        qWarning() << "Live2D: GL context lost — attempting recovery";
        if (!recoverOpenGL()) {
            qWarning() << "Live2D: GL recovery failed — stopping engine";
            stop();
            return;
        }
    }

    // Calculate delta time
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    float deltaSeconds = 0.016f;  // default 16ms
    if (m_lastTickMs > 0) {
        deltaSeconds = (now - m_lastTickMs) / 1000.0f;
        if (deltaSeconds > 0.1f) deltaSeconds = 0.1f;  // clamp to 100ms max
    }

    // Make GL context current
    if (!m_glContext->makeCurrent(m_surface)) {
        qWarning() << "Live2D: makeCurrent failed";
        m_lastPaintSuccessful = false;
        return;
    }

    // M9: Only update m_lastTickMs after makeCurrent succeeds. Previously
    // it was set before the makeCurrent check, so a failed makeCurrent
    // caused the next successful tick to skip the delta-time accumulation
    // (the gap was "swallowed").
    m_lastTickMs = now;

    // Update model
    m_cubismModel->update(deltaSeconds);

    // Check if current motion finished → start idle
    if (m_cubismModel->isMotionFinished()) {
        if (!m_queue.isEmpty()) {
            startNextAnimation();
        } else {
            m_idleTimer.start();
        }
    }

    // Render to FBO
    renderFrame();

    // Release the offscreen context so Qt's own GL context can be bound
    // for widget compositing (translucent frameless window).
    m_glContext->doneCurrent();

    emit frameChanged();
}

void Live2DAnimationEngine::renderFrame()
{
    if (!m_fbo || !m_cubismModel) {
        m_lastPaintSuccessful = false;
        return;
    }

    if (!m_fbo->isValid()) {
        qWarning() << "Live2D: FBO is invalid";
        m_lastPaintSuccessful = false;
        return;
    }

    m_fbo->bind();

    glViewport(0, 0, m_renderWidth, m_renderHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // transparent background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_cubismModel->draw(m_renderWidth, m_renderHeight);

    // Read pixels
    m_image = m_fbo->toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);

    m_fbo->release();

    m_lastPaintSuccessful = !m_image.isNull();
}

void Live2DAnimationEngine::startNextAnimation()
{
    if (!m_queue.isEmpty()) {
        QString next = m_queue.takeFirst();
        playAnimation(next, NormalPriority);
    } else {
        // Vary the gap between idle motions ±50% so the pet doesn't feel
        // mechanical. m_idleTimeoutMs is the midpoint; actual interval lands
        // in [0.5x, 1.5x] uniformly.
        const int half = m_idleTimeoutMs / 2;
        const int jitter = QRandomGenerator::global()->bounded(2 * half + 1) - half;
        m_idleTimer.setInterval(m_idleTimeoutMs + jitter);
        m_idleTimer.start();
    }
}

void Live2DAnimationEngine::startIdleAnimation()
{
    if (m_idleAnims.isEmpty()) return;

    int totalWeight = 0;
    for (int w : m_idleWeights) totalWeight += w;
    if (totalWeight <= 0) return;

    int roll = QRandomGenerator::global()->bounded(totalWeight);
    int cumulative = 0;
    for (int i = 0; i < m_idleAnims.size(); ++i) {
        cumulative += m_idleWeights.at(i);
        if (roll < cumulative) {
            playAnimation(m_idleAnims.at(i), NormalPriority);
            return;
        }
    }
    playAnimation(m_idleAnims.first(), NormalPriority);
}

#endif // SEELIE_LIVE2D_SUPPORT
