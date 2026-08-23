#include "DesktopMotionController.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QtMath>

DesktopMotionController::DesktopMotionController(QObject *parent)
    : QObject(parent)
    , m_now([] { return QDateTime::currentMSecsSinceEpoch(); })
    , m_rng([] { return QRandomGenerator::global()->generateDouble(); })
    , m_activeWindow([] { return WindowGeom{}; })
    , m_shelf([](const QRect &screen) {
        return ShelfTarget{QPoint(screen.center().x(), screen.bottom())};
    })
    , m_screen([] {
        return QRect(0, 0, 1920, 1080);
    })
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &DesktopMotionController::onTimer);
}

void DesktopMotionController::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!m_enabled) {
        m_timer.stop();
        cancelMotion();
        return;
    }
    m_lastTickMs = m_now();
    scheduleNextWander();
    armTimer(kIdlePollMs);
}

void DesktopMotionController::setPetRect(const QRect &rect)
{
    m_petRect = rect;
    if (m_mode == Mode::Perched) {
        m_targetPetRect = rect;
    }
}

void DesktopMotionController::onFsmState(const QString &stateName)
{
    m_fsmState = stateName;
    if (m_mode == Mode::Falling) {
        return;
    }
    if (!isFsmIdle()) {
        if (m_mode == Mode::Wandering || m_mode == Mode::Perched || m_mode == Mode::HoppingOff) {
            cancelMotion();
        }
    }
}

void DesktopMotionController::notifyPerchedWindowMoved()
{
    if (m_mode != Mode::Perched) {
        return;
    }
    startFall();
}

void DesktopMotionController::tick()
{
    if (!m_enabled) {
        return;
    }

    const qint64 now = m_now();
    const qint64 dtMs = (m_lastTickMs > 0) ? qMax<qint64>(1, now - m_lastTickMs) : kMovePollMs;
    m_lastTickMs = now;

    switch (m_mode) {
    case Mode::Idle:
        tickIdle();
        break;
    case Mode::Wandering:
        tickWandering(dtMs);
        break;
    case Mode::Perched:
        tickPerched();
        break;
    case Mode::Falling:
        tickFalling(dtMs);
        break;
    case Mode::HoppingOff:
        tickHoppingOff(dtMs);
        break;
    }
}

void DesktopMotionController::onTimer()
{
    tick();
    if (!m_enabled) {
        return;
    }
    const int interval = (m_mode == Mode::Idle) ? kIdlePollMs : kMovePollMs;
    if (m_timer.interval() != interval) {
        armTimer(interval);
    }
}

void DesktopMotionController::scheduleNextWander()
{
    m_nextWanderAt = m_now() + rollWanderCooldownMs();
}

void DesktopMotionController::tryStartWanderOrPerch()
{
    if (!isFsmIdle()) {
        return;
    }

    const WindowGeom win = m_activeWindow();
    const bool wantPerch = m_forcePerch || (m_rng() < kPerchProbability);
    if (wantPerch && !win.frame.isNull()) {
        startPerch(win);
        return;
    }
    startWander();
}

void DesktopMotionController::startWander()
{
    const QRect screen = m_screen();
    const int rawDist = rollWanderDistance();
    // Snap to whole stride cycles so leg animation and window travel stay in sync.
    const int cycles = qMax(1, qRound(rawDist / double(kWalkStridePx)));
    const int distance = cycles * kWalkStridePx;
    const int dx = (m_rng() < 0.5) ? -distance : distance;
    m_walkSpeedPxPerSec = qMax(1, distance * 1000 / (cycles * kWalkCycleMs));

    QPoint target = m_petRect.topLeft() + QPoint(dx, 0);
    target.setX(qBound(screen.left(), target.x(), screen.right() - m_petRect.width()));

    m_moveTarget = target;
    m_targetPetRect = clampToScreen(QRect(target, m_petRect.size()));
    m_walkFrameSync = true;
    m_requestedClip = (dx < 0) ? QStringLiteral("walk_left") : QStringLiteral("walk_right");
    m_mode = Mode::Wandering;

    emit playClip(m_requestedClip);
    emit moveTo(m_targetPetRect);
    armTimer(kMovePollMs);
}

void DesktopMotionController::startPerch(const WindowGeom &win)
{
    const int x = win.frame.center().x() - m_petRect.width() / 2;
    const int y = win.frame.top() - m_petRect.height();
    const QRect target = clampToScreen(QRect(QPoint(x, y), m_petRect.size()));

    m_perchedWindowId = win.id;
    m_perchedWindowFrame = win.frame;
    m_perchEndAt = m_now() + rollPerchDwellMs();
    m_targetPetRect = target;
    m_petRect = target;
    m_requestedClip = QStringLiteral("sit");
    m_mode = Mode::Perched;

    emit playClip(m_requestedClip);
    emit moveTo(m_targetPetRect);
    armTimer(kMovePollMs);
}

void DesktopMotionController::startHopOff()
{
    const QRect screen = m_screen();
    const ShelfTarget shelf = m_shelf(screen);
    const int landY = shelf.landTopCenter.y() - m_petRect.height();
    m_moveTarget = QPoint(m_petRect.x(), landY);
    m_targetPetRect = clampToScreen(QRect(m_moveTarget, m_petRect.size()));
    m_requestedClip = QStringLiteral("hop_off");
    m_mode = Mode::HoppingOff;

    emit playClip(m_requestedClip);
    emit moveTo(m_targetPetRect);
    armTimer(kMovePollMs);
}

