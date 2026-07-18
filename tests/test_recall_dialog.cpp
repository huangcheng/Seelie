/**
 * test_recall_dialog.cpp
 *
 * Unit tests for the Recall UI ("What do you remember?"):
 *   - RecallFilter::contains (case-insensitive substring filter)
 *   - RecallFilter::relativeTime (locale-aware relative timestamps)
 *   - RecallDialog widget smoke (header, list, empty state, substring search)
 */

#include <QTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QLabel>
#include <QListWidget>

#include "MemoryManager.h"
#include "RecallDialog.h"
#include "RecallFilter.h"

class TestRecallDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Task 2: pure helpers
    void testContainsEmptyQueryReturnsInput();
    void testContainsCaseInsensitiveMatch();
    void testContainsMissReturnsEmpty();
    void testContainsNonAscii();
    void testRelativeTimeJustNow();
    void testRelativeTimeMinutesHoursDays();
    void testRelativeTimeAbsoluteFallback();
    void testRelativeTimeFutureClamp();
    void testRelativeTimeChinese();
    void testRelativeTimeBoundaries();
    void testContainsEmptyEpisodeList();

    // Task 3: dialog smoke
    void testDialogShowsHeaderAndEpisodes();
    void testDialogEmptyState();
    void testDialogNullMemory();

private:
    QTemporaryDir m_tmpDir;
};

void TestRecallDialog::initTestCase()
{
    QVERIFY(m_tmpDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmpDir.path());
}

static Episode ep(qint64 ts, const QString &kind, const QString &text)
{
    Episode e;
    e.ts = ts;
    e.kind = kind;
    e.text = text;
    return e;
}

void TestRecallDialog::testContainsEmptyQueryReturnsInput()
{
    const QVector<Episode> in{ep(1, "session", "a"), ep(2, "poke", "b")};
    QCOMPARE(RecallFilter::contains(in, QString()).size(), 2);
}

void TestRecallDialog::testContainsCaseInsensitiveMatch()
{
    const QVector<Episode> in{ep(1, "session", "Fixed the IPC server"),
                              ep(2, "session", "lunch break")};
    const auto out = RecallFilter::contains(in, QStringLiteral("ipc"));
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().text, QStringLiteral("Fixed the IPC server"));
}

void TestRecallDialog::testContainsMissReturnsEmpty()
{
    const QVector<Episode> in{ep(1, "session", "a"), ep(2, "poke", "b")};
    QVERIFY(RecallFilter::contains(in, QStringLiteral("zzz")).isEmpty());
}

void TestRecallDialog::testContainsNonAscii()
{
    const QVector<Episode> in{ep(1, "session", "第一次抚摸了 Seelie")};
    QCOMPARE(RecallFilter::contains(in, QStringLiteral("抚摸")).size(), 1);
}

void TestRecallDialog::testRelativeTimeJustNow()
{
    QCOMPARE(RecallFilter::relativeTime(10'000, 10'030, QStringLiteral("en")),
             QStringLiteral("just now"));
}

