#ifndef STROKEDETECTOR_H
#define STROKEDETECTOR_H

#include <QPoint>

/**
 * StrokeDetector (TouchReactions, Spec 3) distinguishes petting strokes from
 * window drags inside one left-button press, and measures release velocity
 * for toss detection. Pure C++ (no QObject) so it is unit-testable without
 * widgets; MainWindow feeds it global cursor positions with timestamps.
 *
 * Lifecycle per press: press() → move()* → release() | cancel().
 *
 * Decision rules (spec §1):
 *  - Undecided→Dragging when displacement from press ≥ 15 px (budget spent
 *    before 2 reversals). takeDragEngaged() reports the conversion once so
 *    MainWindow can start moving the window retroactively at full delta.
 *  - Undecided→Stroking at the 2nd x-direction reversal (each reversal ≥ 8 px
 *    from the last extremum, rejecting jitter). The 2nd and every further
 *    reversal queue one pet pulse each, consumed via takeStrokePulses().
 *  - release() returns the final Phase (Undecided == "it was a click") and
 *    keeps releaseSpeedPxPerSec() readable for the toss check; cancel()
 *    discards everything (leaveEvent / double-click).
 */
class StrokeDetector
{
public:
    enum class Phase { Idle, Undecided, Stroking, Dragging };

    void press(const QPoint &globalPos, qint64 nowMs);
    void move(const QPoint &globalPos, qint64 nowMs);
    Phase release(const QPoint &globalPos, qint64 nowMs);
    void cancel();

    Phase phase() const { return m_phase; }
    bool takeDragEngaged();
    int takeStrokePulses();
    double releaseSpeedPxPerSec() const { return m_speedEma; }

    static constexpr qreal REVERSAL_MIN_PX = 8.0;
    static constexpr qreal DISPLACEMENT_BUDGET_PX = 15.0;
    static constexpr double TOSS_SPEED_PX_PER_SEC = 1500.0;

private:
    void trackSpeed(const QPoint &p, qint64 nowMs);
    void onReversal();

    Phase m_phase = Phase::Idle;
    QPoint m_pressPos;
    QPoint m_lastPos;
    qint64 m_lastMs = 0;

    // Reversal tracking on x (strokes are predominantly horizontal)
    int m_dir = 0;          // +1 rightward, -1 leftward, 0 unestablished
    int m_extremumX = 0;    // farthest x in the current direction
    int m_reversals = 0;
    int m_pendingPulses = 0;
    bool m_dragEngaged = false;

    // Velocity EMA, 100 ms time constant
    double m_speedEma = 0.0;
    static constexpr double SPEED_TAU_MS = 100.0;
};

#endif // STROKEDETECTOR_H
