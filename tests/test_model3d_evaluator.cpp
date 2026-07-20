#include <QtTest>
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

QTEST_MAIN(TestModel3DEvaluator)
#include "test_model3d_evaluator.moc"
