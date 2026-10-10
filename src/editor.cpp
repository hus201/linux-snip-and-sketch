#include "editor.h"

#include "controller.h"
#include "icons.h"
#include "inkpopup.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const QList<QColor> kPenColors{
    QColor(0x00, 0x00, 0x00), QColor(0xff, 0xff, 0xff), QColor(0x8a, 0x8a, 0x8a), QColor(0xe8, 0x11, 0x23),
    QColor(0xff, 0x8c, 0x00), QColor(0xff, 0xd1, 0x00), QColor(0x16, 0xc6, 0x0c), QColor(0x00, 0xb7, 0xc3),
    QColor(0x00, 0x78, 0xd7), QColor(0x5c, 0x2d, 0x91), QColor(0xe3, 0x00, 0x8c)};
const QList<QColor> kHighlightColors{
    QColor(0xff, 0xeb, 0x3b), QColor(0x76, 0xff, 0x03), QColor(0x18, 0xff, 0xff), QColor(0xff, 0x40, 0x81),
    QColor(0xff, 0xab, 0x40), QColor(0xb3, 0x88, 0xff)};

QString defaultDir()
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QDir::homePath();
    return pictures + QStringLiteral("/Screenshots");
}

QString rgba(QColor c, qreal alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(qRound(alpha * 255));
}

QFrame *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("sep"));
    line->setFixedSize(1, 22);
    return line;
}

}  // namespace

EditorWindow::EditorWindow(Controller *controller)
    : m_ctl(controller),
      m_settings(QStringLiteral("snip-and-sketch"), QStringLiteral("snip")),
      m_canvas(new Canvas), m_scroll(new QScrollArea), m_stack(new QStackedWidget)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Snip & Sketch for Linux"));
    setMinimumSize(560, 380);

    // Canvas page: scroll area with a floating toast on top.
    m_editorPage = new QWidget;
    auto *pageLayout = new QGridLayout(m_editorPage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_canvas->setScrollArea(m_scroll);
    pageLayout->addWidget(m_scroll, 0, 0);
    m_toast = new QLabel(m_editorPage);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->hide();

    m_stack->addWidget(buildEmptyState());
    m_stack->addWidget(m_editorPage);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildTopBar());
    layout->addWidget(m_stack, 1);
    layout->addWidget(buildBottomBar());
    setCentralWidget(central);

    QUndoStack *stack = m_canvas->undoStack();
    connect(stack, &QUndoStack::canUndoChanged, m_undoButton, &QWidget::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, m_redoButton, &QWidget::setEnabled);
    connect(stack, &QUndoStack::cleanChanged, this, &EditorWindow::updateTitle);
    connect(m_canvas, &Canvas::pageChanged, this, &EditorWindow::updateStatus);
    connect(m_canvas, &Canvas::zoomChanged, this, &EditorWindow::updateStatus);
    connect(m_canvas, &Canvas::cropEditChanged, m_applyCropButton, &QWidget::setEnabled);
    connect(m_canvas, &Canvas::cropApplied, this, [this] { selectTool(m_lastInkTool); });

    // ----------------------------------------------------- keyboard shortcuts
    auto shortcut = [this](const QList<QKeySequence> &keys, auto slot, bool needsImage = true) {
        auto *a = new QAction(this);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        if (needsImage)
            m_editingActions.append(a);
        return a;
    };
    shortcut({QKeySequence::New}, [this] { newSnip(0); }, false);
    shortcut({QKeySequence::Open}, [this] { openDialog(); }, false);
    shortcut({QKeySequence(Qt::Key_F1), QKeySequence(QStringLiteral("Ctrl+/"))}, [this] { showShortcuts(); }, false);
    shortcut({QKeySequence::Copy}, [this] { copy(); });
    shortcut({QKeySequence::Save, QKeySequence(QStringLiteral("Ctrl+Shift+S"))}, [this] { saveAs(); });
    shortcut({QKeySequence::Undo}, [stack] { stack->undo(); });
    shortcut({QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z"))}, [stack] { stack->redo(); });
    shortcut({QKeySequence(Qt::Key_P)}, [this] { selectTool(Canvas::PenTool); });
    shortcut({QKeySequence(Qt::Key_H)}, [this] { selectTool(Canvas::HighlighterTool); });
    shortcut({QKeySequence(Qt::Key_E)}, [this] { selectTool(Canvas::EraserTool); });
    shortcut({QKeySequence(Qt::Key_C)}, [this] { selectTool(Canvas::CropTool); });
    shortcut({QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))}, [this] { m_canvas->zoomBy(1.25); });
    shortcut({QKeySequence(QStringLiteral("Ctrl+-"))}, [this] { m_canvas->zoomBy(0.8); });
    shortcut({QKeySequence(QStringLiteral("Ctrl+0"))}, [this] { m_canvas->setZoom(1.0); });
    shortcut({QKeySequence(QStringLiteral("Ctrl+9"))}, [this] { m_canvas->zoomToFit(); });
    shortcut({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)}, [this] { applyCrop(); });
    shortcut({QKeySequence(Qt::Key_Escape)}, [this] { cancelCrop(); });

    loadSettings();
    applyStyle();
    refreshIcons();
    for (QWidget *w : qAsConst(m_imageOnly))
        w->hide();
    for (QAction *a : qAsConst(m_editingActions))
        a->setEnabled(false);
    resize(m_settings.value(QStringLiteral("editor/size"), QSize(960, 640)).toSize());
}

