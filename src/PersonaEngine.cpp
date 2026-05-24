#include "PersonaEngine.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "TipsCatalog.h"
#include <QSet>
#include <QStringList>

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
    refreshActiveProfile();

    // Keep m_provider in sync when the user adds/edits profiles or changes
    // the default. Without this, a profile configured AFTER startup never
    // takes effect — resolveOnDemand silently early-returns on the
    // !isConfigured() guard and counters stay at zero.
    if (m_config) {
        connect(m_config, &ConfigManager::personaProfileChanged,
                this, [this](const QString &) { refreshActiveProfile(); });
        connect(m_config, &ConfigManager::llmProfilesChanged,
                this, [this]() { refreshActiveProfile(); });
    }
}

void PersonaEngine::refreshActiveProfile()
{
    if (!m_config) return;
    const QString name = m_config->personaProfile();
    if (name.isEmpty()) {
        m_provider.setProfile({});
        return;
    }
    for (const auto &p : m_config->llmProfiles()) {
        if (p.name == name) {
            m_provider.setProfile(p);
            return;
        }
    }
    // Selected profile no longer exists (e.g. deleted) — clear provider.
    m_provider.setProfile({});
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

    // Schedule a background refill when pool runs low.
    if (m_pool.size(m_activePackId, eventName) < PersonaPool::MIN_POOL_SIZE
        && !m_pool.isRefillInFlight(m_activePackId, eventName)
        && !m_pool.isSpamSuppressed(m_activePackId, eventName)
        && m_provider.isConfigured())
    {
        m_pool.markRefillStarted(m_activePackId, eventName);

        const QString system = QStringLiteral(
            "You write short in-character reactions for a desktop pet. "
            "Each line: one short sentence, under 200 characters. "
            "Stay in character.");
        const QString user = QStringLiteral(
            "Generate %1 distinct one-sentence reactions to the event '%2'.")
            .arg(PersonaPool::TARGET_POOL_SIZE).arg(eventName);

        const QString pack = m_activePackId;
        const QString ev = eventName;
        const QString hash = m_personaHash;

        m_provider.generateBatch(system, user, PersonaPool::TARGET_POOL_SIZE,
            [this, pack, ev, hash](QVector<QString> lines) {
                m_pool.markRefillFinished(pack, ev);
                if (lines.isEmpty()) {
                    m_pool.recordEmptyRefill(pack, ev);
                    ++m_stats.refillsFail;
                    if (m_memory) m_memory->increment(QStringLiteral("stats.persona.refills.fail"));
                    return;
                }
                ++m_stats.refillsOk;
                if (m_memory) m_memory->increment(QStringLiteral("stats.persona.refills.ok"));
                QStringList qsList;
                for (const auto &l : lines) qsList << l;
                m_pool.insertMany(pack, ev, hash, qsList);
            });
    }

    return { text.isEmpty() ? fallbackTip(eventName) : text, 0 };
}

PersonaEngine::Resolved PersonaEngine::resolveOnDemand(const QString &eventName,
                                                       const QJsonObject &payload)
{
    Q_UNUSED(payload);
    if (!m_provider.isConfigured()) return { fallbackTip(eventName), 0 };

    const quint64 requestId = m_nextRequestId++;

    // Build context for the prompt
    QStringList recent;
    for (const QString &e : m_eventWindow) recent << e;

    QString systemPrompt = QStringLiteral(
        "You are a desktop pet companion to a software developer. "
        "Reply with ONE short sentence in the user's language.");
    QString userPrompt = QStringLiteral("Event: %1\nRecent events: %2\nReact in-character.")
                          .arg(eventName, recent.join(QStringLiteral(", ")));

    // Privacy: only attach memory snapshot if user opted in.
    if (m_config && m_config->shareMemoryWithAi() && m_memory) {
        const QString name = m_memory->effectiveName();
        if (!name.isEmpty()) userPrompt += QStringLiteral("\nUser name: %1").arg(name);
    }

    m_provider.generate(systemPrompt, userPrompt,
        [this, requestId](LLMResult r) {
            if (!r.ok || r.text.trimmed().isEmpty()) {
                ++m_stats.ondemandFail;
                m_stats.lastError = r.error;
                if (m_memory) m_memory->increment(QStringLiteral("stats.persona.ondemand.fail"));
                return;
            }
            ++m_stats.ondemandOk;
            m_stats.tokensIn  += r.tokensIn;
            m_stats.tokensOut += r.tokensOut;
            m_stats.lastError.clear();
            if (m_memory) {
                m_memory->increment(QStringLiteral("stats.persona.ondemand.ok"));
                m_memory->increment(QStringLiteral("stats.persona.tokens.in"),  r.tokensIn);
                m_memory->increment(QStringLiteral("stats.persona.tokens.out"), r.tokensOut);
            }
            QString t = r.text.trimmed();
            if (t.length() > PersonaPool::MAX_TIP_CHARS) t.truncate(PersonaPool::MAX_TIP_CHARS);
            emit tipUpgraded(requestId, t);
        });

    return { fallbackTip(eventName), requestId };
}
