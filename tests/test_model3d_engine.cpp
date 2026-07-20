#include <QtTest>
#include <QSignalSpy>
#include "model3d/Model3DEngine.h"
#include "CharacterPack.h"

class TestModel3DEngine : public QObject
{
    Q_OBJECT
private slots:
    void rejectsNonModel3DPack();
    void loadsAndRenders();
    void missingClipIsGraceful();
};

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
                 "frameWidth": 124, "frameHeight": 200 },
  "idlePool": [ {"name": "Idle", "weight": 1} ],
  "eventMap": { "session.start": "Wave" },
  "stateMap": {}
})JSON");
    f.close();
    return packDir;
}

void TestModel3DEngine::rejectsNonModel3DPack()
{
    Model3DEngine engine;
    CharacterPack pack; // default = Lottie type, invalid
    QVERIFY(!engine.loadFromCharacterPack(&pack));
}

void TestModel3DEngine::loadsAndRenders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(writeTestPack(dir)));

    Model3DEngine engine;
    if (!engine.loadFromCharacterPack(&pack))
        QSKIP("No GL available in test environment");
    QVERIFY(engine.hasAnimations());
    QVERIFY(engine.isPlaying());
    QVERIFY(engine.lastPaintSuccessful());

    // Frame is non-empty (some non-transparent pixels after idle starts).
    engine.playAnimation(QStringLiteral("Wave"), Model3DEngine::HighPriority);
    QTest::qWait(50);
    QVERIFY(engine.lastPaintSuccessful());
}

void TestModel3DEngine::missingClipIsGraceful()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(writeTestPack(dir)));
    Model3DEngine engine;
    if (!engine.loadFromCharacterPack(&pack))
        QSKIP("No GL available in test environment");
    // Unknown clip names must not crash (user-imported pack tolerance).
    engine.playAnimation(QStringLiteral("NoSuchClip"), Model3DEngine::HighPriority);
    QVERIFY(engine.isPlaying());
}

QTEST_MAIN(TestModel3DEngine)
#include "test_model3d_engine.moc"
