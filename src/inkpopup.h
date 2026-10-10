#pragma once

#include <QColor>
#include <QFrame>
#include <QList>

class QLabel;
class QSlider;
class SwatchButton;
class SizePreview;

// Colour + size flyout for the pen and highlighter.
class InkPopup : public QFrame
{
    Q_OBJECT
public:
    InkPopup(const QString &title, const QList<QColor> &colors, int minSize, int maxSize,
             bool highlighter, QWidget *parent = nullptr);

    void setValues(const QColor &color, int size);
    void popupBelow(QWidget *anchor);

signals:
    void changed(const QColor &color, int size);

private:
    void selectColor(const QColor &color, bool notify);
    void chooseCustom();

    QList<SwatchButton *> m_swatches;
    SwatchButton *m_custom;
    QSlider *m_slider;
    QLabel *m_sizeLabel;
    SizePreview *m_preview;
    QColor m_color;
};