void TestRecallDialog::testRelativeTimeMinutesHoursDays()
{
    QCOMPARE(RecallFilter::relativeTime(0, 5 * 60'000, QStringLiteral("en")),
             QStringLiteral("5m ago"));
    QCOMPARE(RecallFilter::relativeTime(0, 3 * 3'600'000, QStringLiteral("en")),
             QStringLiteral("3h ago"));
    QCOMPARE(RecallFilter::relativeTime(0, 2 * 86'400'000, QStringLiteral("en")),
             QStringLiteral("2d ago"));
}

void TestRecallDialog::testRelativeTimeAbsoluteFallback()
{
    // ≥7d → ISO date of the timestamp itself.
    const qint64 ts = QDateTime(QDate(2026, 7, 1), QTime(12, 0)).toMSecsSinceEpoch();
    QCOMPARE(RecallFilter::relativeTime(ts, ts + 10LL * 86'400'000, QStringLiteral("en")),
             QStringLiteral("2026-07-01"));
}

void TestRecallDialog::testRelativeTimeFutureClamp()
{
    QCOMPARE(RecallFilter::relativeTime(10'000, 5'000, QStringLiteral("en")),
             QStringLiteral("just now"));
}

void TestRecallDialog::testRelativeTimeChinese()
{
    QCOMPARE(RecallFilter::relativeTime(0, 30'000, QStringLiteral("zh_CN")),
             QStringLiteral("刚刚"));
    QCOMPARE(RecallFilter::relativeTime(0, 5 * 60'000, QStringLiteral("zh_CN")),
             QStringLiteral("5 分钟前"));
    QCOMPARE(RecallFilter::relativeTime(0, 3 * 3'600'000, QStringLiteral("zh_CN")),
             QStringLiteral("3 小时前"));
    QCOMPARE(RecallFilter::relativeTime(0, 2 * 86'400'000, QStringLiteral("zh_CN")),
             QStringLiteral("2 天前"));
}

void TestRecallDialog::testRelativeTimeBoundaries()
{
    // Exactly 60 min → rolls up to "1h ago", not "60m ago"
    QCOMPARE(RecallFilter::relativeTime(0, 60 * 60'000, QStringLiteral("en")),
             QStringLiteral("1h ago"));
    // Exactly 24h → rolls up to "1d ago"
    QCOMPARE(RecallFilter::relativeTime(0, 24 * 3'600'000, QStringLiteral("en")),
             QStringLiteral("1d ago"));
    // Exactly 7d → ISO-date fallback (not "7d ago")
    const qint64 ts7 = QDateTime(QDate(2026, 6, 1), QTime(12, 0)).toMSecsSinceEpoch();
    QCOMPARE(RecallFilter::relativeTime(ts7, ts7 + 7LL * 86'400'000, QStringLiteral("en")),
             QStringLiteral("2026-06-01"));
}

void TestRecallDialog::testContainsEmptyEpisodeList()
{
    QVERIFY(RecallFilter::contains({}, QStringLiteral("query")).isEmpty());
}

void TestRecallDialog::testDialogShowsHeaderAndEpisodes()
{
    MemoryManager mm(":memory:");
    QVERIFY(mm.isValid());
    QVERIFY(mm.recordEpisode(QStringLiteral("session"), QStringLiteral("2h 5m, 42 events")) >= 0);
    QVERIFY(mm.recordEpisode(QStringLiteral("poke"), QStringLiteral("Poked!")) >= 0);

    RecallDialog dlg(&mm, nullptr, QStringLiteral("en"));
    auto *header = dlg.findChild<QLabel*>(QStringLiteral("recallHeader"));
    QVERIFY(header);
    QVERIFY(header->text().contains(QStringLiteral("Bond L")));
    QVERIFY(header->text().contains(QStringLiteral("memories")));
    auto *list = dlg.findChild<QListWidget*>(QStringLiteral("recallList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("Poked!")));  // newest first
    QVERIFY(list->item(0)->text().contains(QStringLiteral("[poke]")));
}

void TestRecallDialog::testDialogEmptyState()
{
    MemoryManager mm(":memory:");
    QVERIFY(mm.isValid());
    RecallDialog dlg(&mm, nullptr, QStringLiteral("en"));
    auto *list = dlg.findChild<QListWidget*>(QStringLiteral("recallList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("No memories yet")));
}

void TestRecallDialog::testDialogNullMemory()
{
    // Defensive path: dialog constructed with no MemoryManager must not crash
    // and shows the unavailable variant instead of relationship stats.
    RecallDialog dlg(nullptr, nullptr, QStringLiteral("en"));
    auto *header = dlg.findChild<QLabel*>(QStringLiteral("recallHeader"));
    QVERIFY(header);
    QVERIFY(header->text().contains(QStringLiteral("Memory unavailable")));
    auto *list = dlg.findChild<QListWidget*>(QStringLiteral("recallList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 1);  // empty-state row, not episode rows
}

QTEST_MAIN(TestRecallDialog)
#include "test_recall_dialog.moc"
