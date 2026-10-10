#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

// The app's own line-icon set (24px grid), tinted to the current palette so it
// looks the same on every desktop theme and inside the AppImage.
namespace Icons {

QIcon get(const QString &name);                 // tinted with the palette text colour
QIcon get(const QString &name, const QColor &color);

// A pen/highlighter icon with a colour bar showing the current ink.
QIcon ink(const QString &name, const QColor &ink);

}  // namespace Icons
