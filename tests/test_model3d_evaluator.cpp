#include <QtTest>
#include <cmath>
#include "model3d/AnimationEvaluator.h"
#include "model3d/GltfLoader.h"

class TestModel3DEvaluator : public QObject
{
    Q_OBJECT
private slots:
    void bindPoseIsIdentity();
    void waveClipMidpoint();
    void oneShotClampsAtEnd();
    void loopWraps();
    void rootMotionClampedXZ();
    void missingTrackHoldsBindPose();
    void preTransformAppliedToHierarchy();
    void robotBindPoseAppliesArmatureTransform();
};

static Model3DModel loadCube()
{
    Model3DModel m;
    QString err;
    if (!GltfLoader::loadFromFile(QStringLiteral(SOURCE_DIR) + "/tests/data/rig_cube.glb",
                                  m, &err))
        qFatal("test GLB must load: %s", qPrintable(err));
    return m;
}

void TestModel3DEvaluator::bindPoseIsIdentity()
{
    Model3DModel m = loadCube();
    QVector<QMatrix4x4> palette;
    AnimationEvaluator::evaluate(m, nullptr, 0.0f, false, palette);
    QCOMPARE(palette.size(), 2);
    // Bind pose: palette * bind-position == bind-position.
    const QVector3D p(0.5f, 1.0f, 0.0f);
    QVERIFY((palette[1].map(p) - p).length() < 1e-4f);
}

void TestModel3DEvaluator::waveClipMidpoint()
{
    Model3DModel m = loadCube();
    const Model3DClip &wave = m.clips[m.clipIndexByName["Wave"]];
    QVector<QMatrix4x4> palette;
    AnimationEvaluator::evaluate(m, &wave, 0.25f, false, palette);
    // Tip rotated 45deg about Z at t=0.25: (0.5,1,0) -> (~0.3536, ~1.3536, 0)
    const QVector3D r = palette[1].map(QVector3D(0.5f, 1.0f, 0.0f));
    QVERIFY(qAbs(r.x() - 0.3536f) < 1e-3f);
    QVERIFY(qAbs(r.y() - 1.3536f) < 1e-3f);
    QVERIFY(qAbs(r.z()) < 1e-4f);
}

void TestModel3DEvaluator::oneShotClampsAtEnd()
{
    Model3DModel m = loadCube();
    const Model3DClip &wave = m.clips[m.clipIndexByName["Wave"]];
    QVector<QMatrix4x4> palette;
    AnimationEvaluator::evaluate(m, &wave, 99.0f, false, palette);
    const QVector3D r = palette[1].map(QVector3D(0.5f, 1.0f, 0.0f));
    // 90deg about Z: (0.5,0,0) local -> (0,0.5,0); plus T(0,1,0) -> (0,1.5,0)
    QVERIFY(qAbs(r.x()) < 1e-3f);
    QVERIFY(qAbs(r.y() - 1.5f) < 1e-3f);
}

void TestModel3DEvaluator::loopWraps()
{
    Model3DModel m = loadCube();
    const Model3DClip &wave = m.clips[m.clipIndexByName["Wave"]];
    QVector<QMatrix4x4> a, b;
    AnimationEvaluator::evaluate(m, &wave, 0.25f, false, a);
    AnimationEvaluator::evaluate(m, &wave, 0.75f, true, b); // wraps to 0.25
    for (int i = 0; i < 16; ++i)
        QVERIFY(qAbs(a[1].constData()[i] - b[1].constData()[i]) < 1e-4f);
}

void TestModel3DEvaluator::rootMotionClampedXZ()
{
    // Synthetic model: root joint with a translation track moving +1x.
    Model3DModel m;
    m.joints.resize(1);
    m.joints[0].name = QStringLiteral("Root");
    m.joints[0].parent = -1;
    Model3DClip clip;
    clip.name = QStringLiteral("Walk");
    clip.duration = 1.0f;
    Model3DTrack t;
    t.joint = 0;
    t.path = Model3DTrack::Translation;
    t.times = {0.0f, 1.0f};
    t.values = {0,0,0, 1,0.5f,1};
    clip.tracks.append(t);
    m.clips.append(clip);
    QVector<QMatrix4x4> palette;
    AnimationEvaluator::evaluate(m, &m.clips[0], 1.0f, false, palette);
    // X and Z clamped to bind (0), Y (bob) preserved at 0.5.
    const QVector3D r = palette[0].map(QVector3D(0, 0, 0));
    QVERIFY(qAbs(r.x()) < 1e-4f);
    QVERIFY(qAbs(r.z()) < 1e-4f);
    QVERIFY(qAbs(r.y() - 0.5f) < 1e-4f);
}

