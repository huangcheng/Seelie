#include "IdleBehaviorEngine.h"
#include "PersonaEngine.h"

#include <QDateTime>
#include <QRandomGenerator>

IdleBehaviorEngine::IdleBehaviorEngine(ConfigManager *config, PersonaEngine *persona,
                                       QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_persona(persona)
    , m_now([] { return QDateTime::currentMSecsSinceEpoch(); })
    , m_rng([] { return QRandomGenerator::global()->generateDouble(); })
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &IdleBehaviorEngine::onTimer);

    if (m_persona) {
        connect(m_persona, &PersonaEngine::tipUpgraded,
                this, &IdleBehaviorEngine::onQuipUpgraded);
        connect(m_persona, &PersonaEngine::tipUpgradeFailed,
                this, &IdleBehaviorEngine::onQuipFailed);
    }
}

bool IdleBehaviorEngine::loadSayings(const QString &locale)
{
    m_sayingsUsable = m_pool.load(locale);
    return m_sayingsUsable;
}

void IdleBehaviorEngine::applyConfig()
{
    m_timer.stop();
    if (!m_config || !m_sayingsUsable) return;
    if (m_config->sayingFrequency() == ConfigManager::SayingFrequency::Never) return;
    m_lastEventAt = m_now();
    m_intervalMs = rollIntervalMs();
    armTimer();
}

void IdleBehaviorEngine::onEventProcessed()
{
    m_lastEventAt = m_now();
    // Any real event also cancels a pending idle quip — the event's own
    // bubble wins.
    m_pendingQuipId = 0;
    if (m_timer.isActive()) {
        m_intervalMs = rollIntervalMs();
        armTimer();
    }
}

void IdleBehaviorEngine::tick()
{
    attemptSlot();
}

void IdleBehaviorEngine::armTimer()
{
    if (m_intervalMs > 0) m_timer.start(m_intervalMs);
}

int IdleBehaviorEngine::rollIntervalMs() const
{
    const double r = m_rng();
    switch (m_config ? m_config->sayingFrequency() : ConfigManager::SayingFrequency::Never) {
    case ConfigManager::SayingFrequency::Rarely:    // 12–20 min
        return static_cast<int>((12 * 60 + r * 8 * 60) * 1000);
    case ConfigManager::SayingFrequency::Sometimes: // 6–10 min
        return static_cast<int>((6 * 60 + r * 4 * 60) * 1000);
    case ConfigManager::SayingFrequency::Often:     // 2.5–4 min
        return static_cast<int>((150 + r * 90) * 1000);
    case ConfigManager::SayingFrequency::Never:
        break;
    }
    return 0;
}

void IdleBehaviorEngine::attemptSlot()
{
    if (!m_config || !m_sayingsUsable) return;
    if (m_config->sayingFrequency() == ConfigManager::SayingFrequency::Never) return;
    if (m_intervalMs <= 0) return;
    if (m_now() - m_lastEventAt < m_intervalMs) return;  // not idle long enough

    // Re-arm before the gate so a skipped slot can't stall the schedule.
    m_intervalMs = rollIntervalMs();
    armTimer();

    if (m_canShow && !m_canShow()) return;   // silent skip, no catch-up
    if (m_pool.isEmpty()) return;

    const bool quipAllowed = m_config->llmIdleQuipsEnabled() && m_persona;
    if (quipAllowed && m_rng() < 0.15) {
        fireQuip();
        return;
    }
    fireCanned();
}

void IdleBehaviorEngine::fireCanned()
{
    const SayingPool::Saying s = m_pool.pick();
    if (!s.body.isEmpty()) emit sayingReady(s.title, s.body);
}

void IdleBehaviorEngine::fireQuip()
{
    const PersonaEngine::Resolved r =
        m_persona->resolve(QStringLiteral("idle.quip"), QJsonObject{});
    if (r.requestId == 0) {
        // Persona disabled / no provider configured — stay canned.
        fireCanned();
        return;
    }
    m_pendingQuipId = r.requestId;
}

void IdleBehaviorEngine::onQuipUpgraded(quint64 requestId, const QString &text)
{
    if (requestId != m_pendingQuipId) return;
    m_pendingQuipId = 0;
    // Re-check the gate at delivery time — an event may have arrived while
    // the LLM call was in flight.
    if (m_canShow && !m_canShow()) return;
    if (!text.trimmed().isEmpty())
        emit sayingReady(tr("Idle musing"), text.trimmed());
}

void IdleBehaviorEngine::onQuipFailed(quint64 requestId)
{
    if (requestId != m_pendingQuipId) return;
    m_pendingQuipId = 0;
    // Silent fallback to a canned line, still behind the gate.
    if (m_canShow && !m_canShow()) return;
    fireCanned();
}
