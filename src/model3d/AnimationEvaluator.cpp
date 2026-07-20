#include "AnimationEvaluator.h"

#include <cmath>

namespace {

// Index of the last keyframe with time <= t.
int findKey(const QVector<float> &times, float t)
{
    int lo = 0, hi = times.size() - 1;
    if (t <= times.first()) return 0;
    if (t >= times.last()) return hi;
    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;
        if (times[mid] <= t) lo = mid; else hi = mid;
    }
    return lo;
}

QVector3D makeVec3(const float *p) { return QVector3D(p[0], p[1], p[2]); }
QQuaternion makeQuat(const float *p) { return QQuaternion(p[3], p[0], p[1], p[2]).normalized(); }

void sampleTrack(const Model3DTrack &track, float t,
                 QVector3D &outT, QQuaternion &outR, QVector3D &outS)
{
    const int comps = track.path == Model3DTrack::Rotation ? 4 : 3;
    const int k = findKey(track.times, t);
    float f = 0.0f;
    if (k + 1 < track.times.size()) {
        const float dt = track.times[k + 1] - track.times[k];
        if (dt > 1e-6f) f = qBound(0.0f, (t - track.times[k]) / dt, 1.0f);
    }
    const float *a = track.values.constData() + k * comps;
    const float *b = track.values.constData() + qMin(k + 1, int(track.times.size()) - 1) * comps;
    switch (track.path) {
    case Model3DTrack::Translation:
        outT = makeVec3(a) + (makeVec3(b) - makeVec3(a)) * f;
        break;
    case Model3DTrack::Scale:
        outS = makeVec3(a) + (makeVec3(b) - makeVec3(a)) * f;
        break;
    case Model3DTrack::Rotation:
        outR = QQuaternion::slerp(makeQuat(a), makeQuat(b), f);
        break;
    }
}

} // namespace

void AnimationEvaluator::evaluate(const Model3DModel &model,
                                  const Model3DClip *clip,
                                  float timeSec,
                                  bool loop,
                                  QVector<QMatrix4x4> &outPalette)
{
    const int nj = model.joints.size();
    outPalette.resize(nj);
    if (nj == 0) return;

    float t = 0.0f;
    if (clip && clip->duration > 1e-6f) {
        if (loop) {
            t = std::fmod(timeSec, clip->duration);
            if (t < 0.0f) t += clip->duration;
        } else {
            t = qBound(0.0f, timeSec, clip->duration);
        }
    }

    // Local pose per joint, starting from bind pose.
    QVector<QVector3D> localT(nj), localS(nj);
    QVector<QQuaternion> localR(nj);
    for (int i = 0; i < nj; ++i) {
        localT[i] = model.joints[i].bindT;
        localR[i] = model.joints[i].bindR;
        localS[i] = model.joints[i].bindS;
    }

    if (clip) {
        for (const Model3DTrack &track : clip->tracks) {
            if (track.joint < 0 || track.joint >= nj || track.times.isEmpty()) continue;
            sampleTrack(track, t, localT[track.joint], localR[track.joint], localS[track.joint]);
        }
    }

    // Root-motion clamp: pin root XZ translation to bind pose (Y preserved).
    for (int i = 0; i < nj; ++i) {
        if (model.joints[i].parent == -1) {
            localT[i].setX(model.joints[i].bindT.x());
            localT[i].setZ(model.joints[i].bindT.z());
        }
    }

    // Hierarchy walk. cgltf skin joints are not guaranteed parent-before-child,
    // so iterate until all globals are resolved (small n, trivially fast).
    QVector<QMatrix4x4> global(nj);
    QVector<bool> done(nj, false);
    for (int remaining = nj; remaining > 0;) {
        bool progress = false;
        for (int i = 0; i < nj; ++i) {
            if (done[i]) continue;
            const int p = model.joints[i].parent;
            if (p >= 0 && !done[p]) continue;
            QMatrix4x4 local;
            local.translate(localT[i]);
            local.rotate(localR[i]);
            local.scale(localS[i]);
            global[i] = (p >= 0 ? global[p] : QMatrix4x4()) * local;
            done[i] = true;
            --remaining;
            progress = true;
        }
        if (!progress) break; // cycle — malformed skin, bail with partial pose
    }

    for (int i = 0; i < nj; ++i)
        outPalette[i] = global[i] * model.joints[i].inverseBind;
}
