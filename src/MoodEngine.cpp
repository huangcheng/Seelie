#include "MoodEngine.h"
#include "EventRouter.h"
#include "MemoryManager.h"
#include "StatisticsPersistence.h"
#include <QDateTime>
#include <QTimer>

MoodEngine::MoodEngine(EventRouter *router, MemoryManager *memory, QObject *parent)
    : QObject(parent)
    , m_router(router)
    , m_memory(memory)
{
    if (m_memory) {
        connect(m_memory, &MemoryManager::bondLevelChanged,
                this, &MoodEngine::onBondLevelChanged);
    }
}

QString MoodEngine::tierName(Tier t)
{
    switch (t) {
    case Tier::Excited: return QStringLiteral("excited");
    case Tier::Tense:   return QStringLiteral("tense");
    case Tier::Tired:   return QStringLiteral("tired");
    case Tier::Lonely:  return QStringLiteral("lonely");
    case Tier::Content: break;
    }
    return QStringLiteral("content");
}

QString MoodEngine::stageName() const
{
    const int lvl = m_memory ? m_memory->bondLevel() : 0;
    if (lvl >= 4) return QStringLiteral("Partner");
    if (lvl >= 2) return QStringLiteral("Companion");
    return QStringLiteral("Stranger");
}

qint64 MoodEngine::nowMs() const
{
    return m_nowFn ? m_nowFn() : QDateTime::currentMSecsSinceEpoch();
}

void MoodEngine::start()
{
    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(TICK_MS);
        connect(m_tickTimer, &QTimer::timeout, this, &MoodEngine::tick);
    }
    m_tickTimer->start();
}

void MoodEngine::stop()
{
    if (m_tickTimer) m_tickTimer->stop();
}

void MoodEngine::applyDelta(double dV, double dE)
{
    m_valence = qBound(-1.0, m_valence + dV, 1.0);
    m_energy  = qBound(-1.0, m_energy + dE, 1.0);
    updateTier();
}

void MoodEngine::setVectorForTest(double v, double e)
{
    m_valence = qBound(-1.0, v, 1.0);
    m_energy  = qBound(-1.0, e, 1.0);
    updateTier();
}

double MoodEngine::energyBaseline() const
{
    const int hour = QDateTime::fromMSecsSinceEpoch(nowMs()).time().hour();
    return (hour >= 23 || hour < 6) ? NIGHT_BASELINE : 0.0;
}

void MoodEngine::tick()
{
    // Decay toward baselines (0 for valence, time-of-day for energy).
    const double eb = energyBaseline();
    m_valence -= qBound(-DECAY_PER_TICK, m_valence, DECAY_PER_TICK);
    const double eDiff = m_energy - eb;
    m_energy -= qBound(-DECAY_PER_TICK, eDiff, DECAY_PER_TICK);

    // Long-session energy drain once the session is past 2 h.
    if (m_sessionStartMs > 0
        && nowMs() - m_sessionStartMs >= LONG_SESSION_ENERGY_DRAIN_AGE_MS) {
        m_energy = qBound(-1.0, m_energy - LONG_SESSION_ENERGY_DRAIN, 1.0);
    }
    updateTier();
    checkProactive();
}

MoodEngine::Tier MoodEngine::quantize() const
{
    if (m_lonely) return Tier::Lonely;
    // Hysteresis: current tier keeps the 0.3 stay-threshold; a new tier
    // must beat the 0.4 enter-threshold. Prevents flapping at boundaries.
    constexpr double stay = 0.3, enter = 0.4;
    switch (m_tier) {
    case Tier::Excited:
        if (m_valence > stay && m_energy > stay) return Tier::Excited;
        break;
    case Tier::Tense:
        if (m_valence < -stay && m_energy > stay) return Tier::Tense;
        break;
    case Tier::Tired:
        if (m_energy < -stay) return Tier::Tired;
        break;
    default:
        break;
    }
    if (m_valence > enter && m_energy > enter) return Tier::Excited;
    if (m_valence < -enter && m_energy > enter) return Tier::Tense;
    if (m_energy < -enter) return Tier::Tired;
    return Tier::Content;
}

void MoodEngine::updateTier()
{
    const Tier t = quantize();
    if (t == m_tier) return;
    m_tier = t;
    emit moodTierChanged(t);
}

void MoodEngine::onEventProcessed(const QString &eventName, const QJsonObject &payload)
{
    Q_UNUSED(payload);
    const qint64 now = nowMs();

    if (eventName == QLatin1String("user.pet")) {
        applyDelta(0.08, 0.02);
    } else if (eventName == QLatin1String("user.toss")) {
        applyDelta(-0.20, 0.10);
    } else if (eventName == QLatin1String("todo.updated")) {
        applyDelta(0.05, 0.0);
    } else if (eventName == QLatin1String("session.error")) {
        applyDelta(-0.10, 0.05);
    } else if (eventName == QLatin1String("tool.failed")) {
        m_failTimes.append(now);
        while (!m_failTimes.isEmpty()
               && now - m_failTimes.first() > FAIL_BURST_WINDOW_MS)
            m_failTimes.removeFirst();
        if (m_failTimes.size() >= 3) {
            m_failTimes.clear();      // one burst delta per window
            applyDelta(-0.15, 0.10);
        }
    } else if (eventName == QLatin1String("session.start")) {
        const qint64 absence = m_lastSeenMs > 0 ? now - m_lastSeenMs : 0;
        m_sessionStartMs = now;
        if (m_lonely) {
            m_lonely = false;
            applyDelta(0.25, 0.25);   // excitement spike, clears Lonely tier
        } else if (absence > 12LL * 60 * 60 * 1000) {
            applyDelta(0.25, 0.25);
        }
        checkProactive();             // greeting / missed-you live here
    } else if (eventName == QLatin1String("session.end")) {
        m_sessionStartMs = 0;
    }
}

