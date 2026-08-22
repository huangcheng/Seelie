#ifndef DESKTOPMOTIONCONTROLLER_H
#define DESKTOPMOTIONCONTROLLER_H

#include <QObject>
#include <QRect>
#include <QTimer>
#include <QString>
#include <functional>

/**
 * Desktop wander / perch / fall state machine for sprite pets with
 * character.desktopMotion enabled. Platform geometry is injected — this
 * class performs only math and mode transitions.
 */
class DesktopMotionController : public QObject
{
    Q_OBJECT
public:
    enum class Mode {
        Idle,
        Wandering,
        Perched,
        Falling,
        HoppingOff,
    };

    struct WindowGeom {
        QRect frame;
        qint64 id = 0;
    };

    struct ShelfTarget {
        QPoint landTopCenter;
    };

    using NowFn = std::function<qint64()>;
    using RngFn = std::function<double()>; // [0,1)
    using ActiveWindowFn = std::function<WindowGeom()>;
    using ShelfFn = std::function<ShelfTarget(const QRect &screen)>;
    using ScreenFn = std::function<QRect()>;

    explicit DesktopMotionController(QObject *parent = nullptr);

    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }

    void setPetRect(const QRect &rect);
    QRect petRect() const { return m_petRect; }

    void onFsmState(const QString &stateName);

    Mode mode() const { return m_mode; }
    QRect targetPetRect() const { return m_targetPetRect; }
    QString requestedClip() const { return m_requestedClip; }

    void notifyPerchedWindowMoved();

    void tick();

    // --- Test seams ------------------------------------------------------
    void setNowFn(NowFn fn) { m_now = std::move(fn); }
    void setRngFn(RngFn fn) { m_rng = std::move(fn); }
    void setActiveWindowFn(ActiveWindowFn fn) { m_activeWindow = std::move(fn); }
    void setShelfFn(ShelfFn fn) { m_shelf = std::move(fn); }
    void setScreenFn(ScreenFn fn) { m_screen = std::move(fn); }
    /// When true, the next wander roll always attempts perch (if a window exists).
    void setForcePerch(bool force) { m_forcePerch = force; }

signals:
    void moveTo(const QRect &rect);
    void playClip(const QString &clipName);

private slots:
    void onTimer();

private:
    void scheduleNextWander();
    void tryStartWanderOrPerch();
    void startWander();
    void startPerch(const WindowGeom &win);
    void startHopOff();
    void startFall();
    void cancelMotion();
    void tickIdle();
    void tickWandering(qint64 dtMs);
    void tickPerched();
    void tickFalling(qint64 dtMs);
    void tickHoppingOff(qint64 dtMs);
    void finishToIdle();
    void armTimer(int intervalMs);

    int rollWanderCooldownMs() const;
    int rollPerchDwellMs() const;
    int rollWanderDistance() const;

    bool isFsmIdle() const;
    QRect clampToScreen(const QRect &rect) const;
    QPoint stepToward(const QPoint &from, const QPoint &to, int stepPx) const;

    bool m_enabled = false;
    Mode m_mode = Mode::Idle;
    QString m_fsmState = QStringLiteral("Idle");
    QRect m_petRect;
    QRect m_targetPetRect;
    QString m_requestedClip;

    QPoint m_moveTarget;
    int m_fallLandY = 0;

    qint64 m_nextWanderAt = 0;
    qint64 m_perchEndAt = 0;
    qint64 m_lastTickMs = 0;

    qint64 m_perchedWindowId = 0;
    QRect m_perchedWindowFrame;

    bool m_forcePerch = false;

    NowFn m_now;
    RngFn m_rng;
    ActiveWindowFn m_activeWindow;
    ShelfFn m_shelf;
    ScreenFn m_screen;

    QTimer m_timer;

    static constexpr int kWanderCooldownMinMs = 8000;
    static constexpr int kWanderCooldownMaxMs = 20000;
    static constexpr int kWanderDistMinPx = 40;
    static constexpr int kWanderDistMaxPx = 120;
    static constexpr double kPerchProbability = 0.08;
    static constexpr int kPerchDwellMinMs = 8000;
    static constexpr int kPerchDwellMaxMs = 20000;
    static constexpr int kWalkSpeedPxPerSec = 120;
    static constexpr int kFallSpeedPxPerSec = 800;
    static constexpr int kHopSpeedPxPerSec = 200;
    static constexpr int kIdlePollMs = 1000;
    static constexpr int kMovePollMs = 50;
};

#endif // DESKTOPMOTIONCONTROLLER_H
