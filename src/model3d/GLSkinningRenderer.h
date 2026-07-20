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
    void release();

    int maxJoints() const { return m_maxJoints; }  // from uniform-limit query
    bool modelTooLarge() const { return m_modelTooLarge; } // joints > maxJoints

    // Manifest overrides (0 = auto).
    void setCameraOverrides(float distance, float height);
    void setModelTransform(const QMatrix4x4 &m) { m_modelMatrix = m; } // upAxis/unitScale

private:
    struct PrimitiveGL {
        GLuint vbo = 0, ebo = 0;
        int indexCount = 0;
        int material = -1;
        bool unlit = false;          // KHR_materials_unlit
        bool skinned = false;        // node had a skin -> use joint palette
        int attachedJoint = -1;      // rigid: nearest joint ancestor
        QMatrix4x4 attachTransform;  // rigid: chain from attachedJoint to mesh node
    };
    void fitCameraToBindPose(const Model3DModel &model);

    QOpenGLShaderProgram *m_program = nullptr;
    QVector<PrimitiveGL> m_prims;
    QVector<GLuint> m_textures;      // parallel to model materials
    QMatrix4x4 m_proj, m_view, m_modelMatrix;
    float m_camDistance = 0.0f, m_camHeight = 0.0f;
    float m_fitDistance = 1.0f, m_fitCenterY = 0.5f;
    int m_maxJoints = 64;
    bool m_modelTooLarge = false;
    int m_jointCount = 0;
};

#endif // GLSKINNING_RENDERER_H
