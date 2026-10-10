// Canvas: ink, eraser, crop, expand, zoom and rendering.
#include "testutil.h"

#include <QScrollArea>
#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>

using namespace TestUtil;

class TestCanvas : public QObject
{
    Q_OBJECT
private slots:
    // ------------------------------------------------------------- ink
    void penStrokeAndUndo()
    {
        Canvas c;
        c.setImage(testImage());
        drag(&c, {{100, 300}, {150, 350}, {200, 300}});
        QCOMPARE(c.strokes().size(), 1);
        QCOMPARE(c.undoStack()->count(), 1);
        c.undoStack()->undo();
        QCOMPARE(c.strokes().size(), 0);
        c.undoStack()->redo();
        QCOMPARE(c.strokes().size(), 1);
    }

    void penInkIsRendered()
    {
        Canvas c;
        c.setImage(testImage());
        c.setInk(Canvas::PenTool, QColor(0xe8, 0x11, 0x23), 6);
        drag(&c, {{100, 450}, {300, 450}});
        const QColor ink = c.renderImage().pixelColor(200, 450);
        QVERIFY2(ink.red() > 200 && ink.green() < 60, qPrintable(ink.name()));
    }

    void singleClickMakesADot()
    {
        Canvas c;
        c.setImage(testImage());
        c.setInk(Canvas::PenTool, Qt::black, 8);
        drag(&c, {{300, 450}});
        QCOMPARE(c.strokes().size(), 1);
        QVERIFY(c.renderImage().pixelColor(300, 450).lightness() < 60);
    }

    void strokeBoundsIncludeInkWidth()
    {
        Stroke s(Stroke::Pen, Qt::black, 10, QPointF(100, 100));
        s.add(QPointF(200, 100));
        const QRectF b = s.bounds();
        QVERIFY(b.left() <= 95 && b.right() >= 205 && b.top() <= 95 && b.bottom() >= 105);
    }

    void highlighterKeepsTextReadable()
    {
        Canvas c;
        c.setImage(testImage());
        c.setTool(Canvas::HighlighterTool);
        drag(&c, {{30, 400}, {300, 400}});
        const QImage out = c.renderImage();
        const QColor under = out.pixelColor(150, 400);  // white under yellow → yellow
        QVERIFY(under.red() > 200 && under.green() > 200 && under.blue() < 120);
        QCOMPARE(out.pixelColor(150, 300), QColor(Qt::white));  // untouched
    }

    void setImageResetsEverything()
    {
        Canvas c;
        c.setImage(testImage());
        drag(&c, {{10, 10}, {50, 50}});
        c.setImage(testImage().scaled(400, 300));
        QCOMPARE(c.strokes().size(), 0);
        QCOMPARE(c.undoStack()->count(), 0);
        QCOMPARE(c.cropRect(), QRect(0, 0, 400, 300));
    }

    // ---------------------------------------------------------- eraser
    void eraserHitsWhatWasDrawn()
    {
        // Regression: the eraser used to test the smoothed curve, which can
        // pass ~10px away from a sharp corner the user actually drew.
        Canvas c;
        c.setImage(testImage());
        drag(&c, {{100, 300}, {150, 350}, {200, 300}});
        drag(&c, {{400, 100}, {450, 100}});
        c.setTool(Canvas::EraserTool);
        drag(&c, {{150, 350}, {151, 350}});  // exactly on the corner
        QCOMPARE(c.strokes().size(), 1);
        QCOMPARE(c.strokes().first()->points.first(), QPointF(400, 100));
        c.undoStack()->undo();  // one undo step for the whole eraser drag
        QCOMPARE(c.strokes().size(), 2);
    }

    void eraserMissesFarStrokes()
    {
        Canvas c;
        c.setImage(testImage());
        drag(&c, {{100, 100}, {200, 100}});
        c.setTool(Canvas::EraserTool);
        drag(&c, {{150, 200}});
        QCOMPARE(c.strokes().size(), 1);
        QCOMPARE(c.undoStack()->count(), 1);  // no empty "Erase" entry
    }

    void eraserDragRemovesSeveralInOneStep()
    {
        Canvas c;
        c.setImage(testImage());
        drag(&c, {{100, 100}, {100, 200}});
        drag(&c, {{200, 100}, {200, 200}});
        drag(&c, {{300, 100}, {300, 200}});
        c.setTool(Canvas::EraserTool);
        drag(&c, {{50, 150}, {350, 150}});
        QCOMPARE(c.strokes().size(), 0);
        c.undoStack()->undo();
        QCOMPARE(c.strokes().size(), 3);
    }

