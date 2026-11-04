#include "session/session_coordinator.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

// A background peer for cross-machine camera validation. Audio stays synthetic.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{"s", "server"}, "Signaling URL", "url", "ws://127.0.0.1:8765"},
        {{"r", "room"}, "Meeting room", "room", "camera-check"},
        {{"n", "name"}, "Peer name", "name", "Probe"},
        {"seconds", "Run duration", "seconds", "60"},
        {"members", "Expected final member count", "count", "2"},
        {"camera", "Camera name; empty selects a physical camera, test is synthetic", "name"},
        {"snapshots", "Directory for latest camera frames", "directory"},
        {"exercise", "Hide/restore camera, then leave/rejoin during the run"}
    });
    parser.process(app);
    bool valid = false;
    const int seconds = parser.value("seconds").toInt(&valid);
    if (!valid || seconds < 30) parser.showHelp(2);
    const int members = parser.value("members").toInt(&valid);
    if (!valid || members < 2 || members > 4) parser.showHelp(2);
    const auto snapshots = parser.value("snapshots");
    if (!snapshots.isEmpty() && !QDir().mkpath(snapshots)) return 2;
    const QUrl url(parser.value("server"));
    const auto room = parser.value("room"), name = parser.value("name");
    const auto log = [](const QJsonObject &object) {
        QTextStream out(stdout);
        out << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
    };
    struct Video { quint64 frames = 0, previous = 0; qint64 last = 0, gapMax = 0; QSize size; };
    QMap<QString, Video> video;
    QElapsedTimer elapsed;
    elapsed.start();
    zjdesk::SessionCoordinator session;
    session.setConferenceAutoConnect(true);
    session.setConferenceDevices({parser.value("camera"), "test", "test"});
    session.setConferenceJoinDefaults(false, true, false);
    QObject::connect(&session, &zjdesk::SessionCoordinator::conferenceChanged, &app, [&] {
        log({{"event", "conference"}, {"ms", elapsed.elapsed()}, {"detail", session.conferenceDetail()}});
    });
    QObject::connect(&session, &zjdesk::SessionCoordinator::stateChanged, &app, [&] {
        log({{"event", "membership"}, {"detail", session.detail()}, {"members", session.memberCount()}});
    });
    QObject::connect(&session, &zjdesk::SessionCoordinator::conferenceLocalFrame, &app, [&](const QImage &frame) {
        if (!snapshots.isEmpty() && !frame.isNull() && elapsed.elapsed() / 5000 != video["local"].last / 5000)
        {
            frame.save(snapshots + "/local.png");
            video["local"].last = elapsed.elapsed();
        }
    });
    QObject::connect(&session, &zjdesk::SessionCoordinator::conferencePeerFrame, &app,
        [&](const QString &id, const QImage &frame) {
            if (frame.isNull()) return;
            auto &stats = video[id];
            const auto now = elapsed.elapsed();
            if (stats.last) stats.gapMax = qMax(stats.gapMax, now - stats.last);
            if (!snapshots.isEmpty() && now / 5000 != stats.last / 5000) frame.save(snapshots + "/" + id + ".png");
            stats.last = now; stats.size = frame.size(); ++stats.frames;
        });
    QTimer progress;
    progress.setInterval(5000);
    QObject::connect(&progress, &QTimer::timeout, &app, [&] {
        QJsonArray peers;
        for (const auto &id : session.conferencePeers().keys())
        {
            auto &stats = video[id];
            peers.append(QJsonObject{{"peer", session.conferencePeers().value(id)}, {"id", id},
                {"frames", qint64(stats.frames)}, {"recentFrames", qint64(stats.frames - stats.previous)},
                {"width", stats.size.width()}, {"height", stats.size.height()}, {"maxGapMs", stats.gapMax},
                {"cameraHidden", session.conferencePeerCameraHidden(id)}});
            stats.previous = stats.frames;
        }
        log({{"event", "progress"}, {"ms", elapsed.elapsed()}, {"pid", QCoreApplication::applicationPid()}, {"peers", peers}});
    });
    progress.start();
    if (parser.isSet("exercise"))
    {
        QTimer::singleShot(seconds * 250, &app, [&] { session.setConferenceMuted(true, true, true); });
        QTimer::singleShot(seconds * 350, &app, [&] { session.setConferenceMuted(true, false, true); });
        QTimer::singleShot(seconds * 500, &app, [&] { session.leave(); });
        QTimer::singleShot(seconds * 600, &app, [&] { session.join(url, room, name, zjdesk::SessionCoordinator::RoomType::Meeting); });
    }
    QTimer::singleShot(seconds * 1000, &app, [&] {
        bool passed = session.memberCount() == members;
        for (const auto &id : session.conferencePeers().keys())
            passed = passed && session.conferencePeerStatus().value(id) == "running"
                && video.value(id).frames >= 10 && elapsed.elapsed() - video.value(id).last < 3000;
        log({{"event", "result"}, {"passed", passed}, {"members", session.memberCount()}});
        progress.stop();
        // Let peers finish their measurements before releasing this membership.
        QTimer::singleShot(5000, &app, [&, passed] {
            session.leave();
            QTimer::singleShot(1000, &app, [&, passed] {
                const bool released = !session.conferenceDevicesBusy();
                log({{"event", "cleanup"}, {"released", released}});
                app.exit(passed && released ? 0 : 1);
            });
        });
    });
    if (!session.join(url, room, name, zjdesk::SessionCoordinator::RoomType::Meeting)) return 2;
    return app.exec();
}
