#include "icons.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

namespace {

// Inner SVG markup; drawn with stroke="currentColor", width 1.75, round joins.
const QHash<QString, QString> &shapes()
{
    static const QHash<QString, QString> s{
        {QStringLiteral("snip"),
         QStringLiteral(R"(<path d="M3 8V6a2 2 0 0 1 2-2h2M13 4h2M3 12v2M3 18a2 2 0 0 0 2 2h2M11 20h2"/>
                           <path d="M19 13v8M15 17h8"/>)")},
        {QStringLiteral("timer"),
         QStringLiteral(R"(<circle cx="12" cy="13" r="8"/><path d="M12 9v4l2.5 2.5M10 2h4"/>)")},
        {QStringLiteral("pen"),
         QStringLiteral(R"(<path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L8 18l-4 1 1-4Z"/><path d="M14.5 5.5l3 3"/>)")},
        {QStringLiteral("highlighter"),
         QStringLiteral(R"(<path d="M9 14.5L6 17.5V19h3.5l3-3"/><path d="M9 14.5l8.6-9.6a2 2 0 0 1 2.9 2.8L12.5 16Z"/>)")},
        {QStringLiteral("eraser"),
         QStringLiteral(R"(<path d="M8 20h12"/><path d="M4.6 15.6l9.9-9.9a2 2 0 0 1 2.8 0l2.8 2.8a2 2 0 0 1 0 2.8L12 19.4a2 2 0 0 1-1.4.6H8.5a2 2 0 0 1-1.4-.6l-2.5-2.5a1.2 1.2 0 0 1 0-1.3Z"/><path d="M9 11l5.5 5.5"/>)")},
        {QStringLiteral("crop"),
         QStringLiteral(R"(<path d="M6 2v14a2 2 0 0 0 2 2h14"/><path d="M18 22V8a2 2 0 0 0-2-2H2"/>)")},
        {QStringLiteral("undo"),
         QStringLiteral(R"(<path d="M9 14L4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"/>)")},
        {QStringLiteral("redo"),
         QStringLiteral(R"(<path d="M15 14l5-5-5-5"/><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13"/>)")},
        {QStringLiteral("copy"),
         QStringLiteral(R"(<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M4 16a2 2 0 0 1-1-1.7V5a2 2 0 0 1 2-2h9.3A2 2 0 0 1 16 4"/>)")},
        {QStringLiteral("save"),
         QStringLiteral(R"(<path d="M5 3h11.2a2 2 0 0 1 1.4.6l2.8 2.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2Z"/><path d="M7 3v5h8V3M7 21v-7h10v7"/>)")},
        {QStringLiteral("open"),
         QStringLiteral(R"(<path d="M3 7V5a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v1"/><path d="M3 7h18l-2 12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/>)")},
        {QStringLiteral("more"),
         QStringLiteral(R"(<circle cx="5" cy="12" r="1.2" fill="currentColor"/><circle cx="12" cy="12" r="1.2" fill="currentColor"/><circle cx="19" cy="12" r="1.2" fill="currentColor"/>)")},
        {QStringLiteral("zoom-in"),
         QStringLiteral(R"(<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3M11 8v6M8 11h6"/>)")},
        {QStringLiteral("zoom-out"),
         QStringLiteral(R"(<circle cx="11" cy="11" r="7"/><path d="M21 21l-4.3-4.3M8 11h6"/>)")},
        {QStringLiteral("fit"),
         QStringLiteral(R"(<path d="M3 8V5a2 2 0 0 1 2-2h3M16 3h3a2 2 0 0 1 2 2v3M21 16v3a2 2 0 0 1-2 2h-3M8 21H5a2 2 0 0 1-2-2v-3"/>)")},
        {QStringLiteral("check"), QStringLiteral(R"(<path d="M4 12.5l5 5L20 6.5"/>)")},
        {QStringLiteral("close"), QStringLiteral(R"(<path d="M6 6l12 12M18 6L6 18"/>)")},
        {QStringLiteral("chevron"), QStringLiteral(R"(<path d="M7 10l5 5 5-5"/>)")},
        {QStringLiteral("rect"), QStringLiteral(R"(<rect x="3" y="5" width="18" height="14" rx="2"/>)")},
        {QStringLiteral("freeform"),
         QStringLiteral(R"(<path d="M7 4c-3 1-4.5 5-3 8s1 6 4 7.5 7 .5 9.5-2 4-6 2-9.5S10 3 7 4Z"/>)")},
        {QStringLiteral("window"),
         QStringLiteral(R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18M6.5 6.5h.01M9 6.5h.01"/>)")},
        {QStringLiteral("fullscreen"),
         QStringLiteral(R"(<rect x="2" y="4" width="20" height="13" rx="2"/><path d="M8 21h8M12 17v4"/>)")},
        {QStringLiteral("expand"),
         QStringLiteral(R"(<rect x="7" y="7" width="10" height="10" rx="1.5"/><path d="M3 9V4h5M21 9V4h-5M3 15v5h5M21 15v5h-5"/>)")},
    };
    return s;
}

QPixmap render(const QString &name, const QColor &color, int size, qreal dpr)
{
    const QString svg = QStringLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"
              stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" color="%1">%2</svg>)")
        .arg(color.name(), shapes().value(name)).replace(QLatin1String("currentColor"), color.name());
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pix(qRound(size * dpr), qRound(size * dpr));
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setOpacity(color.alphaF());  // color.name() above drops the alpha channel
    renderer.render(&p);
    p.end();
    pix.setDevicePixelRatio(dpr);
    return pix;
}

QIcon build(const QString &name, const QColor &color, const QColor &bar = {})
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QColor disabled = color;
    disabled.setAlphaF(0.35);
    QIcon icon;
    for (int size : {16, 20, 24, 32}) {
        for (auto [mode, c] : {std::pair{QIcon::Normal, color}, std::pair{QIcon::Disabled, disabled}}) {
            QPixmap pix = render(name, c, size, dpr);
            if (bar.isValid()) {  // ink colour bar along the bottom
                QPainter p(&pix);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(mode == QIcon::Disabled ? disabled : bar);
                p.drawRoundedRect(QRectF(size * 0.12, size * 0.84, size * 0.76, size * 0.14), size * 0.07, size * 0.07);
            }
            icon.addPixmap(pix, mode);
        }
    }
    return icon;
}

}  // namespace

QIcon Icons::get(const QString &name, const QColor &color)
{
    return build(name, color);
}

QIcon Icons::get(const QString &name)
{
    return build(name, QApplication::palette().color(QPalette::WindowText));
}

QIcon Icons::ink(const QString &name, const QColor &ink)
{
    return build(name, QApplication::palette().color(QPalette::WindowText), ink);
}
