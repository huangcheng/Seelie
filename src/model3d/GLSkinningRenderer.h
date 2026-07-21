#ifndef GLSKINNING_RENDERER_H
#define GLSKINNING_RENDERER_H

#include "Model3DTypes.h"

#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector>

class QOpenGLShaderProgram;
class QOpenGLFramebufferObject;

// Renders a Model3DModel with a vertex-skinning shader into an FBO.
// Qt GL wrappers only (QOpenGLShaderProgram/QOpenGLBuffer via raw GL calls
// through QOpenGLFunctions) — GLEW is deliberately NOT used: its function
// pointers are process-global and collide with Live2D's GLEW usage.
//
// Lifecycle (all calls require a current QOpenGLContext on this thread):
//   initialize()         — compile shaders, query uniform limits
//   upload(model)        — create VBOs/EBOs/textures, fit camera to bind pose
//   render(fbo, palette) — draw one frame with the given joint palette
//   release()            — free GL resources
class GLSkinningRenderer : protected QOpenGLFunctions
{
public:
    GLSkinningRenderer() = default;
    ~GLSkinningRenderer();

    bool initialize(QString *error);            // requires current context
    void upload(const Model3DModel &model);
    void render(QOpenGLFramebufferObject *fbo,
                const QVector<QMatrix4x4> &palette,
                const QVector<QMatrix4x4> &globalJoints);
    void releaseModelResources();   // per-model GL resources (context current)
    void release();                 // model resources + shader program

    int maxJoints() const { return m_maxJoints; }  // from uniform-limit query
    bool modelTooLarge() const { return m_modelTooLarge; } // joints > maxJoints

    // Manifest overrides (0 = auto).
    void setCameraOverrides(float distance, float height, float yawDeg = 0.0f);
    void setModelTransform(const QMatrix4x4 &m) { m_modelMatrix = m; } // upAxis/unitScale
    // Procedural idle sway: gentle bob + rock composed onto the model matrix.
    // Gives life even to packs whose clips are static poses (pose libraries).
    void setSway(float timeSec, bool active) { m_swaySec = active ? timeSec : -1.0f; }

private:
    struct PrimitiveGL {
        GLuint vbo = 0, ebo = 0;
        int indexCount = 0;
        int material = -1;
        bool unlit = false;          // KHR_materials_unlit
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // baseColorFactor
        bool skinned = false;        // node had a skin -> use joint palette
        int attachedJoint = -1;      // rigid: nearest joint ancestor
        QMatrix4x4 attachTransform;  // rigid: chain from attachedJoint to mesh node
        // CPU-skinning path (models whose joint count exceeds the uniform
        // palette): bind-pose copy + per-frame posed scratch, streamed to vbo.
        QVector<Model3DVertex> bindVerts;
        QVector<Model3DVertex> scratch;
    };
    void fitCameraToBindPose(const Model3DModel &model);
    void poseOnCpu(PrimitiveGL &g, const QVector<QMatrix4x4> &palette);

    QOpenGLShaderProgram *m_program = nullptr;
    QVector<PrimitiveGL> m_prims;
    QVector<GLuint> m_textures;      // parallel to model materials
    QMatrix4x4 m_proj, m_view, m_modelMatrix;
    float m_swaySec = -1.0f;       // <0 = no sway
    float m_camDistance = 0.0f, m_camHeight = 0.0f, m_camYaw = 0.0f;
    float m_fitDistance = 1.0f, m_fitCenterY = 0.5f;
    int m_maxJoints = 64;
    bool m_modelTooLarge = false;
    bool m_cpuSkinning = false;
    int m_jointCount = 0;
};

#endif // GLSKINNING_RENDERER_H
