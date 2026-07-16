#include "MemoryManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QtGlobal>
#include <QDir>
#include <QDateTime>
#include <QDate>
#include <QStringList>

namespace {
// XP thresholds for bond levels L0..L5. Tune here only.
constexpr int kBondThresholds[] = {0, 50, 150, 400, 1000, 2500};
constexpr int kBondMaxLevel = 5;

constexpr int kAffectionMax = 100;
constexpr int kAffectionDecayPerHour = 5;

constexpr int kEpisodeCap = 2000;

int levelForXP(int xp) {
    int lvl = 0;
    for (int i = 0; i <= kBondMaxLevel; ++i)
        if (xp >= kBondThresholds[i]) lvl = i;
    return lvl;
}
} // namespace

MemoryManager::MemoryManager(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("seelie_memory_%1")
                          .arg(reinterpret_cast<quintptr>(QThread::currentThreadId())))
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);
    m_valid = m_db.open();
    if (!m_valid) {
        qWarning() << "MemoryManager: failed to open database:" << dbPath
                    << m_db.lastError().text();
        return;
    }

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS memory ("
                               "key TEXT PRIMARY KEY, value TEXT NOT NULL)")))
    {
        qWarning() << "MemoryManager: failed to create table:" << q.lastError().text();
        m_valid = false;
    }

    if (!m_valid) return;   // v1 table failed — don't run v2 migration on a broken DB

    // --- v2 migration (user_version 0 -> 1): episodes table + first_met seed ---
    QSqlQuery vq(m_db);
    if (vq.exec(QStringLiteral("PRAGMA user_version")) && vq.next()) {
        const int version = vq.value(0).toInt();
        if (version < 1) {
            QSqlQuery mig(m_db);
            mig.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS episodes ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " ts INTEGER NOT NULL,"
                " kind TEXT NOT NULL,"
                " text TEXT NOT NULL,"
                " embedding BLOB)"));
            mig.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_episodes_ts ON episodes(ts)"));
            // Seed first_met only if absent (preserve across upgrades)
            QSqlQuery seed(m_db);
            seed.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO memory(key, value) VALUES('rel.first_met_ts', :v)"));
            seed.bindValue(QStringLiteral(":v"),
                           QString::number(QDateTime::currentMSecsSinceEpoch()));
            seed.exec();
            QSqlQuery uv(m_db);
            uv.exec(QStringLiteral("PRAGMA user_version = 1"));
        }
    }
}

MemoryManager::~MemoryManager()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool MemoryManager::hasTable(const QString &table) const
{
    if (!m_valid) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=:t"));
    q.bindValue(QStringLiteral(":t"), table);
    return q.exec() && q.next();
}

qint64 MemoryManager::firstMetTs() const
{
    if (!m_valid) return 0;
    return value(QStringLiteral("rel.first_met_ts")).toLongLong();
}

int MemoryManager::bondXP() const
{
    if (!m_valid) return 0;
    return value(QStringLiteral("rel.bond_xp")).toInt();
}

int MemoryManager::bondLevel() const
{
    return levelForXP(bondXP());
}

void MemoryManager::addBondXP(int delta)
{
    if (!m_valid || delta <= 0) return;
    const int before = bondLevel();
    const int now = increment(QStringLiteral("rel.bond_xp"), delta);   // existing API
    if (now < 0) return;                                               // DB failure — silent
    const int after = levelForXP(now);
    if (after != before) emit bondLevelChanged(after);
}

int MemoryManager::affection() const
{
    if (!m_valid) return 0;
    const int stored = value(QStringLiteral("rel.affection"), QStringLiteral("0")).toInt();
    const qint64 ts  = value(QStringLiteral("rel.affection_ts"), QStringLiteral("0")).toLongLong();
    if (ts <= 0) return qBound(0, stored, kAffectionMax);
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - ts;
    if (elapsedMs <= 0) return qBound(0, stored, kAffectionMax);
    const int decay = int(elapsedMs * kAffectionDecayPerHour / 3600000LL);
    return qBound(0, stored - decay, kAffectionMax);
}

void MemoryManager::addAffection(int delta)
{
    if (!m_valid || delta == 0) return;
    const int next = qBound(0, affection() + delta, kAffectionMax);
    // Two writes: not atomic, but main-thread-only + self-correcting on next read.
    setValue(QStringLiteral("rel.affection"), QString::number(next));
    setValue(QStringLiteral("rel.affection_ts"),
             QString::number(QDateTime::currentMSecsSinceEpoch()));
}

QString MemoryManager::value(const QString &key, const QString &defaultValue) const
{
    if (!m_valid) return defaultValue;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM memory WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}

bool MemoryManager::setValue(const QString &key, const QString &value)
{
    if (!m_valid) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO memory(key, value) VALUES(?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    if (!q.exec()) {
        qWarning() << "MemoryManager::setValue failed:" << q.lastError().text();
        return false;
    }
    return true;
}

int MemoryManager::increment(const QString &key, int delta)
{
    if (!m_valid) return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO memory(key, value) VALUES(:key, :delta) "
        "ON CONFLICT(key) DO UPDATE SET value = CAST(value AS INTEGER) + :delta"));
    q.bindValue(QStringLiteral(":key"), key);
    q.bindValue(QStringLiteral(":delta"), delta);
    if (!q.exec()) {
        qWarning() << "MemoryManager::increment failed:" << q.lastError().text();
        return -1;
    }

    QSqlQuery r(m_db);
    r.prepare(QStringLiteral("SELECT value FROM memory WHERE key = ?"));
    r.addBindValue(key);
    if (r.exec() && r.next()) {
        bool ok = false;
        int v = r.value(0).toInt(&ok);
        return ok ? v : -1;
    }
    return -1;
}

