#include "inkpopup.h"

#include <QAbstractButton>
#include <QColorDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

// A round colour swatch; `custom` draws a rainbow ring with a "+".
class SwatchButton : public QAbstractButton
{
public:
    SwatchButton(const QColor &color, bool custom, QWidget *parent)
        : QAbstractButton(parent), m_color(color), m_custom(custom)
    {
        setCheckable(true);
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(custom ? InkPopup::tr("Custom colour…") : color.name().toUpper());
    }
    QColor color() const { return m_color; }
    void setColor(const QColor &c) { m_color = c; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(5, 5, -5, -5);
        const QColor text = palette().color(QPalette::WindowText);
        if (isChecked() || underMouse()) {
            QColor ring = isChecked() ? palette().color(QPalette::Highlight) : text;
            if (!isChecked())
                ring.setAlpha(60);
            p.setPen(QPen(ring, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        }
        if (m_custom) {
            QConicalGradient g(r.center(), 90);
            for (int i = 0; i <= 6; ++i)
                g.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 200, 240));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(r);
            p.setBrush(palette().color(QPalette::Window));
            p.drawEllipse(r.adjusted(3.5, 3.5, -3.5, -3.5));
            p.setPen(QPen(text, 1.6, Qt::SolidLine, Qt::RoundCap));
            const QPointF c = r.center();
            p.drawLine(c - QPointF(4, 0), c + QPointF(4, 0));
            p.drawLine(c - QPointF(0, 4), c + QPointF(0, 4));
            return;
        }
        QColor border = text;
        border.setAlpha(55);
        p.setPen(QPen(border, 1));
        p.setBrush(m_color);
        p.drawEllipse(r);
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    QColor m_color;
    bool m_custom;
};

// Shows the stroke as it will look: a dot (pen) or a bar (highlighter).
class SizePreview : public QWidget
{
public:
    SizePreview(bool highlighter, QWidget *parent) : QWidget(parent), m_highlighter(highlighter)
    {
        setFixedSize(48, 48);
    }
    void set(const QColor &c, int size) { m_color = c; m_size = size; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor bg = palette().color(QPalette::Base);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(rect(), 8, 8);
        const qreal s = qMin<qreal>(m_size, 40);
        p.setBrush(m_color);
        if (m_highlighter)
            p.drawRect(QRectF(8, (height() - s) / 2, width() - 16, s));
        else
            p.drawEllipse(QRectF((width() - s) / 2, (height() - s) / 2, s, s));
    }

private:
    bool m_highlighter;
    QColor m_color;
    int m_size = 4;
};

InkPopup::InkPopup(const QString &title, const QList<QColor> &colors, int minSize, int maxSize,
                   bool highlighter, QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("inkPopup"));
    setAttribute(Qt::WA_TranslucentBackground, false);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    setStyleSheet(QStringLiteral(
        "QFrame#inkPopup { background: palette(window); border: 1px solid %1; border-radius: 10px; }"
        "QLabel#popupTitle { font-weight: 600; }"
        "QLabel#popupHint { color: %2; }")
        .arg(dark ? QStringLiteral("#3a3a3e") : QStringLiteral("#d0d0d6"),
             dark ? QStringLiteral("#b8b8bc") : QStringLiteral("#5a5a60")));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);

    auto *heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("popupTitle"));
    layout->addWidget(heading);

    auto *grid = new QGridLayout;
    grid->setSpacing(4);
    const int columns = 6;
    int i = 0;
    for (const QColor &c : colors) {
        auto *sw = new SwatchButton(c, false, this);
        connect(sw, &QAbstractButton::clicked, this, [this, sw] { selectColor(sw->color(), true); });
        m_swatches.append(sw);
        grid->addWidget(sw, i / columns, i % columns);
        ++i;
    }
    m_custom = new SwatchButton(QColor(), true, this);
    connect(m_custom, &QAbstractButton::clicked, this, &InkPopup::chooseCustom);
    grid->addWidget(m_custom, i / columns, i % columns);
    layout->addLayout(grid);

    auto *sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(10);
    m_preview = new SizePreview(highlighter, this);
    sizeRow->addWidget(m_preview);
    auto *sliderCol = new QVBoxLayout;
    sliderCol->setSpacing(2);
    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setObjectName(QStringLiteral("popupHint"));
    sliderCol->addWidget(m_sizeLabel);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(minSize, maxSize);
    m_slider->setFocusPolicy(Qt::NoFocus);
    sliderCol->addWidget(m_slider);
    sizeRow->addLayout(sliderCol, 1);
    layout->addLayout(sizeRow);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_sizeLabel->setText(tr("Size  %1 px").arg(v));
        m_preview->set(m_color, v);
        emit changed(m_color, v);
    });
    setFixedWidth(columns * 34 + 28);
}

void InkPopup::setValues(const QColor &color, int size)
{
    const QSignalBlocker block(m_slider);
    m_slider->setValue(size);
    m_sizeLabel->setText(tr("Size  %1 px").arg(size));
    selectColor(color, false);
}

void InkPopup::selectColor(const QColor &color, bool notify)
{
    m_color = color;
    bool preset = false;
    for (SwatchButton *sw : qAsConst(m_swatches)) {
        const bool match = sw->color() == color;
        sw->setChecked(match);
        preset |= match;
    }
    m_custom->setChecked(!preset);
    m_preview->set(color, m_slider->value());
    if (notify)
        emit changed(m_color, m_slider->value());
}

void InkPopup::chooseCustom()
{
    hide();  // a Popup would close anyway when the dialog takes focus
    const QColor c = QColorDialog::getColor(m_color, parentWidget(), tr("Choose colour"));
    if (c.isValid())
        selectColor(c, true);
    else
        selectColor(m_color, false);
}

void InkPopup::popupBelow(QWidget *anchor)
{
    adjustSize();
    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() / 2 - width() / 2, anchor->height() + 6));
    if (QScreen *s = anchor->screen()) {
        const QRect avail = s->availableGeometry();
        pos.setX(qBound(avail.left() + 4, pos.x(), avail.right() - width() - 4));
    }
    move(pos);
    show();
}
