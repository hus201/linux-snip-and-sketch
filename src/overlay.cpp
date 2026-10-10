#include "overlay.h"

#include <QButtonGroup>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QToolButton>
#include <QWindow>

namespace {

const QColor kAccent(0, 120, 215);
const QColor kDim(0, 0, 0, 120);

const char *kToolbarStyle = R"(
QFrame#bar { background: rgba(32, 32, 32, 235); border: 1px solid #555; border-radius: 8px; }
QToolButton { color: white; background: transparent; border: none; border-radius: 5px;
              padding: 7px 12px; font-size: 13px; }
QToolButton:hover { background: rgba(255, 255, 255, 30); }
QToolButton:checked { background: rgb(0, 120, 215); }
QToolButton:disabled { color: #777; }
)";

QString modeLabel(const QString &mode)
{
    if (mode == QLatin1String("rect")) return SnipOverlay::tr("▭  Rectangle");
    if (mode == QLatin1String("free")) return SnipOverlay::tr("✎  Freeform");
    if (mode == QLatin1String("window")) return SnipOverlay::tr("❐  Window");
    return SnipOverlay::tr("⛶  Fullscreen");
}

}  // namespace

const QStringList &SnipOverlay::modes()
{
    static const QStringList list{QStringLiteral("rect"), QStringLiteral("free"),
                                  QStringLiteral("window"), QStringLiteral("full")};
    return list;
}

SnipOverlay::SnipOverlay(const ScreenShot &shot, const QString &mode, bool withToolbar,
                         bool windowModeAvailable)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                           | (Capture::usePortal() ? Qt::WindowFlags() : Qt::X11BypassWindowManagerHint))
    , m_shot(shot)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setWindowTitle(tr("Snip & Sketch for Linux"));
    setGeometry(m_shot.geometry);
    if (withToolbar)
        buildToolbar(windowModeAvailable);
    setMode(mode == QLatin1String("full") ? QStringLiteral("rect") : mode);
}

void SnipOverlay::showOnScreen()
{
    setGeometry(m_shot.geometry);
    if (Capture::usePortal()) {
        // Wayland: windows can't position themselves; ask for fullscreen on our output.
        winId();
        if (windowHandle())
            windowHandle()->setScreen(m_shot.screen);
        showFullScreen();
    } else {
        show();
    }
    raise();
    activateWindow();
    if (m_toolbar && !Capture::usePortal())
        grabKeyboard();  // override-redirect windows get no keyboard focus otherwise
}

void SnipOverlay::buildToolbar(bool windowModeAvailable)
{
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName(QStringLiteral("bar"));
    m_toolbar->setStyleSheet(QLatin1String(kToolbarStyle));
    m_toolbar->setCursor(Qt::ArrowCursor);
    auto *layout = new QHBoxLayout(m_toolbar);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto *group = new QButtonGroup(this);
    for (const QString &mode : modes()) {
        auto *btn = new QToolButton(m_toolbar);
        btn->setText(modeLabel(mode));
        btn->setCheckable(true);
        btn->setToolTip(tr("%1 snip").arg(modeLabel(mode).section(QLatin1Char(' '), -1)));
        if (mode == QLatin1String("window") && !windowModeAvailable) {
            btn->setEnabled(false);
            btn->setToolTip(tr("Window snip isn't available on Wayland"));
        }
        connect(btn, &QToolButton::clicked, this, [this, mode] { emit modeRequested(mode); });
        group->addButton(btn);
        layout->addWidget(btn);
        m_buttons.insert(mode, btn);
    }
    auto *close = new QToolButton(m_toolbar);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Cancel (Esc)"));
    connect(close, &QToolButton::clicked, this, &SnipOverlay::cancelled);
    layout->addWidget(close);
    m_toolbar->adjustSize();
    m_toolbar->move((m_shot.geometry.width() - m_toolbar->width()) / 2, 16);
}

void SnipOverlay::setMode(const QString &mode)
{
    m_mode = mode;
    if (auto *btn = m_buttons.value(mode))
        btn->setChecked(true);
    m_path = QPainterPath();
    m_dragging = false;
    if (m_mode == QLatin1String("window"))
        hoverWindow(mapFromGlobal(QCursor::pos()));
    update();
}