void TestModel3DEvaluator::missingTrackHoldsBindPose()
{
    Model3DModel m = loadCube();
    const Model3DClip &wave = m.clips[m.clipIndexByName["Wave"]]; // touches Tip only
    QVector<QMatrix4x4> palette;
    AnimationEvaluator::evaluate(m, &wave, 0.25f, false, palette);
    // Root has no track in Wave: palette[0] stays identity (bind).
    QVERIFY(palette[0].isIdentity());
}

void TestModel3DEvaluator::preTransformAppliedToHierarchy()
{
    // Synthetic model with a non-identity preTransform on the root joint.
    // Verifies the evaluator folds preTransform into the hierarchy walk.
    Model3DModel m;
    m.joints.resize(1);
    m.joints[0].name = QStringLiteral("Root");
    m.joints[0].parent = -1;
    m.joints[0].inverseBind = QMatrix4x4(); // identity
    m.joints[0].preTransform.scale(2.5f);   // pretend a non-joint ancestor scaled ×2.5
    QVector<QMatrix4x4> palette, globals;
    AnimationEvaluator::evaluate(m, nullptr, 0.0f, false, palette, &globals);
    // palette = global × IBM = (preTransform × local) × identity = preTransform.
    QCOMPARE(palette.size(), 1);
    QCOMPARE(globals.size(), 1);
    QVERIFY2(qAbs(palette[0](0, 0) - 2.5f) < 1e-5f,
             qPrintable(QStringLiteral("palette[0](0,0) = %1 (expected 2.5)").arg(palette[0](0, 0))));
    QVERIFY2(qAbs(globals[0](0, 0) - 2.5f) < 1e-5f,
             qPrintable(QStringLiteral("global[0](0,0) = %1 (expected 2.5)").arg(globals[0](0, 0))));
}

void TestModel3DEvaluator::robotBindPoseAppliesArmatureTransform()
{
    // RobotExpressive (Quaternius/Blender, CC0) exposes the bug: the
    // RobotArmature node carries scale(100) + rotX(-90°) and is NOT a skin
    // joint. Without applying preTransform in the hierarchy walk, joint
    // globals stay armature-local while the raw vertices are in armature-
    // local space (±0.03) — the near-identity palette maps them through
    // unchanged and the robot renders as an invisible dot.
    //
    // With the fix, palette[i] inherits the armature ×100 scale and vertices
    // land in world space. The armature-local IBMs (a Blender exporter
    // convention — distinct from the glTF-spec world-space IBMs) mean the
    // palette is NOT identity at bind; it equals preTransform × (local × IBM)
    // which carries the armature scale on every joint.
    Model3DModel m;
    QVERIFY(GltfLoader::loadFromFile(QStringLiteral(SOURCE_DIR) + "/tests/data/robot_expressive.glb",
                                     m, nullptr));
    int rootIdx = -1;
    for (int i = 0; i < m.joints.size(); ++i)
        if (m.joints[i].parent == -1) { rootIdx = i; break; }
    QVERIFY(rootIdx >= 0);
    QVERIFY2(!m.joints[rootIdx].preTransform.isIdentity(),
             "root joint preTransform must be non-identity for the robot "
             "(loader must capture RobotArmature's scale/rotation)");

    QVector<QMatrix4x4> palette, globals;
    AnimationEvaluator::evaluate(m, nullptr, 0.0f, false, palette, &globals);
    QCOMPARE(palette.size(), 43);
    QCOMPARE(globals.size(), 43);
    // At bind, every joint's palette must include the armature ×100 scale.
    // The buggy code produced an identity-like palette (column length ≈ 1).
    for (int i = 0; i < palette.size(); ++i) {
        const float *e = palette[i].constData();
        const float col0Len = std::sqrt(e[0]*e[0] + e[1]*e[1] + e[2]*e[2]);
        QVERIFY2(col0Len > 50.0f,
                 qPrintable(QStringLiteral("joint %1 palette column 0 length = %2 "
                                           "(expected >50 — armature scale missing)")
                            .arg(i).arg(col0Len)));
    }
}

QTEST_MAIN(TestModel3DEvaluator)
#include "test_model3d_evaluator.moc"