    // ------------------------------------------------------------ crop
    void cropWithHandlesRenderAndUndo()
    {
        Canvas c;
        c.setImage(testImage());
        c.setTool(Canvas::CropTool);
        QSignalSpy edited(&c, &Canvas::cropEditChanged);
        drag(&c, {{0, 0}, {20, 40}});        // top-left handle
        drag(&c, {{800, 500}, {780, 480}});  // bottom-right handle
        QVERIFY(edited.count() > 0 && edited.last().first().toBool());
        QVERIFY(c.cropEdited());
        QVERIFY(c.applyCrop());
        QCOMPARE(c.cropRect(), QRect(20, 40, 760, 440));
        QCOMPARE(c.renderImage().size(), QSize(760, 440));
        QCOMPARE(c.renderImage().pixelColor(0, 0), testImage().pixelColor(20, 40));
        c.undoStack()->undo();
        QCOMPARE(c.cropRect(), QRect(0, 0, 800, 500));
    }

    void cropMoveKeepsSizeAndStaysInside()
    {
        Canvas c;
        c.setImage(testImage());
        c.setTool(Canvas::CropTool);
        drag(&c, {{800, 250}, {600, 250}});  // right edge → 600 wide
        drag(&c, {{300, 250}, {500, 250}});  // move right; clamped to the image
        QVERIFY(c.applyCrop());
        QCOMPARE(c.cropRect(), QRect(200, 0, 600, 500));
    }

    void cropCannotCollapse()
    {
        Canvas c;
        c.setImage(testImage());
        c.setTool(Canvas::CropTool);
        drag(&c, {{800, 250}, {-500, 250}});  // drag the right edge past the left
        QVERIFY(c.applyCrop());
        QVERIFY(c.cropRect().width() >= 8);
    }

    void cropCancelRestores()
    {
        Canvas c;
        c.setImage(testImage());
        c.setTool(Canvas::CropTool);
        drag(&c, {{800, 250}, {600, 250}});
        c.cancelCrop();
        QVERIFY(!c.cropEdited());
        QVERIFY(!c.applyCrop());
        QCOMPARE(c.cropRect(), QRect(0, 0, 800, 500));
        QCOMPARE(c.undoStack()->count(), 0);
    }

    void leavingCropToolCancelsEdit()
    {
        Canvas c;
        c.setImage(testImage());
        c.setTool(Canvas::CropTool);
        drag(&c, {{800, 250}, {600, 250}});
        c.setTool(Canvas::PenTool);
        c.setTool(Canvas::CropTool);
        QVERIFY(!c.cropEdited());
    }

    void inkSurvivesCropAndUndo()
    {
        Canvas c;
        c.setImage(testImage());
        c.setInk(Canvas::PenTool, Qt::black, 6);
        drag(&c, {{700, 450}, {780, 450}});
        c.setTool(Canvas::CropTool);
        drag(&c, {{800, 250}, {600, 250}});
        QVERIFY(c.applyCrop());  // cuts the stroke off...
        c.undoStack()->undo();   // ...and undo brings it back
        QVERIFY(c.renderImage().pixelColor(750, 450).lightness() < 60);
    }

    // ---------------------------------------------------------- expand
    void drawingOutsideDoesNotExpandByDefault()
    {
        Canvas c;
        c.setImage(testImage());
        QVERIFY(!c.expandEnabled());
        drag(&c, {{700, 250}, {900, 250}, {950, 600}});
        QCOMPARE(c.strokes().size(), 1);
        QCOMPARE(c.cropRect(), QRect(0, 0, 800, 500));
        QCOMPARE(c.renderImage().size(), QSize(800, 500));
    }

    void drawingOutsideExpandsWhenEnabled()
    {
        Canvas c;
        c.setImage(testImage());
        c.setExpandEnabled(true);
        drag(&c, {{700, 250}, {900, 250}, {950, 600}});
        const QRect page = c.cropRect();
        QVERIFY2(page.right() > 950 && page.bottom() > 600,
                 qPrintable(QStringLiteral("page %1x%2").arg(page.width()).arg(page.height())));
        QCOMPARE(page.topLeft(), QPoint(0, 0));  // only grows where the ink went
        const QImage out = c.renderImage();
        QCOMPARE(out.size(), page.size());
        QCOMPARE(out.pixelColor(page.width() - 3, 10), QColor(Qt::white));  // new area is white
        QCOMPARE(out.pixelColor(50, 40), testImage().pixelColor(50, 40));   // original untouched

        c.undoStack()->undo();  // one step removes the ink and the extra space
        QCOMPARE(c.strokes().size(), 0);
        QCOMPARE(c.cropRect(), QRect(0, 0, 800, 500));
        c.undoStack()->redo();
        QCOMPARE(c.cropRect(), page);
    }

