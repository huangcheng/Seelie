#include "GLSkinningRenderer.h"
#include "AnimationEvaluator.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QDebug>
#include <QtMath>
#include <cmath>

namespace {

// GLSL 120 (GL 2.1 compatibility profile, matches Live2D's context format).
// Skinning with a mat4 joint palette; unskinned models take the identity path
// via a single identity palette entry uploaded by render().
const char *kVertShader = R"GLSL(
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUV;
attribute vec4 aJoints;
attribute vec4 aWeights;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uJoints[64];
varying vec2 vUV;
varying vec3 vNormal;
void main() {
    ivec4 j = ivec4(int(aJoints.x), int(aJoints.y), int(aJoints.z), int(aJoints.w));
    mat4 skin = uJoints[j.x] * aWeights.x + uJoints[j.y] * aWeights.y
              + uJoints[j.z] * aWeights.z + uJoints[j.w] * aWeights.w;
    vec4 posed = skin * vec4(aPos, 1.0);
    gl_Position = uMVP * posed;
    mat3 skin3 = mat3(skin[0].xyz, skin[1].xyz, skin[2].xyz);
    mat3 model3 = mat3(uModel[0].xyz, uModel[1].xyz, uModel[2].xyz);
    vNormal = normalize(model3 * (skin3 * aNormal));
    vUV = aUV;
}
)GLSL";

// sRGB: decode baseColor, apply hemisphere lighting, re-encode for output.
// (GL_FRAMEBUFFER_SRGB is not guaranteed on a 2.1 compat context.)
const char *kFragShader = R"GLSL(
uniform sampler2D uTexture;
uniform int uHasTexture;
uniform int uUnlit;
uniform vec4 uBaseColor;
varying vec2 vUV;
varying vec3 vNormal;
void main() {
    vec4 tex = (uHasTexture == 1)
        ? pow(texture2D(uTexture, vUV), vec4(2.2, 2.2, 2.2, 1.0))
        : vec4(1.0);
    tex *= uBaseColor;
    float hemi = 1.0;
    if (uUnlit == 0) {
        // Hemisphere light: sky from +Y, ground bounce, no hard terminator —
        // forgiving on stylized models.
        hemi = 0.75 + 0.25 * vNormal.y;
    }
    vec3 linear = tex.rgb * hemi;
    gl_FragColor = vec4(pow(linear, vec3(1.0 / 2.2)), tex.a);
}
)GLSL";

} // namespace

GLSkinningRenderer::~GLSkinningRenderer()
{
    // GL resources must already be freed via release() while a context was
    // current; the destructor cannot make GL calls safely.
}

bool GLSkinningRenderer::initialize(QString *error)
{
    initializeOpenGLFunctions();

    // Uniform-limit query: GL 2.1 guarantees only 512 vertex uniform
    // components (vec4s). uJoints[64] costs 256; reserve headroom for the
    // other uniforms on near-limit drivers (llvmpipe, old macOS).
    GLint maxComponents = 0;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxComponents);
    m_maxJoints = qBound(8, (maxComponents - 64) / 4, 64);

    m_program = new QOpenGLShaderProgram;
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertShader) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragShader) ||
        !m_program->link()) {
        if (error) *error = m_program->log();
        qWarning() << "Model3D: shader compile/link failed:" << m_program->log();
        delete m_program;
        m_program = nullptr;
        return false;
    }
    return true;
}