void DesktopMotionController::startFall()
{
    const QRect screen = m_screen();
    const ShelfTarget shelf = m_shelf(screen);
    m_fallLandY = shelf.landTopCenter.y() - m_petRect.height();
    m_moveTarget = QPoint(m_petRect.x(), m_fallLandY);
    m_targetPetRect = clampToScreen(QRect(m_moveTarget, m_petRect.size()));
    m_requestedClip = QStringLiteral("fall");
    m_mode = Mode::Falling;

    emit playClip(m_requestedClip);
    armTimer(kMovePollMs);
}

void DesktopMotionController::cancelMotion()
{
    m_mode = Mode::Idle;
    m_walkFrameSync = false;
    m_requestedClip.clear();
    m_perchedWindowId = 0;
    m_perchedWindowFrame = QRect();
    scheduleNextWander();
    armTimer(kIdlePollMs);
}

void DesktopMotionController::tickIdle()
{
    if (!isFsmIdle()) {
        return;
    }
    if (m_now() < m_nextWanderAt) {
        return;
    }
    tryStartWanderOrPerch();
}

void DesktopMotionController::tickWandering(qint64 dtMs)
{
    if (!m_walkFrameSync) {
        const QPoint next = stepToward(m_petRect.topLeft(), m_moveTarget,
                                       m_walkSpeedPxPerSec * int(dtMs) / 1000);
        m_petRect.moveTopLeft(next);
        m_targetPetRect = m_petRect;
        emit moveTo(m_petRect);
    }

    if (m_petRect.topLeft() == m_moveTarget) {
        finishToIdle();
    }
}

void DesktopMotionController::advanceWalkStep()
{
    if (m_mode != Mode::Wandering || !m_walkFrameSync) {
        return;
    }
    const int stepPx = qMax(1, kWalkStridePx / kWalkFrameCount);
    const QPoint next = stepToward(m_petRect.topLeft(), m_moveTarget, stepPx);
    m_petRect.moveTopLeft(next);
    m_targetPetRect = m_petRect;
    emit moveTo(m_petRect);

    if (m_petRect.topLeft() == m_moveTarget) {
        finishToIdle();
    }
}

void DesktopMotionController::tickPerched()
{
    const WindowGeom win = m_activeWindow();
    if (!win.frame.isNull() && win.id == m_perchedWindowId &&
        win.frame != m_perchedWindowFrame) {
        notifyPerchedWindowMoved();
        return;
    }

    if (m_now() >= m_perchEndAt) {
        startHopOff();
    }
}

void DesktopMotionController::tickFalling(qint64 dtMs)
{
    const QPoint next = stepToward(m_petRect.topLeft(), m_moveTarget,
                                   kFallSpeedPxPerSec * int(dtMs) / 1000);
    m_petRect.moveTopLeft(next);
    m_targetPetRect = m_petRect;
    emit moveTo(m_petRect);

    if (m_petRect.topLeft() == m_moveTarget) {
        m_requestedClip = QStringLiteral("land");
        emit playClip(m_requestedClip);
        finishToIdle();
    }
}

void DesktopMotionController::tickHoppingOff(qint64 dtMs)
{
    const QPoint next = stepToward(m_petRect.topLeft(), m_moveTarget,
                                   kHopSpeedPxPerSec * int(dtMs) / 1000);
    m_petRect.moveTopLeft(next);
    m_targetPetRect = m_petRect;
    emit moveTo(m_petRect);

    if (m_petRect.topLeft() == m_moveTarget) {
        finishToIdle();
    }
}

void DesktopMotionController::finishToIdle()
{
    const bool wasWandering = (m_mode == Mode::Wandering);
    m_walkFrameSync = false;
    m_mode = Mode::Idle;
    m_perchedWindowId = 0;
    m_perchedWindowFrame = QRect();
    if (wasWandering) {
        emit playClip(QStringLiteral("idle"));
    }
    scheduleNextWander();
    armTimer(kIdlePollMs);
}

void DesktopMotionController::armTimer(int intervalMs)
{
    if (!m_enabled) {
        return;
    }
    if (m_timer.interval() != intervalMs) {
        m_timer.setInterval(intervalMs);
    }
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

int DesktopMotionController::rollWanderCooldownMs() const
{
    const double r = m_rng();
    return kWanderCooldownMinMs +
           int(r * double(kWanderCooldownMaxMs - kWanderCooldownMinMs));
}

int DesktopMotionController::rollPerchDwellMs() const
{
    const double r = m_rng();
    return kPerchDwellMinMs +
           int(r * double(kPerchDwellMaxMs - kPerchDwellMinMs));
}

int DesktopMotionController::rollWanderDistance() const
{
    const double r = m_rng();
    return kWanderDistMinPx +
           int(r * double(kWanderDistMaxPx - kWanderDistMinPx));
}

bool DesktopMotionController::isFsmIdle() const
{
    return m_fsmState == QStringLiteral("Idle");
}

QRect DesktopMotionController::clampToScreen(const QRect &rect) const
{
    const QRect screen = m_screen();
    QRect out = rect;
    if (out.left() < screen.left()) {
        out.moveLeft(screen.left());
    }
    if (out.right() > screen.right()) {
        out.moveRight(screen.right());
    }
    if (out.top() < screen.top()) {
        out.moveTop(screen.top());
    }
    if (out.bottom() > screen.bottom()) {
        out.moveBottom(screen.bottom());
    }
    return out;
}

QPoint DesktopMotionController::stepToward(const QPoint &from, const QPoint &to,
                                         int stepPx) const
{
    if (stepPx < 1) {
        stepPx = 1;
    }
    if (from == to) {
        return from;
    }

    const int dx = to.x() - from.x();
    const int dy = to.y() - from.y();
    const double dist = qSqrt(double(dx * dx + dy * dy));
    if (dist <= stepPx) {
        return to;
    }

    const double scale = double(stepPx) / dist;
    return QPoint(from.x() + int(qRound(dx * scale)),
                  from.y() + int(qRound(dy * scale)));
}
