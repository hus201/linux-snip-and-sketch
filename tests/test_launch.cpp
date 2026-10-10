// Capture stitching and the single-instance launch protocol.
#include "capture.h"
#include "controller.h"
#include "testutil.h"

#include <QTest>

using namespace TestUtil;

class TestLaunch : public QObject
{
    Q_OBJECT
private slots:
    void launchRequestRoundTrip()
    {
        LaunchRequest r;
        r.mode = QStringLiteral("free");
        r.delay = 3;
        r.noEditor = true;
        r.app = false;
        r.file = QStringLiteral("/tmp/some file.png");
        const LaunchRequest back = LaunchRequest::deserialize(r.serialize());
        QCOMPARE(back.mode, r.mode);
        QCOMPARE(back.delay, 3);
        QVERIFY(back.noEditor);
        QVERIFY(!back.app);
        QCOMPARE(back.file, r.file);
    }

    void garbageRequestFallsBackToDefaults()
    {
        // A broken message from another process must not do anything surprising.
        const LaunchRequest r = LaunchRequest::deserialize("not json at all");
        QVERIFY(r.mode.isEmpty());
        QCOMPARE(r.delay, 0);
        QVERIFY(!r.noEditor);
        QVERIFY(!r.app);
        QVERIFY(r.file.isEmpty());
    }

    void stitchPlacesMonitorsSideBySide()
    {
        QPixmap left(400, 300);
        left.fill(Qt::red);
        QPixmap right(200, 300);
        right.fill(Qt::blue);
        const ScreenShots shots{{nullptr, QRect(0, 0, 400, 300), left},
                                {nullptr, QRect(400, 0, 200, 300), right}};
        const QImage desktop = Capture::stitch(shots);
        QCOMPARE(desktop.size(), QSize(600, 300));
        QCOMPARE(desktop.pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(desktop.pixelColor(590, 10), QColor(Qt::blue));
    }

    void stitchFillsGapsBetweenMonitorsWithBlack()
    {
        QPixmap a(100, 100);
        a.fill(Qt::white);
        QPixmap b(100, 50);
        b.fill(Qt::white);
        const QImage desktop = Capture::stitch({{nullptr, QRect(0, 0, 100, 100), a},
                                                {nullptr, QRect(100, 0, 100, 50), b}});
        QCOMPARE(desktop.size(), QSize(200, 100));
        QCOMPARE(desktop.pixelColor(150, 80), QColor(Qt::black));  // below the shorter monitor
    }

    void stitchKeepsHiDpiResolution()
    {
        QPixmap pm(800, 600);
        pm.fill(Qt::green);
        pm.setDevicePixelRatio(2.0);
        const QImage desktop = Capture::stitch({{nullptr, QRect(0, 0, 400, 300), pm}});
        QCOMPARE(desktop.size(), QSize(800, 600));
        QCOMPARE(desktop.devicePixelRatio(), 2.0);
    }
};

QTEST_MAIN(TestLaunch)
#include "test_launch.moc"
