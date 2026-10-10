// Editor window: toolbar state, tools, ink palette, settings, clipboard,
// opening files. Set SNIP_SCREENSHOT_DIR to also render the UI to PNGs.
#include "editor.h"
#include "inkpopup.h"
#include "testutil.h"

#include <QAbstractButton>
#include <QAction>
#include <QClipboard>
#include <QMenu>
#include <QSettings>
#include <QTest>
#include <QUndoStack>

#include <cmath>

using namespace TestUtil;

class TestEditor : public QObject
{
    Q_OBJECT
    IsolatedSettings *m_settings = nullptr;

    static QSettings appSettings() { return QSettings(QStringLiteral("snip-and-sketch"), QStringLiteral("snip")); }
    static Canvas *canvasOf(EditorWindow &w) { return w.findChild<Canvas *>(); }
    static void discardChanges(EditorWindow &w) { canvasOf(w)->undoStack()->setClean(); }

private slots:
    void initTestCase() { m_settings = new IsolatedSettings; }
    void cleanupTestCase() { delete m_settings; }
    void init() { appSettings().clear(); }

    void emptyStateHidesEditingTools()
    {
        EditorWindow w(nullptr);
        w.show();
        QVERIFY(!button(w, QStringLiteral("Pen"))->isVisible());
        QVERIFY(!button(w, QStringLiteral("Copy"))->isVisible());
        QVERIFY(button(w, QStringLiteral("New snip"))->isVisible());

        w.setImage(testImage());
        QVERIFY(button(w, QStringLiteral("Pen"))->isVisible());
        QVERIFY(button(w, QStringLiteral("Copy"))->isVisible());
        QVERIFY(button(w, QStringLiteral("Pen"))->isChecked());
    }

    void undoRedoButtonsFollowHistory()
    {
        EditorWindow w(nullptr);
        w.setImage(testImage());
        QToolButton *undo = button(w, QStringLiteral("Undo"));
        QToolButton *redo = button(w, QStringLiteral("Redo"));
        QVERIFY(!undo->isEnabled());
        QVERIFY(!redo->isEnabled());
        drag(canvasOf(w), {{10, 10}, {50, 50}});
        QVERIFY(undo->isEnabled());
        undo->click();
        QVERIFY(!undo->isEnabled());
        QVERIFY(redo->isEnabled());
        redo->click();
        QCOMPARE(canvasOf(w)->strokes().size(), 1);
        discardChanges(w);
    }

    void disabledIconsLookDimmed()
    {
        // Regression: icon colours lost their alpha, so disabled buttons
        // looked exactly like enabled ones.
        EditorWindow w(nullptr);
        w.setImage(testImage());
        w.show();
        auto ink = [&w](QWidget *b) {  // grab through the window: a lone button grabs as black
            const QImage im = w.grab(QRect(b->mapTo(&w, QPoint(0, 0)), b->size())).toImage();
            const bool dark = w.palette().color(QPalette::Window).lightness() < 128;
            int strongest = 0;
            for (int y = 0; y < im.height(); ++y)
                for (int x = 0; x < im.width(); ++x) {
                    const int l = im.pixelColor(x, y).lightness();
                    strongest = std::max(strongest, dark ? l : 255 - l);
                }
            return strongest;
        };
        const int disabled = ink(button(w, QStringLiteral("Undo")));
        const int enabled = ink(button(w, QStringLiteral("Copy")));
        QVERIFY2(disabled < enabled - 60, qPrintable(QStringLiteral("disabled %1 vs enabled %2").arg(disabled).arg(enabled)));
    }

    void cropModeShowsApplyAndCancel()
    {
        EditorWindow w(nullptr);
        w.setImage(testImage());
        w.show();
        QToolButton *apply = button(w, QStringLiteral("Apply crop"));
        QToolButton *cancel = button(w, QStringLiteral("Cancel crop"));
        QVERIFY(!apply->isVisible());

        button(w, QStringLiteral("Crop"))->click();
        QVERIFY(apply->isVisible() && cancel->isVisible());
        QVERIFY(!apply->isEnabled());  // nothing to apply yet
        drag(canvasOf(w), {{800, 250}, {600, 250}});
        QVERIFY(apply->isEnabled());

        apply->click();
        QCOMPARE(canvasOf(w)->cropRect().width(), 600);
        QVERIFY(!apply->isVisible());                          // back to drawing
        QVERIFY(button(w, QStringLiteral("Pen"))->isChecked());
        discardChanges(w);
    }

    void cancelCropKeepsImage()
    {
        EditorWindow w(nullptr);
        w.setImage(testImage());
        w.show();
        button(w, QStringLiteral("Crop"))->click();
        drag(canvasOf(w), {{800, 250}, {600, 250}});
        button(w, QStringLiteral("Cancel crop"))->click();
        QCOMPARE(canvasOf(w)->cropRect(), QRect(0, 0, 800, 500));
        QCOMPARE(canvasOf(w)->tool(), Canvas::PenTool);
    }

    void clickingActivePenOpensPalette()
    {
        EditorWindow w(nullptr);
        w.setImage(testImage());
        w.show();
        QToolButton *pen = button(w, QStringLiteral("Pen"));
        QVERIFY(!w.findChild<InkPopup *>());
        pen->click();  // pen is already active → palette
        auto *popup = w.findChild<InkPopup *>();
        QVERIFY(popup && popup->isVisible());
        popup->hide();

        button(w, QStringLiteral("Highlighter"))->click();  // first click only selects
        QCOMPARE(canvasOf(w)->tool(), Canvas::HighlighterTool);
        QCOMPARE(w.findChildren<InkPopup *>().size(), 1);
    }

