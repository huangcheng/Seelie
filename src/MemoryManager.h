#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct Episode {
    qint64  ts = 0;
    QString kind;
    QString text;
};

class MemoryManager : public QObject
{
    Q_OBJECT
public:
    explicit MemoryManager(const QString &dbPath, QObject *parent = nullptr);
    ~MemoryManager() override;

    bool isValid() const { return m_valid; }

    /// Borrow the underlying QSqlDatabase. Returned by value (QSqlDatabase is a handle).
    /// PersonaPool uses this so all SQLite access lives on the same connection / thread.
    QSqlDatabase database() const { return m_db; }

    /// Exposed for tests + EmbeddingService (same connection, main thread only).
    QString connectionName() const { return m_connectionName; }
    /// Test/diagnostic helper: true if the given table exists.
    bool hasTable(const QString &table) const;
    /// Epoch ms of first-ever run with memory v2. 0 if invalid.
    qint64 firstMetTs() const;

    int  bondXP() const;
    int  bondLevel() const;          // derived from XP; L0..L5
    void addBondXP(int delta);       // no-op if delta <= 0 or invalid

    int  affection() const;          // decay-adjusted 0..100
    void addAffection(int delta);    // applies decay, then delta, clamps, stamps ts

    QString value(const QString &key, const QString &defaultValue = QString()) const;
    bool setValue(const QString &key, const QString &value);

    int increment(const QString &key, int delta = 1);

    QString lastGreeting() const;
    bool setLastGreeting(const QString &text);

    QString userName() const;
    void setUserName(const QString &name);
    QString displayName() const;
    void setDisplayName(const QString &name);
    // Free-form "about me" the user fills in on the Profile tab. Optional and
    // never auto-derived. Sent to the persona LLM only when the user has
    // explicitly opted in via ConfigManager::shareMemoryWithAi(); see
    // PersonaEngine::resolveOnDemand for the gated read site.
    QString userBio() const;
    void setUserBio(const QString &bio);

    bool hasMilestone(const QString &key) const;
    bool setMilestone(const QString &key);
    void checkMilestone(const QString &key, const QString &title, const QString &body);

    QString effectiveName() const;

    qint64 recordEpisode(const QString &kind, const QString &text);   // new episode id, or -1 on failure/no-op
    QVector<Episode> recentEpisodes(int limit = 10) const;   // newest first
    int episodeCount() const;

    // --- Embedding storage (Task 6) ---
    // BLOB layout: raw host-endian float32 array (single-platform user DB; not
    // portable across architectures, documented here for future migrations).
    bool hasEmbeddings() const;
    void setEpisodeEmbedding(qint64 episodeId, const QVector<float> &vec);
    /// Exact cosine top-k over embedded episodes (brute force; <=2000 rows).
    QVector<Episode> recallByVector(const QVector<float> &query, int limit = 5) const;
    /// Test/diagnostic: insert episode with prebuilt embedding blob.
    qint64 insertEpisodeForTest(const QString &kind, const QString &text, const QByteArray &blob);

    int daysMet() const;
    /// Machine-facing English digest for LLM prompts. Uses similarEpisodes()
    /// when embeddings exist (Task 7+), else recentEpisodes(). Never tr().
    QString memoryDigest(int maxChars = 600) const;

signals:
    void userNameChanged(const QString &name);
    void milestoneReached(const QString &title, const QString &body);
    void bondLevelChanged(int newLevel);

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_valid = false;

    void enforceEpisodeCap();   // plain (FIFO) mode here; embedding mode added in Task 7
};

#endif // MEMORYMANAGER_H
