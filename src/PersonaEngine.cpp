#include "PersonaEngine.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "TipsCatalog.h"
#include <QSet>

namespace {
const QSet<QString> &poolTierEvents()
{
    static const QSet<QString> s = {
        QStringLiteral("tool.before"), QStringLiteral("tool.after"),
        QStringLiteral("tool.failed"),
        QStringLiteral("file.edited"), QStringLiteral("file.watched"),
        QStringLiteral("prompt.submitted"), QStringLiteral("todo.updated"),
        QStringLiteral("notification.sent"), QStringLiteral("permission.response"),
    };
    return s;
}
}  // namespace

PersonaEngine::PersonaEngine(MemoryManager *memory, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_memory(memory)
    , m_config(config)
    , m_pool(memory ? memory->database() : QSqlDatabase{})
{
}

PersonaEngine::Tier PersonaEngine::tierFor(const QString &eventName)
{
    return poolTierEvents().contains(eventName) ? Tier::Pool : Tier::OnDemand;
}

QString PersonaEngine::fallbackTip(const QString &eventName) const
{
    return TipsCatalog::instance().eventTip(eventName).body;
}

PersonaEngine::Resolved PersonaEngine::resolve(const QString &eventName,
                                               const QJsonObject &payload)
{
    // Maintain rolling window
    m_eventWindow.enqueue(eventName);
    while (m_eventWindow.size() > EVENT_WINDOW_SIZE) m_eventWindow.dequeue();

    if (!m_config || !m_config->personaEnabled() || m_config->personaProfile().isEmpty()) {
        return { fallbackTip(eventName), 0 };
    }

    if (tierFor(eventName) == Tier::Pool) return resolvePool(eventName);
    return resolveOnDemand(eventName, payload);
}

PersonaEngine::Resolved PersonaEngine::resolvePool(const QString &eventName)
{
    const QString text = m_pool.pick(m_activePackId, eventName, m_personaHash);
    if (!text.isEmpty()) return { text, 0 };
    // Cold pool — return fallback. Refill scheduling lives in Task 9 once we
    // wire LLMProvider into PersonaEngine; for now return fallback so the
    // engine stays useful even with persona enabled.
    return { fallbackTip(eventName), 0 };
}

PersonaEngine::Resolved PersonaEngine::resolveOnDemand(const QString &eventName,
                                                       const QJsonObject &)
{
    // On-demand async path lands in Task 9. For now this also returns fallback.
    return { fallbackTip(eventName), 0 };
}
