#include "controller.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>

namespace {

// Qt doesn't always detect the icon theme outside KDE/GNOME (e.g. Cinnamon,
// MATE, Xfce without a Qt platform theme). Ask the desktop's settings.
void detectIconTheme()
{
    if (!QIcon::themeName().isEmpty() && QIcon::themeName() != QLatin1String("hicolor"))
        return;
    const QList<QStringList> queries{
        {QStringLiteral("gsettings"), QStringLiteral("get"), QStringLiteral("org.cinnamon.desktop.interface"), QStringLiteral("icon-theme")},
        {QStringLiteral("gsettings"), QStringLiteral("get"), QStringLiteral("org.mate.interface"), QStringLiteral("icon-theme")},
        {QStringLiteral("gsettings"), QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("icon-theme")},
        {QStringLiteral("xfconf-query"), QStringLiteral("-c"), QStringLiteral("xsettings"), QStringLiteral("-p"), QStringLiteral("/Net/IconThemeName")},
    };
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    for (const QStringList &q : queries) {
        const bool relevant = (q.contains(QStringLiteral("org.cinnamon.desktop.interface")) && desktop.contains(QLatin1String("cinnamon")))
                           || (q.contains(QStringLiteral("org.mate.interface")) && desktop.contains(QLatin1String("mate")))
                           || (q.first() == QLatin1String("xfconf-query") && desktop.contains(QLatin1String("xfce")))
                           || q.contains(QStringLiteral("org.gnome.desktop.interface"));
        if (!relevant || QStandardPaths::findExecutable(q.first()).isEmpty())
            continue;
        QProcess proc;
        proc.start(q.first(), q.mid(1));
        if (!proc.waitForFinished(1000) || proc.exitCode() != 0)
            continue;
        QString name = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        name.remove(QLatin1Char('\''));
        if (!name.isEmpty()) {
            QIcon::setThemeName(name);
            return;
        }
    }
}

// `snip --setup-hotkey install|remove|status`: run the bundled snip-hotkey
// script, pointing the hotkey at this binary (or at the AppImage file).
int setupHotkey(const QString &action)
{
    const QString script = QCoreApplication::applicationDirPath() + QStringLiteral("/snip-hotkey");
    if (!QFileInfo(script).isExecutable()) {
        qWarning("snip-hotkey script not found next to %s", qPrintable(QCoreApplication::applicationFilePath()));
        return 1;
    }
    QString self = qEnvironmentVariable("APPIMAGE");
    if (self.isEmpty())
        self = QCoreApplication::applicationFilePath();
    return QProcess::execute(script, {action, QStringLiteral("--command"), self + QStringLiteral(" --no-editor")});
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("snip-and-sketch"));
    QApplication::setApplicationDisplayName(QStringLiteral("Snip & Sketch for Linux"));
    QApplication::setApplicationVersion(QStringLiteral(SNIP_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("snip-and-sketch"));
    QApplication::setQuitOnLastWindowClosed(false);  // overlay → editor hand-off has no window

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Snip & Sketch for Linux — capture, annotate and share screenshots.\n\n"
        "  snip                take a snip, then open it in the editor\n"
        "  snip --no-editor    copy the snip and show a notification (Win+Shift+S)\n"
        "  snip --app          open the editor without snipping\n"
        "  snip picture.png    open an image in the editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption modeOpt(QStringLiteral("mode"), QStringLiteral("Snip mode: rect, free, window or full (default: last used)."), QStringLiteral("mode"));
    QCommandLineOption delayOpt(QStringLiteral("delay"), QStringLiteral("Wait this many seconds before snipping."), QStringLiteral("seconds"), QStringLiteral("0"));
    QCommandLineOption noEditorOpt(QStringLiteral("no-editor"), QStringLiteral("Only copy to the clipboard and show a notification."));
    QCommandLineOption appOpt(QStringLiteral("app"), QStringLiteral("Open the editor without snipping."));
    QCommandLineOption hotkeyOpt(QStringLiteral("setup-hotkey"),
                                 QStringLiteral("Register (install), remove or show (status) the Shift+Super+S hotkey."),
                                 QStringLiteral("install|remove|status"));
    parser.addOptions({modeOpt, delayOpt, noEditorOpt, appOpt, hotkeyOpt});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image to open in the editor."), QStringLiteral("[file]"));
    parser.process(app);
    if (parser.isSet(hotkeyOpt))
        return setupHotkey(parser.value(hotkeyOpt));

    LaunchRequest request;
    request.mode = parser.value(modeOpt);
    if (!request.mode.isEmpty() && !QStringList{QStringLiteral("rect"), QStringLiteral("free"), QStringLiteral("window"), QStringLiteral("full")}.contains(request.mode)) {
        qWarning("Unknown mode '%s' (use rect, free, window or full)", qPrintable(request.mode));
        return 2;
    }
    request.delay = qMax(0, parser.value(delayOpt).toInt());
    request.noEditor = parser.isSet(noEditorOpt);
    request.app = parser.isSet(appOpt);
    if (!parser.positionalArguments().isEmpty())
        request.file = QDir::current().absoluteFilePath(parser.positionalArguments().first());

    Controller controller;
    if (!controller.becomePrimary(request))
        return 0;  // forwarded to the running instance

    detectIconTheme();
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("snip-and-sketch"),
                                                 QIcon(QStringLiteral(":/snip-and-sketch.svg"))));
    controller.handle(request);
    return app.exec();
}