EditorWindow::~EditorWindow()
{
    // The canvas's undo stack emits signals while it is being destroyed;
    // don't let them reach this half-destroyed window.
    m_canvas->undoStack()->disconnect(this);
    m_canvas->undoStack()->disconnect(m_undoButton);
    m_canvas->undoStack()->disconnect(m_redoButton);
    m_canvas->disconnect(this);
    m_canvas->disconnect(m_applyCropButton);
}

// ===================================================================== layout
QToolButton *EditorWindow::toolButton(const QString &icon, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setToolTip(tip);
    b->setIconSize(QSize(20, 20));
    b->setFocusPolicy(Qt::NoFocus);  // keep Space for panning the canvas
    b->setCursor(Qt::PointingHandCursor);
    m_iconNames.insert(b, icon);
    return b;
}

QWidget *EditorWindow::buildTopBar()
{
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(4);

    // --- left: New (snip now) + delay menu, like Windows' "New ▾"
    m_newButton = toolButton(QStringLiteral("snip"), tr("New snip (Ctrl+N)"), bar);
    m_newButton->setObjectName(QStringLiteral("primaryLeft"));
    m_newButton->setText(tr("New"));
    m_newButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_newButton, &QToolButton::clicked, this, [this] { newSnip(0); });
    auto *delay = toolButton(QStringLiteral("chevron"), tr("Snip after a delay"), bar);
    delay->setObjectName(QStringLiteral("primaryRight"));
    delay->setPopupMode(QToolButton::InstantPopup);
    auto *delayMenu = new QMenu(delay);
    delayMenu->addAction(tr("Snip now"), this, [this] { newSnip(0); });
    delayMenu->addAction(tr("Snip in 3 seconds"), this, [this] { newSnip(3); });
    delayMenu->addAction(tr("Snip in 10 seconds"), this, [this] { newSnip(10); });
    delay->setMenu(delayMenu);
    auto *newGroup = new QHBoxLayout;
    newGroup->setSpacing(1);
    newGroup->addWidget(m_newButton);
    newGroup->addWidget(delay);
    row->addLayout(newGroup);
    row->addStretch(1);

    // --- centre: drawing tools
    auto *tools = new QFrame(bar);
    tools->setObjectName(QStringLiteral("toolGroup"));
    auto *toolRow = new QHBoxLayout(tools);
    toolRow->setContentsMargins(4, 3, 4, 3);
    toolRow->setSpacing(2);
    m_tools = new QButtonGroup(this);
    m_tools->setExclusive(true);
    const struct { Canvas::Tool tool; const char *icon; QString tip; } defs[] = {
        {Canvas::PenTool, "pen", tr("Pen (P) — click again for colour and size")},
        {Canvas::HighlighterTool, "highlighter", tr("Highlighter (H) — click again for colour and size")},
        {Canvas::EraserTool, "eraser", tr("Eraser (E) — removes whole strokes")},
        {Canvas::CropTool, "crop", tr("Crop (C)")},
    };
    for (const auto &d : defs) {
        if (d.tool == Canvas::CropTool)
            toolRow->addWidget(separator(tools));
        auto *b = toolButton(QLatin1String(d.icon), d.tip, tools);
        b->setCheckable(true);
        b->setIconSize(QSize(22, 22));
        m_tools->addButton(b, d.tool);
        m_toolButtons.insert(d.tool, b);
        connect(b, &QToolButton::clicked, this, [this, t = d.tool] { toolClicked(t); });
        toolRow->addWidget(b);
    }
    row->addWidget(tools);
    m_imageOnly << tools;

    // --- crop mode: Cancel / Apply
    m_cropBar = new QWidget(bar);
    auto *cropRow = new QHBoxLayout(m_cropBar);
    cropRow->setContentsMargins(8, 0, 0, 0);
    cropRow->setSpacing(4);
    auto *cancel = toolButton(QStringLiteral("close"), tr("Cancel crop (Esc)"), m_cropBar);
    cancel->setText(tr("Cancel"));
    cancel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(cancel, &QToolButton::clicked, this, &EditorWindow::cancelCrop);
    m_applyCropButton = toolButton(QStringLiteral("check"), tr("Apply crop (Enter, or double-click)"), m_cropBar);
    m_applyCropButton->setObjectName(QStringLiteral("primary"));
    m_applyCropButton->setText(tr("Apply"));
    m_applyCropButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_applyCropButton->setEnabled(false);
    connect(m_applyCropButton, &QToolButton::clicked, this, &EditorWindow::applyCrop);
    cropRow->addWidget(cancel);
    cropRow->addWidget(m_applyCropButton);
    m_cropBar->hide();
    row->addWidget(m_cropBar);
    row->addStretch(1);

    // --- right: history + output
    auto *right = new QWidget(bar);
    auto *rightRow = new QHBoxLayout(right);
    rightRow->setContentsMargins(0, 0, 0, 0);
    rightRow->setSpacing(2);
    m_undoButton = toolButton(QStringLiteral("undo"), tr("Undo (Ctrl+Z)"), right);
    m_redoButton = toolButton(QStringLiteral("redo"), tr("Redo (Ctrl+Y)"), right);
    m_undoButton->setEnabled(false);
    m_redoButton->setEnabled(false);
    connect(m_undoButton, &QToolButton::clicked, this, [this] { m_canvas->undoStack()->undo(); });
    connect(m_redoButton, &QToolButton::clicked, this, [this] { m_canvas->undoStack()->redo(); });
    m_copyButton = toolButton(QStringLiteral("copy"), tr("Copy to clipboard (Ctrl+C)"), right);
    connect(m_copyButton, &QToolButton::clicked, this, &EditorWindow::copy);
    m_saveButton = toolButton(QStringLiteral("save"), tr("Save as (Ctrl+S)"), right);
    connect(m_saveButton, &QToolButton::clicked, this, &EditorWindow::saveAs);
    rightRow->addWidget(m_undoButton);
    rightRow->addWidget(m_redoButton);
    rightRow->addWidget(separator(right));
    rightRow->addWidget(m_copyButton);
    rightRow->addWidget(m_saveButton);
    row->addWidget(right);
    m_imageOnly << right;

    m_moreButton = toolButton(QStringLiteral("more"), tr("More options"), bar);
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    auto *more = new QMenu(m_moreButton);
    QAction *open = more->addAction(tr("Open image…"), this, &EditorWindow::openDialog);
    open->setShortcut(QKeySequence::Open);
    more->addSeparator();
    m_expandAction = more->addAction(tr("Expand canvas when drawing outside the image"));
    m_expandAction->setCheckable(true);
    connect(m_expandAction, &QAction::toggled, this, [this](bool on) {
        m_canvas->setExpandEnabled(on);
        m_settings.setValue(QStringLiteral("editor/expandCanvas"), on);
    });
    more->addSeparator();
    QAction *keys = more->addAction(tr("Keyboard shortcuts"), this, &EditorWindow::showShortcuts);
    keys->setShortcut(QKeySequence(Qt::Key_F1));
    more->addAction(tr("About Snip & Sketch for Linux"), this, [this] {
        QMessageBox::about(this, tr("About Snip & Sketch for Linux"),
                           tr("<h3>Snip & Sketch for Linux %1</h3>"
                              "<p>Released under the MIT License.</p>"
                              "<p>Capture, annotate and share screenshots.</p>"
                              "<p>Press <b>Win+Shift+S</b> anywhere to snip.</p>")
                               .arg(QApplication::applicationVersion()));
    });
    m_moreButton->setMenu(more);
    row->addWidget(m_moreButton);
    return bar;
}

