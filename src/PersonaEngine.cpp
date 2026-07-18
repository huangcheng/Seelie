#include "PersonaEngine.h"
#include "MemoryManager.h"
#include "ConfigManager.h"
#include "StatisticsPersistence.h"
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
        // Spec 4: context senses + touch reactions get pool-tier canned lines
        // (auto-seeded via generateBatch on first low-water access).
        // context.timeofday is deliberately NOT pool-tier: its catalog tip is
        // intentionally empty (enrichment-only event) — seeding lines for an
        // event that never bubbles would waste an LLM batch per session.
        QStringLiteral("context.latenight"), QStringLiteral("context.longsession"),
        QStringLiteral("context.idle"), QStringLiteral("context.away"),
        QStringLiteral("context.gaming"), QStringLiteral("context.lowbattery"),
        QStringLiteral("user.pet"), QStringLiteral("user.toss"),
    };
    return s;
}

QString localeToHuman(const QString &locale)
{
    // Map Qt locale codes (en, zh_CN, ja_JP, ...) to a language name the LLM
    // recognises. Falls back to "English" so we never send an empty token.
    if (locale.startsWith(QLatin1String("zh"), Qt::CaseInsensitive))
        return QStringLiteral("Simplified Chinese (zh-CN)");
    if (locale.startsWith(QLatin1String("ja"), Qt::CaseInsensitive))
        return QStringLiteral("Japanese");
    if (locale.startsWith(QLatin1String("ko"), Qt::CaseInsensitive))
        return QStringLiteral("Korean");
    if (locale.startsWith(QLatin1String("fr"), Qt::CaseInsensitive))
        return QStringLiteral("French");
    if (locale.startsWith(QLatin1String("de"), Qt::CaseInsensitive))
        return QStringLiteral("German");
    if (locale.startsWith(QLatin1String("es"), Qt::CaseInsensitive))
        return QStringLiteral("Spanish");
    return QStringLiteral("English");
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
    // Spec 4: touch events fall back to the Spec-3 canned touch pools
    // (gesture key = event name suffix). Everything else uses event tips.
    if (eventName == QLatin1String("user.pet")) {
        return TipsCatalog::instance().touchLine(QStringLiteral("pet")).body;
    }
    if (eventName == QLatin1String("user.toss")) {
        return TipsCatalog::instance().touchLine(QStringLiteral("toss")).body;
    }
    // user.hover intentionally has no catalog entry — hover never bubbles.
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

        const QString lang = m_config ? localeToHuman(m_config->language())
                                      : QStringLiteral("English");
        const QString system = QStringLiteral(
            "You write short in-character reactions for a desktop pet. "
            "Each line: one short sentence, under 200 characters, "
            "in %1. Stay in character.").arg(lang);
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
    // Privacy: only include recent event history when the user opted in to
    // sharing memory with the AI.  The rolling window reveals workflow
    // patterns to a third-party API.
    const bool shareMemory = m_config && m_config->shareMemoryWithAi();
    QStringList recent;
    if (shareMemory) {
        for (const QString &e : m_eventWindow) recent << e;
    }

    // Tell the LLM the user's language explicitly. ConfigManager stores it
    // as a Qt locale code ("en", "zh_CN", ...); map to a human name the
    // model recognises.
    const QString lang = m_config ? localeToHuman(m_config->language())
                                  : QStringLiteral("English");
    QString systemPrompt = QStringLiteral(
        "You are a desktop pet companion to a software developer. "
        "Reply with ONE short sentence in %1. Do not add quotes, "
        "translation, or commentary — just the sentence itself.").arg(lang);
    QString userPrompt;
    if (shareMemory) {
        userPrompt = QStringLiteral("Event: %1\nRecent events: %2\nReact in-character.")
                      .arg(eventName, recent.join(QStringLiteral(", ")));
    } else {
        userPrompt = QStringLiteral("Event: %1\nReact in-character.").arg(eventName);
    }

    // Privacy: only attach memory snapshot if user opted in.
    if (shareMemory && m_memory) {
        const QString name = m_memory->effectiveName();
        if (!name.isEmpty()) userPrompt += QStringLiteral("\nUser name: %1").arg(name);
        // Bio is free-form markdown the user wrote on the Profile tab; pass
        // it through verbatim. The LLM understands markdown structure (lists,
        // bold, etc.) and can use it to tune tone and content. Length is
        // already capped at 4000 chars by the Profile tab save handler.
        const QString bio = m_memory->userBio().trimmed();
        if (!bio.isEmpty()) {
            userPrompt += QStringLiteral("\nUser bio (markdown):\n%1").arg(bio);
        }

        // Spec 4: the pet's memory digest (bond, affection, similarity-ranked
        // episodes) joins the prompt behind the same opt-in gate.
        if (m_memory->isValid()) {
            const QString digest = m_memory->memoryDigest();
            if (!digest.isEmpty()) {
                userPrompt += QStringLiteral("\nMemory:\n%1").arg(digest);
            }
        }
    }

    // Capture current pack/hash by value so we can detect stale callbacks
    // if the user switches packs while the LLM request is in flight.
    const QString capturedPackId = m_activePackId;
    const QString capturedHash   = m_personaHash;

    m_provider.generate(systemPrompt, userPrompt,
        [this, requestId, capturedPackId, capturedHash](LLMResult r) {
            // Bail silently if the pack or persona hash changed mid-flight.
            if (capturedPackId != m_activePackId || capturedHash != m_personaHash) {
                ++m_stats.ondemandStale;
                return;
            }

            if (!r.ok || r.text.trimmed().isEmpty()) {
                ++m_stats.ondemandFail;
                m_stats.lastError = r.error;
                if (m_memory) m_memory->increment(QStringLiteral("stats.persona.ondemand.fail"));
                emit tipUpgradeFailed(requestId);
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

void PersonaEngine::loadStats(const QString &configDir)
{
    StatisticsPersistence sp(configDir);
    const QJsonObject section = sp.loadSection(QStringLiteral("persona"));

    m_stats.refillsOk    = section.value(QStringLiteral("refillsOk")).toInt(0);
    m_stats.refillsFail  = section.value(QStringLiteral("refillsFail")).toInt(0);
    m_stats.ondemandOk    = section.value(QStringLiteral("ondemandOk")).toInt(0);
    m_stats.ondemandFail  = section.value(QStringLiteral("ondemandFail")).toInt(0);
    m_stats.ondemandStale = section.value(QStringLiteral("ondemandStale")).toInt(0);
    m_stats.tokensIn     = section.value(QStringLiteral("tokensIn")).toInteger(0);
    m_stats.tokensOut    = section.value(QStringLiteral("tokensOut")).toInteger(0);
    m_stats.lastError    = section.value(QStringLiteral("lastError")).toString();
}

void PersonaEngine::saveStats(const QString &configDir)
{
    StatisticsPersistence sp(configDir);
    QJsonObject section;
    section[QStringLiteral("refillsOk")]    = m_stats.refillsOk;
    section[QStringLiteral("refillsFail")]  = m_stats.refillsFail;
    section[QStringLiteral("ondemandOk")]     = m_stats.ondemandOk;
    section[QStringLiteral("ondemandFail")]   = m_stats.ondemandFail;
    section[QStringLiteral("ondemandStale")]  = m_stats.ondemandStale;
    section[QStringLiteral("tokensIn")]     = m_stats.tokensIn;
    section[QStringLiteral("tokensOut")]    = m_stats.tokensOut;
    section[QStringLiteral("lastError")]    = m_stats.lastError;
    sp.saveSection(QStringLiteral("persona"), section);
}
