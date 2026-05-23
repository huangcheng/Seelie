#include "MemoryManager.h"
#include <QtTest/QtTest>
#include <QDir>

class TestMemoryManager : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Use an in-memory DB for test isolation
        m_dbPath = ":memory:";
    }

    void testSetAndGet()
    {
        MemoryManager mm(m_dbPath);
        QVERIFY(mm.isValid());
        QCOMPARE(mm.value("nonexistent", "default"), QString("default"));
        QVERIFY(mm.setValue("test.key", "hello"));
        QCOMPARE(mm.value("test.key"), QString("hello"));
    }

    void testIncrement()
    {
        MemoryManager mm(m_dbPath);
        QCOMPARE(mm.increment("counter"), 1);
        QCOMPARE(mm.increment("counter"), 2);
        QCOMPARE(mm.increment("counter", 5), 7);
    }

    void testMilestoneOnce()
    {
        MemoryManager mm(m_dbPath);
        int callCount = 0;
        connect(&mm, &MemoryManager::milestoneReached, [&](const QString &, const QString &) {
            ++callCount;
        });

        QVERIFY(!mm.hasMilestone("test_milestone"));
        mm.checkMilestone("test_milestone", "Title", "Body");
        QCOMPARE(callCount, 1);
        QVERIFY(mm.hasMilestone("test_milestone"));

        // Second call should NOT emit
        mm.checkMilestone("test_milestone", "Title", "Body");
        QCOMPARE(callCount, 1);
    }

    void testMilestoneSilentOnDbFailure()
    {
        // Use an invalid path that can't be written to
        MemoryManager mm("/dev/null/impossible/path/memory.db");
        int callCount = 0;
        connect(&mm, &MemoryManager::milestoneReached, [&](const QString &, const QString &) {
            ++callCount;
        });
        QVERIFY(!mm.isValid());
        mm.checkMilestone("test_milestone", "Title", "Body");
        QCOMPARE(callCount, 0);
    }

    void testEffectiveName()
    {
        MemoryManager mm(m_dbPath);
        mm.setUserName("Alex");
        mm.setDisplayName("AlexC");
        QCOMPARE(mm.effectiveName(), QString("AlexC"));  // displayName wins

        mm.setDisplayName("");
        QCOMPARE(mm.effectiveName(), QString("Alex"));   // userName fallback
    }

private:
    QString m_dbPath;
};

QTEST_MAIN(TestMemoryManager)
#include "test_memory_manager.moc"
