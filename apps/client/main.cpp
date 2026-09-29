#include "main_window.h"
#include "zjdesk/build_info.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QHostInfo>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("ZJdesk");
    QCoreApplication::setOrganizationName("ZJdesk");
    QCoreApplication::setApplicationVersion(zjdesk::build::Version);
    QApplication::setStyle("Fusion");
    QFont font;
    font.setFamilies({"Microsoft YaHei UI", "Noto Sans CJK SC", "sans-serif"});
    font.setPointSize(10);
    QApplication::setFont(font);
    QFile style(":/resources/styles.qss");
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));
    QCommandLineParser parser;
    parser.setApplicationDescription("ZJdesk cross-platform collaboration workbench");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"server", "Signaling URL", "url", "ws://127.0.0.1:8765"});
    parser.addOption({"room", "Room ID", "room", "demo"});
    parser.addOption({{"display-name", "name"}, "Display name", "name", QHostInfo::localHostName()});
    parser.process(app);
    MainWindow window(parser.value("server"), parser.value("room"), parser.value("display-name"));
    window.show();
    return app.exec();
}
