#include "MemoryManager.h"
#include <QtTest/QtTest>
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
    void cleanupTestCase() { QFile::remove(m_dbPath); }

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

private:
    QString m_dbPath;
};

QTEST_MAIN(TestMemory2)
#include "test_memory2.moc"
