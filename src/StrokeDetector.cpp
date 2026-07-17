#include "StrokeDetector.h"

#include <QLineF>
#include <QtMath>

void StrokeDetector::press(const QPoint &globalPos, qint64 nowMs)
{
    m_phase = Phase::Undecided;
    m_pressPos = globalPos;
    m_lastPos = globalPos;
    m_lastMs = nowMs;
    m_dir = 0;
    m_extremumX = globalPos.x();
    m_reversals = 0;
    m_pendingPulses = 0;
    m_dragEngaged = false;
    m_speedEma = 0.0;
}

void StrokeDetector::move(const QPoint &globalPos, qint64 nowMs)
{
    if (m_phase == Phase::Idle) return;
    trackSpeed(globalPos, nowMs);

    if (m_phase != Phase::Dragging) {
        const QPoint totalDelta = globalPos - m_pressPos;
        if (m_phase == Phase::Undecided
            && totalDelta.manhattanLength() >= DISPLACEMENT_BUDGET_PX) {
            // Budget spent before a stroke formed: it's a drag. MainWindow
            // converts and moves the window at the FULL delta, so the motion
            // is positionally identical to a drag from the start.
            m_phase = Phase::Dragging;
            m_dragEngaged = true;
            m_lastPos = globalPos;
            m_lastMs = nowMs;
            return;
        }

        // Count x-direction reversals with jitter rejection via extremum.
        // Direction establishment is PRESS-RELATIVE (not per-move delta):
        // slow 6px steps must still establish a direction over several moves.
        const int x = globalPos.x();
        if (m_dir == 0) {
            const int fromPress = x - m_pressPos.x();
            if (qAbs(fromPress) >= REVERSAL_MIN_PX) {
                m_dir = (fromPress > 0) ? 1 : -1;
                m_extremumX = x;
            }
        } else if (m_dir > 0) {
            if (x >= m_extremumX) {
                m_extremumX = x;
            } else if (m_extremumX - x >= REVERSAL_MIN_PX) {
                ++m_reversals;
                m_dir = -1;
                m_extremumX = x;
                onReversal();
            }
        } else {
            if (x <= m_extremumX) {
                m_extremumX = x;
            } else if (x - m_extremumX >= REVERSAL_MIN_PX) {
                ++m_reversals;
                m_dir = 1;
                m_extremumX = x;
                onReversal();
            }
        }
    }

    m_lastPos = globalPos;
    m_lastMs = nowMs;
}

StrokeDetector::Phase StrokeDetector::release(const QPoint &globalPos, qint64 nowMs)
{
    const Phase result = m_phase;
    if (m_phase != Phase::Idle) {
        // Zero-distance sample: a cursor parked ≥200ms before release decays
        // the EMA toward 0, so a slow end to a fast drag is never a false toss.
        trackSpeed(m_lastPos, nowMs);
        if (result == Phase::Undecided) m_speedEma = 0.0;
    }
    // Reset detection state but KEEP m_speedEma for the caller's toss check.
    m_phase = Phase::Idle;
    m_dir = 0;
    m_reversals = 0;
    m_pendingPulses = 0;
    m_dragEngaged = false;
    return result;
}

void StrokeDetector::cancel()
{
    m_phase = Phase::Idle;
    m_dir = 0;
    m_reversals = 0;
    m_pendingPulses = 0;
    m_dragEngaged = false;
    m_speedEma = 0.0;
}

bool StrokeDetector::takeDragEngaged()
{
    const bool v = m_dragEngaged;
    m_dragEngaged = false;
    return v;
}

int StrokeDetector::takeStrokePulses()
{
    const int v = m_pendingPulses;
    m_pendingPulses = 0;
    return v;
}

void StrokeDetector::onReversal()
{
    // The 2nd reversal confirms stroking; spec: that reversal and every
    // further one is one stroke endpoint → one pet pulse each.
    if (m_reversals >= 2) {
        if (m_phase == Phase::Undecided) m_phase = Phase::Stroking;
        ++m_pendingPulses;
    }
}

void StrokeDetector::trackSpeed(const QPoint &p, qint64 nowMs)
{
    if (m_lastMs <= 0) return;
    const qint64 dt = nowMs - m_lastMs;
    if (dt <= 0) return;
    const double dist = QLineF(m_lastPos, p).length();
    const double inst = dist * 1000.0 / double(dt);   // px/s
    const double alpha = 1.0 - qExp(-double(dt) / SPEED_TAU_MS);
    m_speedEma = alpha * inst + (1.0 - alpha) * m_speedEma;
}
