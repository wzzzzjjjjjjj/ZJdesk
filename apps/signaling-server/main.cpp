#include "signaling/room_server.h"
#include "zjdesk/build_info.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("zjdesk-server");
    QCoreApplication::setApplicationVersion(zjdesk::build::Version);
    QCommandLineParser parser;
    parser.setApplicationDescription("ZJdesk two-person LAN signaling service");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"listen", "Listen address (use 0.0.0.0 for LAN)", "address", "127.0.0.1"});
    parser.addOption({"port", "Listen port", "port", "8765"});
    parser.process(app);
    QHostAddress address;
    bool portValid = false;
    const auto port = parser.value("port").toUShort(&portValid);
    if (!address.setAddress(parser.value("listen")) || !portValid || port == 0)
    {
        std::fprintf(stderr, "Invalid listen address or port\n");
        return 1;
    }
    zjdesk::RoomServer server;
    if (!server.listen(address, port))
    {
        std::fprintf(stderr, "Listen failed: %s\n", server.errorString().toUtf8().constData());
        return 2;
    }
    QObject::connect(&server, &zjdesk::RoomServer::event, &app, [](const QString &message) {
        std::fprintf(stdout, "%s %s\n", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8().constData(),
                     message.toUtf8().constData());
        std::fflush(stdout);
    });
    std::printf("ZJdesk signaling: ws://%s:%u\n", address.toString().toUtf8().constData(), port);
    std::fflush(stdout);
    return app.exec();
}