void GLSkinningRenderer::upload(const Model3DModel &model)
{
    release();

    m_jointCount = model.joints.size();
    m_modelTooLarge = m_jointCount > m_maxJoints;
    if (m_modelTooLarge) {
        // Loud warning — Mixamo rigs often exceed 64 joints, and silently
        // clamping would drop finger/leaf joints. See docs/model3d-packs.md.
        qWarning() << "Model3D: model has" << m_jointCount
                   << "joints but this GL stack supports at most" << m_maxJoints
                   << "— animation will be degraded. Reduce the rig's joint count.";
    }

    for (const Model3DPrimitive &p : model.primitives) {
        PrimitiveGL g;
        g.material = p.material;
        g.unlit = p.material >= 0 && p.material < model.materials.size()
                  && model.materials[p.material].unlit;
        if (p.material >= 0 && p.material < model.materials.size())
            for (int c = 0; c < 4; ++c)
                g.color[c] = model.materials[p.material].baseColorFactor[c];
        g.indexCount = p.indices.size();
        g.skinned = p.skinned;
        g.attachedJoint = p.attachedJoint;
        g.attachTransform = p.attachTransform;
        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, p.vertices.size() * int(sizeof(Model3DVertex)),
                     p.vertices.constData(), GL_STATIC_DRAW);
        glGenBuffers(1, &g.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, p.indices.size() * int(sizeof(uint32_t)),
                     p.indices.constData(), GL_STATIC_DRAW);
        m_prims.append(g);
    }

    for (const Model3DMaterial &m : model.materials) {
        GLuint tex = 0;
        if (!m.baseColor.isNull()) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const QImage img = m.baseColor.convertToFormat(QImage::Format_RGBA8888);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        }
        m_textures.append(tex);
    }

    fitCameraToBindPose(model);  // O(joints + vertices), once per pack switch
}

void GLSkinningRenderer::fitCameraToBindPose(const Model3DModel &model)
{
    // Bind-pose bbox in WORLD space (NOT per-frame — an animated bbox makes
    // the camera jump). For models with armature-local vertices (Blender
    // exports like RobotExpressive), raw vertex positions are tiny and the
    // palette/attachTransform must be applied to get world coordinates.
    QVector<QMatrix4x4> palette, globals;
    AnimationEvaluator::evaluate(model, nullptr, 0.0f, false, palette, &globals);

    QVector3D mn( 1e9f,  1e9f,  1e9f), mx(-1e9f, -1e9f, -1e9f);
    for (const Model3DPrimitive &p : model.primitives) {
        QMatrix4x4 rigidXform;
        if (!p.skinned) {
            rigidXform = (p.attachedJoint >= 0 && p.attachedJoint < globals.size())
                ? globals[p.attachedJoint] * p.attachTransform
                : p.attachTransform;
        }
        for (const Model3DVertex &v : p.vertices) {
            QVector3D q;
            if (p.skinned) {
                QVector4D sum(0, 0, 0, 0);
                for (int k = 0; k < 4; ++k) {
                    if (v.weights[k] <= 0.0f) continue;
                    const int j = v.joints[k];
                    if (j < 0 || j >= palette.size()) continue;
                    const QVector4D pv = palette[j].map(QVector4D(v.px, v.py, v.pz, 1.0f));
                    sum += pv * v.weights[k];
                }
                q = sum.toVector3D();
            } else {
                q = rigidXform.map(QVector3D(v.px, v.py, v.pz));
            }
            mn = QVector3D(qMin(mn.x(), q.x()), qMin(mn.y(), q.y()), qMin(mn.z(), q.z()));
            mx = QVector3D(qMax(mx.x(), q.x()), qMax(mx.y(), q.y()), qMax(mx.z(), q.z()));
        }
    }
    if (mn.x() > mx.x()) {   // no vertices — degenerate model
        m_fitDistance = 5.0f;
        m_fitCenterY = 0.5f;
    } else {
        const QVector3D ext = mx - mn;
        const float maxDim = qMax(ext.x(), qMax(ext.y(), ext.z()));
        const QVector3D center = (mn + mx) * 0.5f;   // recenters off-origin geometry
        const float fov = qDegreesToRadians(30.0f);
        m_fitDistance = (maxDim * 0.5f) / std::tan(fov * 0.5f) * 1.2f;
        m_fitCenterY = center.y();
    }
    m_view.setToIdentity();
    const float dist = m_camDistance > 0.0f ? m_camDistance : m_fitDistance;
    const float height = m_camHeight != 0.0f ? m_camHeight : m_fitCenterY;
    m_view.lookAt(QVector3D(0, height, dist),
                  QVector3D(0, height, 0),
                  QVector3D(0, 1, 0));
}

