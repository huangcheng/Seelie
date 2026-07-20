#include <QtTest>
#include "model3d/GltfLoader.h"

class TestModel3DLoader : public QObject
{
    Q_OBJECT
private slots:
    void loadsSkeleton();
    void loadsClips();
    void loadsMesh();
    void loadsTexture();
    void rejectsMissingFile();
};

static QString glbPath()
{
    return QStringLiteral(SOURCE_DIR) + "/tests/data/rig_cube.glb";
}

void TestModel3DLoader::loadsSkeleton()
{
    Model3DModel model;
    QString err;
    QVERIFY2(GltfLoader::loadFromFile(glbPath(), model, &err), qPrintable(err));
    QCOMPARE(model.joints.size(), 2);
    QCOMPARE(model.joints[0].name, QStringLiteral("Root"));
    QCOMPARE(model.joints[1].name, QStringLiteral("Tip"));
    QCOMPARE(model.joints[0].parent, -1);
    QCOMPARE(model.joints[1].parent, 0);
    QVERIFY(model.joints[1].bindT.distanceToPoint(QVector3D(0, 1, 0)) < 1e-5f);
    // IBM[1] maps (0,1,0) to origin.
    QVERIFY(model.joints[1].inverseBind.map(QVector3D(0, 1, 0)).length() < 1e-5f);
}

void TestModel3DLoader::loadsClips()
{
    Model3DModel model;
    QVERIFY(GltfLoader::loadFromFile(glbPath(), model, nullptr));
    QCOMPARE(model.clips.size(), 2);
    QVERIFY(model.clipIndexByName.contains(QStringLiteral("Idle")));
    QVERIFY(model.clipIndexByName.contains(QStringLiteral("Wave")));
    const Model3DClip &idle = model.clips[model.clipIndexByName["Idle"]];
    QVERIFY(qAbs(idle.duration - 1.0f) < 1e-4f);
    const Model3DClip &wave = model.clips[model.clipIndexByName["Wave"]];
    QVERIFY(qAbs(wave.duration - 0.5f) < 1e-4f);
    QCOMPARE(wave.tracks.size(), 1);
    QCOMPARE(wave.tracks[0].joint, 1);
    QCOMPARE(wave.tracks[0].path, Model3DTrack::Rotation);
    QCOMPARE(wave.tracks[0].times.size(), 2);
}

void TestModel3DLoader::loadsMesh()
{
    Model3DModel model;
    QVERIFY(GltfLoader::loadFromFile(glbPath(), model, nullptr));
    QCOMPARE(model.primitives.size(), 1);
    QCOMPARE(model.primitives[0].vertices.size(), 8);
    QCOMPARE(model.primitives[0].indices.size(), 36);
    // Bottom verts -> joint 0 full weight; top verts -> joint 1.
    QCOMPARE(model.primitives[0].vertices[0].joints[0], 0);
    QVERIFY(qAbs(model.primitives[0].vertices[0].weights[0] - 1.0f) < 1e-6f);
    QCOMPARE(model.primitives[0].vertices[4].joints[0], 1);
}

void TestModel3DLoader::loadsTexture()
{
    Model3DModel model;
    QVERIFY(GltfLoader::loadFromFile(glbPath(), model, nullptr));
    QCOMPARE(model.materials.size(), 1);
    const QImage &tex = model.materials[0].baseColor;
    QCOMPARE(tex.size(), QSize(2, 2));
    QCOMPARE(tex.pixelColor(0, 0).red(), 255);
    QVERIFY(!model.materials[0].unlit);
}

void TestModel3DLoader::rejectsMissingFile()
{
    Model3DModel model;
    QString err;
    QVERIFY(!GltfLoader::loadFromFile(QStringLiteral("/nonexistent.glb"), model, &err));
    QVERIFY(!err.isEmpty());
}

QTEST_MAIN(TestModel3DLoader)
#include "test_model3d_loader.moc"
