#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("MAVLink Drone Simulator");
    app.setOrganizationName("MAVLink_sim");
    app.setApplicationVersion("1.0.0");

    // ダークテーマのパレット
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 35));
    darkPalette.setColor(QPalette::WindowText, QColor(200, 200, 200));
    darkPalette.setColor(QPalette::Base, QColor(40, 40, 48));
    darkPalette.setColor(QPalette::AlternateBase, QColor(50, 50, 58));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(50, 50, 60));
    darkPalette.setColor(QPalette::ToolTipText, QColor(200, 200, 200));
    darkPalette.setColor(QPalette::Text, QColor(200, 200, 200));
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 55));
    darkPalette.setColor(QPalette::ButtonText, QColor(200, 200, 200));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(52, 152, 219));
    darkPalette.setColor(QPalette::Highlight, QColor(52, 152, 219));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(darkPalette);

    MainWindow window;
    window.show();

    return app.exec();
}
