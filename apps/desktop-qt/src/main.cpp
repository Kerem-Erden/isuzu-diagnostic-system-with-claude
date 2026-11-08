#include <QApplication>
#include <QCommandLineParser>
#include <QStandardPaths>

#include "ui/app_window.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Isuzu Diagnostic System"));
    QApplication::setOrganizationName(QStringLiteral("IsuzuDiagnostic"));
    QApplication::setApplicationVersion(QStringLiteral(ISUZU_APP_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshots(
        QStringLiteral("screenshots"),
        QStringLiteral("Developer aid: connect to the SIMULATION, save a PNG of every page into <dir> and quit."),
        QStringLiteral("dir"));
    parser.addOption(screenshots);
    parser.process(app);

    if (parser.isSet(screenshots)) {
        // Keep the tour's audit rows and settings out of the user's real profile.
        QStandardPaths::setTestModeEnabled(true);
    }
    AppWindow window;
    window.show();
    if (parser.isSet(screenshots)) window.runScreenshotTour(parser.value(screenshots));
    return QApplication::exec();
}
