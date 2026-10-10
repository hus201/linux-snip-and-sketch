#include "windowlist.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace {

QString run(const QString &program, const QStringList &args)
{
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(2000))
        return {};
    return QString::fromLocal8Bit(proc.readAllStandardOutput());
}

QList<qint64> cardinals(const QString &text, const QString &prop)
{
    const QRegularExpression re(QStringLiteral("^%1\\(CARDINAL\\) = ([\\d, ]+)$").arg(prop),
                                QRegularExpression::MultilineOption);
    QList<qint64> values;
    const auto m = re.match(text);
    if (m.hasMatch())
        for (const QString &v : m.captured(1).split(QLatin1Char(',')))
            values.append(v.trimmed().toLongLong());
    return values;
}

bool windowRect(const QString &id, qint64 currentDesktop, QRect *out)
{
    const QString props = run(QStringLiteral("xprop"),
                              {QStringLiteral("-id"), id, QStringLiteral("_NET_WM_STATE"),
                               QStringLiteral("_NET_WM_DESKTOP"), QStringLiteral("_NET_WM_WINDOW_TYPE"),
                               QStringLiteral("_NET_FRAME_EXTENTS"), QStringLiteral("_GTK_FRAME_EXTENTS")});
    if (props.contains(QLatin1String("_NET_WM_STATE_HIDDEN"))
        || props.contains(QLatin1String("_NET_WM_WINDOW_TYPE_DESKTOP")))
        return false;
    const auto desktop = cardinals(props, QStringLiteral("_NET_WM_DESKTOP"));
    if (!desktop.isEmpty() && desktop[0] != currentDesktop && desktop[0] != 0xFFFFFFFF)
        return false;

    const QString info = run(QStringLiteral("xwininfo"), {QStringLiteral("-id"), id});
    if (!info.contains(QLatin1String("IsViewable")))
        return false;
    int vals[4];
    const char *keys[4] = {"Absolute upper-left X", "Absolute upper-left Y", "Width", "Height"};
    for (int i = 0; i < 4; ++i) {
        const auto m = QRegularExpression(QStringLiteral("%1:\\s+(-?\\d+)").arg(QLatin1String(keys[i])))
                           .match(info);
        if (!m.hasMatch())
            return false;
        vals[i] = m.captured(1).toInt();
    }
    QRect rect(vals[0], vals[1], vals[2], vals[3]);

    // Server-side decorations (title bar) sit outside the client window...
    const auto frame = cardinals(props, QStringLiteral("_NET_FRAME_EXTENTS"));
    if (frame.size() == 4)
        rect.adjust(-frame[0], -frame[2], frame[1], frame[3]);
    // ...while client-side decorated (GTK) windows include an invisible shadow.
    const auto shadow = cardinals(props, QStringLiteral("_GTK_FRAME_EXTENTS"));
    if (shadow.size() == 4)
        rect.adjust(shadow[0], shadow[2], -shadow[1], -shadow[3]);

    *out = rect;
    return rect.isValid();
}

}  // namespace

QList<QRect> WindowList::visibleWindows(qreal dpr)
{
    if (QStandardPaths::findExecutable(QStringLiteral("xprop")).isEmpty()
        || QStandardPaths::findExecutable(QStringLiteral("xwininfo")).isEmpty())
        return {};

    const QString root = run(QStringLiteral("xprop"),
                             {QStringLiteral("-root"), QStringLiteral("_NET_CLIENT_LIST_STACKING"),
                              QStringLiteral("_NET_CURRENT_DESKTOP")});
    const QString stacking = root.section(QStringLiteral("_NET_CURRENT_DESKTOP"), 0, 0);
    const auto current = cardinals(root, QStringLiteral("_NET_CURRENT_DESKTOP"));

    QStringList ids;
    auto it = QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+")).globalMatch(stacking);
    while (it.hasNext())
        ids.append(it.next().captured());

    QList<QRect> result;
    for (auto id = ids.crbegin(); id != ids.crend(); ++id) {  // stacking is bottom-to-top
        QRect r;
        if (!windowRect(*id, current.value(0, 0), &r))
            continue;
        if (!qFuzzyCompare(dpr, 1.0))
            r = QRect(qRound(r.x() / dpr), qRound(r.y() / dpr),
                      qRound(r.width() / dpr), qRound(r.height() / dpr));
        result.append(r);
    }
    return result;
}