QWidget *EditorWindow::buildBottomBar()
{
    m_bottomBar = new QWidget;
    m_bottomBar->setObjectName(QStringLiteral("bottomBar"));
    m_bottomBar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(m_bottomBar);
    row->setContentsMargins(14, 3, 10, 3);
    row->setSpacing(2);
    m_sizeLabel = new QLabel(m_bottomBar);
    m_sizeLabel->setObjectName(QStringLiteral("status"));
    row->addWidget(m_sizeLabel);
    row->addStretch(1);

    auto *out = toolButton(QStringLiteral("zoom-out"), tr("Zoom out (Ctrl+−)"), m_bottomBar);
    connect(out, &QToolButton::clicked, this, [this] { m_canvas->zoomBy(0.8); });
    m_zoomLabel = new QToolButton(m_bottomBar);
    m_zoomLabel->setObjectName(QStringLiteral("zoomLabel"));
    m_zoomLabel->setToolTip(tr("Actual size (Ctrl+0)"));
    m_zoomLabel->setFocusPolicy(Qt::NoFocus);
    m_zoomLabel->setMinimumWidth(56);
    connect(m_zoomLabel, &QToolButton::clicked, this, [this] { m_canvas->setZoom(1.0); });
    auto *in = toolButton(QStringLiteral("zoom-in"), tr("Zoom in (Ctrl+=)"), m_bottomBar);
    connect(in, &QToolButton::clicked, this, [this] { m_canvas->zoomBy(1.25); });
    auto *fit = toolButton(QStringLiteral("fit"), tr("Fit to window (Ctrl+9)"), m_bottomBar);
    connect(fit, &QToolButton::clicked, m_canvas, &Canvas::zoomToFit);
    for (QToolButton *b : {out, in, fit})
        b->setIconSize(QSize(16, 16));
    row->addWidget(out);
    row->addWidget(m_zoomLabel);
    row->addWidget(in);
    row->addWidget(separator(m_bottomBar));
    row->addWidget(fit);
    m_imageOnly << m_bottomBar;
    return m_bottomBar;
}

