#include "canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kEraserRadius = 8;   // logical px
constexpr int kMargin = 40;          // widget px around the page
constexpr qreal kHandleReach = 12;   // widget px for grabbing crop handles
constexpr qreal kMinCrop = 8;        // image px
constexpr int kExpandPadding = 16;   // logical px of white added past the ink

qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    const qreal t = len2 > 0 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / len2, 1.0) : 0.0;
    const QPointF d = p - (a + t * ab);
    return std::hypot(d.x(), d.y());
}

QCursor circleCursor(qreal diameter, const QColor &fill)
{
    const int size = qMax(9, int(diameter) + 4);
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(2, 2, size - 4, size - 4);
    if (fill.isValid()) {
        QColor c = fill;
        c.setAlpha(140);
        p.setBrush(c);
    }
    p.setPen(QPen(QColor(0, 0, 0, 200), 1));
    p.drawEllipse(r);
    p.setPen(QPen(QColor(255, 255, 255, 200), 1));
    p.drawEllipse(r.adjusted(1, 1, -1, -1));
    p.end();
    return QCursor(pix, size / 2, size / 2);
}

// ---------------------------------------------------------------- commands
class AddStroke : public QUndoCommand
{
public:
    AddStroke(Canvas *c, StrokePtr s) : QUndoCommand(QObject::tr("Draw")), m_canvas(c), m_stroke(std::move(s)) {}
    void redo() override { m_canvas->strokes().append(m_stroke); m_canvas->update(); }
    void undo() override { m_canvas->strokes().removeOne(m_stroke); m_canvas->update(); }

private:
    Canvas *m_canvas;
    StrokePtr m_stroke;
};

class EraseStroke : public QUndoCommand
{
public:
    EraseStroke(Canvas *c, StrokePtr s)
        : QUndoCommand(QObject::tr("Erase")), m_canvas(c), m_stroke(std::move(s)),
          m_index(int(c->strokes().indexOf(m_stroke))) {}
    void redo() override { m_canvas->strokes().removeAt(m_index); m_canvas->update(); }
    void undo() override { m_canvas->strokes().insert(m_index, m_stroke); m_canvas->update(); }

private:
    Canvas *m_canvas;
    StrokePtr m_stroke;
    int m_index;
};

// Changes the page rect: used for crop (smaller) and expand (larger).
class PageCommand : public QUndoCommand
{
public:
    PageCommand(Canvas *c, const QRect &rect, const QString &text)
        : QUndoCommand(text), m_canvas(c), m_old(c->cropRect()), m_new(rect) {}
    void redo() override { m_canvas->setCropRect(m_new); }
    void undo() override { m_canvas->setCropRect(m_old); }

private:
    Canvas *m_canvas;
    QRect m_old, m_new;
};

}  // namespace

// ==================================================================== Stroke
Stroke::Stroke(Kind k, const QColor &c, qreal w, const QPointF &start)
    : kind(k), color(c), width(w), points{start}
{
    rebuild();
}

void Stroke::add(const QPointF &point)
{
    const QPointF d = point - points.last();
    if (std::abs(d.x()) + std::abs(d.y()) >= 0.75) {
        points.append(point);
        rebuild();
    }
}

void Stroke::rebuild()
{
    path = QPainterPath(points.first());
    if (points.size() == 1) {
        path.lineTo(points.first() + QPointF(0.01, 0));  // a dot
        return;
    }
    // Quadratic curves through segment midpoints smooth out mouse jitter.
    for (int i = 1; i < points.size() - 1; ++i)
        path.quadTo(points[i], (points[i] + points[i + 1]) / 2);
    path.lineTo(points.last());
}

void Stroke::paint(QPainter &p) const
{
    p.save();
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (kind == Highlighter) {
        // Multiply keeps the text under the highlight readable, like real ink.
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        pen.setCapStyle(Qt::SquareCap);
        pen.setJoinStyle(Qt::BevelJoin);
    }
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.restore();
}

bool Stroke::hit(const QPointF &point, qreal radius) const
{
    const qreal reach = width / 2 + radius;
    if (points.size() == 1)
        return distanceToSegment(point, points[0], points[0]) <= reach;
    for (int i = 1; i < points.size(); ++i)
        if (distanceToSegment(point, points[i - 1], points[i]) <= reach)
            return true;
    return false;
}

QRectF Stroke::bounds() const
{
    const qreal half = width / 2;
    return path.boundingRect().adjusted(-half, -half, half, half);
}

// ==================================================================== Canvas
Canvas::Canvas(QWidget *parent)
    : QWidget(parent), m_undo(new QUndoStack(this))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    updateCursor();
}

