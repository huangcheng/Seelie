#ifndef PERSONA_ENGINE_H
#define PERSONA_ENGINE_H

#include "PersonaPool.h"
#include "llm/LLMProvider.h"
#include <QObject>
#include <QQueue>
#include <QJsonObject>

class MemoryManager;
class ConfigManager;
class CharacterPack;

/**
 * @brief Resolves canonical events into in-character tip text.
 *
 * Pool-tier events (tool.before, file.edited, ...) return immediately from
 * PersonaPool. On-demand events (session.start, milestones, ...) return
 * a TipsCatalog fallback synchronously; in Task 9 they will also fire an
 * LLM call and emit tipUpgraded() when the response arrives.
 */
struct PersonaStats {
    int refillsOk    = 0;
    int refillsFail  = 0;
    int ondemandOk   = 0;
    int ondemandFail = 0;
    int ondemandStale = 0;
    qint64 tokensIn  = 0;
    qint64 tokensOut = 0;
    QString lastError;
};

class PersonaEngine : public QObject
{
    Q_OBJECT
public:
    enum class Tier { Pool, OnDemand };

    struct Resolved {
        QString text;
        quint64 requestId = 0;   // 0 if no upgrade will arrive
    };

    PersonaEngine(MemoryManager *memory, ConfigManager *config, QObject *parent = nullptr);

    void setActivePackId(const QString &packId) { m_activePackId = packId; }
    void setPersonaHash(const QString &hash) { m_personaHash = hash; }

    /// Wipe the entire pool for the active pack. Triggered by the "Regenerate" button.
    void regenerateActivePackPool() { m_pool.wipePack(m_activePackId); }

    /// Synchronous entry point. Always returns a non-empty text (or empty if
    /// no fallback is available).
    Resolved resolve(const QString &eventName, const QJsonObject &payload);

    /// Test seam — expose internal pool for seeding.
    PersonaPool &pool() { return m_pool; }

    PersonaStats stats() const { return m_stats; }

    void loadStats(const QString &configDir);
    void saveStats(const QString &configDir);

    static Tier tierFor(const QString &eventName);

signals:
    void tipUpgraded(quint64 requestId, const QString &newText);
    // Fires when an on-demand LLM call fails (network error, auth, empty
    // response, etc). The listener should fall back to whatever catalog text
    // it had for this requestId — see MainWindow::onTipUpgradeFailed for the
    // canonical handling. Does NOT fire for stale callbacks (pack switched
    // mid-flight) since in that case the bubble is no longer relevant.
    void tipUpgradeFailed(quint64 requestId);

private:
    Resolved resolvePool(const QString &eventName);
    Resolved resolveOnDemand(const QString &eventName, const QJsonObject &payload);
    QString fallbackTip(const QString &eventName) const;
    /// Re-resolve m_provider from the current ConfigManager state. Called from
    /// the ctor and whenever ConfigManager::personaProfileChanged or
    /// llmProfilesChanged fires.
    void refreshActiveProfile();

    MemoryManager *m_memory;
    ConfigManager *m_config;

    QString m_activePackId;
    QString m_personaHash;

    PersonaStats m_stats;

    PersonaPool m_pool;
    LLMProvider m_provider;

    QQueue<QString> m_eventWindow;
    static constexpr int EVENT_WINDOW_SIZE = 5;

    quint64 m_nextRequestId = 1;
};

#endif // PERSONA_ENGINE_H
