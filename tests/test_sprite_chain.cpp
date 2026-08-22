#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "SpriteAnimationEngine.h"
#include "CharacterPack.h"

class TestSpriteChain : public QObject
{
    Q_OBJECT
private slots:
    void playsFirstExistingClipInChain();
    void noOpsWhenNoneExist();
};

static bool writeSpritePack(const QString &dirPath, const QStringList &clipNames,
                            const QJsonObject &nameMap = {})
{
    QJsonArray anims;
    for (int i = 0; i < clipNames.size(); ++i) {
        anims.append(QJsonObject{
            {"Name", clipNames[i]},
            {"Frames", QJsonArray{QJsonObject{
                {"Duration", 100},
                {"ImagesOffsets", QJsonObject{{"Column", i}, {"Row", 0}}}
            }}}
        });
    }
    QFile anim(dirPath + "/animations.json");
    if (!anim.open(QIODevice::WriteOnly)) return false;
    anim.write(QJsonDocument(anims).toJson());
    anim.close();

    QImage img(192 * qMax(1, clipNames.size()), 208, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    if (!img.save(dirPath + "/sheet.webp", "WEBP")) return false;

    QJsonObject character{
        {"type", "spriteSheet"},
        {"spriteSheet", "sheet.webp"},
        {"frameWidth", 192},
        {"frameHeight", 208},
        {"definitions", "animations.json"}
    };
    QJsonObject manifest{
        {"formatVersion", "1.0.0"},
        {"id", "test.sprite.chain"},
        {"name", "T"},
        {"author", "a"},
        {"version", "1"},
        {"character", character},
        {"idlePool", QJsonArray{QJsonObject{{"name", clipNames.first()}, {"weight", 1}}}},
        {"nameMap", nameMap}
    };
    QFile mf(dirPath + "/manifest.json");
    if (!mf.open(QIODevice::WriteOnly)) return false;
    mf.write(QJsonDocument(manifest).toJson());
    return true;
}

void TestSpriteChain::playsFirstExistingClipInChain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeSpritePack(dir.path(), {"idle", "pet"},
                             QJsonObject{{"pat", "pet"}}));
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(dir.path()));
    SpriteAnimationEngine eng;
    QVERIFY(eng.loadFromCharacterPack(&pack));
    eng.playAnimationChain({"missing", "pat", "pet", "idle"},
                           SpriteAnimationEngine::HighPriority);
    QCOMPARE(eng.currentAnimation(), QString("pet"));
}

void TestSpriteChain::noOpsWhenNoneExist()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeSpritePack(dir.path(), {"idle"}));
    CharacterPack pack;
    QVERIFY(pack.loadFromDirectory(dir.path()));
    SpriteAnimationEngine eng;
    QVERIFY(eng.loadFromCharacterPack(&pack));
    eng.playAnimation("idle");
    const QString before = eng.currentAnimation();
    eng.playAnimationChain({"nope", "also_nope"},
                           SpriteAnimationEngine::HighPriority);
    QCOMPARE(eng.currentAnimation(), before);
}

QTEST_MAIN(TestSpriteChain)
#include "test_sprite_chain.moc"