void Canvas::setScrollArea(QScrollArea *area)
{
    m_scroll = area;
    area->setWidgetResizable(false);
    area->setWidget(this);
    area->viewport()->installEventFilter(this);
    relayout();
}

bool Canvas::eventFilter(QObject *watched, QEvent *event)
{
    if (m_scroll && watched == m_scroll->viewport() && event->type() == QEvent::Resize)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void Canvas::setImage(const QImage &image)
{
    m_dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
    m_image = image.convertToFormat(QImage::Format_RGB32);
    m_image.setDevicePixelRatio(1.0);
    m_crop = m_image.rect();
    m_cropEdit = QRectF(m_crop);
    m_strokes.clear();
    m_undo->clear();
    relayout();
    emit pageChanged();
}

QImage Canvas::renderImage() const
{
    QImage out(m_crop.size(), QImage::Format_RGB32);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-QPointF(m_crop.topLeft()));
    paintContent(p);
    p.end();
    out.setDevicePixelRatio(m_dpr);
    return out;
}

void Canvas::setCropRect(const QRect &rect)
{
    m_crop = rect;
    m_cropEdit = QRectF(rect);
    relayout();
    emit pageChanged();
    emit cropEditChanged(false);
}

// ---------------------------------------------------------------------- crop
bool Canvas::cropEdited() const
{
    return m_tool == CropTool && m_cropEdit.toRect() != m_crop;
}

bool Canvas::applyCrop()
{
    const QRect rect = m_cropEdit.toRect().intersected(m_crop);
    if (rect.width() < kMinCrop || rect.height() < kMinCrop || rect == m_crop) {
        cancelCrop();
        return false;
    }
    m_undo->push(new PageCommand(this, rect, tr("Crop")));
    return true;
}

void Canvas::cancelCrop()
{
    m_cropDrag = None;
    setCropEdit(QRectF(m_crop));
}

void Canvas::setCropEdit(const QRectF &rect)
{
    m_cropEdit = rect;
    emit cropEditChanged(cropEdited());
    update();
}

// ---------------------------------------------------------------------- zoom
void Canvas::setZoom(qreal zoom)
{
    m_zoom = qBound(0.05, zoom, 8.0);
    relayout();
    emit zoomChanged(m_zoom);
}

void Canvas::zoomBy(qreal factor, const QPointF &anchor)
{
    if (!hasImage())
        return;
    QPointF widgetAnchor = anchor;
    if (widgetAnchor.x() < 0 && m_scroll)  // default: centre of the visible area
        widgetAnchor = mapFrom(m_scroll->viewport(), m_scroll->viewport()->rect().center());
    const QPointF viewportAnchor = mapToParent(widgetAnchor.toPoint());
    const QPointF imagePoint = widgetToImage(widgetAnchor);

    setZoom(m_zoom * factor);

    // Keep the image point under the anchor where it was on screen.
    if (m_scroll) {
        const QPointF now = imageToWidget(imagePoint);
        m_scroll->horizontalScrollBar()->setValue(qRound(now.x() - viewportAnchor.x()));
        m_scroll->verticalScrollBar()->setValue(qRound(now.y() - viewportAnchor.y()));
    }
}

void Canvas::zoomToFit()
{
    if (!hasImage())
        return;
    const QSize avail = m_scroll ? m_scroll->viewport()->size() - QSize(2 * kMargin, 2 * kMargin)
                                 : QSize(1200, 800);
    const QSizeF logical = QSizeF(m_crop.size()) / m_dpr;
    setZoom(std::min({1.0, avail.width() / logical.width(), avail.height() / logical.height()}));
}

void Canvas::relayout()
{
    QSize want(2 * kMargin, 2 * kMargin);
    if (hasImage())
        want += (QSizeF(m_crop.size()) * scale()).toSize();
    if (m_scroll) {
        // Fill the viewport.
        // QScrollArea decides on scrollbars using the current (possibly
        // already reduced) viewport, so it never removes them by itself once
        // the page fits again: switch them off explicitly in that case.
        const QSize full = m_scroll->maximumViewportSize();
        const bool fits = want.width() <= full.width() && want.height() <= full.height();
        const auto policy = fits ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
        m_scroll->setHorizontalScrollBarPolicy(policy);
        m_scroll->setVerticalScrollBarPolicy(policy);
        want = fits ? full : want.expandedTo(m_scroll->viewport()->size());
    }
    resize(want);
    updateCursor();
    update();
}