void MoodEngine::checkProactive()
{
    const qint64 now = nowMs();
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(now);
    const QString today = local.toString(QStringLiteral("yyyy-MM-dd"));
    const int hour = local.time().hour();

    // Missed-you: session.start after > 24 h absence.
    // Checked BEFORE greeting (rarer, more important moment) so that a
    // first-session-of-the-day after a long absence reports missed-you
    // rather than being eaten by the once-per-day greeting.
    if (m_sessionStartMs == now && m_lastSeenMs > 0
        && now - m_lastSeenMs > MISS_ABSENCE_MS) {
        emitMood(QStringLiteral("mood.missed_you"),
                 {{QStringLiteral("hoursAbsent"),
                   static_cast<int>((now - m_lastSeenMs) / (60LL * 60 * 1000))}});
        return;   // one proactive per check pass
    }

    // Morning greeting: first session.start of a new local date, after 06:00.
    if (m_sessionStartMs > 0 && hour >= 6 && m_lastGreetingDate != today) {
        if (emitMood(QStringLiteral("mood.greeting"), {}))
            m_lastGreetingDate = today;
        return;   // one proactive per check pass
    }

    // Long-session nudge: > 2.5 h continuous AND low energy.
    if (m_sessionStartMs > 0 && now - m_sessionStartMs >= NUDGE_SESSION_AGE_MS
        && m_energy < -0.3) {
        emitMood(QStringLiteral("mood.long_session"),
                 {{QStringLiteral("hours"),
                   (now - m_sessionStartMs) / (60LL * 60 * 1000)}});
    }
}

bool MoodEngine::emitMood(const QString &name, const QJsonObject &payload)
{
    if (!m_router) return false;
    const qint64 now = nowMs();

    static const QHash<QString, qint64> cooldowns = {
        {QStringLiteral("mood.greeting"),     20LL * 60 * 60 * 1000},
        {QStringLiteral("mood.long_session"),  3LL * 60 * 60 * 1000},
        {QStringLiteral("mood.missed_you"),   20LL * 60 * 60 * 1000},
        // mood.stage_up: milestone-guarded, no time cooldown
    };
    // Global 1/hour cap on all proactive bubbles (incl. stage_up).
    if (now - m_lastProactiveMs < GLOBAL_PROACTIVE_COOLDOWN_MS) return false;
    const qint64 cd = cooldowns.value(name, 0);
    if (cd > 0 && m_lastFired.contains(name)
        && now - m_lastFired.value(name) < cd) return false;

    m_lastFired.insert(name, now);
    m_lastProactiveMs = now;
    QJsonObject ev = payload;
    ev.insert(QStringLiteral("type"), QStringLiteral("event"));
    ev.insert(QStringLiteral("source"), QStringLiteral("system"));
    ev.insert(QStringLiteral("event"), name);
    m_router->routeEvent(ev);
    return true;
}

void MoodEngine::onBondLevelChanged(int newLevel)
{
    if (!m_memory) return;
    const QString key = QStringLiteral("mood.stage_up.L%1").arg(newLevel);
    if (m_memory->hasMilestone(key)) return;
    m_memory->setMilestone(key);
    emitMood(QStringLiteral("mood.stage_up"),
             {{QStringLiteral("bondLevel"), newLevel},
              {QStringLiteral("stage"), stageName()}});
}

void MoodEngine::loadStats(const QString &configDir)
{
    StatisticsPersistence p(configDir);
    const QJsonObject o = p.loadSection(QStringLiteral("mood"));
    m_valence = qBound(-1.0, o.value(QStringLiteral("valence")).toDouble(0.0), 1.0);
    m_energy  = qBound(-1.0, o.value(QStringLiteral("energy")).toDouble(0.0), 1.0);
    m_lastGreetingDate = o.value(QStringLiteral("lastGreetingDate")).toString();
    m_lastProactiveMs = static_cast<qint64>(
        o.value(QStringLiteral("lastProactiveMs")).toDouble());
    const QJsonObject fired = o.value(QStringLiteral("lastFired")).toObject();
    for (auto it = fired.begin(); it != fired.end(); ++it)
        m_lastFired.insert(it.key(), static_cast<qint64>(it.value().toDouble()));

    m_lastSeenMs = static_cast<qint64>(
        o.value(QStringLiteral("lastSeenMs")).toDouble());
    if (m_lastSeenMs > 0 && nowMs() - m_lastSeenMs > MISS_ABSENCE_MS) {
        m_lonely = true;          // cleared by first session.start
        m_valence = 0.0;
        m_energy = 0.0;
    }
    updateTier();
}

void MoodEngine::saveStats(const QString &configDir)
{
    StatisticsPersistence p(configDir);
    QJsonObject fired;
    for (auto it = m_lastFired.begin(); it != m_lastFired.end(); ++it)
        fired.insert(it.key(), static_cast<double>(it.value()));
    p.saveSection(QStringLiteral("mood"),
                  {{QStringLiteral("valence"), m_valence},
                   {QStringLiteral("energy"), m_energy},
                   {QStringLiteral("lastGreetingDate"), m_lastGreetingDate},
                   {QStringLiteral("lastProactiveMs"),
                    static_cast<double>(m_lastProactiveMs)},
                   {QStringLiteral("lastFired"), fired},
                   {QStringLiteral("lastSeenMs"),
                    static_cast<double>(nowMs())}});
}
