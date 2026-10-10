#pragma once

#include "capture.h"

#include <QHash>
#include <QPainterPath>
#include <QWidget>

class QAbstractButton;
class QFrame;

// Full-screen selection overlay for ONE monitor, showing its frozen screenshot.
// The controller creates one per screen and keeps their modes in sync.
class SnipOverlay : public QWidget
{
    Q_OBJECT
public:
    SnipOverlay(const ScreenShot &shot, const QString &mode, bool withToolbar,
                bool windowModeAvailable);

    void setMode(const QString &mode);
    void setWindows(const QList<QRect> &globalRects);
    void clearSelection();
    void showOnScreen();

    static const QStringList &modes();

signals:
    void modeRequested(const QString &mode);
    void snipped(const QImage &image);
    void cancelled();
    void interactionStarted(SnipOverlay *self);  // so other screens drop their selection

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void buildToolbar(bool windowModeAvailable);
    void hoverWindow(const QPoint &pos);
    void finish(const QPainterPath &path);
    QImage crop(const QPainterPath &path) const;

    ScreenShot m_shot;
    QString m_mode;
    QPainterPath m_path;
    QPointF m_origin;
    bool m_dragging = false;
    QList<QRect> m_windows;  // local coordinates, top-most first
    QFrame *m_toolbar = nullptr;
    QHash<QString, QAbstractButton *> m_buttons;
};