QRectF Canvas::pageRect() const
{
    const QSizeF size = QSizeF(m_crop.size()) * scale();
    const qreal x = std::round(std::max<qreal>(kMargin, (width() - size.width()) / 2));
    const qreal y = std::round(std::max<qreal>(kMargin, (height() - size.height()) / 2));
    return QRectF(QPointF(x, y), size);
}

QPointF Canvas::imageToWidget(const QPointF &p) const
{
    return pageRect().topLeft() + (p - QPointF(m_crop.topLeft())) * scale();
}

QPointF Canvas::widgetToImage(const QPointF &p) const
{
    return (p - pageRect().topLeft()) / scale() + QPointF(m_crop.topLeft());
}

// ---------------------------------------------------------------------- tools
void Canvas::setTool(Tool tool)
{
    if (m_tool == CropTool && tool != CropTool)
        cancelCrop();
    m_tool = tool;
    if (tool == CropTool)
        setCropEdit(QRectF(m_crop));
    updateCursor();
    update();
}

QColor Canvas::inkColor(Tool tool) const
{
    return tool == HighlighterTool ? m_highlightColor : m_penColor;
}

int Canvas::inkWidth(Tool tool) const
{
    return tool == HighlighterTool ? m_highlightWidth : m_penWidth;
}

void Canvas::setInk(Tool tool, const QColor &color, int width)
{
    if (tool == HighlighterTool) {
        m_highlightColor = color;
        m_highlightWidth = width;
    } else {
        m_penColor = color;
        m_penWidth = width;
    }
    updateCursor();
}

void Canvas::updateCursor()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_spaceHeld) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    switch (m_tool) {
    case PenTool:
    case HighlighterTool:
        setCursor(circleCursor(inkWidth(m_tool) * m_zoom, inkColor(m_tool)));
        break;
    case EraserTool:
        setCursor(circleCursor(kEraserRadius * 2 * m_zoom, QColor()));
        break;
    case CropTool:
        setCursor(Qt::CrossCursor);
        break;
    }
}

// ------------------------------------------------------------------- painting
void Canvas::paintContent(QPainter &p) const
{
    p.save();
    p.setClipRect(m_crop);
    p.fillRect(m_crop, Qt::white);  // margin added by "expand"
    const QRect pixels = m_crop.intersected(m_image.rect());
    p.drawImage(pixels.topLeft(), m_image, pixels);
    for (const StrokePtr &s : m_strokes)
        s->paint(p);
    if (m_current && !m_expand)
        m_current->paint(p);
    p.restore();
}

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    p.fillRect(rect(), dark ? QColor(0x18, 0x18, 0x1a) : QColor(0xe9, 0xe9, 0xec));
    if (!hasImage())
        return;

    const QRectF page = pageRect();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    for (int i = 1; i <= 10; ++i) {  // soft drop shadow
        p.setBrush(QColor(0, 0, 0, dark ? 14 : 7));
        p.drawRoundedRect(page.adjusted(-i, -i + 3, i, i + 3), i, i);
    }

    p.save();
    p.translate(page.topLeft());
    p.scale(scale(), scale());
    p.translate(-QPointF(m_crop.topLeft()));
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1);
    paintContent(p);
    if (m_current && m_expand)
        m_current->paint(p);  // may run past the page; the page grows on release
    p.restore();

    if (m_tool == CropTool)
        paintCropOverlay(p);
}

