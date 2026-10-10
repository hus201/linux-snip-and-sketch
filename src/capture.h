#pragma once

#include <QImage>
#include <QList>
#include <QPixmap>
#include <QRect>
#include <QString>

#include <functional>

class QScreen;

// One monitor's worth of screenshot. `pixmap` has its devicePixelRatio set so
// that drawing it at (0, 0) covers exactly `screen->geometry().size()` logical px.
struct ScreenShot {
    QScreen *screen = nullptr;
    QRect geometry;  // logical, in virtual-desktop coordinates
    QPixmap pixmap;
};
using ScreenShots = QList<ScreenShot>;

namespace Capture {

// True when the screen must be captured through xdg-desktop-portal (Wayland).
bool usePortal();

// Capture every monitor. Calls `done` exactly once — synchronously on X11,
// asynchronously via D-Bus on Wayland. On failure `shots` is empty.
void grab(std::function<void(const ScreenShots &shots, const QString &error)> done);

// Stitch all monitors into one image (used for "Fullscreen snip").
QImage stitch(const ScreenShots &shots);

}  // namespace Capture
