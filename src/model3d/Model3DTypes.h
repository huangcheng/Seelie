#ifndef MODEL3D_TYPES_H
#define MODEL3D_TYPES_H

#include <QVector>
#include <QVector3D>
#include <QQuaternion>
#include <QMatrix4x4>
#include <QImage>
#include <QString>
#include <QMap>
#include <cstdint>

// Plain-data model representation shared by GltfLoader, AnimationEvaluator
// and GLSkinningRenderer. No GL types — this header is unit-testable without
// a context, and a future ufbx loader can produce the same structs.

struct Model3DVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    uint8_t joints[4];
    float weights[4];
};

struct Model3DPrimitive {
    QVector<Model3DVertex> vertices;
    QVector<uint32_t> indices;
    int material = -1;
};

struct Model3DJoint {
    QString name;
    int parent = -1;                 // index into joints, -1 = root
    QVector3D bindT;                 // local bind TRS
    QQuaternion bindR;
    QVector3D bindS{1, 1, 1};
    QMatrix4x4 inverseBind;
};

struct Model3DTrack {
    enum Path { Translation, Rotation, Scale };
    int joint = -1;
    Path path = Translation;
    QVector<float> times;            // seconds, ascending
    QVector<float> values;           // vec3 (3/t) or quat xyzw (4/t)
};

struct Model3DClip {
    QString name;
    float duration = 0.0f;
    QVector<Model3DTrack> tracks;
};

struct Model3DMaterial {
    QImage baseColor;                // may be null -> default white
    bool unlit = false;              // KHR_materials_unlit
};

struct Model3DModel {
    QVector<Model3DPrimitive> primitives;
    QVector<Model3DJoint> joints;
    QVector<Model3DClip> clips;
    QMap<QString, int> clipIndexByName;
    QVector<Model3DMaterial> materials;
    bool hasSkin() const { return !joints.isEmpty(); }
};

#endif // MODEL3D_TYPES_H
