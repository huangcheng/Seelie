#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "Model3DTypes.h"

// Loads a .glb (binary glTF) into plain Model3DModel structs via vendored
// cgltf. Pure parsing — no GL, no Qt event loop. Thread-unsafe per call;
// call from the GUI thread.
class GltfLoader
{
public:
    static bool loadFromFile(const QString &path, Model3DModel &out, QString *error);
};

#endif // GLTF_LOADER_H
