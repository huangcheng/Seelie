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

    /// Synchronous entry point. Always returns a non-empty text (or empty if
    /// no fallback is available).
    Resolved resolve(const QString &eventName, const QJsonObject &payload);

    /// Test seam — expose internal pool for seeding.
    PersonaPool &pool() { return m_pool; }

    static Tier tierFor(const QString &eventName);

signals:
    void tipUpgraded(quint64 requestId, const QString &newText);

private:
    Resolved resolvePool(const QString &eventName);
    Resolved resolveOnDemand(const QString &eventName, const QJsonObject &payload);
    QString fallbackTip(const QString &eventName) const;

    MemoryManager *m_memory;
    ConfigManager *m_config;

    QString m_activePackId;
    QString m_personaHash;

    PersonaPool m_pool;
    LLMProvider m_provider;

    QQueue<QString> m_eventWindow;
    static constexpr int EVENT_WINDOW_SIZE = 5;

    quint64 m_nextRequestId = 1;
};

#endif // PERSONA_ENGINE_H
