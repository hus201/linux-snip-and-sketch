#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointer>
#include <QWidget>

#include <memory>

class QScrollArea;
class QUndoStack;

// A freehand ink stroke, stored in image-pixel coordinates.
struct Stroke {
    enum Kind { Pen, Highlighter };

    Stroke(Kind kind, const QColor &color, qreal width, const QPointF &start);
    void add(const QPointF &point);
    void paint(QPainter &p) const;
    bool hit(const QPointF &point, qreal radius) const;
    QRectF bounds() const;  // including the ink width

    Kind kind;
    QColor color;
    qreal width;
    QList<QPointF> points;
    QPainterPath path;

private:
    void rebuild();
};
using StrokePtr = std::shared_ptr<Stroke>;

// The drawing surface. It fills the whole view and shows the snip as a
// centred "page"; edits (ink, erase, crop, expand) are undoable. The page is
// `cropRect()` in image coordinates — smaller than the image after a crop,
// larger (white margin) after the canvas expands.
class Canvas : public QWidget
{
    Q_OBJECT
public:
    enum Tool { PenTool, HighlighterTool, EraserTool, CropTool };

    explicit Canvas(QWidget *parent = nullptr);

    void setScrollArea(QScrollArea *area);
    QUndoStack *undoStack() const { return m_undo; }

    void setImage(const QImage &image);
    bool hasImage() const { return !m_image.isNull(); }
    QImage renderImage() const;

    QRect cropRect() const { return m_crop; }
    void setCropRect(const QRect &rect);

    // Crop tool: the rectangle being edited, applied or cancelled as a whole.
    bool cropEdited() const;
    bool applyCrop();
    void cancelCrop();

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    void zoomBy(qreal factor, const QPointF &anchor = QPointF(-1, -1));
    void zoomToFit();

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    QColor inkColor(Tool tool) const;
    int inkWidth(Tool tool) const;
    void setInk(Tool tool, const QColor &color, int width);

    // Grow the page when ink goes past its edge (off by default).
    bool expandEnabled() const { return m_expand; }
    void setExpandEnabled(bool on) { m_expand = on; }

    QList<StrokePtr> &strokes() { return m_strokes; }

    QPointF imageToWidget(const QPointF &p) const;
    QPointF widgetToImage(const QPointF &p) const;

signals:
    void zoomChanged(qreal zoom);
    void cropEditChanged(bool edited);
    void pageChanged();  // page size changed (crop / expand / undo)
    void cropApplied();  // e.g. by double-click, so the editor can leave crop mode

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum Handle { None, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, NewRect };

    qreal scale() const { return m_zoom / m_dpr; }
    QRectF pageRect() const;  // widget coordinates
    void paintContent(QPainter &p) const;
    void paintCropOverlay(QPainter &p) const;
    Handle cropHandleAt(const QPointF &widgetPos) const;
    void updateCropHover(const QPointF &widgetPos);
    void eraseAt(const QPointF &imagePos);
    void eraseAlong(const QPointF &from, const QPointF &to);
    void finishStroke();
    void relayout();
    void updateCursor();
    void setCropEdit(const QRectF &rect);

    QUndoStack *m_undo;
    QPointer<QScrollArea> m_scroll;
    QImage m_image;  // devicePixelRatio forced to 1; real ratio kept in m_dpr
    qreal m_dpr = 1.0;
    QRect m_crop;
    QList<StrokePtr> m_strokes;
    qreal m_zoom = 1.0;
    bool m_expand = false;

    Tool m_tool = PenTool;
    QColor m_penColor{QStringLiteral("#000000")};
    int m_penWidth = 4;
    QColor m_highlightColor{QStringLiteral("#ffeb3b")};
    int m_highlightWidth = 20;

    StrokePtr m_current;
    bool m_eraseMacro = false;
    QPointF m_lastErase;      // image coords of the previous eraser event

    QRectF m_cropEdit;        // image coordinates
    Handle m_cropDrag = None;
    QPointF m_cropOrigin;     // image coords where the drag started
    QRectF m_cropStart;       // m_cropEdit when the drag started

    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_panLast;
};
