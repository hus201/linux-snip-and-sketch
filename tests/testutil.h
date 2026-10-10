#pragma once
// Shared helpers for the Qt test suites. Tests run with QT_QPA_PLATFORM=offscreen
// (CTest sets it), so no real screen or window manager is needed.

#include "canvas.h"

#include <QApplication>
#include <QFont>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>

namespace TestUtil {

// Points the app's QSettings at a throw-away directory so tests never read or
// write the real user configuration. Create it before anything uses QSettings.
class IsolatedSettings
{
public:
    IsolatedSettings() { qputenv("XDG_CONFIG_HOME", m_dir.path().toLocal8Bit()); }
    QString path() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
};

inline void mouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button = Qt::LeftButton)
{
    const Qt::MouseButton changed = type == QEvent::MouseMove ? Qt::NoButton : button;
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), changed, held, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// Press, move through `points`, release — all in WIDGET coordinates.
inline void dragWidget(QWidget *w, const QList<QPointF> &points, Qt::MouseButton button = Qt::LeftButton)
{
    mouse(w, QEvent::MouseButtonPress, points.first(), button);
    for (const QPointF &p : points.mid(1))
        mouse(w, QEvent::MouseMove, p, button);
    mouse(w, QEvent::MouseButtonRelease, points.last(), button);
}

// Drag on a canvas through `points` given in IMAGE coordinates.
inline void drag(Canvas *c, const QList<QPointF> &points)
{
    QList<QPointF> widget;
    for (const QPointF &p : points)
        widget << c->imageToWidget(p);
    dragWidget(c, widget);
}

// 800×500 white image with text and a blue box: easy to check pixels on.
inline QImage testImage()
{
    QImage img(800, 500, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    QFont f = p.font();
    f.setPixelSize(36);
    p.setFont(f);
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawText(40, 100, QStringLiteral("Hello from Snip & Sketch for Linux"));
    p.drawText(40, 200, QStringLiteral("Highlight me please"));
    p.fillRect(500, 280, 200, 150, QColor(0x88, 0x88, 0xcc));
    return img;
}

// Finds a toolbar button by the start of its tooltip ("Undo", "Crop"...).
inline QToolButton *button(QWidget &w, const QString &tooltipStart)
{
    for (QToolButton *b : w.findChildren<QToolButton *>())
        if (b->toolTip().startsWith(tooltipStart))
            return b;
    return nullptr;
}

}  // namespace TestUtil
