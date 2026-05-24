#include "PersonaPool.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QtGlobal>

PersonaPool::PersonaPool(QSqlDatabase db) : m_db(db)
{
    if (!m_db.isValid() || !m_db.isOpen()) return;
    QSqlQuery q(m_db);
    const bool table = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS persona_pool ("
        "  pack_id      TEXT NOT NULL,"
        "  event        TEXT NOT NULL,"
        "  text         TEXT NOT NULL,"
        "  persona_hash TEXT NOT NULL,"
        "  created_at   INTEGER NOT NULL,"
        "  PRIMARY KEY (pack_id, event, text)"
        ")"));
    const bool idx = q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_persona_pool_lookup "
        "ON persona_pool (pack_id, event, persona_hash)"));
    m_valid = table && idx;
    if (!m_valid) qWarning() << "PersonaPool init failed:" << q.lastError().text();
}

int PersonaPool::size(const QString &packId, const QString &eventName) const
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM persona_pool WHERE pack_id=? AND event=?"));
    q.addBindValue(packId);
    q.addBindValue(eventName);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

bool PersonaPool::insert(const QString &packId, const QString &eventName,
                         const QString &personaHash, const QString &text)
{
    if (!m_valid) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO persona_pool"
        "(pack_id, event, text, persona_hash, created_at) VALUES(?,?,?,?,?)"));
    q.addBindValue(packId);
    q.addBindValue(eventName);
    q.addBindValue(text);
    q.addBindValue(personaHash);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << "PersonaPool::insert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QString PersonaPool::pick(const QString &packId, const QString &eventName,
                          const QString &personaHash)
{
    if (!m_valid) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT text FROM persona_pool "
        "WHERE pack_id=? AND event=? AND persona_hash=? "
        "ORDER BY RANDOM() LIMIT 1"));
    q.addBindValue(packId);
    q.addBindValue(eventName);
    q.addBindValue(personaHash);
    if (q.exec() && q.next()) return q.value(0).toString();
    return {};
}

int PersonaPool::wipeStale(const QString &packId, const QString &currentHash)
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM persona_pool WHERE pack_id=? AND persona_hash != ?"));
    q.addBindValue(packId);
    q.addBindValue(currentHash);
    if (!q.exec()) {
        qWarning() << "PersonaPool::wipeStale failed:" << q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

int PersonaPool::wipePack(const QString &packId)
{
    if (!m_valid) return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM persona_pool WHERE pack_id=?"));
    q.addBindValue(packId);
    if (!q.exec()) return 0;
    return q.numRowsAffected();
}
