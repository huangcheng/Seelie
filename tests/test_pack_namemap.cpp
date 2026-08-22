#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include "CharacterPack.h"

class TestPackNameMap : public QObject
{
    Q_OBJECT
private slots:
    void loadsNameMapAndDesktopMotionFromManifest();
};

void TestPackNameMap::loadsNameMapAndDesktopMotionFromManifest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile anim(dir.path() + "/animations.json");
    QVERIFY(anim.open(QIODevice::WriteOnly));
    anim.write(R"([{"Name":"idle","Frames":[{"Duration":100,"ImagesOffsets":{"Column":0,"Row":0}}]}])");
    anim.close();
    QImage img(192, 208, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QVERIFY(img.save(dir.path() + "/sheet.webp", "WEBP"));

    const QByteArray manifest = R"({
      "formatVersion":"1.0.0","id":"test.pack","name":"T","author":"a","version":"1",
      "character":{"type":"spriteSheet","spriteSheet":"sheet.webp",
        "frameWidth":192,"frameHeight":208,"definitions":"animations.json",
        "desktopMotion":true},
      "idlePool":[{"name":"idle","weight":1}],
      "nameMap":{"pat":"pet","wave":"greet"},
      "stateMap":{"Petted":["pet"]}
    })";
    QFile mf(dir.path() + "/manifest.json");
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(manifest);
    mf.close();

    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(dir.path()));
    QCOMPARE(pack.nameMap().value("pat"), QString("pet"));
    QCOMPARE(pack.nameMap().value("wave"), QString("greet"));
    QVERIFY(pack.desktopMotion());
    QCOMPARE(pack.stateMap().value("Petted"), QStringList({"pet"}));
}

QTEST_MAIN(TestPackNameMap)
#include "test_pack_namemap.moc"
