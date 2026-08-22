#include "DesktopGeometry.h"

#include <QGuiApplication>
#include <QScreen>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <CoreGraphics/CoreGraphics.h>
#include <unistd.h>
#elif defined(Q_OS_LINUX) && defined(SEELIE_HAS_X11)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace DesktopGeometry {

DesktopMotionController::ShelfTarget shelfFromGeometries(const QRect &fullScreen,
                                                         const QRect &available)
{
    const QPoint center(fullScreen.center().x(), fullScreen.bottom());

    if (available.bottom() < fullScreen.bottom()) {
        return {QPoint(available.center().x(), available.bottom())};
    }
    if (available.top() > fullScreen.top()) {
        return {QPoint(available.center().x(), available.top())};
    }
    return {center};
}

QRect currentScreenAvailable(const QPoint &anchor)
{
    if (QScreen *screen = QGuiApplication::screenAt(anchor)) {
        return screen->availableGeometry();
    }
    if (QScreen *primary = QGuiApplication::primaryScreen()) {
        return primary->availableGeometry();
    }
    return QRect(0, 0, 1920, 1080);
}

DesktopMotionController::ShelfTarget shelfForScreen(const QRect &screen)
{
    if (QScreen *qs = QGuiApplication::screenAt(screen.center())) {
        return shelfFromGeometries(qs->geometry(), qs->availableGeometry());
    }
    return shelfFromGeometries(screen, screen);
}

#if defined(Q_OS_WIN)

DesktopMotionController::WindowGeom activeWindow()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindow(hwnd) || hwnd == GetShellWindow()) {
        return {};
    }

    DWORD fgPid = 0;
    GetWindowThreadProcessId(hwnd, &fgPid);
    if (fgPid == GetCurrentProcessId()) {
        return {};
    }
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return {};
    }

    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) {
        return {};
    }
    if (wr.right <= wr.left || wr.bottom <= wr.top) {
        return {};
    }

    const QRect frame(wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);
    const qint64 id = static_cast<qint64>(reinterpret_cast<quintptr>(hwnd));
    return {frame, id};
}

#elif defined(Q_OS_MAC)

DesktopMotionController::WindowGeom activeWindow()
{
    const pid_t ownPid = getpid();

    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windows) {
        return {};
    }

    DesktopMotionController::WindowGeom best;
    const CFIndex count = CFArrayGetCount(windows);

    for (CFIndex i = 0; i < count; ++i) {
        auto *win = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));

        auto *pidRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(win, kCGWindowOwnerPID));
        if (pidRef) {
            int pid = 0;
            CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
            if (static_cast<pid_t>(pid) == ownPid) {
                continue;
            }
        }

        auto *layerRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(win, kCGWindowLayer));
        int layer = 0;
        if (layerRef) {
            CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
        }
        if (layer != 0) {
            continue;
        }

        auto *boundsRef = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(win, kCGWindowBounds));
        if (!boundsRef) {
            continue;
        }

        CGRect bounds = CGRectZero;
        if (!CGRectMakeWithDictionaryRepresentation(boundsRef, &bounds)) {
            continue;
        }
        if (bounds.size.width < 80 || bounds.size.height < 80) {
            continue;
        }

        auto *idRef = static_cast<CFNumberRef>(
            CFDictionaryGetValue(win, kCGWindowNumber));
        qint64 winId = 0;
        if (idRef) {
            int wid = 0;
            CFNumberGetValue(idRef, kCFNumberIntType, &wid);
            winId = wid;
        }

        // CGWindow bounds are in screen coordinates with origin top-left in
        // the dictionary, but Y increases downward — matches Qt global coords.
        const QRect frame(int(bounds.origin.x),
                          int(bounds.origin.y),
                          int(bounds.size.width),
                          int(bounds.size.height));
        best = {frame, winId};
        break;
    }

    CFRelease(windows);
    return best;
}

#elif defined(Q_OS_LINUX) && defined(SEELIE_HAS_X11)

DesktopMotionController::WindowGeom activeWindow()
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        return {};
    }

    DesktopMotionController::WindowGeom result;
    const Atom netActive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    if (netActive != None) {
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long nitems = 0, bytesAfter = 0;
        unsigned char *prop = nullptr;
        if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), netActive, 0, 1, False,
                               XA_WINDOW, &actualType, &actualFormat, &nitems,
                               &bytesAfter, &prop) == Success && prop && nitems == 1) {
            const Window active = *reinterpret_cast<Window *>(prop);
            XFree(prop);

            if (active != 0) {
                Window root = 0;
                int x = 0, y = 0;
                unsigned int w = 0, h = 0, border = 0, depth = 0;
                if (XGetGeometry(dpy, active, &root, &x, &y, &w, &h, &border, &depth)) {
                    int absX = x;
                    int absY = y;
                    Window child = 0;
                    XTranslateCoordinates(dpy, active, root, 0, 0, &absX, &absY, &child);
                    result.frame = QRect(absX, absY, int(w), int(h));
                    result.id = static_cast<qint64>(active);
                }
            }
        }
    }

    XCloseDisplay(dpy);
    return result;
}

#else

DesktopMotionController::WindowGeom activeWindow()
{
    return {};
}

#endif

} // namespace DesktopGeometry
