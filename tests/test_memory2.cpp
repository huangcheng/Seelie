#include "MemoryManager.h"
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <QFile>
#include <QCoreApplication>

class TestMemory2 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Unique file DB per run (":memory:" is per-connection and would
        // break the manager's open/verify pattern across reopens).
        m_dbPath = QDir::temp().filePath(
            QStringLiteral("seelie_test_mem2_%1.db").arg(QCoreApplication::applicationPid()));
        QFile::remove(m_dbPath);
    }
    void cleanupTestCase() {
        QFile::remove(m_dbPath);
        for (const QString &p : std::as_const(m_cleanup)) QFile::remove(p);
    }

    void testMigrationCreatesEpisodesTable() {
        MemoryManager mem(m_dbPath);
        QVERIFY(mem.isValid());
        // episodes table exists with expected columns incl. embedding BLOB
        QVERIFY(mem.hasTable(QStringLiteral("episodes")));   // helper added in Task 1
    }

    void testFirstMetSeededOnce() {
        qint64 first;
        {
            MemoryManager mem(m_dbPath);
            first = mem.firstMetTs();
            QVERIFY(first > 0);
        }  // mem destroyed, connection removed
        MemoryManager mem2(m_dbPath);
        QCOMPARE(mem2.firstMetTs(), first);
    }

    void testUserVersionIsOne() {
        MemoryManager mem(m_dbPath);
        QSqlDatabase db = QSqlDatabase::database(mem.connectionName());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // --- Task 2: bond XP / levels ---

    void testBondXPStartsAtZero() {
        MemoryManager mem(freshDb());
        QCOMPARE(mem.bondXP(), 0);
        QCOMPARE(mem.bondLevel(), 0);
    }

    void testAddBondXPAccumulates() {
        MemoryManager mem(freshDb());
        mem.addBondXP(30);
        mem.addBondXP(25);
        QCOMPARE(mem.bondXP(), 55);
        QCOMPARE(mem.bondLevel(), 1);   // threshold L1 = 50
    }

    void testBondLevelThresholds() {
        const int xpFor[] = {0, 50, 150, 400, 1000, 2500};
        for (int lvl = 0; lvl < 6; ++lvl) {
            MemoryManager m(freshDb());
            m.addBondXP(xpFor[lvl]);
            QCOMPARE(m.bondLevel(), lvl);
        }
    }

    void testBondLevelChangedSignal() {
        MemoryManager mem(freshDb());
        QSignalSpy spy(&mem, &MemoryManager::bondLevelChanged);
        mem.addBondXP(10);
        QCOMPARE(spy.count(), 0);       // still L0
        mem.addBondXP(50);              // crosses into L1
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
    }

    void testAddBondXPIgnoresNonPositive() {
        MemoryManager mem(freshDb());
        mem.addBondXP(0);
        mem.addBondXP(-5);
        QCOMPARE(mem.bondXP(), 0);
    }

private:
    // Unique pristine DB path per call (tests are independent of each other).
    QString freshDb() {
        const QString p = QDir::temp().filePath(QStringLiteral("seelie_t2_%1.db").arg(m_counter++));
        QFile::remove(p);
        m_cleanup << p;
        return p;
    }

    QString m_dbPath;
    int m_counter = 0;
    QStringList m_cleanup;
};

QTEST_MAIN(TestMemory2)
#include "test_memory2.moc"
