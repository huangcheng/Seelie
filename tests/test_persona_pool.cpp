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

    void testWipeStale()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        pool.insert("p", "e", "old_hash", "A");
        pool.insert("p", "e", "old_hash", "B");
        pool.insert("p", "e", "new_hash", "C");

        QCOMPARE(pool.wipeStale("p", "new_hash"), 2);
        QCOMPARE(pool.size("p", "e"), 1);
        QCOMPARE(pool.pick("p", "e", "new_hash"), QString("C"));
    }

    void testInsertManyValid()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        int accepted = pool.insertMany("p", "e", "h",
            { "Hello", "  ", "", "World", QString(220, 'x'), "Tch." });
        // Whitespace and empty rejected; oversized truncated; valid duplicates ignored.
        QCOMPARE(accepted, 4);  // Hello, World, truncated-x..., Tch.
        QCOMPARE(pool.size("p", "e"), 4);

        // Reinserting same texts should not increase size.
        QCOMPARE(pool.insertMany("p", "e", "h", { "Hello", "Tch." }), 0);
        QCOMPARE(pool.size("p", "e"), 4);
    }

    void testInflightLifecycle()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());
        pool.setInflightTimeoutMs(50);

        QVERIFY(!pool.isRefillInFlight("p", "e"));
        pool.markRefillStarted("p", "e");
        QVERIFY(pool.isRefillInFlight("p", "e"));
        pool.markRefillFinished("p", "e");
        QVERIFY(!pool.isRefillInFlight("p", "e"));

        // Stale in-flight is auto-cleaned on next access.
        pool.markRefillStarted("p", "e");
        QTest::qWait(80);
        QVERIFY(!pool.isRefillInFlight("p", "e"));
    }

    void testSpamGuard()
    {
        MemoryManager mm(":memory:");
        PersonaPool pool(mm.database());

        QVERIFY(!pool.isSpamSuppressed("p", "e"));
        pool.recordEmptyRefill("p", "e");
        pool.recordEmptyRefill("p", "e");
        QVERIFY(!pool.isSpamSuppressed("p", "e"));
        pool.recordEmptyRefill("p", "e");
        QVERIFY(pool.isSpamSuppressed("p", "e"));

        pool.clearSpamSuppression("p", "e");
        QVERIFY(!pool.isSpamSuppressed("p", "e"));
    }
};

QTEST_MAIN(TestPersonaPool)
#include "test_persona_pool.moc"
