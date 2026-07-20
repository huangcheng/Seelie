#ifndef MODEL3D_ANIMATION_EVALUATOR_H
#define MODEL3D_ANIMATION_EVALUATOR_H

#include "Model3DTypes.h"

// Evaluates skeletal animation on the CPU: samples a clip's TRS tracks at a
// local time, walks the joint hierarchy, multiplies by inverse bind matrices
// and outputs the skinning palette. Pure math — no GL, unit-testable.
//
// Clip semantics (spec, council-amended):
//  - loop=true  -> time wraps into [0, duration)
//  - loop=false -> time clamps to [0, duration] (one-shot; caller detects end)
//  - duration==0 clip -> static pose at first keyframe
//  - joints missing from the clip hold bind pose (never snap to zero)
//  - root-motion clamp: root joint (parent==-1) XZ translation pinned to bind
class AnimationEvaluator
{
public:
    // clip may be nullptr -> bind pose. outPalette resized to joint count.
    // If outGlobal is non-null it receives the joint world transforms BEFORE
    // the inverse-bind multiply (needed for rigid bone-parented primitives).
    static void evaluate(const Model3DModel &model,
                         const Model3DClip *clip,
                         float timeSec,
                         bool loop,
                         QVector<QMatrix4x4> &outPalette,
                         QVector<QMatrix4x4> *outGlobal = nullptr);
};

#endif // MODEL3D_ANIMATION_EVALUATOR_H
