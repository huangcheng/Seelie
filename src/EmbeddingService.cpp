#include "EmbeddingService.h"
#include "MemoryManager.h"

// --- EmbeddingWorker (lives on m_thread) -----------------------------------
//
// Runs ONLY the injected embed fn and emits embedded()/failed(). Holds no DB
// state and never touches SQLite — that's the main thread's job. The fn is
// assumed pure / thread-safe (production: LLMProvider::embedTextSync, which
// spins up its own QNetworkAccessManager on the calling thread).

void EmbeddingWorker::onJob(qint64 episodeId, const QString &text)
{
    // Runs on m_thread. m_fn is the only thing executed here; the result is
    // marshalled back via signal so the main thread applies the DB write.
    QString err;
    QVector<float> vec = m_fn(text, &err);
    if (vec.isEmpty())
        emit failed(episodeId);
    else
        emit embedded(episodeId, vec);
}

// --- EmbeddingService (main thread) ----------------------------------------

EmbeddingService::EmbeddingService(MemoryManager *memory, EmbedFn fn, QObject *parent)
    : QObject(parent), m_memory(memory)
{
    // Worker is intentionally NOT parented to `this`: it lives on m_thread and
    // would otherwise be destroyed on the wrong thread by ~QObject. We delete
    // it manually in the destructor after the thread has joined. (TTSEngine
    // uses a queued-cleanup + moveToThread-back dance because the engine
    // itself is the worker; here the worker is a separate object so a plain
    // quit/wait/delete is sufficient and bulletproof.)
    m_worker = new EmbeddingWorker(std::move(fn));
    m_worker->moveToThread(&m_thread);

    // -1 is the digest-query job id; keep requestDigestEmbedding() working.
    m_queryKeys.insert(-1, MemoryManager::kDigestQueryKey);

    // main -> worker (auto => queued across threads)
    connect(this, &EmbeddingService::jobRequested, m_worker, &EmbeddingWorker::onJob);

    // worker -> main (auto => queued across threads): apply DB writes here,
    // on the main thread, never inside the worker.
    connect(m_worker, &EmbeddingWorker::embedded, this, [this](qint64 id, const QVector<float> &vec) {
        if (m_pending > 0) --m_pending;
        if (id < 0) {
            // Query job: resolve the caller's storage key. -1 (the digest
            // query) is permanently reserved — read it, never consume it;
            // synthetic query ids (≤ -2) are one-shot (take).
            const QString key = (id == -1) ? m_queryKeys.value(id)
                                           : m_queryKeys.take(id);
            if (!key.isEmpty()) {
                if (m_memory)
                    m_memory->setQueryEmbedding(key, vec);
                emit queryEmbeddingReady(key, vec);
                if (key == MemoryManager::kDigestQueryKey) {
                    emit digestEmbeddingReady(vec);  // back-compat
                }
            }
        } else {
            if (m_memory)
                m_memory->setEpisodeEmbedding(id, vec);
        }
    });
    connect(m_worker, &EmbeddingWorker::failed, this, [this](qint64 id) {
        if (m_pending > 0) --m_pending;
        // Never remove the reserved -1 (digest) key — a later digest call
        // must still resolve. Synthetic query ids (≤ -2) are one-shot.
        if (id < 0 && id != -1) m_queryKeys.remove(id);
        // Silent: row keeps NULL embedding / query key never lands.
    });

    m_thread.start();
}

EmbeddingService::~EmbeddingService()
{
    // Drain: queued pending jobs run before quit() takes effect (FIFO event
    // queue). wait(3000) bounds total shutdown time.
    m_thread.quit();
    if (m_thread.wait(3000)) {
        delete m_worker;      // thread joined — safe
    } else {
        qWarning() << "EmbeddingService: worker thread did not finish in 3s; detaching (OS reclaims at exit)";
    }
    m_worker = nullptr;
}

void EmbeddingService::enqueueEpisode(qint64 episodeId, const QString &text)
{
    if (text.isEmpty()) return;
    if (m_pending >= kMaxQueue) {
        qDebug() << "EmbeddingService: queue full, dropping episode" << episodeId;
        return;
    }
    ++m_pending;
    emit jobRequested(episodeId, text);
}

void EmbeddingService::requestDigestEmbedding(const QString &contextText)
{
    // id < 0 marks this as a digest-query job in the main-thread apply handler.
    enqueueEpisode(-1, contextText);
}

void EmbeddingService::enqueueQuery(const QString &key, const QString &text)
{
    if (key.isEmpty() || text.isEmpty()) return;
    m_queryKeys.insert(m_nextQueryId, key);
    enqueueEpisode(m_nextQueryId, text);
    --m_nextQueryId;
}
