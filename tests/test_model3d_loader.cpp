#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "model3d/GltfLoader.h"
#include "CharacterPack.h"

class TestModel3DLoader : public QObject
{
    Q_OBJECT
private slots:
    void loadsSkeleton();
    void loadsClips();
    void loadsMesh();
    void loadsTexture();
    void rejectsMissingFile();
    void manifestParsesModel3DFields();
    void loadsRealBlenderModel();
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

static QString writeTestPack(QTemporaryDir &dir)
{
    // Minimal model3d pack: manifest + symlinked test GLB.
    const QString packDir = dir.path() + "/cube3d";
    QDir().mkpath(packDir);
    QFile::copy(QStringLiteral(SOURCE_DIR) + "/tests/data/rig_cube.glb",
                packDir + "/model.glb");
    QFile f(packDir + "/manifest.json");
    f.open(QIODevice::WriteOnly);
    f.write(R"JSON({
  "formatVersion": "1.0.0",
  "id": "test.cube3d", "name": "Cube3D", "author": "test", "version": "1.0.0",
  "character": { "type": "model3d", "model": "model.glb",
                 "frameWidth": 124, "frameHeight": 200,
                 "cameraDistance": 0.0, "cameraHeight": 0.0,
                 "unitScale": 1.0, "upAxis": "y" },
  "idlePool": [ {"name": "Idle", "weight": 1} ],
  "eventMap": { "session.start": "Wave" },
  "stateMap": {}
})JSON");
    f.close();
    return packDir;
}

void TestModel3DLoader::manifestParsesModel3DFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(writeTestPack(dir)));
    QCOMPARE(pack.characterConfig().engineType, CharacterPack::EngineType::Model3D);
    QCOMPARE(pack.characterConfig().modelFile, QStringLiteral("model.glb"));
    QCOMPARE(pack.characterConfig().frameWidth, 124);
    QCOMPARE(pack.characterConfig().frameHeight, 200);
    QVERIFY(qAbs(pack.characterConfig().unitScale - 1.0f) < 1e-6f);
    QCOMPARE(pack.characterConfig().upAxis, QStringLiteral("y"));
    QVERIFY(pack.assetPath(pack.characterConfig().modelFile).endsWith("model.glb"));
}

void TestModel3DLoader::loadsRealBlenderModel()
{
    Model3DModel model;
    QString err;
    QVERIFY2(GltfLoader::loadFromFile(QStringLiteral(SOURCE_DIR) + "/tests/data/robot_expressive.glb",
                                      model, &err), qPrintable(err));
    // RobotExpressive (Quaternius/Blender, CC0): 43 joints in first skin,
    // 14 clips, 3 materials, no embedded textures (material colors).
    QCOMPARE(model.joints.size(), 43);
    QCOMPARE(model.clips.size(), 14);
    QVERIFY(model.clipIndexByName.contains(QStringLiteral("Idle")));
    QVERIFY(model.clipIndexByName.contains(QStringLiteral("Wave")));
    QVERIFY(model.clipIndexByName.contains(QStringLiteral("Dance")));
    QCOMPARE(model.materials.size(), 3);
    QVERIFY(model.materials[0].baseColor.isNull());
    QVERIFY(!model.primitives.isEmpty());
    // Real exporters emit dense keyframes — every track must have matching
    // time/value counts (3 comps for T/S, 4 for R).
    for (const Model3DClip &clip : model.clips)
        for (const Model3DTrack &t : clip.tracks) {
            const int comps = t.path == Model3DTrack::Rotation ? 4 : 3;
            QCOMPARE(t.values.size(), t.times.size() * comps);
        }
}

QTEST_MAIN(TestModel3DLoader)
#include "test_model3d_loader.moc"
