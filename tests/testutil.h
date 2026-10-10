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

// A small "Weekly report" card with a bar chart, used for documentation
// screenshots (looks like something you'd actually snip).
inline QImage demoReport(int w = 900, int h = 560)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor(0xf7, 0xf8, 0xfa));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(24, 24, w - 48, h - 48), 14, 14);

    QFont f = p.font();
    f.setPixelSize(30);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x1f, 0x23, 0x28));
    p.drawText(60, 86, QStringLiteral("Weekly report"));
    f.setPixelSize(17);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor(0x6b, 0x72, 0x80));
    p.drawText(60, 118, QStringLiteral("Visits per day · last week"));

    const int values[] = {620, 840, 760, 1284, 930, 540};
    const char *days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int base = h - 150, top = 170, barW = 70, gap = 50, x0 = 80;
    p.setPen(QColor(0xe5, 0xe7, 0xeb));
    for (int i = 0; i <= 3; ++i)
        p.drawLine(60, base - i * (base - top) / 3, w - 60, base - i * (base - top) / 3);
    for (int i = 0; i < 6; ++i) {
        const int bh = values[i] * (base - top) / 1300;
        const QRectF bar(x0 + i * (barW + gap), base - bh, barW, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(i == 3 ? QColor(0x25, 0x63, 0xeb) : QColor(0x93, 0xb4, 0xf5));
        p.drawRoundedRect(bar, 6, 6);
        p.setPen(QColor(0x6b, 0x72, 0x80));
        p.drawText(QRectF(bar.left(), base + 8, barW, 24), Qt::AlignCenter, QLatin1String(days[i]));
    }
    f.setPixelSize(19);
    p.setFont(f);
    p.setPen(QColor(0x1f, 0x23, 0x28));
    p.drawText(60, h - 70, QStringLiteral("Peak on Thursday: 1,284 visits (+38% vs. average)"));
    return img;
}

// A fake desktop (wallpaper, a window showing demoReport(), a panel) for
// screenshots of the selection overlay — never the real screen.
inline QPixmap demoDesktop(int w = 1280, int h = 720)
{
    QPixmap pm(w, h);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, w, h);
    sky.setColorAt(0, QColor(0x1e, 0x3a, 0x8a));
    sky.setColorAt(1, QColor(0x6d, 0x28, 0xd9));
    p.fillRect(pm.rect(), sky);

    const QRect win(150, 70, 960, 560);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawRoundedRect(win.adjusted(-2, 4, 2, 10), 12, 12);
    p.setBrush(QColor(0xf3, 0xf4, 0xf6));
    p.drawRoundedRect(win, 10, 10);
    p.setBrush(QColor(0xe5, 0xe7, 0xeb));
    p.drawRoundedRect(QRect(win.left(), win.top(), win.width(), 36), 10, 10);
    p.drawRect(QRect(win.left(), win.top() + 20, win.width(), 16));
    const QColor dots[] = {QColor(0xef, 0x44, 0x44), QColor(0xf5, 0x9e, 0x0b), QColor(0x22, 0xc5, 0x5e)};
    for (int i = 0; i < 3; ++i) {
        p.setBrush(dots[i]);
        p.drawEllipse(QPoint(win.right() - 24 - i * 22, win.top() + 18), 6, 6);
    }
    p.drawImage(QRect(win.left() + 30, win.top() + 46, win.width() - 60, win.height() - 60),
                demoReport(), demoReport().rect());

    p.setBrush(QColor(0x11, 0x18, 0x27, 230));
    p.drawRect(0, h - 40, w, 40);
    return pm;
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
