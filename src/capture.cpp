#include "capture.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>
#include <QUrl>
#include <QVariantMap>

namespace {

QRect virtualGeometry()
{
    QRect geo;
    for (QScreen *s : QGuiApplication::screens())
        geo = geo.united(s->geometry());
    return geo;
}

void setLogicalSize(QPixmap &pixmap, const QRect &geometry)
{
    if (geometry.width() > 0)
        pixmap.setDevicePixelRatio(qreal(pixmap.width()) / geometry.width());
}

// Split one big desktop image (as the portal returns it) into per-monitor shots.
ScreenShots splitDesktop(const QImage &desktop)
{
    const QRect virt = virtualGeometry();
    const qreal sx = qreal(desktop.width()) / virt.width();
    const qreal sy = qreal(desktop.height()) / virt.height();
    ScreenShots shots;
    for (QScreen *s : QGuiApplication::screens()) {
        const QRect g = s->geometry();
        const QRect src(qRound((g.x() - virt.x()) * sx), qRound((g.y() - virt.y()) * sy),
                        qRound(g.width() * sx), qRound(g.height() * sy));
        QPixmap pm = QPixmap::fromImage(desktop.copy(src));
        setLogicalSize(pm, g);
        shots.append({s, g, pm});
    }
    return shots;
}

// org.freedesktop.portal.Screenshot: request → wait for the Response signal.
class PortalRequest : public QObject
{
    Q_OBJECT
public:
    explicit PortalRequest(std::function<void(const ScreenShots &, const QString &)> done)
        : m_done(std::move(done)) {}

    void start()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            finish({}, QStringLiteral("No D-Bus session bus"));
            return;
        }
        const QString token = QStringLiteral("snip%1").arg(QRandomGenerator::global()->generate());
        QString sender = bus.baseService().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                                 .arg(sender, token);
        // Subscribe *before* calling to avoid missing a fast response.
        bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), path,
                    QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                    this, SLOT(onResponse(uint, QVariantMap)));

        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Screenshot"), QStringLiteral("Screenshot"));
        QVariantMap options{{QStringLiteral("handle_token"), token},
                            {QStringLiteral("interactive"), false}};
        call << QString() << options;
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QDBusObjectPath> reply = *w;
            if (reply.isError())
                finish({}, reply.error().message());
            w->deleteLater();
        });
    }

private slots:
    void onResponse(uint response, const QVariantMap &results)
    {
        if (response != 0) {
            finish({}, QStringLiteral("Screenshot was cancelled or denied"));
            return;
        }
        const QString file = QUrl(results.value(QStringLiteral("uri")).toString()).toLocalFile();
        QImage image(file);
        QFile::remove(file);  // a temporary file the portal wrote for us
        if (image.isNull())
            finish({}, QStringLiteral("Couldn't read the portal screenshot"));
        else
            finish(splitDesktop(image), {});
    }

private:
    void finish(const ScreenShots &shots, const QString &error)
    {
        if (!m_done)
            return;
        auto done = std::move(m_done);
        m_done = nullptr;
        done(shots, error);
        deleteLater();
    }

    std::function<void(const ScreenShots &, const QString &)> m_done;
};

}  // namespace

bool Capture::usePortal()
{
    return QGuiApplication::platformName() == QLatin1String("wayland")
        || qgetenv("XDG_SESSION_TYPE") == "wayland";
}

void Capture::grab(std::function<void(const ScreenShots &, const QString &)> done)
{
    if (usePortal()) {
        (new PortalRequest(std::move(done)))->start();
        return;
    }
    ScreenShots shots;
    for (QScreen *s : QGuiApplication::screens()) {
        QPixmap pm = s->grabWindow(0);
        setLogicalSize(pm, s->geometry());
        shots.append({s, s->geometry(), pm});
    }
    done(shots, shots.isEmpty() ? QStringLiteral("No screens found") : QString());
}

QImage Capture::stitch(const ScreenShots &shots)
{
    QRect virt;
    qreal dpr = 1.0;
    for (const ScreenShot &s : shots) {
        virt = virt.united(s.geometry);
        dpr = qMax(dpr, s.pixmap.devicePixelRatio());
    }
    QImage out(qRound(virt.width() * dpr), qRound(virt.height() * dpr), QImage::Format_RGB32);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::black);
    QPainter p(&out);
    for (const ScreenShot &s : shots)
        p.drawPixmap(QRect(s.geometry.topLeft() - virt.topLeft(), s.geometry.size()), s.pixmap);
    return out;
}

#include "capture.moc"
