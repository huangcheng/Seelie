#include "CharacterPack.h"
#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>

class TestCharacterPackPersona : public QObject
{
    Q_OBJECT
private slots:
    void testPersonaParse()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString manifest = R"({
            "format_version": "1.0",
            "id": "test_pack",
            "name": "Test",
            "character": { "engine": "lottie", "anim_directory": "anims",
                           "frame_width": 100, "frame_height": 100 },
            "animations": {},
            "persona": {
                "system": "You are Test. Reply with one sentence.",
                "language": "en",
                "style_examples": ["Hello.", "Hi there."]
            }
        })";
        QFile f(tmp.path() + "/manifest.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(manifest.toUtf8());
        f.close();
        QDir(tmp.path()).mkdir("anims");

        CharacterPack pack;
        QVERIFY(pack.loadFromDirectory(tmp.path()));
        QCOMPARE(pack.persona().system, QString("You are Test. Reply with one sentence."));
        QCOMPARE(pack.persona().language, QString("en"));
        QCOMPARE(pack.persona().styleExamples.size(), 2);
        QVERIFY(!pack.personaHash().isEmpty());
        QCOMPARE(pack.personaHash().length(), 64);  // SHA-256 hex
    }

    void testPersonaAbsent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString manifest = R"({
            "format_version": "1.0",
            "id": "no_persona",
            "name": "NoPersona",
            "character": { "engine": "lottie", "anim_directory": "anims",
                           "frame_width": 100, "frame_height": 100 },
            "animations": {}
        })";
        QFile f(tmp.path() + "/manifest.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(manifest.toUtf8());
        f.close();
        QDir(tmp.path()).mkdir("anims");

        CharacterPack pack;
        QVERIFY(pack.loadFromDirectory(tmp.path()));
        QVERIFY(pack.persona().system.isEmpty());
        QVERIFY(!pack.personaHash().isEmpty());  // stable hash even for empty persona
    }
};

QTEST_MAIN(TestCharacterPackPersona)
#include "test_character_pack_persona.moc"
