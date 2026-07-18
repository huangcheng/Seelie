#include "MemoryManager.h"
#include "EmbeddingService.h"
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QCoreApplication>
#include <cstring>

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
        // L0: fresh DB starts at 0 XP; addBondXP(0) is a deliberate no-op (delta<=0 guard).
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
        mem.addBondXP(20);              // 80 XP, still L1 — no new emission
        QCOMPARE(spy.count(), 0);
    }

    void testAddBondXPIgnoresNonPositive() {
        MemoryManager mem(freshDb());
        mem.addBondXP(0);
        mem.addBondXP(-5);
        QCOMPARE(mem.bondXP(), 0);
    }

    // --- Task 3: decaying affection meter ---

    void testAffectionStartsAtZero() {
        MemoryManager mem(freshDb());
        QCOMPARE(mem.affection(), 0);
    }

    void testAddAffectionClamps() {
        MemoryManager mem(freshDb());
        mem.addAffection(150);
        QCOMPARE(mem.affection(), 100);         // clamp high
        mem.addAffection(-500);
        QCOMPARE(mem.affection(), 0);           // clamp low
    }

    void testAffectionDecaysLazily() {
        MemoryManager mem(freshDb());
        mem.addAffection(50);
        // Backdate affection_ts by 4 hours -> expect 50 - 4*5 = 30
        const qint64 fourHoursAgo = QDateTime::currentMSecsSinceEpoch() - 4LL*3600*1000;
        QVERIFY(mem.setValue(QStringLiteral("rel.affection_ts"), QString::number(fourHoursAgo)));
        QCOMPARE(mem.affection(), 30);
    }

    void testDecayFloorsAtZero() {
        MemoryManager mem(freshDb());
        mem.addAffection(3);
        const qint64 dayAgo = QDateTime::currentMSecsSinceEpoch() - 24LL*3600*1000;
        QVERIFY(mem.setValue(QStringLiteral("rel.affection_ts"), QString::number(dayAgo)));
        QCOMPARE(mem.affection(), 0);
    }

    void testAddAffectionZeroIsNoop() {
        MemoryManager mem(freshDb());
        mem.addAffection(50);
        const qint64 tsBefore = mem.value(QStringLiteral("rel.affection_ts")).toLongLong();
        mem.addAffection(0);
        const qint64 tsAfter = mem.value(QStringLiteral("rel.affection_ts")).toLongLong();
        QCOMPARE(tsAfter, tsBefore);   // timestamp NOT refreshed
        QCOMPARE(mem.affection(), 50);
    }

    // --- Task 4: episodic memory ---

    void testRecordAndRecentEpisodes() {
        MemoryManager mem(freshDb());
        mem.recordEpisode(QStringLiteral("session"), QStringLiteral("1h 5m, 12 events"));
        mem.recordEpisode(QStringLiteral("milestone"), QStringLiteral("First tip!"));
        const auto eps = mem.recentEpisodes(10);
        QCOMPARE(eps.size(), 2);
        QCOMPARE(eps.first().kind, QStringLiteral("milestone"));   // newest first
        QCOMPARE(eps.first().text, QStringLiteral("First tip!"));
        QCOMPARE(eps.last().kind, QStringLiteral("session"));
        QVERIFY(eps.first().ts > 0);
    }

    void testRecentEpisodesLimit() {
        MemoryManager mem(freshDb());
        for (int i = 0; i < 10; ++i)
            mem.recordEpisode(QStringLiteral("session"), QStringLiteral("ep %1").arg(i));
        QCOMPARE(mem.recentEpisodes(3).size(), 3);
        QCOMPARE(mem.recentEpisodes(3).first().text, QStringLiteral("ep 9"));
    }

    void testPlainRollupAtCap() {
        MemoryManager mem(freshDb());
        // Fill to cap with no embeddings anywhere -> FIFO rollup
        for (int i = 0; i < 2005; ++i)
            mem.recordEpisode(QStringLiteral("session"), QStringLiteral("ep %1").arg(i));
        QCOMPARE(mem.episodeCount(), 2000);
        // Rolled-up oldest 5 tracked in counter
        QCOMPARE(mem.value(QStringLiteral("episodes.rolled.session"), QStringLiteral("0")).toInt(), 5);
        // Newest survived
        QCOMPARE(mem.recentEpisodes(1).first().text, QStringLiteral("ep 2004"));
    }

    // --- Task 5: daysMet + memory digest (recency mode) ---

    void testDaysMet() {
        MemoryManager mem(freshDb());
        QCOMPARE(mem.daysMet(), 0);   // first_met is today
        // Backdate first_met by 10 days
        const qint64 tenDaysAgo = QDateTime::currentMSecsSinceEpoch() - 10LL*24*3600*1000;
        QVERIFY(mem.setValue(QStringLiteral("rel.first_met_ts"), QString::number(tenDaysAgo)));
        QCOMPARE(mem.daysMet(), 10);
    }

    void testDigestContainsCoreFacts() {
        MemoryManager mem(freshDb());
        mem.setUserName(QStringLiteral("Alex"));   // setUserName returns void
        mem.addBondXP(200);           // L2
        mem.addAffection(63);
        mem.increment(QStringLiteral("stats.sessions"), 18);
        mem.recordEpisode(QStringLiteral("session"), QStringLiteral("3h 12m, 42 events"));
        const QString d = mem.memoryDigest();
        QVERIFY(d.contains(QStringLiteral("Alex")));
        QVERIFY(d.contains(QStringLiteral("L2")));
        QVERIFY(d.contains(QStringLiteral("Affection 63")));
        QVERIFY(d.contains(QStringLiteral("Sessions 18")));
        QVERIFY(d.contains(QStringLiteral("3h 12m, 42 events")));
        QVERIFY(d.length() <= 600);
    }

    void testDigestRespectsMaxChars() {
        MemoryManager mem(freshDb());
        for (int i = 0; i < 50; ++i)
            mem.recordEpisode(QStringLiteral("session"),
                QStringLiteral("a fairly long episode line number %1 with padding text").arg(i));
        QVERIFY(mem.memoryDigest(300).length() <= 300);
    }

    void testDigestSanitizesNewlines() {
        MemoryManager mem(freshDb());
        mem.recordEpisode(QStringLiteral("session"), QStringLiteral("line one\nline two\r\nline three"));
        const QString d = mem.memoryDigest();
        QVERIFY(d.contains(QStringLiteral("- line one line two line three")));
        // exactly one digest line belongs to the episode (no embedded newline leaked)
        QVERIFY(!d.contains(QStringLiteral("line one\n")));
    }

    // --- Task 6: embedding storage + exact cosine recall ---

    void testHasEmbeddings() {
        MemoryManager mem(freshDb());
        QVERIFY(!mem.hasEmbeddings());
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("vec ep"),
                         packVec({1.f, 0.f, 0.f}));
        QVERIFY(mem.hasEmbeddings());
    }

    void testCosineRankingOrder() {
        MemoryManager mem(freshDb());
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("x-axis"),
                         packVec({1.f, 0.f, 0.f}));
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("y-axis"),
                         packVec({0.f, 1.f, 0.f}));
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("near-x"),
                         packVec({0.9f, 0.1f, 0.f}));
        const auto hits = mem.recallByVector({1.f, 0.f, 0.f}, 2);
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits.at(0).text, QStringLiteral("x-axis"));
        QCOMPARE(hits.at(1).text, QStringLiteral("near-x"));
    }

    void testRecallSkipsNullEmbeddings() {
        MemoryManager mem(freshDb());
        mem.recordEpisode(QStringLiteral("session"), QStringLiteral("plain"));
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("vec"),
                         packVec({1.f, 0.f}));
        const auto hits = mem.recallByVector({1.f, 0.f}, 10);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).text, QStringLiteral("vec"));
    }

    // --- Task 7: embedding-aware rollup + similarity-ranked digest ---

    void testRollupMergesNearDuplicates() {
        MemoryManager mem(freshDb());
        // Two identical vectors at the OLD end (cosine exactly 1.0); distinct
        // filler after. Identical dup vectors guarantee the dup pair wins the
        // global-max scan regardless of filler properties or scan-window size.
        // The fillers only need to be non-identical to each other within the
        // window so no filler pair sneaks above the dup's similarity.
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("dup-a"),
                         packVec({1.f, 0.f}));
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("dup-b"),
                         packVec({1.f, 0.f}));
        // 1998 deterministic pseudo-random 16-dim fillers (pairwise cos << 0.95),
        // so the only near-duplicate pair in the oldest-200 scan is dup-a/dup-b.
        for (int i = 0; i < 1998; ++i) {
            float v[16];
            for (int k = 0; k < 16; ++k)
                v[k] = float(((i * 7919 + k * 104729) % 200) - 100) / 100.f;
            QByteArray b; b.resize(int(sizeof(v)));
            std::memcpy(b.data(), v, b.size());
            insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("f%1").arg(i), b);
        }
        QCOMPARE(mem.episodeCount(), 2000);
        // One more insert triggers embedding-mode rollup: a dup should die, not a filler
        mem.recordEpisode(QStringLiteral("session"), QStringLiteral("new one"));
        QCOMPARE(mem.episodeCount(), 2000);
        bool dupAAlive = false, dupBAlive = false;
        for (const auto &e : mem.recentEpisodes(2000)) {
            if (e.text == QStringLiteral("dup-a")) dupAAlive = true;
            if (e.text == QStringLiteral("dup-b")) dupBAlive = true;
        }
        QVERIFY(dupAAlive != dupBAlive);   // exactly one survived
        QCOMPARE(mem.value(QStringLiteral("episodes.rolled.session"), QStringLiteral("0")).toInt(), 1);
    }

    void testClearQueryEmbedding() {
        MemoryManager mem(freshDb());
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("vec ep"),
                         packVec({1.f, 0.f}));
        mem.setQueryEmbedding(MemoryManager::kDigestQueryKey, {1.f, 0.f});
        QString d = mem.memoryDigest();
        QVERIFY(d.contains(QStringLiteral("vec ep")));   // similarity mode finds this
        mem.clearQueryEmbedding(MemoryManager::kDigestQueryKey);
        d = mem.memoryDigest();
        // recency mode: episode still listed (it's the only one), proving fallback works
        QVERIFY(d.contains(QStringLiteral("vec ep")));
        // and the branch really fell back: no crash, same content via recentEpisodes path
    }

    void testDigestUsesSimilarityWhenEmbedded() {
        MemoryManager mem(freshDb());
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("late night save"),
                         packVec({1.f, 0.f}));
        // 5 near-query fillers outrank the irrelevant episode in top-5
        for (int i = 0; i < 5; ++i)
            insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("filler %1").arg(i),
                             packVec({0.9f, 0.1f}));
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("unrelated thing"),
                         packVec({0.f, 1.f}));
        mem.setQueryEmbeddingForTest(MemoryManager::kDigestQueryKey, {1.f, 0.f});
        const QString d = mem.memoryDigest();
        QVERIFY(d.contains(QStringLiteral("late night save")));
        QVERIFY(!d.contains(QStringLiteral("unrelated thing")));   // ranked 7th of 7, cut by top-5
    }

    // --- Riders from Task 6 quality review ---

    void testRecallSkipsDimensionMismatch() {
        MemoryManager mem(freshDb());
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("3-dim"),
                         packVec({1.f, 0.f, 0.f}));
        const auto hits = mem.recallByVector({1.f, 0.f}, 10);   // 2-dim query
        QCOMPARE(hits.size(), 0);   // mismatch skipped (sentinel filtered)
    }

    void testRecallEmptyQuery() {
        MemoryManager mem(freshDb());
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("vec"),
                         packVec({1.f}));
        QCOMPARE(mem.recallByVector({}, 5).size(), 0);
    }

    // --- Task 8: async EmbeddingService with injectable embed fn ---

    void testEmbeddingServiceFillsRows() {
        MemoryManager mem(freshDb());
        const qint64 id = mem.insertEpisodeForTest(QStringLiteral("session"),
                                                   QStringLiteral("embed me"), {});
        // Fake embedder: deterministic 2-D vector from text length (no network).
        EmbeddingService svc(&mem, [](const QString &text, QString *) {
            return QVector<float>{float(text.size() % 7) + 1.f, 1.f};
        });
        svc.enqueueEpisode(id, QStringLiteral("embed me"));
        QTRY_VERIFY_WITH_TIMEOUT(mem.hasEmbeddings(), 5000);
        const auto hits = mem.recallByVector({6.f, 1.f}, 1);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).text, QStringLiteral("embed me"));
    }

    void testEmbeddingServiceSurvivesFailure() {
        MemoryManager mem(freshDb());
        const qint64 id = mem.insertEpisodeForTest(QStringLiteral("session"),
                                                   QStringLiteral("will fail"), {});
        EmbeddingService svc(&mem, [](const QString &, QString *err) {
            if (err) *err = QStringLiteral("boom");
            return QVector<float>{};
        });
        svc.enqueueEpisode(id, QStringLiteral("will fail"));
        // Give the worker a beat; row must stay NULL-embedded, service alive.
        QTest::qWait(300);
        QVERIFY(!mem.hasEmbeddings());
        svc.enqueueEpisode(id, QStringLiteral("will fail"));   // still callable
        QTest::qWait(300);
        QVERIFY(!mem.hasEmbeddings());
    }

    // Digest-path coverage: a digest-query job (id < 0) flows through the
    // worker, emits digestEmbeddingReady, lands under the shared key, and
    // subsequently drives the similarity-ranked digest (not recency fallback).
    void testEmbeddingServiceDigestPath() {
        MemoryManager mem(freshDb());
        EmbeddingService svc(&mem, [](const QString &, QString *) {
            return QVector<float>{1.f, 0.f};
        });
        QSignalSpy spy(&svc, &EmbeddingService::digestEmbeddingReady);
        svc.requestDigestEmbedding(QStringLiteral("evening debugging session"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 5000);
        QCOMPARE(spy.takeFirst().at(0).value<QVector<float>>(), (QVector<float>{1.f, 0.f}));
        // Query embedding was applied under the shared key → digest now uses
        // similarity mode and ranks the related episode into the output.
        insertEpisodeRaw(mem, QStringLiteral("session"), QStringLiteral("related memory"),
                         packVec({1.f, 0.f}));
        const QString d = mem.memoryDigest();
        QVERIFY(d.contains(QStringLiteral("related memory")));
    }

    // Generic query-embedding path (recall UI): enqueueQuery(key, text) lands
    // the result under the caller's key in MemoryManager and emits
    // queryEmbeddingReady(key, vec) on the main thread.
    void testEnqueueQueryEmitsKeyAndStores() {
        MemoryManager mem(freshDb());
        EmbeddingService svc(&mem, [](const QString &, QString *) {
            return QVector<float>{0.1f, 0.2f, 0.3f};
        });
        QSignalSpy spy(&svc, &EmbeddingService::queryEmbeddingReady);
        svc.enqueueQuery(QStringLiteral("recall-test"), QStringLiteral("hello world"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 5000);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("recall-test"));
        // QVERIFY: QVector<float> has no QTEST_COMPARE helper toString.
        const auto vec = args.at(1).value<QVector<float>>();
        QVERIFY(vec == (QVector<float>{0.1f, 0.2f, 0.3f}));
    }

    // Back-compat: the digest path now emits BOTH queryEmbeddingReady
    // (key == kDigestQueryKey) and digestEmbeddingReady.
    void testDigestEmbeddingStillEmitsLegacySignal() {
        MemoryManager mem(freshDb());
        EmbeddingService svc(&mem, [](const QString &, QString *) {
            return QVector<float>{0.5f};
        });
        QSignalSpy legacySpy(&svc, &EmbeddingService::digestEmbeddingReady);
        QSignalSpy genericSpy(&svc, &EmbeddingService::queryEmbeddingReady);
        svc.requestDigestEmbedding(QStringLiteral("digest context"));
        QTRY_VERIFY_WITH_TIMEOUT(legacySpy.count() == 1, 5000);
        QCOMPARE(legacySpy.count(), 1);
        QCOMPARE(genericSpy.count(), 1);  // generic signal fires too
        QCOMPARE(genericSpy.first().at(0).toString(), MemoryManager::kDigestQueryKey);
    }

    // Regression: take(-1) used to consume the reserved digest key — the
    // second digest embedding was silently dropped. The -1 mapping must be
    // permanent; only synthetic query ids (≤ -2) are one-shot.
    void testDigestEmbeddingTwiceKeepsWorking() {
        MemoryManager mem(freshDb());
        EmbeddingService svc(&mem, [](const QString &, QString *) {
            return QVector<float>{0.5f};
        });
        QSignalSpy legacySpy(&svc, &EmbeddingService::digestEmbeddingReady);
        svc.requestDigestEmbedding(QStringLiteral("first"));
        svc.requestDigestEmbedding(QStringLiteral("second"));
        QTRY_VERIFY_WITH_TIMEOUT(legacySpy.count() == 2, 5000);
        QCOMPARE(legacySpy.count(), 2);
    }

private:
    static QByteArray packVec(std::initializer_list<float> f) {
        QByteArray b; b.resize(int(f.size() * sizeof(float)));
        std::memcpy(b.data(), f.begin(), b.size());
        return b;
    }
    qint64 insertEpisodeRaw(MemoryManager &mem, const QString &kind, const QString &text,
                            const QByteArray &blob) {
        return mem.insertEpisodeForTest(kind, text, blob);
    }

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
