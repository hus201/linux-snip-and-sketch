#include "controller.h"

#include "editor.h"
#include "overlay.h"
#include "windowlist.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include <unistd.h>

namespace {

// org.freedesktop.Notifications toast with a clickable "Edit" action.
class Notifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    void show(const QString &summary, const QString &body, const QString &imagePath)
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const QString service = QStringLiteral("org.freedesktop.Notifications");
        const QString path = QStringLiteral("/org/freedesktop/Notifications");
        bus.connect(service, path, service, QStringLiteral("ActionInvoked"), this, SLOT(onAction(uint, QString)));
        bus.connect(service, path, service, QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));

        QDBusMessage call = QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Notify"));
        const QVariantMap hints{{QStringLiteral("image-path"), imagePath},
                                {QStringLiteral("desktop-entry"), QStringLiteral("snip-and-sketch")}};
        call << tr("Snip & Sketch for Linux") << uint(0) << QStringLiteral("snip-and-sketch") << summary << body
             << QStringList{QStringLiteral("default"), tr("Edit")} << hints << int(-1);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<uint> reply = *w;
            if (reply.isError())
                emit closed();  // no notification daemon — nothing to wait for
            else
                m_id = reply.value();
            w->deleteLater();
        });
    }

signals:
    void activated();
    void closed();

private slots:
    void onAction(uint id, const QString &)
    {
        if (id == m_id)
            emit activated();
    }
    void onClosed(uint id, uint)
    {
        if (id == m_id)
            emit closed();
    }

private:
    uint m_id = 0;
};

QString serverName()
{
    return QStringLiteral("snip-and-sketch-%1").arg(getuid());
}

}  // namespace

// ============================================================ LaunchRequest
QByteArray LaunchRequest::serialize() const
{
    return QJsonDocument(QJsonObject{{QStringLiteral("mode"), mode},
                                     {QStringLiteral("delay"), delay},
                                     {QStringLiteral("noEditor"), noEditor},
                                     {QStringLiteral("app"), app},
                                     {QStringLiteral("file"), file}})
        .toJson(QJsonDocument::Compact);
}

LaunchRequest LaunchRequest::deserialize(const QByteArray &data)
{
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    LaunchRequest r;
    r.mode = o.value(QStringLiteral("mode")).toString();
    r.delay = o.value(QStringLiteral("delay")).toInt();
    r.noEditor = o.value(QStringLiteral("noEditor")).toBool();
    r.app = o.value(QStringLiteral("app")).toBool();
    r.file = o.value(QStringLiteral("file")).toString();
    return r;
}

// =============================================================== Controller
Controller::Controller()
    : m_settings(QStringLiteral("snip-and-sketch"), QStringLiteral("snip"))
{
    m_lastMode = m_settings.value(QStringLiteral("mode"), QStringLiteral("rect")).toString();
    if (!SnipOverlay::modes().contains(m_lastMode))
        m_lastMode = QStringLiteral("rect");

    // We stay alive while we own the clipboard so the snip can still be pasted
    // on desktops without a clipboard manager; quit once someone else copies.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            [this] { QTimer::singleShot(0, this, &Controller::maybeQuit); });
    auto *poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, &Controller::maybeQuit);
    poll->start(30 * 1000);
}

Controller::~Controller()
{
    qDeleteAll(m_overlays);
}

bool Controller::becomePrimary(const LaunchRequest &request)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (socket.waitForConnected(500)) {
        socket.write(request.serialize());
        socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
        return false;
    }
    QLocalServer::removeServer(serverName());  // stale socket from a crash
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    m_server->listen(serverName());
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server->nextPendingConnection()) {
            auto *buffer = new QByteArray;
            connect(client, &QLocalSocket::readyRead, client, [client, buffer] { *buffer += client->readAll(); });
            connect(client, &QLocalSocket::disconnected, this, [this, client, buffer] {
                *buffer += client->readAll();
                handle(LaunchRequest::deserialize(*buffer));
                delete buffer;
                client->deleteLater();
            });
        }
    });
    return true;
}

void Controller::handle(const LaunchRequest &request)
{
    if (!request.file.isEmpty())
        showEditor()->openFile(request.file);
    else if (request.app)
        showEditor();
    else
        startSnip(request.mode, request.delay, nullptr, request.noEditor);
}

// ----------------------------------------------------------------- snipping
void Controller::startSnip(const QString &mode, int delay, EditorWindow *editor, bool noEditor)
{
    if (m_busy) {  // e.g. hotkey pressed twice: bring the overlay back up front
        for (SnipOverlay *o : qAsConst(m_overlays))
            o->raise();
        return;
    }
    m_busy = true;
    m_snipEditor = editor;
    m_snipNoEditor = noEditor;
    const QString chosen = mode.isEmpty() ? m_lastMode : mode;
    if (editor)
        editor->hide();
    // Give the compositor a moment to repaint without our editor window.
    const int wait = delay * 1000 + (editor ? 350 : 0);
    QTimer::singleShot(wait, this, [this, chosen] { capture(chosen); });
}