QWidget *EditorWindow::buildEmptyState()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("emptyPage"));
    page->setAttribute(Qt::WA_StyledBackground);
    auto *col = new QVBoxLayout(page);
    col->setSpacing(10);
    col->addStretch(3);

    auto *logo = new QLabel(page);
    logo->setPixmap(QIcon(QStringLiteral(":/snip-and-sketch.svg")).pixmap(88, 88));
    logo->setAlignment(Qt::AlignCenter);
    col->addWidget(logo);
    auto *title = new QLabel(tr("Snip & Sketch for Linux"), page);
    title->setObjectName(QStringLiteral("emptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    col->addWidget(title);
    auto *hint = new QLabel(tr("Press <b>Win+Shift+S</b> anywhere to snip, or start one here."), page);
    hint->setObjectName(QStringLiteral("emptyHint"));
    hint->setAlignment(Qt::AlignCenter);
    col->addWidget(hint);
    col->addSpacing(12);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *snip = toolButton(QStringLiteral("snip"), tr("New snip (Ctrl+N)"), page);
    snip->setObjectName(QStringLiteral("primaryBig"));
    snip->setText(tr("New snip"));
    snip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(snip, &QToolButton::clicked, this, [this] { newSnip(0); });
    auto *open = toolButton(QStringLiteral("open"), tr("Open an image (Ctrl+O)"), page);
    open->setObjectName(QStringLiteral("secondaryBig"));
    open->setText(tr("Open image"));
    open->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(open, &QToolButton::clicked, this, &EditorWindow::openDialog);
    buttons->addWidget(snip);
    buttons->addSpacing(6);
    buttons->addWidget(open);
    buttons->addStretch(1);
    col->addLayout(buttons);
    col->addStretch(4);
    return page;
}

// ====================================================================== style
void EditorWindow::applyStyle()
{
    const QPalette pal = QApplication::palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const bool dark = window.lightness() < 128;
    QColor accent = pal.color(QPalette::Highlight);
    if (accent.saturation() < 40)  // grey highlight in some themes: use a real accent
        accent = QColor(0x00, 0x67, 0xc0);
    const QColor accentText = accent.lightness() > 150 ? QColor(Qt::black) : QColor(Qt::white);
    const QColor line = dark ? QColor(0x3a, 0x3a, 0x3e) : QColor(0xdc, 0xdc, 0xe0);
    const QColor group = dark ? QColor(0x2c, 0x2c, 0x30) : QColor(0xec, 0xec, 0xef);

    setStyleSheet(QStringLiteral(R"(
        QWidget#topBar { background: %1; border-bottom: 1px solid %4; }
        QWidget#bottomBar { background: %1; border-top: 1px solid %4; }
        QWidget#emptyPage { background: %11; }
        QFrame#sep { background: %4; margin: 0 4px; }
        QFrame#toolGroup { background: %5; border-radius: 9px; }
        QToolButton { border: none; border-radius: 6px; padding: 6px; background: transparent; color: %2; }
        QToolButton:hover { background: %6; }
        QToolButton:pressed { background: %7; }
        QToolButton:checked { background: %8; }
        QToolButton:disabled { color: %9; }
        QToolButton::menu-indicator { image: none; width: 0px; }
        QToolButton#primary, QToolButton#primaryLeft, QToolButton#primaryRight, QToolButton#primaryBig {
            background: %3; color: %10; font-weight: 600; }
        QToolButton#primary:hover, QToolButton#primaryLeft:hover, QToolButton#primaryRight:hover,
        QToolButton#primaryBig:hover { background: %12; }
        QToolButton#primary:disabled { background: %6; color: %9; }
        QToolButton#primaryLeft { border-top-right-radius: 0; border-bottom-right-radius: 0; padding: 6px 12px 6px 9px; }
        QToolButton#primaryRight { border-top-left-radius: 0; border-bottom-left-radius: 0; padding: 6px 3px; }
        QToolButton#primaryBig, QToolButton#secondaryBig { padding: 10px 18px; border-radius: 8px; font-size: 14px; }
        QToolButton#secondaryBig { background: %5; }
        QToolButton#secondaryBig:hover { background: %6; }
        QToolButton#zoomLabel { padding: 4px 6px; }
        QLabel#status { color: %9; }
        QLabel#emptyTitle { font-size: 22px; font-weight: 600; }
        QLabel#emptyHint { color: %9; font-size: 13px; }
        QLabel#toast { background: rgba(28, 28, 30, 235); color: white; border-radius: 15px; padding: 7px 16px; }
        QScrollArea { border: none; }
    )")
        .arg(window.name(), text.name(), accent.name(), line.name(), group.name(),
             rgba(text, 0.08), rgba(text, 0.14), rgba(accent, dark ? 0.35 : 0.20), rgba(text, 0.45))
        .arg(accentText.name(), dark ? QStringLiteral("#18181a") : QStringLiteral("#e9e9ec"),
             accent.lighter(dark ? 120 : 110).name()));
}