    void paletteChangesInkAndRemembersIt()
    {
        {
            EditorWindow w(nullptr);
            w.setImage(testImage());
            w.show();
            button(w, QStringLiteral("Pen"))->click();
            auto *popup = w.findChild<InkPopup *>();
            QList<QAbstractButton *> swatches = popup->findChildren<QAbstractButton *>();
            QVERIFY(swatches.size() > 4);
            swatches[3]->click();  // 4th swatch: red
            QCOMPARE(canvasOf(w)->inkColor(Canvas::PenTool), QColor(0xe8, 0x11, 0x23));
            QCOMPARE(appSettings().value(QStringLiteral("ink/pen/color")).toString(), QStringLiteral("#e81123"));
        }
        EditorWindow again(nullptr);  // a new window starts with the remembered ink
        QCOMPARE(canvasOf(again)->inkColor(Canvas::PenTool), QColor(0xe8, 0x11, 0x23));
    }

    void expandCanvasIsOffByDefaultAndRemembered()
    {
        {
            EditorWindow w(nullptr);
            QVERIFY(!canvasOf(w)->expandEnabled());
            QAction *toggle = nullptr;
            for (QAction *a : w.findChildren<QAction *>())
                if (a->text().startsWith(QLatin1String("Expand canvas")))
                    toggle = a;
            QVERIFY(toggle && toggle->isCheckable() && !toggle->isChecked());
            toggle->trigger();
            QVERIFY(canvasOf(w)->expandEnabled());
        }
        EditorWindow again(nullptr);
        QVERIFY(canvasOf(again)->expandEnabled());
    }

    void copyPutsTheEditedImageOnTheClipboard()
    {
        EditorWindow w(nullptr);
        w.setImage(testImage());
        w.show();
        canvasOf(w)->setInk(Canvas::PenTool, Qt::black, 8);
        drag(canvasOf(w), {{100, 450}, {300, 450}});
        button(w, QStringLiteral("Copy"))->click();
        const QImage copied = QGuiApplication::clipboard()->image();
        if (copied.isNull())
            QSKIP("this platform plugin has no clipboard");
        QCOMPARE(copied.size(), QSize(800, 500));
        QVERIFY(copied.pixelColor(200, 450).lightness() < 60);  // includes the ink
        discardChanges(w);
    }

    void openFileShowsNameInTitle()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("diagram.png"));
        QVERIFY(testImage().save(file));
        EditorWindow w(nullptr);
        w.openFile(file);
        QCOMPARE(canvasOf(w)->cropRect().size(), QSize(800, 500));
        QVERIFY(w.windowTitle().startsWith(QLatin1String("diagram.png")));
        drag(canvasOf(w), {{10, 10}, {50, 50}});
        QVERIFY(w.windowTitle().contains(QStringLiteral("•")));  // unsaved changes marker
        discardChanges(w);
    }

    void newSnipWithoutControllerIsHarmless()
    {
        EditorWindow w(nullptr);
        w.show();
        button(w, QStringLiteral("New snip"))->click();
        QVERIFY(w.isVisible());
    }

    void screenshots()
    {
        const QString dir = qEnvironmentVariable("SNIP_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("set SNIP_SCREENSHOT_DIR=/some/dir to render the editor");
        const QPalette original = QApplication::palette();
        for (const bool dark : {false, true}) {
            if (dark) {
                QPalette p;
                p.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2e));
                p.setColor(QPalette::WindowText, QColor(0xee, 0xee, 0xee));
                p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x20));
                p.setColor(QPalette::Text, QColor(0xee, 0xee, 0xee));
                p.setColor(QPalette::Button, QColor(0x33, 0x33, 0x36));
                p.setColor(QPalette::ButtonText, QColor(0xee, 0xee, 0xee));
                p.setColor(QPalette::Highlight, QColor(0x35, 0x84, 0xe4));
                QApplication::setPalette(p);
            }
            const QString prefix = dir + (dark ? QStringLiteral("/dark-") : QStringLiteral("/"));

            EditorWindow empty(nullptr);
            empty.resize(960, 600);
            empty.show();
            QVERIFY(empty.grab().save(prefix + QStringLiteral("empty.png")));

            EditorWindow w(nullptr);
            w.resize(1000, 680);
            w.show();
            w.setImage(testImage());
            QApplication::processEvents();
            Canvas *c = canvasOf(w);
            c->zoomToFit();
            c->setInk(Canvas::PenTool, QColor(0xe8, 0x11, 0x23), 4);
            QList<QPointF> circle;
            for (int i = 0; i <= 40; ++i)
                circle << QPointF(600 + 110 * std::cos(i / 40.0 * 2 * M_PI), 355 + 95 * std::sin(i / 40.0 * 2 * M_PI));
            drag(c, circle);
            c->setTool(Canvas::HighlighterTool);
            drag(c, {{40, 185}, {380, 186}});
            c->setTool(Canvas::PenTool);
            QVERIFY(w.grab().save(prefix + QStringLiteral("editor.png")));

            button(w, QStringLiteral("Pen"))->click();
            if (auto *popup = w.findChild<InkPopup *>()) {
                QVERIFY(popup->grab().save(prefix + QStringLiteral("popup.png")));
                popup->hide();
            }
            button(w, QStringLiteral("Crop"))->click();
            drag(c, {{800, 500}, {700, 430}});
            QVERIFY(w.grab().save(prefix + QStringLiteral("crop.png")));
            discardChanges(w);
        }
        QApplication::setPalette(original);
    }
};

QTEST_MAIN(TestEditor)
#include "test_editor.moc"
