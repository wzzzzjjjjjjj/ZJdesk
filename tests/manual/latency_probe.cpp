#include "media/screen_share.h"
#include "media/media_environment.h"
#include "media/test_frame_marker.h"
#include "signaling/room_server.h"
#include "signaling/signaling_client.h"
#include "video_surface.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QMap>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRandomGenerator>
#include <QThread>

// Explicit diagnostic tool. The marker travels over the normal DataChannel and VP9 path.
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"host", "Send a marked 1920x1080 synthetic source at 30 FPS."},
        {"listen", "Start an isolated signaling service."},
        {"server", "Signaling URL.", "url", "ws://127.0.0.1:18765"},
        {"room", "Isolated test room.", "id", "wan-latency"},
        {"output", "Raw JSON lines.", "path"},
        {"seconds", "Duration, including 20 seconds of warmup.", "seconds", "260"},
        {"stall-at", "Receiver UI stall after this many seconds, zero disables.", "seconds", "0"},
        {"stall-ms", "Injected UI stall duration.", "ms", "600"}});
    parser.process(app);
    bool valid = false;
    const int seconds = parser.value("seconds").toInt(&valid);
    if (!valid || seconds < 10 || seconds > 7200) return 2;
    const int stallAt = parser.value("stall-at").toInt(&valid);
    if (!valid || stallAt < 0 || stallAt >= seconds) return 2;
    const int stallMs = parser.value("stall-ms").toInt(&valid);
    if (!valid || stallMs < 1 || stallMs > 10000) return 2;
    const bool host = parser.isSet("host");
    const QUrl url(parser.value("server"));
    QFile output(parser.value("output"));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    const auto media = zjdesk::MediaEnvironment::inspect();
    VideoSurface surface;
    if (media.ready && !host)
    {
        surface.setAttribute(Qt::WA_ShowWithoutActivating);
        surface.resize(960, 540);
        surface.show();
    }
    const auto startedNs = zjdesk::FrameTiming::nowNs();
    const auto record = [&](QJsonObject event) {
        const auto now = zjdesk::FrameTiming::nowNs();
        event.insert("monotonicNs", QString::number(now));
        event.insert("elapsedMs", (now - startedNs) / 1e6);
        event.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        output.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
        output.flush();
    };
    QJsonObject environment;
    for (const auto *name : {"ZJDESK_SHARE_WAN", "ZJDESK_SHARE_LOW_BANDWIDTH", "ZJDESK_WEBRTC_GCC",
            "ZJDESK_WEBRTC_MAX_BITRATE", "ZJDESK_WEBRTC_LATENCY_MS", "ZJDESK_WEBRTC_LOCAL_ADDRESS",
            "ZJDESK_WEBRTC_PEER_ADDRESS"})
        environment.insert(name, qEnvironmentVariable(name));
    record({{"event", "start"}, {"host", host}, {"seconds", seconds}, {"environment", environment},
        {"metric", "controller-command-to-VP9-marker-frameSwapped"}, {"warmupSeconds", 20},
        {"source", "test-latency"}, {"gstreamer", media.version}, {"qt", qVersion()}});

    if (!media.ready)
    {
        record({{"event", "error"}, {"detail", media.description}});
        return 4;
    }
    zjdesk::RoomServer server;
    if (parser.isSet("listen") && !server.listen(QHostAddress::AnyIPv4, quint16(url.port(18765)))) return 3;
    zjdesk::SignalingClient signaling;
    zjdesk::ScreenShare share;
    bool running = false, channelReady = false, finishing = false;
    quint64 frames = 0, samples = 0, timeouts = 0;
    const quint32 markerEpoch = QRandomGenerator::global()->bounded(quint32(1), quint32(0xffffffff));
    quint32 sequence = 0, lastVisibleSequence = 0;
    QMap<quint32, qint64> pending;
    QMap<quint64, zjdesk::TestFrameMarker::Value> frameMarkers;
    qint64 previousSwapNs = 0;
    const auto finish = [&](const QString &reason) {
        if (finishing) return;
        finishing = true;
        const bool ok = frames >= 30 && (host || (samples > 0 && pending.isEmpty()));
        record({{"event", "result"}, {"ok", ok}, {"reason", reason}, {"frames", qint64(frames)},
            {"samples", qint64(samples)}, {"timeouts", qint64(timeouts)}, {"pending", pending.size()}});
        signaling.close();
        share.stop(2);
        app.exit(ok ? 0 : 4);
    };
    const auto start = [&] {
        if (running) return;
        running = true;
        share.start(host, "test-latency", 1, 30, 30);
    };
    QObject::connect(&signaling, &zjdesk::SignalingClient::connected, &app, [&] {
        signaling.send({"join", {{"roomId", parser.value("room")}, {"name", host ? "LatencyHost" : "LatencyReceiver"}}});
    });
    QObject::connect(&signaling, &zjdesk::SignalingClient::messageReceived, &app,
        [&](const zjdesk::protocol::Message &message) {
            if (message.type == "error")
            {
                record({{"event", "error"}, {"payload", message.payload}});
                return app.exit(3);
            }
            if (message.type == "joined")
            {
                // Receiver starts first so early profile/ICE messages cannot be discarded.
                if (!host) start();
                if (host && !message.payload.value("peers").toArray().isEmpty()) start();
            }
            else if (host && message.type == "peer_joined") start();
            else if (running && zjdesk::protocol::isRelayType(message.type)) share.negotiate(message.type, message.payload, 1);
            else if (message.type == "peer_left" && !finishing)
            {
                record({{"event", "peer-left"}});
                if (host) finish("receiver-left");
            }
        });
    QObject::connect(&signaling, &zjdesk::SignalingClient::failed, &app, [&](const QString &detail) {
        record({{"event", "error"}, {"detail", detail}});
        app.exit(3);
    });
    QObject::connect(&share, &zjdesk::ScreenShare::negotiationReady, &app,
        [&](quint64, const QString &type, QJsonObject payload) {
            payload.insert("streamId", "wan-latency");
            signaling.send({type, payload});
        });
    QObject::connect(&share, &zjdesk::ScreenShare::statusChanged, &app,
        [&](quint64, const QString &status, const QString &detail) {
            record({{"event", "status"}, {"status", status}, {"detail", detail}});
            if (status == "failed") app.exit(4);
        });
    QObject::connect(&share, &zjdesk::ScreenShare::controlChannelChanged, &app,
        [&](quint64, bool ready) { channelReady = ready; });
    QObject::connect(&share, &zjdesk::ScreenShare::controlReceived, &app,
        [&](quint64, const QJsonObject &message) {
            if (message.contains("_zjdeskLatencyMarker"))
                record({{"event", "marker-applied"}, {"epoch", message.value("epoch")},
                    {"sequence", message.value("sequence")}});
        });
    QObject::connect(&share, &zjdesk::ScreenShare::frameReady, &app, [&](quint64, const QImage &image) {
        ++frames;
        if (host) return;
        if (const auto marker = zjdesk::TestFrameMarker::read(image)) frameMarkers.insert(frames, *marker);
        while (frameMarkers.size() > 16) frameMarkers.erase(frameMarkers.begin());
        surface.setFrame(image);
    });
    QObject::connect(&surface, &VideoSurface::framePresented, &app, [&](quint64 frameId, qint64 now) {
        if (!previousSwapNs)
        {
            surface.makeCurrent();
            auto *functions = surface.context()->functions();
            record({{"event", "renderer"}, {"platform", QGuiApplication::platformName()},
                {"vendor", QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_VENDOR)))},
                {"renderer", QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_RENDERER)))},
                {"version", QString::fromLatin1(reinterpret_cast<const char *>(functions->glGetString(GL_VERSION)))}});
            surface.doneCurrent();
        }
        record({{"event", "present"}, {"frameId", qint64(frameId)},
            {"gapMs", previousSwapNs ? (now - previousSwapNs) / 1e6 : 0}, {"swappedNs", QString::number(now)}});
        previousSwapNs = now;
        const auto found = frameMarkers.constFind(frameId);
        if (found != frameMarkers.cend() && found->epoch == markerEpoch && found->sequence != lastVisibleSequence)
        {
            lastVisibleSequence = found->sequence;
            const auto sent = pending.find(found->sequence);
            if (sent != pending.end())
            {
                ++samples;
                record({{"event", "marker"}, {"epoch", QString::number(markerEpoch)},
                    {"sequence", QString::number(found->sequence)}, {"frameId", qint64(frameId)},
                    {"sentNs", QString::number(sent.value())}, {"swappedNs", QString::number(now)},
                    {"latencyMs", (now - sent.value()) / 1e6}});
                pending.erase(sent);
            }
            else record({{"event", "marker-late-or-duplicate"}, {"sequence", QString::number(found->sequence)}});
        }
        while (!frameMarkers.isEmpty() && frameMarkers.firstKey() <= frameId) frameMarkers.erase(frameMarkers.begin());
    });
    qint64 previousStatsNs = startedNs;
    quint64 previousPresented = 0;
    QObject::connect(&share, &zjdesk::ScreenShare::statisticsReady, &app,
        [&](quint64, const zjdesk::ShareStats &stats) {
            const auto now = zjdesk::FrameTiming::nowNs();
            const double interval = (now - previousStatsNs) / 1e9;
            const auto presented = surface.presentedFrames();
            const auto gap = host ? -1 : surface.takePresentationGapMaxMs();
            const auto presentation = surface.presentationStatistics();
            if (!host) share.reportPresentation(presented, 1, true, gap, presentation);
            record({{"event", "stats"}, {"frames", qint64(frames)}, {"presentedFrames", qint64(presented)},
                {"presentedFps", host ? -1 : (presented - previousPresented) / interval},
                {"presentationIntervalSeconds", interval}, {"intervalSeconds", stats.intervalSeconds},
                {"presentationGapMaxMs", gap}, {"presentation", presentation.toJson()},
                {"width", stats.size.width()}, {"height", stats.size.height()}, {"targetFps", stats.targetFps},
                {"processedFps", stats.processedFps}, {"deliveredFps", stats.deliveredFps},
                {"mbps", stats.networkMbps}, {"packetsLost", stats.packetsLost}, {"jitterMs", stats.jitterMs},
                {"packetsReceived", stats.packetsReceived}, {"controlRttMs", stats.controlRttMs},
                {"webRtcRttMs", stats.webRtcRttMs}, {"transportExcessMs", stats.transportExcessMs},
                {"controlBufferedBytes", qint64(stats.controlBufferedBytes)},
                {"maxKeyframeBytes", qint64(stats.maxKeyframeBytes)}, {"keyframes", qint64(stats.keyframes)},
                {"transport", stats.transport}, {"local", stats.localAddress}, {"remote", stats.remoteAddress},
                {"codec", stats.codec}, {"diagnostics", stats.timingDiagnostics()}});
            previousStatsNs = now;
            previousPresented = presented;
        });
    QTimer markerTimer;
    markerTimer.setInterval(1000);
    markerTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&markerTimer, &QTimer::timeout, &app, [&] {
        const auto now = zjdesk::FrameTiming::nowNs();
        while (!pending.isEmpty() && now - pending.first() > 5000000000LL)
        {
            ++timeouts;
            record({{"event", "marker-timeout"}, {"sequence", QString::number(pending.firstKey())}});
            pending.erase(pending.begin());
        }
        if (host || !channelReady || pending.size() >= 16 || now - startedNs > qint64(seconds - 6) * 1000000000LL) return;
        ++sequence;
        pending.insert(sequence, now);
        record({{"event", "marker-sent"}, {"epoch", QString::number(markerEpoch)},
            {"sequence", QString::number(sequence)}, {"sentNs", QString::number(now)}});
        share.sendControl({{"_zjdeskLatencyMarker", true}, {"epoch", QString::number(markerEpoch)},
            {"sequence", QString::number(sequence)}}, 1);
    });
    markerTimer.start();
    if (!host && stallAt)
        QTimer::singleShot(stallAt * 1000, Qt::PreciseTimer, &app, [&] {
            record({{"event", "stall-start"}, {"durationMs", stallMs}});
            QThread::msleep(stallMs);
            record({{"event", "stall-end"}});
        });
    QTimer::singleShot(seconds * 1000, Qt::PreciseTimer, &app, [&] {
        finish("duration");
    });
    signaling.open(url);
    return app.exec();
}