void EditorWindow::refreshIcons()
{
    for (auto it = m_iconNames.constBegin(); it != m_iconNames.constEnd(); ++it) {
        QToolButton *b = it.key();
        const QString name = it.value();
        const bool primary = b->objectName().startsWith(QLatin1String("primary"));
        if (b == m_toolButtons.value(Canvas::PenTool))
            b->setIcon(Icons::ink(name, m_canvas->inkColor(Canvas::PenTool)));
        else if (b == m_toolButtons.value(Canvas::HighlighterTool))
            b->setIcon(Icons::ink(name, m_canvas->inkColor(Canvas::HighlighterTool)));
        else
            b->setIcon(primary ? Icons::get(name, QColor(Qt::white)) : Icons::get(name));
    }
}

void EditorWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        applyStyle();
        refreshIcons();
    }
    QMainWindow::changeEvent(event);
}

// ====================================================================== tools
void EditorWindow::selectTool(Canvas::Tool tool)
{
    if (!m_canvas->hasImage())
        return;
    m_canvas->setTool(tool);
    if (QToolButton *b = m_toolButtons.value(tool))
        b->setChecked(true);
    m_cropBar->setVisible(tool == Canvas::CropTool);
    if (tool == Canvas::PenTool || tool == Canvas::HighlighterTool)
        m_lastInkTool = tool;
    m_canvas->setFocus();
}

void EditorWindow::toolClicked(Canvas::Tool tool)
{
    const bool ink = tool == Canvas::PenTool || tool == Canvas::HighlighterTool;
    if (ink && m_canvas->tool() == tool)
        showInkPopup(tool);
    else
        selectTool(tool);
}

