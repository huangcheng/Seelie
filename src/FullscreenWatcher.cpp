#include "FullscreenWatcher.h"

#include <QTimer>
#include <QDebug>

// ── Platform-specific fullscreen detection ──────────────────────────────────

#if defined(Q_OS_WIN)

#include <windows.h>

static bool platformCheckFullscreen()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    if (!IsWindow(hwnd)) return false;

    // The shell (desktop) is never a game
    if (hwnd == GetShellWindow()) return false;

    // Skip our own process
    DWORD fgPid = 0;
    GetWindowThreadProcessId(hwnd, &fgPid);
    if (fgPid == GetCurrentProcessId()) return false;

    // Require the window to be visible and not minimised
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;

    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) return false;

    // Width/height must be non-zero
    if (wr.right <= wr.left || wr.bottom <= wr.top) return false;

    // Get the monitor the foreground window sits on
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hmon, &mi)) return false;

    // Fullscreen / borderless-windowed: window rect equals or exceeds the
    // full monitor rect (rcMonitor, not rcWork — the work area excludes the
    // taskbar and we want to catch borderless windows that go under it).
    return wr.left  <= mi.rcMonitor.left
        && wr.top   <= mi.rcMonitor.top
        && wr.right >= mi.rcMonitor.right
        && wr.bottom >= mi.rcMonitor.bottom;
}

#elif defined(Q_OS_MAC)

#include <CoreGraphics/CoreGraphics.h>
#include <unistd.h>
#include <vector>

static bool platformCheckFullscreen()
{
    const pid_t ownPid = getpid();

    // Enumerate connected displays
    uint32_t displayCount = 0;
    CGGetActiveDisplayList(0, nullptr, &displayCount);
    if (displayCount == 0) return false;

    std::vector<CGDirectDisplayID> displays(displayCount);
    CGGetActiveDisplayList(displayCount, displays.data(), &displayCount);

    // Collect screen bounds for all displays once
    std::vector<CGRect> screens(displayCount);
    for (uint32_t d = 0; d < displayCount; ++d)
        screens[d] = CGDisplayBounds(displays[d]);

    // List all on-screen windows (excludes desktop/wallpaper elements)
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windows) return false;

    bool result = false;
    const CFIndex count = CFArrayGetCount(windows);

    for (CFIndex i = 0; i < count && !result; ++i) {
        auto *win = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));

        // Skip our own windows
        auto *pidRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(win, kCGWindowOwnerPID));
        if (pidRef) {
            int pid = 0;
            CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
            if (static_cast<pid_t>(pid) == ownPid) continue;
        }

        // Only inspect layer-0 windows (normal app windows).
        // System UI (menu bar, Dock, overlays) live at higher layers.
        auto *layerRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(win, kCGWindowLayer));
        int layer = 0;
        if (layerRef) CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
        if (layer != 0) continue;

        // Get window bounds
        auto *boundsRef = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(win, kCGWindowBounds));
        if (!boundsRef) continue;

        CGRect bounds = CGRectZero;
        if (!CGRectMakeWithDictionaryRepresentation(boundsRef, &bounds)) continue;
        if (bounds.size.width < 100 || bounds.size.height < 100) continue;

        // Check against each display (allow 1 px tolerance for HiDPI)
        for (uint32_t d = 0; d < displayCount; ++d) {
            if (bounds.size.width  >= screens[d].size.width  - 1 &&
                bounds.size.height >= screens[d].size.height - 1) {
                result = true;
                break;
            }
        }
    }

    CFRelease(windows);
    return result;
}

#else

// Linux / other Unix: fullscreen detection not yet implemented.
// Returns false so Gaming Mode is a harmless no-op on these platforms.
static bool platformCheckFullscreen()
{
    return false;
}

#endif

// ── FullscreenWatcher implementation ────────────────────────────────────────

FullscreenWatcher::FullscreenWatcher(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(POLL_INTERVAL_MS);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &FullscreenWatcher::onPoll);
}

FullscreenWatcher::~FullscreenWatcher() = default;

void FullscreenWatcher::start()
{
    m_prevState = false;
    m_timer->start();
    qDebug() << "FullscreenWatcher: started (interval" << POLL_INTERVAL_MS << "ms)";
}

void FullscreenWatcher::stop()
{
    m_timer->stop();
    qDebug() << "FullscreenWatcher: stopped";
}

bool FullscreenWatcher::isRunning() const
{
    return m_timer->isActive();
}

void FullscreenWatcher::onPoll()
{
    const bool current = checkFullscreen();
    if (current == m_prevState) return;
    m_prevState = current;
    if (current) {
        qDebug() << "FullscreenWatcher: fullscreen app detected";
        emit fullscreenAppStarted();
    } else {
        qDebug() << "FullscreenWatcher: fullscreen app gone";
        emit fullscreenAppStopped();
    }
}

bool FullscreenWatcher::checkFullscreen()
{
    return platformCheckFullscreen();
}