    void expandToTheLeftKeepsInkInPlace()
    {
        Canvas c;
        c.setImage(testImage());
        c.setExpandEnabled(true);
        drag(&c, {{100, 100}, {-60, 100}});
        QVERIFY(c.cropRect().left() < -60);
        // The stroke keeps its image position; the page grew around it.
        QCOMPARE(c.strokes().first()->points.last(), QPointF(-60, 100));
        const QPoint ink = QPoint(-30, 100) - c.cropRect().topLeft();
        QVERIFY(c.renderImage().pixelColor(ink).lightness() < 80);
    }

    void insideStrokeDoesNotExpand()
    {
        Canvas c;
        c.setImage(testImage());
        c.setExpandEnabled(true);
        drag(&c, {{100, 100}, {200, 200}});
        QCOMPARE(c.cropRect(), QRect(0, 0, 800, 500));
        QCOMPARE(c.undoStack()->count(), 1);
    }

    void cropAfterExpandKeepsWorking()
    {
        Canvas c;
        c.setImage(testImage());
        c.setExpandEnabled(true);
        drag(&c, {{700, 250}, {950, 250}});
        const QRect grown = c.cropRect();
        c.setTool(Canvas::CropTool);
        drag(&c, {{qreal(grown.right() + 1), 250}, {400, 250}});
        QVERIFY(c.applyCrop());
        QCOMPARE(c.cropRect().right(), 399);
    }

    // ------------------------------------------------------------ zoom
    void zoomKeepsCoordinateMapping()
    {
        Canvas c;
        c.setImage(testImage());
        for (const qreal z : {0.25, 1.0, 2.0, 3.5}) {
            c.setZoom(z);
            QCOMPARE(c.widgetToImage(c.imageToWidget(QPointF(123, 45))), QPointF(123, 45));
        }
        c.setZoom(2.0);
        drag(&c, {{100, 100}, {200, 100}});
        QCOMPARE(c.strokes().first()->points.first(), QPointF(100, 100));
    }

    void zoomIsClamped()
    {
        Canvas c;
        c.setImage(testImage());
        c.setZoom(1000);
        QCOMPARE(c.zoom(), 8.0);
        c.setZoom(0);
        QCOMPARE(c.zoom(), 0.05);
    }

    void zoomAroundAnchorKeepsPointUnderCursor()
    {
        QScrollArea area;
        area.resize(600, 400);
        auto *c = new Canvas;
        c->setScrollArea(&area);
        c->setImage(testImage());
        area.show();
        c->setZoom(1.0);
        const QPointF anchor = c->imageToWidget(QPointF(600, 300));
        const QPointF before = c->mapTo(area.viewport(), anchor.toPoint());
        c->zoomBy(2.0, anchor);
        const QPointF after = c->mapTo(area.viewport(), c->imageToWidget(QPointF(600, 300)).toPoint());
        QVERIFY2((after - before).manhattanLength() <= 2,
                 qPrintable(QStringLiteral("moved from %1,%2 to %3,%4").arg(before.x()).arg(before.y()).arg(after.x()).arg(after.y())));
    }

    void fitZoomShowsWholeImage()
    {
        QScrollArea area;
        area.resize(500, 400);
        auto *c = new Canvas;
        c->setScrollArea(&area);
        c->setImage(testImage());
        area.show();
        c->zoomToFit();
        QVERIFY(c->zoom() < 1.0);
        QVERIFY(800 * c->zoom() <= area.viewport()->width());
        QVERIFY(500 * c->zoom() <= area.viewport()->height());
    }

    void hiDpiSnipKeepsFullResolution()
    {
        QImage img = testImage();
        img.setDevicePixelRatio(2.0);  // e.g. a snip from a 200 % screen
        Canvas c;
        c.setImage(img);
        const QImage out = c.renderImage();
        QCOMPARE(out.size(), QSize(800, 500));
        QCOMPARE(out.devicePixelRatio(), 2.0);
    }
};

QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"
