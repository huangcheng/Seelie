#ifndef FULLSCREENWATCHER_H
#define FULLSCREENWATCHER_H

#include <QObject>

class QTimer;

/**
 * FullscreenWatcher polls the OS to detect whether a fullscreen non-Seelie
 * application is the foreground window.  When the state changes it emits
 * fullscreenAppStarted() or fullscreenAppStopped().
 *
 * Platform support:
 *   Windows  — GetForegroundWindow + GetMonitorInfo, covers both exclusive
 *               fullscreen and borderless-windowed (e.g. Genshin Impact).
 *   macOS    — CGWindowListCopyWindowInfo; covers borderless fullscreen.
 *               True fullscreen apps live in their own Space, so overlap
 *               is rare but the check still runs correctly.
 *   Linux    — X11: _NET_ACTIVE_WINDOW + _NET_WM_STATE_FULLSCREEN scan
 *               (built only when SEELIE_HAS_X11 is defined by CMake).
 *               Wayland / no-X11-dev-files: no-op returning false.
 */
class FullscreenWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FullscreenWatcher(QObject *parent = nullptr);
    ~FullscreenWatcher() override;

    void start();
    void stop();
    bool isRunning() const;

signals:
    void fullscreenAppStarted();
    void fullscreenAppStopped();

private slots:
    void onPoll();

protected:
    virtual bool checkFullscreen();

    QTimer *m_timer = nullptr;
    bool m_prevState = false;

    static constexpr int POLL_INTERVAL_MS = 2000;
};

#endif // FULLSCREENWATCHER_H