void Canvas::paintCropOverlay(QPainter &p) const
{
    const QRectF page = pageRect();
    const QRectF sel(imageToWidget(m_cropEdit.topLeft()), imageToWidget(m_cropEdit.bottomRight()));

    QPainterPath outside;
    outside.addRect(page);
    QPainterPath inside;
    inside.addRect(sel);
    p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 130));

    p.setBrush(Qt::NoBrush);
    if (m_cropDrag != None) {  // rule-of-thirds guides while adjusting
        p.setPen(QPen(QColor(255, 255, 255, 110), 1));
        for (int i = 1; i <= 2; ++i) {
            const qreal x = sel.left() + sel.width() * i / 3;
            const qreal y = sel.top() + sel.height() * i / 3;
            p.drawLine(QPointF(x, sel.top()), QPointF(x, sel.bottom()));
            p.drawLine(QPointF(sel.left(), y), QPointF(sel.right(), y));
        }
    }
    p.setPen(QPen(QColor(255, 255, 255, 235), 1));
    p.drawRect(sel);

    // Corner brackets and edge bars, outlined so they show on any image.
    const qreal len = std::min<qreal>(22, std::min(sel.width(), sel.height()) / 3);
    QPainterPath marks;
    const QPointF tl = sel.topLeft(), tr = sel.topRight(), bl = sel.bottomLeft(), br = sel.bottomRight();
    marks.moveTo(tl + QPointF(0, len)); marks.lineTo(tl); marks.lineTo(tl + QPointF(len, 0));
    marks.moveTo(tr - QPointF(len, 0)); marks.lineTo(tr); marks.lineTo(tr + QPointF(0, len));
    marks.moveTo(bl - QPointF(0, len)); marks.lineTo(bl); marks.lineTo(bl + QPointF(len, 0));
    marks.moveTo(br - QPointF(len, 0)); marks.lineTo(br); marks.lineTo(br - QPointF(0, len));
    const QPointF c = sel.center();
    marks.moveTo(c.x() - len / 2, sel.top()); marks.lineTo(c.x() + len / 2, sel.top());
    marks.moveTo(c.x() - len / 2, sel.bottom()); marks.lineTo(c.x() + len / 2, sel.bottom());
    marks.moveTo(sel.left(), c.y() - len / 2); marks.lineTo(sel.left(), c.y() + len / 2);
    marks.moveTo(sel.right(), c.y() - len / 2); marks.lineTo(sel.right(), c.y() + len / 2);
    p.setPen(QPen(QColor(0, 0, 0, 110), 5.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.drawPath(marks);
    p.setPen(QPen(Qt::white, 3.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.drawPath(marks);
}

// ---------------------------------------------------------------------- crop
Canvas::Handle Canvas::cropHandleAt(const QPointF &pos) const
{
    const QRectF sel(imageToWidget(m_cropEdit.topLeft()), imageToWidget(m_cropEdit.bottomRight()));
    const bool nearL = std::abs(pos.x() - sel.left()) <= kHandleReach;
    const bool nearR = std::abs(pos.x() - sel.right()) <= kHandleReach;
    const bool nearT = std::abs(pos.y() - sel.top()) <= kHandleReach;
    const bool nearB = std::abs(pos.y() - sel.bottom()) <= kHandleReach;
    const bool inX = pos.x() >= sel.left() - kHandleReach && pos.x() <= sel.right() + kHandleReach;
    const bool inY = pos.y() >= sel.top() - kHandleReach && pos.y() <= sel.bottom() + kHandleReach;
    if (nearT && nearL) return TopLeft;
    if (nearT && nearR) return TopRight;
    if (nearB && nearL) return BottomLeft;
    if (nearB && nearR) return BottomRight;
    if (nearL && inY) return Left;
    if (nearR && inY) return Right;
    if (nearT && inX) return Top;
    if (nearB && inX) return Bottom;
    if (sel.contains(pos)) return Move;
    return None;
}

void Canvas::updateCropHover(const QPointF &pos)
{
    switch (cropHandleAt(pos)) {
    case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
    case Left: case Right: setCursor(Qt::SizeHorCursor); break;
    case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
    case Move: setCursor(Qt::SizeAllCursor); break;
    default: setCursor(Qt::CrossCursor); break;
    }
}

// ---------------------------------------------------------------------- mouse
void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (!hasImage())
        return;
    setFocus();
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld)) {
        m_panning = true;
        m_panLast = event->globalPosition().toPoint();
        updateCursor();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    const QPointF pt = widgetToImage(event->position());
    switch (m_tool) {
    case PenTool:
    case HighlighterTool:
        m_current = std::make_shared<Stroke>(m_tool == PenTool ? Stroke::Pen : Stroke::Highlighter,
                                             inkColor(m_tool), inkWidth(m_tool) * m_dpr, pt);
        update();
        break;
    case EraserTool:
        m_lastErase = pt;
        eraseAt(pt);
        break;
    case CropTool: {
        m_cropDrag = cropHandleAt(event->position());
        if (m_cropDrag == None)
            m_cropDrag = NewRect;
        const QRectF bounds(m_crop);
        m_cropOrigin = QPointF(qBound(bounds.left(), pt.x(), bounds.right()),
                               qBound(bounds.top(), pt.y(), bounds.bottom()));
        m_cropStart = m_cropEdit;
        update();
        break;
    }
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint now = event->globalPosition().toPoint();
        const QPoint delta = now - m_panLast;
        m_panLast = now;
        if (m_scroll) {
            m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() - delta.x());
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->value() - delta.y());
        }
        return;
    }
    if (m_tool == CropTool && m_cropDrag == None && !m_spaceHeld) {
        updateCropHover(event->position());
        return;
    }
    if (!(event->buttons() & Qt::LeftButton))
        return;

    const QPointF pt = widgetToImage(event->position());
    if (m_current) {
        m_current->add(pt);
        update();
    } else if (m_tool == EraserTool) {
        eraseAlong(m_lastErase, pt);
        m_lastErase = pt;
    } else if (m_tool == CropTool && m_cropDrag != None) {
        const QRectF bounds(m_crop);
        const QPointF clamped(qBound(bounds.left(), pt.x(), bounds.right()),
                              qBound(bounds.top(), pt.y(), bounds.bottom()));
        const QPointF d = clamped - m_cropOrigin;
        QRectF r = m_cropStart;
        switch (m_cropDrag) {
        case Move: {
            r.translate(d);
            r.moveLeft(qBound(bounds.left(), r.left(), bounds.right() - r.width()));
            r.moveTop(qBound(bounds.top(), r.top(), bounds.bottom() - r.height()));
            break;
        }
        case NewRect: r = QRectF(m_cropOrigin, clamped).normalized(); break;
        default: {
            const Handle h = m_cropDrag;
            if (h == Left || h == TopLeft || h == BottomLeft) r.setLeft(std::min(r.left() + d.x(), r.right() - kMinCrop));
            if (h == Right || h == TopRight || h == BottomRight) r.setRight(std::max(r.right() + d.x(), r.left() + kMinCrop));
            if (h == Top || h == TopLeft || h == TopRight) r.setTop(std::min(r.top() + d.y(), r.bottom() - kMinCrop));
            if (h == Bottom || h == BottomLeft || h == BottomRight) r.setBottom(std::max(r.bottom() + d.y(), r.top() + kMinCrop));
            r = r.intersected(bounds);
            break;
        }
        }
        setCropEdit(r);
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        updateCursor();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (m_current)
        finishStroke();
    if (m_eraseMacro) {
        m_undo->endMacro();
        m_eraseMacro = false;
    }
    if (m_cropDrag != None) {
        if (m_cropDrag == NewRect && (m_cropEdit.width() < kMinCrop || m_cropEdit.height() < kMinCrop))
            setCropEdit(m_cropStart);  // a click, not a drag
        m_cropDrag = None;
        updateCropHover(event->position());
        update();
    }
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_tool == CropTool && event->button() == Qt::LeftButton && applyCrop())
        emit cropApplied();
}

