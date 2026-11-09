#include "session/network_setup.h"
#include "session/session_coordinator.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QFile>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>

// Explicit manual test: captures the real desktop and clicks only this probe's window.
class InputTarget final : public QWidget
{
public:
    int moves = 0, presses = 0, releases = 0;
protected:
    void mouseMoveEvent(QMouseEvent *) override { ++moves; }
    void mousePressEvent(QMouseEvent *) override { ++presses; update(); }
    void mouseReleaseEvent(QMouseEvent *) override { ++releases; update(); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), presses ? QColor("#16804a") : QColor("#245b96"));
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter,
            QString("ZJdesk remote input test\npresses=%1 releases=%2").arg(presses).arg(releases));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"host", "Share desktop and accept input on the test window."},
        {"listen", "Start signaling service locally."},
        {"server", "Signaling URL.", "url"}, {"peer", "Other computer IPv4.", "ip"},
        {"output", "JSON lines report.", "path"},
        {"seconds", "Test duration.", "seconds", "45"}});
    parser.process(app);
    QFile output(parser.value("output"));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    const auto record = [&](QJsonObject event) {
        event.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        output.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
        output.flush();
    };
    const bool host = parser.isSet("host");
    const QUrl server(parser.value("server"));
    const auto savedCursor = QCursor::pos();
    InputTarget target;
    if (host)
    {
        target.setWindowTitle("ZJdesk remote input test");
        target.setWindowFlag(Qt::WindowStaysOnTopHint);
        target.setAttribute(Qt::WA_ShowWithoutActivating);
        target.setMouseTracking(true);
        target.resize(400, 240);
        target.move(QGuiApplication::primaryScreen()->geometry().center() - QPoint(200, 120));
        target.show();
    }
    bool dialed = false, sent = false;
    int frames = 0, accepted = 0;
    double maximumRttMs = 0;
    zjdesk::NetworkSetup network;
    zjdesk::SessionCoordinator session;
    QObject::connect(session.events(), &zjdesk::EventLog::appended, &app,
        [&](const QString &message) { record({{"event", "log"}, {"message", message}}); });
    QObject::connect(&network, &zjdesk::NetworkSetup::finished, &app, [&](bool ok, const QString &detail) {
        record({{"event", "network"}, {"ok", ok}, {"detail", detail}});
        if (!ok || !session.startRemoteAccess(server, host ? "ProbeHost" : "ProbeController", parser.value("peer")))
            app.exit(3);
    });
    QObject::connect(&session, &zjdesk::SessionCoordinator::stateChanged, &app,
        [&](zjdesk::SessionCoordinator::State state) {
            record({{"event", "state"}, {"state", int(state)}, {"detail", session.detail()}});
            if (!host && !dialed && state == zjdesk::SessionCoordinator::State::WaitingForPeer)
                dialed = session.connectRemoteIp();
        });
    QObject::connect(&session, &zjdesk::SessionCoordinator::remoteChanged, &app, [&] {
        record({{"event", "remote"}, {"state", int(session.remoteState())}, {"detail", session.remoteDetail()}});
        if (host && session.remoteState() == zjdesk::SessionCoordinator::RemoteState::Pending)
            session.grantControl();
    });
    QObject::connect(&session, &zjdesk::SessionCoordinator::shareFrame, &app,
        [&](const QImage &frame) { if (!frame.isNull()) ++frames; });
    QObject::connect(&session, &zjdesk::SessionCoordinator::inputProbeAcknowledged, &app,
        [&](quint64 sequence, qint64 rtt, qint64 inject, bool ok) {
            if (ok) ++accepted;
            maximumRttMs = qMax(maximumRttMs, rtt / 1000.0);
            record({{"event", "ack"}, {"sequence", qint64(sequence)}, {"accepted", ok},
                {"rttMs", rtt / 1000.0}, {"injectUs", inject}});
        });
    QObject::connect(&session, &zjdesk::SessionCoordinator::shareStatisticsChanged, &app,
        [&](const zjdesk::ShareStats &stats) {
            record({{"event", "stats"}, {"frames", frames}, {"width", stats.size.width()},
                {"height", stats.size.height()}, {"fps", stats.deliveredFps}, {"targetFps", stats.targetFps},
                {"mbps", stats.networkMbps}, {"codec", stats.codec}, {"transport", stats.transport},
                {"local", stats.localAddress}, {"remote", stats.remoteAddress},
                {"maximumBitrate", stats.congestionMaximumBitrate}, {"encoderBitrate", stats.encoderBitrate}});
            if (host || sent || frames < 10
                || session.remoteState() != zjdesk::SessionCoordinator::RemoteState::Controlling) return;
            sent = true;
            session.sendRemoteInput({{"kind", "move"}, {"x", 0.5}, {"y", 0.5}, {"probe", true}});
            QTimer::singleShot(400, &app, [&] {
                session.sendRemoteInput({{"kind", "button"}, {"button", 1}, {"pressed", true}, {"x", 0.5}, {"y", 0.5}});
            });
            QTimer::singleShot(600, &app, [&] {
                session.sendRemoteInput({{"kind", "button"}, {"button", 1}, {"pressed", false}, {"x", 0.5}, {"y", 0.5}});
            });
        });
    QTimer::singleShot(qBound(10, parser.value("seconds").toInt(), 300) * 1000, &app, [&] {
        const bool ok = host ? target.moves > 0 && target.presses == 1 && target.releases == 1
                             : frames >= 30 && accepted == 3;
        record({{"event", "result"}, {"ok", ok}, {"frames", frames}, {"accepted", accepted},
            {"moves", target.moves}, {"presses", target.presses}, {"releases", target.releases}, {"maximumRttMs", maximumRttMs}});
        session.leave();
        app.exit(ok ? 0 : 4);
    });
    QTimer::singleShot(0, &app, [&] {
        if (parser.isSet("listen") && !network.startLocalServer(quint16(server.port(8765))))
            return app.exit(5);
        network.verifyPeer(parser.value("peer"), server);
    });
    const int result = app.exec();
    if (host) QCursor::setPos(savedCursor);
    return result;
}
