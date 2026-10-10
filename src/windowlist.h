#pragma once

#include <QList>
#include <QRect>

namespace WindowList {

// Visible top-level windows on the current workspace, top-most first, in
// logical virtual-desktop coordinates. X11 only (uses xprop/xwininfo from
// x11-utils); returns an empty list on Wayland or when the tools are missing.
QList<QRect> visibleWindows(qreal devicePixelRatio = 1.0);

}  // namespace WindowList