QString MemoryManager::lastGreeting() const
{
    return value(QStringLiteral("greeting.last_text"));
}

bool MemoryManager::setLastGreeting(const QString &text)
{
    return setValue(QStringLiteral("greeting.last_text"), text);
}

QString MemoryManager::userName() const
{
    return value(QStringLiteral("profile.name"));
}

void MemoryManager::setUserName(const QString &name)
{
    setValue(QStringLiteral("profile.name"), name);
    emit userNameChanged(name);
}

QString MemoryManager::displayName() const
{
    return value(QStringLiteral("profile.display_name"));
}

void MemoryManager::setDisplayName(const QString &name)
{
    setValue(QStringLiteral("profile.display_name"), name);
}

QString MemoryManager::userBio() const
{
    return value(QStringLiteral("profile.bio"));
}

void MemoryManager::setUserBio(const QString &bio)
{
    setValue(QStringLiteral("profile.bio"), bio);
}

bool MemoryManager::hasMilestone(const QString &key) const
{
    return !value(QStringLiteral("milestone.") + key).isEmpty();
}

bool MemoryManager::setMilestone(const QString &key)
{
    return setValue(QStringLiteral("milestone.") + key, QStringLiteral("1"));
}

void MemoryManager::checkMilestone(const QString &key, const QString &title, const QString &body)
{
    if (hasMilestone(key)) return;
    if (!setMilestone(key)) return;
    emit milestoneReached(title, body);
}

QString MemoryManager::effectiveName() const
{
    // displayName → userName → OS env → home dir name → ""
    const QString dn = displayName();
    if (!dn.isEmpty()) return dn;
    const QString un = userName();
    if (!un.isEmpty()) return un;

    QString env = qEnvironmentVariable("USER");
    if (env.isEmpty()) env = qEnvironmentVariable("USERNAME");
    if (!env.isEmpty()) return env;

    return QDir::home().dirName();
}

qint64 MemoryManager::recordEpisode(const QString &kind, const QString &text) {
    if (!m_valid || kind.isEmpty() || text.isEmpty()) return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO episodes(ts, kind, text) VALUES(:ts, :kind, :text)"));
    q.bindValue(QStringLiteral(":ts"), QDateTime::currentMSecsSinceEpoch());
    q.bindValue(QStringLiteral(":kind"), kind);
    q.bindValue(QStringLiteral(":text"), text);
    if (!q.exec()) return -1;
    const qint64 id = q.lastInsertId().toLongLong();
    enforceEpisodeCap();
    return id;
}

QVector<Episode> MemoryManager::recentEpisodes(int limit) const
{
    QVector<Episode> out;
    if (!m_valid || limit <= 0) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT ts, kind, text FROM episodes ORDER BY ts DESC, id DESC LIMIT :n"));
    q.bindValue(QStringLiteral(":n"), limit);
    if (!q.exec()) return out;
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString()});
    return out;
}

int MemoryManager::episodeCount() const
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM episodes")) || !q.next()) return 0;
    return q.value(0).toInt();
}

int MemoryManager::daysMet() const
{
    const qint64 first = firstMetTs();
    if (first <= 0) return 0;
    return int(QDateTime::fromMSecsSinceEpoch(first).date().daysTo(QDate::currentDate()));
}

QString MemoryManager::memoryDigest(int maxChars) const
{
    if (!m_valid || maxChars <= 0) return QString();
    const QString name = effectiveName();   // v1 API: displayName -> userName -> OS user
    QStringList lines;
    lines << QStringLiteral("Known %1 for %2 days.")
                 .arg(name.isEmpty() ? QStringLiteral("the user") : name)
                 .arg(daysMet());
    lines << QStringLiteral("Bond L%1 (%2 XP). Affection %3/100.")
                 .arg(bondLevel()).arg(bondXP()).arg(affection());
    const int sessions = value(QStringLiteral("stats.sessions"), QStringLiteral("0")).toInt();
    lines << QStringLiteral("Sessions %1.").arg(sessions);
    const auto eps = recentEpisodes(10);
    if (!eps.isEmpty()) {
        lines << QStringLiteral("Memories:");
        for (const Episode &e : eps) lines << QStringLiteral("- ") + e.text.simplified();
    }
    // Task 7 upgrades the episode selection to similarity-ranked when embedded.
    QString out;
    for (const QString &l : lines) {
        const int sep = out.isEmpty() ? 0 : 1;
        if (out.length() + l.length() + sep > maxChars) break;
        if (!out.isEmpty()) out += QLatin1Char('\n');
        out += l;
    }
    return out;
}

void MemoryManager::enforceEpisodeCap()
{
    // Plain mode (Task 4): FIFO oldest-by-ts with rollup counter.
    // Task 7 replaces the selection strategy when embeddings are present.
    while (episodeCount() > kEpisodeCap) {
        QSqlQuery oldest(m_db);
        if (!oldest.exec(QStringLiteral(
                "SELECT id, kind FROM episodes ORDER BY ts ASC, id ASC LIMIT 1")) || !oldest.next())
            return;
        const qint64 id = oldest.value(0).toLongLong();
        const QString kind = oldest.value(1).toString();
        increment(QStringLiteral("episodes.rolled.") + kind, 1);
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM episodes WHERE id=:id"));
        del.bindValue(QStringLiteral(":id"), id);
        if (!del.exec() || del.numRowsAffected() == 0) return;   // can't make progress — avoid infinite loop
    }
}
