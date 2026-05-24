#include "PersonaPool.h"
#include "MemoryManager.h"
#include <QtTest/QtTest>

class TestPersonaPool : public QObject
{
    Q_OBJECT
private slots:
    void testInsertAndPick()
    {
        MemoryManager mm(":memory:");
        QVERIFY(mm.isValid());
        PersonaPool pool(mm.database());
        QVERIFY(pool.isValid());

        QCOMPARE(pool.size("pack_a", "tool.before"), 0);
        QVERIFY(pool.insert("pack_a", "tool.before", "hash1", "Hello"));
        QVERIFY(pool.insert("pack_a", "tool.before", "hash1", "Hi"));
        QCOMPARE(pool.size("pack_a", "tool.before"), 2);

        QString picked = pool.pick("pack_a", "tool.before", "hash1");
        QVERIFY(picked == "Hello" || picked == "Hi");
    }

    void testPickEmpty()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        QVERIFY(pool.pick("nope", "tool.before", "h").isEmpty());
    }

    void testInsertDuplicatesIgnored()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        QVERIFY(pool.insert("p", "e", "h", "Hello"));
        QVERIFY(pool.insert("p", "e", "h", "Hello"));  // duplicate text — PK conflict
        QCOMPARE(pool.size("p", "e"), 1);
    }

    void testPickRespectsHash()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        pool.insert("p", "e", "old_hash", "A");
        pool.insert("p", "e", "new_hash", "B");
        // Pick with old hash gets only A.
        QCOMPARE(pool.pick("p", "e", "old_hash"), QString("A"));
        // Pick with new hash gets only B.
        QCOMPARE(pool.pick("p", "e", "new_hash"), QString("B"));
        // Pick with unknown hash returns empty.
        QVERIFY(pool.pick("p", "e", "other").isEmpty());
    }
};

QTEST_MAIN(TestPersonaPool)
#include "test_persona_pool.moc"
