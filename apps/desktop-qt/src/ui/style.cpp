#include "ui/style.hpp"

namespace style {

QString applicationStyleSheet() {
    return QStringLiteral(R"qss(
QMainWindow, QWidget#page { background: #F8F9FA; color: #30343B; }
QListWidget#sidebar { background: #FFFFFF; border: none; border-right: 1px solid #D5D8DE;
                      font-size: 14px; padding-top: 8px; outline: 0; }
QListWidget#sidebar::item { padding: 10px 16px; color: #30343B; }
QListWidget#sidebar::item:selected { background: #FFF1F3; color: #A5001E; border-left: 4px solid #A5001E; }
QLabel#brand { color: #A5001E; font-size: 22px; font-weight: 800; padding: 14px 16px 0 16px; background: #FFFFFF; }
QLabel#brandSub { color: #68707C; padding: 0 16px 10px 16px; background: #FFFFFF; }
QLabel#pageTitle { font-size: 20px; font-weight: 700; color: #30343B; }
QLabel#hint { color: #68707C; }
QFrame#card { background: #FFFFFF; border: 1px solid #D5D8DE; border-radius: 6px; }
QLabel#cardTitle { color: #68707C; font-weight: 600; }
QLabel#cardValue { font-size: 18px; font-weight: 700; }
QPushButton { background: #FFFFFF; border: 1px solid #D5D8DE; border-radius: 4px; padding: 6px 14px; }
QPushButton:hover { border-color: #A5001E; }
QPushButton:disabled { color: #9AA0A8; }
QPushButton#primary { background: #A5001E; color: #FFFFFF; border-color: #850018; }
QPushButton#primary:disabled { background: #D5D8DE; color: #FFFFFF; border-color: #D5D8DE; }
QTableView { background: #FFFFFF; border: 1px solid #D5D8DE; gridline-color: #E8EAED;
             selection-background-color: #FFF1F3; selection-color: #30343B; }
QHeaderView::section { background: #F4F5F7; border: none; border-bottom: 1px solid #D5D8DE; padding: 6px; font-weight: 600; }
QTextBrowser { background: #FFFFFF; border: 1px solid #D5D8DE; }
QStatusBar { background: #FFFFFF; border-top: 1px solid #D5D8DE; color: #59616D; }
)qss");
}

QString simulationBanner() {
    return QStringLiteral("font-weight:800; font-size:14px; padding:8px; background:#FFF1F3; color:#A5001E;"
                          "border-bottom:3px solid #A5001E;");
}

QString vehicleBanner() {
    return QStringLiteral("font-weight:800; font-size:14px; padding:8px; background:#A5001E; color:#FFFFFF;");
}

QString disconnectedBanner() {
    return QStringLiteral("font-weight:600; padding:8px; background:#E8EAED; color:#30343B;");
}

}  // namespace style
