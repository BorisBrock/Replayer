#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QSettings>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    // Must be set before the QApplication is created to apply everywhere.
    QSurfaceFormat format;
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1); // vsync
    QSurfaceFormat::setDefaultFormat(format);

#ifdef Q_OS_LINUX
    // File dialogs come from the desktop via the XDG portal (the GNOME/KDE
    // native chooser), so they follow the system light/dark setting while the
    // app itself is always dark. Without a portal Qt falls back to its own
    // dialog. An explicit QT_QPA_PLATFORMTHEME still wins.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName("Replayer");
    QApplication::setApplicationVersion(REPLAYER_VERSION);
    QApplication::setOrganizationName("Replayer");
    QApplication::setDesktopFileName("replayer");
    QIcon icon;
    for (const QString& file : QDir(":/icons").entryList())
        icon.addFile(":/icons/" + file);
    QApplication::setWindowIcon(icon);

    // Names above define where QSettings stores its file
    Theme::apply(app, QColor(QSettings().value("appearance/accent", Theme::kDefaultAccent).toString()));

    QCommandLineParser parser;
    parser.setApplicationDescription("Video player for high-frame-rate action cam footage");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("path", "Folder of clips, or a video file to open.", "[path]");
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList paths = parser.positionalArguments();
    if (!paths.isEmpty()) window.open(paths.first());
    else window.restoreSession();

    return app.exec();
}
