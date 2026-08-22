#ifndef DESKTOPGEOMETRY_H
#define DESKTOPGEOMETRY_H

#include "DesktopMotionController.h"

#include <QRect>
#include <QPoint>

namespace DesktopGeometry {

/// Screen work area containing @p anchor (Qt availableGeometry).
QRect currentScreenAvailable(const QPoint &anchor);

/// Foreground / frontmost non-Seelie window, or an empty frame when none.
DesktopMotionController::WindowGeom activeWindow();

/// Shelf landing point for a screen: taskbar / Dock top edge, else screen bottom.
DesktopMotionController::ShelfTarget shelfForScreen(const QRect &screen);

/// Pure helper: derive shelf from full vs available geometry (unit-test seam).
DesktopMotionController::ShelfTarget shelfFromGeometries(const QRect &fullScreen,
                                                         const QRect &available);

} // namespace DesktopGeometry

#endif // DESKTOPGEOMETRY_H