void Controller::capture(const QString &mode)
{
    Capture::grab([this, mode](const ScreenShots &shots, const QString &error) {
        if (shots.isEmpty()) {
            QMessageBox::warning(nullptr, tr("Snip & Sketch for Linux"), tr("Couldn't capture the screen:\n%1").arg(error));
            onSnip({});
            return;
        }
        if (mode == QLatin1String("full")) {
            setMode(mode);
            onSnip(Capture::stitch(shots));
            return;
        }
        showOverlays(shots, mode);
    });
}

void Controller::showOverlays(const ScreenShots &shots, const QString &mode)
{
    m_shots = shots;
    m_windowsLoaded = false;
    QScreen *toolbarScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!toolbarScreen)
        toolbarScreen = QGuiApplication::primaryScreen();

    SnipOverlay *toolbarOverlay = nullptr;
    for (const ScreenShot &shot : shots) {
        auto *overlay = new SnipOverlay(shot, mode, shot.screen == toolbarScreen, !Capture::usePortal());
        if (shot.screen == toolbarScreen)
            toolbarOverlay = overlay;
        connect(overlay, &SnipOverlay::modeRequested, this, &Controller::setMode);
        connect(overlay, &SnipOverlay::snipped, this, [this](const QImage &image) {
            closeOverlays();
            onSnip(image);
        });
        connect(overlay, &SnipOverlay::cancelled, this, [this] {
            closeOverlays();
            onSnip({});
        });
        connect(overlay, &SnipOverlay::interactionStarted, this, [this](SnipOverlay *active) {
            for (SnipOverlay *o : qAsConst(m_overlays))
                if (o != active)
                    o->clearSelection();
        });
        m_overlays.append(overlay);
    }
    // Show the toolbar's overlay last so it ends up focused and on top.
    for (SnipOverlay *o : qAsConst(m_overlays))
        if (o != toolbarOverlay)
            o->showOnScreen();
    if (toolbarOverlay)
        toolbarOverlay->showOnScreen();
    setMode(mode);
}

void Controller::setMode(const QString &mode)
{
    m_lastMode = mode;
    m_settings.setValue(QStringLiteral("mode"), mode);
    if (m_overlays.isEmpty())
        return;
    if (mode == QLatin1String("full")) {
        const QImage image = Capture::stitch(m_shots);
        closeOverlays();
        onSnip(image);
        return;
    }
    if (mode == QLatin1String("window") && !m_windowsLoaded) {
        m_windowsLoaded = true;
        const QList<QRect> windows = WindowList::visibleWindows(QGuiApplication::primaryScreen()->devicePixelRatio());
        for (SnipOverlay *o : qAsConst(m_overlays))
            o->setWindows(windows);
    }
    for (SnipOverlay *o : qAsConst(m_overlays))
        o->setMode(mode);
}

void Controller::closeOverlays()
{
    const QList<SnipOverlay *> overlays = std::exchange(m_overlays, {});
    for (SnipOverlay *o : overlays) {
        o->disconnect(this);
        o->close();  // WA_DeleteOnClose
    }
    m_shots.clear();
}

void Controller::onSnip(const QImage &image)
{
    m_busy = false;
    QPointer<EditorWindow> editor = std::exchange(m_snipEditor, nullptr);
    if (image.isNull()) {
        if (editor)
            editor->show();
        QTimer::singleShot(0, this, &Controller::maybeQuit);
        return;
    }
    QGuiApplication::clipboard()->setImage(image);
    if (m_snipNoEditor && !editor)
        notifyCopied(image);
    else
        showEditor(image, {}, editor);
}

// ------------------------------------------------------------------ windows
EditorWindow *Controller::showEditor(const QImage &image, const QString &path, EditorWindow *editor)
{
    if (!editor) {
        editor = new EditorWindow(this);
        connect(editor, &QObject::destroyed, this, [this] { QTimer::singleShot(0, this, &Controller::maybeQuit); });
        m_editors.append(editor);
    }
    if (!image.isNull())
        editor->setImage(image, path);
    editor->show();
    editor->raise();
    editor->activateWindow();
    return editor;
}

void Controller::notifyCopied(const QImage &image)
{
    // Thumbnail for the toast; the notification daemon reads it from disk.
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    const QString thumb = cache + QStringLiteral("/last-snip.png");
    image.save(thumb);

    auto *notifier = new Notifier(this);
    m_notificationOpen = true;
    connect(notifier, &Notifier::activated, this, [this, image] { showEditor(image); });
    connect(notifier, &Notifier::closed, this, [this, notifier] {
        m_notificationOpen = false;
        notifier->deleteLater();
        QTimer::singleShot(0, this, &Controller::maybeQuit);
    });
    notifier->show(tr("Snip copied to clipboard"), tr("Select this notification to mark up and save."), thumb);
}

void Controller::maybeQuit()
{
    m_editors.removeAll(nullptr);
    if (m_busy || !m_editors.isEmpty() || m_notificationOpen)
        return;
    if (QGuiApplication::clipboard()->ownsClipboard())
        return;
    QCoreApplication::quit();
}

#include "controller.moc"