void Canvas::finishStroke()
{
    StrokePtr stroke = std::move(m_current);
    m_current.reset();
    QRect page = m_crop;
    if (m_expand) {
        const qreal pad = kExpandPadding * m_dpr;
        const QRect ink = stroke->bounds().adjusted(-pad, -pad, pad, pad).toAlignedRect();
        if (!m_crop.contains(stroke->bounds().toAlignedRect()))
            page = m_crop.united(ink);
    }
    if (page == m_crop) {
        m_undo->push(new AddStroke(this, stroke));
        return;
    }
    m_undo->beginMacro(tr("Draw"));  // one undo removes the ink and the extra space
    m_undo->push(new AddStroke(this, stroke));
    m_undo->push(new PageCommand(this, page, tr("Expand")));
    m_undo->endMacro();
}

// Mouse events arrive spaced out on fast swipes; test the whole path so the
// eraser can't jump over a stroke between two events.
void Canvas::eraseAlong(const QPointF &from, const QPointF &to)
{
    const qreal step = std::max<qreal>(1.0, kEraserRadius * m_dpr / m_zoom);
    const qreal length = std::hypot(to.x() - from.x(), to.y() - from.y());
    const int steps = std::max(1, int(std::ceil(length / step)));
    for (int i = 1; i <= steps; ++i)
        eraseAt(from + (to - from) * (qreal(i) / steps));
}

void Canvas::eraseAt(const QPointF &pt)
{
    const qreal radius = kEraserRadius * m_dpr / m_zoom;
    const QList<StrokePtr> snapshot = m_strokes;
    for (auto it = snapshot.crbegin(); it != snapshot.crend(); ++it) {
        if (!(*it)->hit(pt, radius))
            continue;
        if (!m_eraseMacro) {  // one undo step per eraser drag
            m_undo->beginMacro(tr("Erase"));
            m_eraseMacro = true;
        }
        m_undo->push(new EraseStroke(this, *it));
    }
}

// ------------------------------------------------------------ wheel / keys
void Canvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        zoomBy(std::pow(1.0015, event->angleDelta().y()), event->position());
        event->accept();
    } else {
        event->ignore();  // let the scroll area scroll
    }
}

void Canvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        updateCursor();
        return;
    }
    QWidget::keyPressEvent(event);
}

void Canvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        m_panning = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
