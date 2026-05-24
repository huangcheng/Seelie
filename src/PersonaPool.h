#ifndef PERSONA_POOL_H
#define PERSONA_POOL_H

#include <QHash>
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

    /// Insert multiple lines at once. Returns count actually inserted (after
    /// validation: empty/whitespace dropped; lines >MAX_TIP_CHARS truncated;
    /// duplicate exact-text rejected via PK).
    int insertMany(const QString &packId, const QString &eventName,
                   const QString &personaHash, const QStringList &texts);

    // --- In-flight refill bookkeeping -----------------------------------------
    bool isRefillInFlight(const QString &packId, const QString &eventName) const;
    void markRefillStarted(const QString &packId, const QString &eventName);
    void markRefillFinished(const QString &packId, const QString &eventName);

    /// Default 30000 ms. Stuck entries older than this are swept on access.
    void setInflightTimeoutMs(int ms) { m_inflightTimeoutMs = ms; }

    // --- Spam guard (3-empty-refill suppression) ------------------------------
    bool isSpamSuppressed(const QString &packId, const QString &eventName) const;
    void recordEmptyRefill(const QString &packId, const QString &eventName);
    void clearSpamSuppression(const QString &packId, const QString &eventName);

    static constexpr int MAX_TIP_CHARS = 200;
    static constexpr int TARGET_POOL_SIZE = 20;
    static constexpr int MIN_POOL_SIZE = 5;

private:
    QSqlDatabase m_db;
    bool m_valid = false;

    QHash<QString, qint64> m_inflight;       // key = "pack|event", value = start ms
    QHash<QString, int>    m_emptyCounters;  // key = "pack|event", value = count
    int m_inflightTimeoutMs = 30000;
    static QString makeKey(const QString &p, const QString &e) { return p + QChar('|') + e; }
};

#endif // PERSONA_POOL_H