void EditorWindow::showInkPopup(Canvas::Tool tool)
{
    const bool hl = tool == Canvas::HighlighterTool;
    InkPopup *&popup = hl ? m_highlightPopup : m_penPopup;
    if (!popup) {
        popup = hl ? new InkPopup(tr("Highlighter"), kHighlightColors, 6, 48, true, this)
                   : new InkPopup(tr("Pen"), kPenColors, 1, 32, false, this);
        connect(popup, &InkPopup::changed, this, [this, tool](const QColor &c, int size) {
            m_canvas->setInk(tool, c, size);
            saveInk(tool);
            refreshIcons();
        });
    }
    popup->setValues(m_canvas->inkColor(tool), m_canvas->inkWidth(tool));
    popup->popupBelow(m_toolButtons.value(tool));
}

void EditorWindow::loadSettings()
{
    for (Canvas::Tool tool : {Canvas::PenTool, Canvas::HighlighterTool}) {
        const QString key = tool == Canvas::PenTool ? QStringLiteral("ink/pen/") : QStringLiteral("ink/highlighter/");
        const QColor c(m_settings.value(key + QStringLiteral("color"), m_canvas->inkColor(tool).name()).toString());
        const int w = m_settings.value(key + QStringLiteral("width"), m_canvas->inkWidth(tool)).toInt();
        m_canvas->setInk(tool, c.isValid() ? c : m_canvas->inkColor(tool), qBound(1, w, 64));
    }
    m_expandAction->setChecked(m_settings.value(QStringLiteral("editor/expandCanvas"), false).toBool());
    m_canvas->setExpandEnabled(m_expandAction->isChecked());
}

void EditorWindow::saveInk(Canvas::Tool tool)
{
    const QString key = tool == Canvas::PenTool ? QStringLiteral("ink/pen/") : QStringLiteral("ink/highlighter/");
    m_settings.setValue(key + QStringLiteral("color"), m_canvas->inkColor(tool).name());
    m_settings.setValue(key + QStringLiteral("width"), m_canvas->inkWidth(tool));
}

// ==================================================================== actions
void EditorWindow::newSnip(int delay)
{
    if (m_ctl && confirmDiscard())
        m_ctl->startSnip({}, delay, this);
}

void EditorWindow::setImage(const QImage &image, const QString &path)
{
    const bool first = !m_canvas->hasImage();
    m_path = path;
    m_canvas->setImage(image);
    m_stack->setCurrentWidget(m_editorPage);
    for (QWidget *w : qAsConst(m_imageOnly))
        w->show();
    for (QAction *a : qAsConst(m_editingActions))
        a->setEnabled(true);
    selectTool(m_lastInkTool);
    if (first && !isMaximized())
        fitWindowToImage(image);
    QTimer::singleShot(0, m_canvas, &Canvas::zoomToFit);  // once the viewport has its size
    updateTitle();
}

void EditorWindow::openFile(const QString &path)
{
    const QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Snip & Sketch for Linux"), tr("Couldn't open %1.").arg(path));
        return;
    }
    setImage(image, QFileInfo(path).absoluteFilePath());
}

