#pragma once

#include "canvas.h"

#include <QHash>
#include <QMainWindow>
#include <QSettings>

class Controller;
class InkPopup;
class QAction;
class QButtonGroup;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QWidget;

// The "Sketch" window: annotate, crop, copy and save a snip.
class EditorWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit EditorWindow(Controller *controller);
    ~EditorWindow() override;

    void setImage(const QImage &image, const QString &path = {});
    void openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QWidget *buildTopBar();
    QWidget *buildBottomBar();
    QWidget *buildEmptyState();
    QToolButton *toolButton(const QString &icon, const QString &tip, QWidget *parent);
    void applyStyle();
    void refreshIcons();

    void selectTool(Canvas::Tool tool);
    void toolClicked(Canvas::Tool tool);
    void showInkPopup(Canvas::Tool tool);
    void loadSettings();
    void saveInk(Canvas::Tool tool);

    void newSnip(int delay);
    void openDialog();
    void copy();
    bool saveAs();
    bool confirmDiscard();
    void applyCrop();
    void cancelCrop();
    void showShortcuts();
    void toast(const QString &text);
    void fitWindowToImage(const QImage &image);
    void updateTitle();
    void updateStatus();

    Controller *m_ctl;
    QSettings m_settings;
    Canvas *m_canvas;
    QScrollArea *m_scroll;
    QStackedWidget *m_stack;
    QWidget *m_editorPage = nullptr;
    QLabel *m_toast = nullptr;
    QString m_path;
    Canvas::Tool m_lastInkTool = Canvas::PenTool;

    // top bar
    QToolButton *m_newButton = nullptr;
    QButtonGroup *m_tools = nullptr;
    QHash<int, QToolButton *> m_toolButtons;
    QWidget *m_cropBar = nullptr;
    QToolButton *m_applyCropButton = nullptr;
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_redoButton = nullptr;
    QToolButton *m_copyButton = nullptr;
    QToolButton *m_saveButton = nullptr;
    QToolButton *m_moreButton = nullptr;
    QAction *m_expandAction = nullptr;
    InkPopup *m_penPopup = nullptr;
    InkPopup *m_highlightPopup = nullptr;

    // bottom bar
    QWidget *m_bottomBar = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QToolButton *m_zoomLabel = nullptr;
    QList<QToolButton *> m_zoomButtons;

    QList<QAction *> m_editingActions;
    QList<QWidget *> m_imageOnly;  // hidden on the empty start screen
    QHash<QToolButton *, QString> m_iconNames;
};
