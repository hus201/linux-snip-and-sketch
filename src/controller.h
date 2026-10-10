#pragma once

#include "capture.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QStringList>

class EditorWindow;
class QLocalServer;
class SnipOverlay;

// What a launch (or a forwarded launch from a second process) asks for.
struct LaunchRequest {
    QString mode;          // empty = last used
    int delay = 0;
    bool noEditor = false;
    bool app = false;      // open the editor without snipping
    QString file;          // open this image in the editor

    QByteArray serialize() const;
    static LaunchRequest deserialize(const QByteArray &data);
};

// Glue: capture → overlays → clipboard → editor / notification.
// Runs as a single instance per user: later launches are forwarded here.
class Controller : public QObject
{
    Q_OBJECT
public:
    Controller();
    ~Controller() override;

    // Returns false if another instance is running; the request was forwarded to it.
    bool becomePrimary(const LaunchRequest &request);
    void handle(const LaunchRequest &request);

    void startSnip(const QString &mode, int delay, EditorWindow *editor = nullptr,
                   bool noEditor = false);
    EditorWindow *showEditor(const QImage &image = {}, const QString &path = {},
                             EditorWindow *editor = nullptr);

private:
    void capture(const QString &mode);
    void showOverlays(const ScreenShots &shots, const QString &mode);
    void setMode(const QString &mode);
    void closeOverlays();
    void onSnip(const QImage &image);
    void notifyCopied(const QImage &image);
    void maybeQuit();

    QSettings m_settings;
    QString m_lastMode;
    QLocalServer *m_server = nullptr;
    QList<QPointer<EditorWindow>> m_editors;
    QList<SnipOverlay *> m_overlays;
    ScreenShots m_shots;
    bool m_windowsLoaded = false;
    bool m_busy = false;
    bool m_notificationOpen = false;
    // Context of the snip in progress.
    QPointer<EditorWindow> m_snipEditor;
    bool m_snipNoEditor = false;
};