void EditorWindow::openDialog()
{
    if (!confirmDiscard())
        return;
    const QString name = QFileDialog::getOpenFileName(this, tr("Open image"), defaultDir(),
                                                      tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
    if (!name.isEmpty())
        openFile(name);
}

void EditorWindow::copy()
{
    if (!m_canvas->hasImage())
        return;
    QGuiApplication::clipboard()->setImage(m_canvas->renderImage());
    toast(tr("Copied to clipboard"));
}

bool EditorWindow::saveAs()
{
    if (!m_canvas->hasImage())
        return false;
    const QString suggested = !m_path.isEmpty()
        ? m_path
        : defaultDir() + QStringLiteral("/Snip %1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
    QString filter;
    QString name = QFileDialog::getSaveFileName(this, tr("Save as"), suggested,
                                                tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;BMP image (*.bmp)"),
                                                &filter);
    if (name.isEmpty())
        return false;
    if (QFileInfo(name).suffix().isEmpty())
        name += filter.contains(QLatin1String("JPEG")) ? QStringLiteral(".jpg")
              : filter.contains(QLatin1String("BMP"))  ? QStringLiteral(".bmp")
                                                       : QStringLiteral(".png");
    QDir().mkpath(QFileInfo(name).absolutePath());
    if (!m_canvas->renderImage().save(name, nullptr, 95)) {
        QMessageBox::warning(this, tr("Snip & Sketch for Linux"), tr("Couldn't save %1.").arg(name));
        return false;
    }
    m_path = name;
    m_canvas->undoStack()->setClean();
    updateTitle();
    toast(tr("Saved  %1").arg(QFileInfo(name).fileName()));
    return true;
}

void EditorWindow::applyCrop()
{
    if (m_canvas->tool() != Canvas::CropTool)
        return;
    m_canvas->applyCrop();
    selectTool(m_lastInkTool);
}

void EditorWindow::cancelCrop()
{
    if (m_canvas->tool() != Canvas::CropTool)
        return;
    m_canvas->cancelCrop();
    selectTool(m_lastInkTool);
}

bool EditorWindow::confirmDiscard()
{
    if (!m_canvas->hasImage() || m_canvas->undoStack()->isClean())
        return true;
    const auto answer = QMessageBox::question(this, tr("Snip & Sketch for Linux"), tr("Save changes to your snip?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveAs();
    return answer == QMessageBox::Discard;
}

void EditorWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    if (!isMaximized())
        m_settings.setValue(QStringLiteral("editor/size"), size());
    event->accept();
}

void EditorWindow::showShortcuts()
{
    const QList<QPair<QString, QString>> rows{
        {tr("Win+Shift+S"), tr("Snip from anywhere")},
        {tr("Ctrl+N"), tr("New snip")},
        {tr("Ctrl+O"), tr("Open an image")},
        {tr("P / H / E / C"), tr("Pen, highlighter, eraser, crop")},
        {tr("Click the active pen again"), tr("Colour and size")},
        {tr("Enter / Esc"), tr("Apply / cancel crop")},
        {tr("Ctrl+Z / Ctrl+Y"), tr("Undo / redo")},
        {tr("Ctrl+C"), tr("Copy")},
        {tr("Ctrl+S"), tr("Save as")},
        {tr("Ctrl+wheel, Ctrl+= / Ctrl+−"), tr("Zoom")},
        {tr("Ctrl+0 / Ctrl+9"), tr("Actual size / fit to window")},
        {tr("Space+drag or middle-drag"), tr("Pan")},
    };
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='5'>");
    for (const auto &[keys, what] : rows)
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(keys.toHtmlEscaped(), what.toHtmlEscaped());
    html += QStringLiteral("</table>");
    QMessageBox box(QMessageBox::NoIcon, tr("Keyboard shortcuts"), html, QMessageBox::Close, this);
    box.exec();
}

// ==================================================================== helpers
void EditorWindow::toast(const QString &text)
{
    m_toast->setText(text);
    m_toast->adjustSize();
    m_toast->move((m_editorPage->width() - m_toast->width()) / 2, m_editorPage->height() - m_toast->height() - 18);
    auto *effect = new QGraphicsOpacityEffect(m_toast);
    effect->setOpacity(1.0);
    m_toast->setGraphicsEffect(effect);  // replaces (and deletes) the previous one
    m_toast->raise();
    m_toast->show();
    auto *fade = new QPropertyAnimation(effect, "opacity", effect);
    fade->setDuration(450);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, m_toast, &QWidget::hide);
    QTimer::singleShot(1600, fade, [fade] { fade->start(); });
}

void EditorWindow::fitWindowToImage(const QImage &image)
{
    QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect avail = s->availableGeometry();
    const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
    const QSize logical = (QSizeF(image.size()) / dpr).toSize();
    const QSize want = logical + QSize(80, 56 + 36 + 80);  // margins, top bar, bottom bar
    resize(std::clamp(want.width(), 760, std::max(760, int(avail.width() * 0.85))),
           std::clamp(want.height(), 520, std::max(520, int(avail.height() * 0.85))));
}

void EditorWindow::updateStatus()
{
    if (!m_canvas->hasImage())
        return;
    const QRect c = m_canvas->cropRect();
    m_sizeLabel->setText(tr("%1 × %2 px").arg(c.width()).arg(c.height()));
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_canvas->zoom() * 100)));
}

void EditorWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
    const QString dirty = m_canvas->undoStack()->isClean() ? QString() : QStringLiteral(" •");
    setWindowTitle(QStringLiteral("%1%2 — %3").arg(name, dirty, tr("Snip & Sketch for Linux")));
    updateStatus();
}
