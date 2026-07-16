#ifndef EMBEDDING_SERVICE_H
#define EMBEDDING_SERVICE_H

#include <QObject>
#include <QThread>
#include <QVector>
#include <QString>
#include <functional>

class MemoryManager;

/// Inner worker for EmbeddingService. Lives on a dedicated QThread and runs
/// ONLY the injected embed fn, emitting embedded()/failed() back to the main
/// thread. Has no DB state and never touches SQLite.
///
/// Declared at file scope (not as a nested class of EmbeddingService) because
/// Qt's moc does not support Q_OBJECT on nested classes.
class EmbeddingWorker : public QObject
{
    Q_OBJECT
public:
    using EmbedFn = std::function<QVector<float>(const QString &text, QString *errorOut)>;

    explicit EmbeddingWorker(EmbedFn fn) : m_fn(std::move(fn)) {}

public slots:
    void onJob(qint64 episodeId, const QString &text);

signals:
    void embedded(qint64 episodeId, const QVector<float> &vec);
    void failed(qint64 episodeId);

private:
    EmbedFn m_fn;
};

/// Fills episode embedding BLOBs asynchronously on a worker thread.
///
/// The embed function is injected (production: LLMProvider::embedTextSync;
/// tests: a fake) so this class is fully unit-testable offline. All failures
/// are silent: rows keep NULL embeddings.
///
/// Threading model (mirrors TTSEngine): an EmbeddingWorker QObject lives on a
/// dedicated QThread. Public methods are main-thread only; the injected embed
/// function executes ONLY on the worker thread (never the main thread, never
/// blocking the UI). Cross-thread delivery uses Qt signals (auto/queued
/// connection across the thread boundary).
///
/// The Worker does NOT touch MemoryManager or SQLite directly — SQLite is
/// main-thread-only by the v1 invariant. The worker runs the (pure, thread-
/// safe) embed fn and emits embedded()/failed(); EmbeddingService receives
/// those on the main thread and applies the write to MemoryManager.
class EmbeddingService : public QObject
{
    Q_OBJECT
public:
    using EmbedFn = EmbeddingWorker::EmbedFn;

    explicit EmbeddingService(MemoryManager *memory, EmbedFn fn, QObject *parent = nullptr);
    ~EmbeddingService() override;

    /// Queue an embedding for an existing episode row. id < 0 means this is a
    /// digest-query job (stored under MemoryManager::kDigestQueryKey instead of
    /// a row id). Empty text and over-capacity jobs are dropped silently.
    void enqueueEpisode(qint64 episodeId, const QString &text);

    /// Convenience: request an embedding for the digest query text.
    void requestDigestEmbedding(const QString &contextText);

signals:
    /// Emitted on the main thread when a digest-query embedding (id < 0)
    /// completes successfully. Episode-row jobs do not emit this.
    void digestEmbeddingReady(const QVector<float> &vec);

    // Internal cross-thread signal: main -> worker. Not for outside use.
    void jobRequested(qint64 episodeId, const QString &text);

private:
    MemoryManager *m_memory;
    QThread m_thread;
    EmbeddingWorker *m_worker = nullptr;

    // Backpressure: count of jobs outstanding on the worker. Tracked on the
    // main thread only (enqueue increments, embedded/failed decrements).
    int m_pending = 0;
    static constexpr int kMaxQueue = 100;
};

#endif // EMBEDDING_SERVICE_H