void GLSkinningRenderer::setCameraOverrides(float distance, float height)
{
    m_camDistance = distance;
    m_camHeight = height;
}

void GLSkinningRenderer::render(QOpenGLFramebufferObject *fbo,
                                const QVector<QMatrix4x4> &palette,
                                const QVector<QMatrix4x4> &globalJoints)
{
    if (!m_program || !fbo) return;

    fbo->bind();
    glViewport(0, 0, fbo->size().width(), fbo->size().height());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);   // transparent for compositing
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // FBO->toImage() handles premultiply
    glEnable(GL_CULL_FACE);

    m_proj.setToIdentity();
    m_proj.perspective(30.0f, float(fbo->size().width()) / float(fbo->size().height()),
                       0.01f, 100.0f);
    const QMatrix4x4 mvp = m_proj * m_view * m_modelMatrix;

    m_program->bind();
    m_program->setUniformValue("uMVP", mvp);
    m_program->setUniformValue("uModel", m_modelMatrix);

    // Per-primitive palette upload:
    //  - skinned primitives use the full joint palette (capped to m_maxJoints);
    //  - rigid primitives upload a 1-entry palette = globalJoints[attachedJoint]
    //    * attachTransform (or just attachTransform if no joint ancestor),
    //    reused via slot 0 since static verts have weight[0]=1, joint[0]=0.
    QMatrix4x4 joints[64];

    const int aPos = m_program->attributeLocation("aPos");
    const int aNrm = m_program->attributeLocation("aNormal");
    const int aUV  = m_program->attributeLocation("aUV");
    const int aJnt = m_program->attributeLocation("aJoints");
    const int aWgt = m_program->attributeLocation("aWeights");
    m_program->setUniformValue("uTexture", 0);

    for (const PrimitiveGL &g : m_prims) {
        if (g.skinned) {
            const int n = qMin(qMin(palette.size(), m_maxJoints), 64);
            for (int i = 0; i < qMax(n, 1); ++i)
                joints[i] = (i < n) ? palette[i] : QMatrix4x4();
            m_program->setUniformValueArray("uJoints", joints, qMax(n, 1));
        } else {
            joints[0] = (g.attachedJoint >= 0 && g.attachedJoint < globalJoints.size())
                ? globalJoints[g.attachedJoint] * g.attachTransform
                : g.attachTransform;
            m_program->setUniformValueArray("uJoints", joints, 1);
        }

        const bool hasTex = g.material >= 0 && g.material < m_textures.size()
                            && m_textures[g.material] != 0;
        m_program->setUniformValue("uHasTexture", hasTex ? 1 : 0);
        m_program->setUniformValue("uUnlit", g.unlit ? 1 : 0);
        m_program->setUniformValue("uBaseColor", g.color[0], g.color[1], g.color[2], g.color[3]);
        if (hasTex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_textures[g.material]);
        }
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        const int stride = int(sizeof(Model3DVertex));
        glEnableVertexAttribArray(aPos);
        glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(Model3DVertex, px)));
        glEnableVertexAttribArray(aNrm);
        glVertexAttribPointer(aNrm, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(Model3DVertex, nx)));
        glEnableVertexAttribArray(aUV);
        glVertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(Model3DVertex, u)));
        glEnableVertexAttribArray(aJnt);
        glVertexAttribPointer(aJnt, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(Model3DVertex, joints)));
        glEnableVertexAttribArray(aWgt);
        glVertexAttribPointer(aWgt, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(Model3DVertex, weights)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glDisableVertexAttribArray(aPos);
    glDisableVertexAttribArray(aNrm);
    glDisableVertexAttribArray(aUV);
    glDisableVertexAttribArray(aJnt);
    glDisableVertexAttribArray(aWgt);

    m_program->release();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    fbo->release();
}

void GLSkinningRenderer::release()
{
    for (PrimitiveGL &g : m_prims) {
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
        if (g.ebo) glDeleteBuffers(1, &g.ebo);
    }
    m_prims.clear();
    for (GLuint t : m_textures)
        if (t) glDeleteTextures(1, &t);
    m_textures.clear();
    delete m_program;
    m_program = nullptr;
}
