#include <QApplication>

#include "ui/main_window.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Isuzu Diagnostic System"));
    QApplication::setOrganizationName(QStringLiteral("IsuzuDiagnostic"));
    MainWindow window;
    window.show();
    return QApplication::exec();
}
