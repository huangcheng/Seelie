#ifndef PERSONA_POOL_H
#define PERSONA_POOL_H

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

/**
 * @brief SQLite-backed text pool keyed by (packId, eventName).
 *
 * Lives entirely on the main thread (same as MemoryManager). Borrows
 * MemoryManager's QSqlDatabase by value — no separate connection.
 */
class PersonaPool
{
public:
    explicit PersonaPool(QSqlDatabase db);

    /// True iff the persona_pool table is present on the connection.
    bool isValid() const { return m_valid; }

    /// Number of stored entries for (packId, eventName) regardless of hash.
    int size(const QString &packId, const QString &eventName) const;

    /// Insert one text line. Duplicate (pack, event, text) is silently ignored.
    bool insert(const QString &packId, const QString &eventName,
                const QString &personaHash, const QString &text);

    /// Random pick from entries matching (packId, eventName, personaHash).
    /// Returns empty string if no such entries exist.
    QString pick(const QString &packId, const QString &eventName,
                 const QString &personaHash);

    /// Wipe rows for a pack whose persona_hash differs from `currentHash`.
    int wipeStale(const QString &packId, const QString &currentHash);

    /// Wipe ALL rows for a pack (used by the "Regenerate" button).
    int wipePack(const QString &packId);

private:
    QSqlDatabase m_db;
    bool m_valid = false;
};

#endif // PERSONA_POOL_H
