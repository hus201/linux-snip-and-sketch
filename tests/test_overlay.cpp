// Selection overlay: rectangle, freeform, window snips, cancelling, HiDPI.
#include "overlay.h"
#include "testutil.h"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace TestUtil;

namespace {

const QColor kScreen(0x3a, 0x6e, 0xa5);

ScreenShot fakeScreen(int w = 1280, int h = 720, qreal dpr = 1.0)
{
    QPixmap pm(qRound(w * dpr), qRound(h * dpr));
    pm.fill(kScreen);
    pm.setDevicePixelRatio(dpr);
    return {nullptr, QRect(0, 0, w, h), pm};
}

QImage snipOf(QSignalSpy &spy)
{
    return spy.isEmpty() ? QImage() : spy.first().first().value<QImage>();
}

}  // namespace

class TestOverlay : public QObject
{
    Q_OBJECT
private slots:
    void rectangleSnip()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("rect"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        dragWidget(&o, {{100, 100}, {400, 300}});
        QCOMPARE(snipped.count(), 1);
        QCOMPARE(snipOf(snipped).size(), QSize(300, 200));
        QCOMPARE(snipOf(snipped).pixelColor(150, 100), kScreen);
    }

    void rectangleDraggedBackwards()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("rect"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        dragWidget(&o, {{400, 300}, {100, 100}});
        QCOMPARE(snipOf(snipped).size(), QSize(300, 200));
    }

    void freeformSnipFillsOutsideWhite()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("free"), false, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        QList<QPointF> ellipse;
        for (int i = 0; i <= 60; ++i) {
            const qreal a = i / 60.0 * 2 * M_PI;
            ellipse << QPointF(600 + 100 * std::cos(a), 500 + 80 * std::sin(a));
        }
        dragWidget(&o, ellipse);
        QCOMPARE(snipped.count(), 1);
        const QImage img = snipOf(snipped);
        QCOMPARE(img.size(), QSize(200, 160));
        QCOMPARE(img.pixelColor(2, 2), QColor(Qt::white));  // outside the shape
        QCOMPARE(img.pixelColor(100, 80), kScreen);         // inside
    }

    void clickWithoutDragDoesNothing()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("rect"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        dragWidget(&o, {{10, 10}, {11, 11}});
        QCOMPARE(snipped.count(), 0);
    }

    void selectionIsClippedToTheScreen()
    {
        SnipOverlay o(fakeScreen(640, 480), QStringLiteral("rect"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        dragWidget(&o, {{500, 400}, {900, 900}});
        QCOMPARE(snipOf(snipped).size(), QSize(140, 80));
    }

    void escapeAndRightClickCancel()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("rect"), true, true);
        QSignalSpy cancelled(&o, &SnipOverlay::cancelled);
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&o, &esc);
        QCOMPARE(cancelled.count(), 1);
        mouse(&o, QEvent::MouseButtonPress, {50, 50}, Qt::RightButton);
        QCOMPARE(cancelled.count(), 2);
    }

    void windowModePicksTopmostWindowUnderCursor()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("window"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        // Top-most first: a small dialog above a big window.
        o.setWindows({QRect(300, 200, 200, 120), QRect(100, 100, 800, 500)});
        o.setMode(QStringLiteral("window"));
        mouse(&o, QEvent::MouseMove, {350, 250}, Qt::NoButton);
        mouse(&o, QEvent::MouseButtonPress, {350, 250});
        QCOMPARE(snipOf(snipped).size(), QSize(200, 120));
    }

    void windowModeFallsBackToWholeScreen()
    {
        SnipOverlay o(fakeScreen(640, 480), QStringLiteral("window"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        o.setWindows({QRect(100, 100, 50, 50)});
        o.setMode(QStringLiteral("window"));
        mouse(&o, QEvent::MouseButtonPress, {600, 400});
        QCOMPARE(snipOf(snipped).size(), QSize(640, 480));
    }

    void modeButtonsRequestModes()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("rect"), true, true);
        QSignalSpy requested(&o, &SnipOverlay::modeRequested);
        QToolButton *free = button(o, QStringLiteral("Freeform"));
        QVERIFY(free);
        free->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().first().toString(), QStringLiteral("free"));
    }

    void windowButtonDisabledWhenUnavailable()
    {
        SnipOverlay o(fakeScreen(), QStringLiteral("rect"), true, false);  // e.g. Wayland
        QToolButton *window = nullptr;
        for (QToolButton *b : o.findChildren<QToolButton *>())
            if (b->text().contains(QLatin1String("Window")))
                window = b;
        QVERIFY(window);
        QVERIFY(!window->isEnabled());
    }

    void hiDpiSnipIsFullResolution()
    {
        SnipOverlay o(fakeScreen(1280, 720, 2.0), QStringLiteral("rect"), true, true);
        QSignalSpy snipped(&o, &SnipOverlay::snipped);
        dragWidget(&o, {{100, 100}, {400, 300}});
        const QImage img = snipOf(snipped);
        QCOMPARE(img.size(), QSize(600, 400));  // 300×200 logical at 200 %
        QCOMPARE(img.devicePixelRatio(), 2.0);
    }
};

QTEST_MAIN(TestOverlay)
#include "test_overlay.moc"