void SnipOverlay::setWindows(const QList<QRect> &globalRects)
{
    m_windows.clear();
    const QRect local(QPoint(0, 0), m_shot.geometry.size());
    for (const QRect &r : globalRects) {
        const QRect l = r.translated(-m_shot.geometry.topLeft()).intersected(local);
        if (!l.isEmpty())
            m_windows.append(l);
    }
    if (m_mode == QLatin1String("window"))
        hoverWindow(mapFromGlobal(QCursor::pos()));
}

void SnipOverlay::clearSelection()
{
    if (m_mode != QLatin1String("window")) {
        m_path = QPainterPath();
        update();
    }
}

// ------------------------------------------------------------------- events
void SnipOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        emit cancelled();
    else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !m_path.isEmpty())
        finish(m_path);
}

void SnipOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        emit cancelled();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (m_mode == QLatin1String("window")) {
        hoverWindow(event->position().toPoint());
        if (!m_path.isEmpty())
            finish(m_path);
        return;
    }
    emit interactionStarted(this);
    m_dragging = true;
    m_origin = event->position();
    m_path = QPainterPath(m_origin);
    if (m_toolbar)
        m_toolbar->hide();
    update();
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (m_mode == QLatin1String("window")) {
        hoverWindow(pos.toPoint());
    } else if (m_dragging) {
        if (m_mode == QLatin1String("rect")) {
            m_path = QPainterPath();
            m_path.addRect(QRectF(m_origin, pos).normalized());
        } else {
            m_path.lineTo(pos);
        }
    }
    update();
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    if (m_mode == QLatin1String("free"))
        m_path.closeSubpath();
    const QRectF box = m_path.boundingRect();
    if (box.width() >= 3 && box.height() >= 3) {
        finish(m_path);
    } else {
        m_path = QPainterPath();
        if (m_toolbar)
            m_toolbar->show();
        update();
    }
}

void SnipOverlay::hoverWindow(const QPoint &pos)
{
    QRect target(QPoint(0, 0), m_shot.geometry.size());  // whole monitor by default
    for (const QRect &r : qAsConst(m_windows)) {
        if (r.contains(pos)) {
            target = r;
            break;
        }
    }
    m_path = QPainterPath();
    m_path.addRect(QRectF(target));
    update();
}

// ----------------------------------------------------------------- painting
void SnipOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawPixmap(0, 0, m_shot.pixmap);
    p.fillRect(rect(), kDim);
    if (m_path.isEmpty())
        return;

    p.save();
    p.setClipPath(m_path);
    p.drawPixmap(0, 0, m_shot.pixmap);
    p.restore();

    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kAccent, 2));
    p.drawPath(m_path);

    // Size badge in physical pixels.
    const QRectF box = m_path.boundingRect();
    const qreal dpr = m_shot.pixmap.devicePixelRatio();
    const QString label = QStringLiteral("%1 × %2").arg(qRound(box.width() * dpr)).arg(qRound(box.height() * dpr));
    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);
    const QFontMetrics fm(f);
    QRectF badge(0, 0, fm.horizontalAdvance(label) + 12, fm.height() + 6);
    const qreal y = box.top() - badge.height() - 6;
    badge.moveTo(box.left(), y > 0 ? y : box.top() + 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(32, 32, 32, 220));
    p.drawRoundedRect(badge, 4, 4);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, label);
}

// ------------------------------------------------------------------- result
void SnipOverlay::finish(const QPainterPath &path)
{
    emit snipped(crop(path));
}

// Cut the selection at full physical resolution. Outside a freeform shape is
// filled white, as on Windows.
QImage SnipOverlay::crop(const QPainterPath &path) const
{
    const qreal dpr = m_shot.pixmap.devicePixelRatio();
    const QRect box = path.boundingRect().toAlignedRect().intersected(
        QRect(QPoint(0, 0), m_shot.geometry.size()));
    QImage image(qRound(box.width() * dpr), qRound(box.height() * dpr), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.scale(dpr, dpr);
    p.translate(-box.topLeft());
    p.setClipPath(path);
    p.drawPixmap(0, 0, m_shot.pixmap);
    p.end();
    image.setDevicePixelRatio(dpr);
    return image;
}
