#include "session/session_coordinator.h"
#include "signaling/room_server.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QThread>
#include <QScopeGuard>
#include <QtTest>

using Preview = zjdesk::SessionCoordinator::PreviewState;

class MediaTests : public QObject
{
    Q_OBJECT
private slots:
    void gccCapConstrainsNegotiatedPacerAndEncoder()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        const auto oldGcc = qgetenv("ZJDESK_WEBRTC_GCC");
        const auto oldCap = qgetenv("ZJDESK_WEBRTC_MAX_BITRATE");
        const auto restore = qScopeGuard([&] {
            if (oldGcc.isNull()) qunsetenv("ZJDESK_WEBRTC_GCC");
            else qputenv("ZJDESK_WEBRTC_GCC", oldGcc);
            if (oldCap.isNull()) qunsetenv("ZJDESK_WEBRTC_MAX_BITRATE");
            else qputenv("ZJDESK_WEBRTC_MAX_BITRATE", oldCap);
        });
        qputenv("ZJDESK_WEBRTC_GCC", "1");
        qputenv("ZJDESK_WEBRTC_MAX_BITRATE", "512000");
        zjdesk::ScreenShare sender, receiver;
        zjdesk::ShareStats sent;
        QSignalSpy frames(&receiver, &zjdesk::ScreenShare::frameReady);
        connect(&sender, &zjdesk::ScreenShare::statisticsReady, this,
            [&](quint64, const zjdesk::ShareStats &stats) { sent = stats; });
        connect(&sender, &zjdesk::ScreenShare::negotiationReady, &receiver,
            [&](quint64, const QString &type, const QJsonObject &message) { receiver.negotiate(type, message, 9); });
        connect(&receiver, &zjdesk::ScreenShare::negotiationReady, &sender,
            [&](quint64, const QString &type, const QJsonObject &message) { sender.negotiate(type, message, 7); });
        receiver.start(false, {}, 9);
        sender.start(true, "test", 7, 30);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty() && sent.congestionBitrate > 0, 15000);
        QCOMPARE(sent.congestionMinimumBitrate, 128000);
        QCOMPARE(sent.congestionMaximumBitrate, 512000);
        QVERIFY(sent.congestionBitrate >= 128000 && sent.congestionBitrate <= 512000);
        QVERIFY(sent.encoderBitrate > 0 && sent.encoderBitrate <= 409600);
        QCOMPARE(sent.timingDiagnostics().value("congestion").toObject().value("maximumBitrate").toInt(), 512000);
        sender.stop(8);
        receiver.stop(10);
    }

    void invalidGccCapFailsBeforeCapture_data()
    {
        QTest::addColumn<QByteArray>("mode");
        QTest::addColumn<QByteArray>("cap");
        QTest::newRow("missing-gcc") << QByteArray() << QByteArray("512000");
        QTest::newRow("invalid-integer") << QByteArray("1") << QByteArray("garbage");
        QTest::newRow("out-of-range") << QByteArray("tcp") << QByteArray("127999");
    }

    void invalidGccCapFailsBeforeCapture()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        QFETCH(QByteArray, mode);
        QFETCH(QByteArray, cap);
        const auto oldGcc = qgetenv("ZJDESK_WEBRTC_GCC");
        const auto oldCap = qgetenv("ZJDESK_WEBRTC_MAX_BITRATE");
        const auto restore = qScopeGuard([&] {
            if (oldGcc.isNull()) qunsetenv("ZJDESK_WEBRTC_GCC");
            else qputenv("ZJDESK_WEBRTC_GCC", oldGcc);
            if (oldCap.isNull()) qunsetenv("ZJDESK_WEBRTC_MAX_BITRATE");
            else qputenv("ZJDESK_WEBRTC_MAX_BITRATE", oldCap);
        });
        qputenv("ZJDESK_WEBRTC_GCC", mode);
        qputenv("ZJDESK_WEBRTC_MAX_BITRATE", cap);
        zjdesk::ScreenShare sender;
        QSignalSpy status(&sender, &zjdesk::ScreenShare::statusChanged);
        QSignalSpy frames(&sender, &zjdesk::ScreenShare::frameReady);
        sender.start(true, "test", 7, 30);
        QTRY_VERIFY(!status.isEmpty());
        QCOMPARE(status.last().at(1).toString(), QString("failed"));
        QVERIFY(status.last().at(2).toString().contains("ZJDESK_WEBRTC_MAX_BITRATE"));
        QVERIFY(frames.isEmpty());
    }

    void invalidMediaLocalAddressFailsWithoutStartingCapture()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        const bool wasSet = qEnvironmentVariableIsSet("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        const auto previous = qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        const auto restore = qScopeGuard([&] {
            if (wasSet)
                qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", previous);
            else
                qunsetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        });
        qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", "invalid-local-address");
        zjdesk::ScreenShare sender;
        QSignalSpy status(&sender, &zjdesk::ScreenShare::statusChanged);
        QSignalSpy frames(&sender, &zjdesk::ScreenShare::frameReady);
        sender.start(true, "test", 7, 30);
        QTRY_VERIFY(!status.isEmpty());
        QCOMPARE(status.last().at(0).toULongLong(), quint64(7));
        QCOMPARE(status.last().at(1).toString(), QString("failed"));
        QCOMPARE(status.last().at(2).toString(), QStringLiteral("指定的媒体本地地址不可用"));
        QCOMPARE(frames.size(), 0);
    }

    void physicalDesktopSenderWithoutWindow()
    {
#ifndef Q_OS_LINUX
        QSKIP("Linux physical desktop sender only");
#else
        if (qEnvironmentVariable("ZJDESK_PHYSICAL_SENDER") != "1")
            QSKIP("Explicit physical desktop validation only; opens no test windows");
        zjdesk::SessionCoordinator sender;
        connect(&sender, &zjdesk::SessionCoordinator::shareChanged, this, [&] {
            qInfo() << "Physical sender state" << int(sender.shareState()) << sender.shareDetail();
        });
        connect(&sender, &zjdesk::SessionCoordinator::shareStatisticsChanged, this,
            [](const zjdesk::ShareStats &stats) {
                qInfo().noquote() << "Physical sender timings"
                    << QJsonDocument(stats.timingDiagnostics()).toJson(QJsonDocument::Compact);
            });
        QVERIFY(sender.join(QUrl(qEnvironmentVariable("ZJDESK_TEST_SERVER")), "physical-first-frame", "Linux"));
        QTRY_VERIFY_WITH_TIMEOUT(sender.memberCount() == 2
            || sender.state() == zjdesk::SessionCoordinator::State::Failed, 60000);
        QVERIFY2(sender.memberCount() == 2, qPrintable(sender.detail()));
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            QVERIFY(sender.startScreenShare());
            QTRY_COMPARE_WITH_TIMEOUT(sender.shareState(), Preview::Running, 15000);
            QTRY_COMPARE_WITH_TIMEOUT(sender.shareStats().transport,
                qEnvironmentVariable("ZJDESK_TEST_TRANSPORT", "TCP"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(sender.shareState() == Preview::Stopped || sender.shareState() == Preview::Failed, 45000);
            QVERIFY2(sender.shareState() == Preview::Stopped, qPrintable(sender.shareDetail()));
            QTest::qWait(500);
        }
        sender.leave();
#endif
    }

    void receiverReportsFeedbackBeforeFirstFrame()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        zjdesk::ScreenShare receiver;
        QSignalSpy frames(&receiver, &zjdesk::ScreenShare::frameReady);
        QSignalSpy statistics(&receiver, &zjdesk::ScreenShare::statisticsReady);
        QSignalSpy feedback(&receiver, &zjdesk::ScreenShare::negotiationReady);
        receiver.start(false, {}, 9);
        receiver.negotiate("share", {{"action", "profile"}, {"revision", 7}, {"fps", 60}}, 9);
        QTRY_VERIFY_WITH_TIMEOUT(!feedback.isEmpty(), 4000);
        QVERIFY(frames.isEmpty());
        QVERIFY(!statistics.isEmpty());
        const auto message = feedback.last().at(2).toJsonObject();
        QCOMPARE(feedback.last().at(1).toString(), QString("share"));
        QCOMPARE(message.value("action").toString(), QString("stats"));
        QCOMPARE(message.value("revision").toInt(), 7);
        QCOMPARE(message.value("fps").toDouble(), 0.0);
        receiver.stop(10);
    }

    void lateStopAfterMediaFailureMatchesOnlyTheFailedStream()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator alice;
        zjdesk::SignalingClient bob;
        QSignalSpy connected(&bob, &zjdesk::SignalingClient::connected);
        bob.open(url);
        QTRY_VERIFY(!connected.isEmpty());
        QVERIFY(bob.send({"join", {{"roomId", "late-stop"}, {"name", "Bob"}}}));
        QVERIFY(alice.join(url, "late-stop", "Alice"));
        QTRY_COMPARE(alice.memberCount(), 2);
        QSignalSpy negotiations(&bob, &zjdesk::SignalingClient::messageReceived);
        const bool wasSet = qEnvironmentVariableIsSet("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        const auto previous = qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        const auto restoreAddress = [&] {
            if (wasSet)
                qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", previous);
            else
                qunsetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        };
        const auto restore = qScopeGuard(restoreAddress);
        qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", "invalid-local-address");
        QVERIFY(alice.startScreenShare("test"));
        QTRY_COMPARE(alice.shareState(), Preview::Failed);
        QTRY_VERIFY(!negotiations.isEmpty());
        QString failedId;
        for (const auto &event : negotiations)
        {
            const auto message = qvariant_cast<zjdesk::protocol::Message>(event.at(0));
            if (message.type == "share" && message.payload.value("action") == "start")
                failedId = message.payload.value("streamId").toString();
        }
        QVERIFY(!failedId.isEmpty());
        restoreAddress();
        QVERIFY(bob.send({"share", {{"action", "stop"}, {"streamId", "different-stream"}}}));
        QTest::qWait(100);
        QCOMPARE(alice.shareState(), Preview::Failed);
        QVERIFY(bob.send({"share", {{"action", "stop"}, {"streamId", failedId}}}));
        QTRY_COMPARE(alice.shareState(), Preview::Stopped);
        QVERIFY(alice.startScreenShare("test"));
        QCOMPARE(alice.shareState(), Preview::Starting);
        QVERIFY(bob.send({"share", {{"action", "stop"}, {"streamId", failedId}}}));
        QTest::qWait(100);
        QVERIFY(alice.shareState() == Preview::Starting || alice.shareState() == Preview::Running);
        alice.stopScreenShare();
        QTRY_COMPARE(alice.shareState(), Preview::Stopped);
    }

    void sharedFramesSurvivePipelineRestartAndDetachOnWrite()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        zjdesk::ScreenShare sender, receiver;
        quint64 senderEpoch = 1, receiverEpoch = 10;
        QImage latest;
        QList<QImage> retained, snapshots;
        connect(&receiver, &zjdesk::ScreenShare::frameReady, this,
            [&](quint64 epoch, const QImage &image) {
                if (epoch == receiverEpoch)
                    latest = image;
            });
        connect(&sender, &zjdesk::ScreenShare::negotiationReady, &receiver,
            [&](quint64, const QString &type, const QJsonObject &message) {
                receiver.negotiate(type, message, receiverEpoch);
            });
        connect(&receiver, &zjdesk::ScreenShare::negotiationReady, &sender,
            [&](quint64, const QString &type, const QJsonObject &message) {
                sender.negotiate(type, message, senderEpoch);
            });
        for (int restart = 0; restart < 3; ++restart)
        {
            receiver.start(false, {}, receiverEpoch);
            sender.start(true, "test", senderEpoch, 60);
            QTRY_VERIFY_WITH_TIMEOUT(!latest.isNull(), 15000);
            for (int sample = 0; sample < 4; ++sample)
            {
                retained.append(latest);
                snapshots.append(latest.copy());
                QTest::qWait(100);
            }
            sender.stop(++senderEpoch);
            receiver.stop(++receiverEpoch);
            QTest::qWait(200);
            latest = {};
            for (int i = 0; i < retained.size(); ++i)
                QCOMPARE(retained.at(i), snapshots.at(i));
            auto writable = retained.last();
            writable.fill(Qt::magenta);
            QCOMPARE(retained.last(), snapshots.last());
            QVERIFY(writable != retained.last());
        }
    }

    void lossFeedbackChangesEncoderBudgetAndRejectsStaleRevision()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        zjdesk::ScreenShare sender, receiver;
        zjdesk::ShareStats sent, received;
        connect(&sender, &zjdesk::ScreenShare::statisticsReady, this,
            [&](quint64, const zjdesk::ShareStats &stats) { sent = stats; });
        connect(&receiver, &zjdesk::ScreenShare::statisticsReady, this,
            [&](quint64, const zjdesk::ShareStats &stats) { received = stats; });
        connect(&sender, &zjdesk::ScreenShare::negotiationReady, &receiver,
            [&](quint64, const QString &type, const QJsonObject &message) { receiver.negotiate(type, message, 9); });
        connect(&receiver, &zjdesk::ScreenShare::negotiationReady, &sender,
            [&](quint64, const QString &type, const QJsonObject &message) {
                if (type != "share")
                    sender.negotiate(type, message, 7);
            });
        receiver.start(false, {}, 9);
        sender.start(true, "test", 7, 120);
        QTRY_VERIFY_WITH_TIMEOUT(sent.maxEncodedFrameBytes > 0 && received.codecMeanMs >= 0, 15000);
        QVERIFY(sent.codecMeanMs >= 0);
        QVERIFY(sent.codecMaxMs >= sent.codecMeanMs);
        QVERIFY(received.outputGapMaxMs > 0);
        QCOMPARE(received.copyMaxMs, 0.0);
        QCOMPARE(received.decoder, QString("vp9dec"));
        QVERIFY(!received.hardwareDecoder);
        QCOMPARE(sent.codecProfile, QString("1"));
        QCOMPARE(received.codecOutputFormat, QString("Y444"));
        QVERIFY(!received.codecOutputMemory.isEmpty());
        QVERIFY(sent.captureToEncoderTiming.samples > 0);
        QVERIFY(received.codecTiming.samples > 0);
        QVERIFY(received.decodedToSinkTiming.samples > 0);
        QVERIFY(received.sinkToPullTiming.samples > 0);
        QVERIFY(received.frameMapTiming.samples > 0);
        QVERIFY(received.mailboxTiming.samples > 0);
        QVERIFY(received.workerPollTiming.samples > 0);
        QVERIFY(received.workerPollGapTiming.samples > 0);
        QVERIFY(received.deliveredFrameId > 0);
        QCOMPARE(received.timingDiagnostics().value("scope").toString(), QString("stream-cumulative"));
        QVERIFY(sent.sendBufferBytes > 0);
        QVERIFY(received.receiveBufferBytes > 0);
        QCOMPARE(sent.rtcpFeedbackBudget, 64000);
        QCOMPARE(received.rtcpFeedbackBudget, 64000);
        QCOMPARE(received.size, QSize(640, 480));
        const int initial = sent.encoderBitrate;
        const int initialQuantizer = sent.encoderQuantizer;
        QVERIFY(initial > 2000000);
        auto feedback = [&](int revision, qint64 packets, qint64 lost) {
            sender.negotiate("share", {{"action", "stats"}, {"revision", revision}, {"fps", 60},
                {"packetsReceived", packets}, {"packetsLost", lost}}, 7);
        };
        feedback(sent.revision, 1000, 0);
        feedback(sent.revision, 1980, 20);
        feedback(sent.revision - 1, 2960, 40);
        QTest::qWait(1100);
        QCOMPARE(sent.encoderBitrate, initial);
        feedback(sent.revision, 2960, 40);
        QTRY_COMPARE_WITH_TIMEOUT(sent.encoderBitrate, qMax(2000000, initial * 85 / 100), 4000);
        if (sent.hardwareEncoder)
            QVERIFY(sent.encoderQuantizer > initialQuantizer && sent.encoderQuantizer <= 16);
        QCOMPARE(sent.targetFps, 120);
        QCOMPARE(received.size, QSize(640, 480));
        sender.stop(8);
        receiver.stop(10);
    }

    void dataChannelTransportsBothDirectionsAndRejectsOldEpoch()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        zjdesk::ScreenShare sender, receiver;
        QSignalSpy senderReady(&sender, &zjdesk::ScreenShare::controlChannelChanged);
        QSignalSpy receiverReady(&receiver, &zjdesk::ScreenShare::controlChannelChanged);
        QSignalSpy atSender(&sender, &zjdesk::ScreenShare::controlReceived);
        QSignalSpy atReceiver(&receiver, &zjdesk::ScreenShare::controlReceived);
        zjdesk::ShareStats sent;
        connect(&sender, &zjdesk::ScreenShare::statisticsReady, this,
            [&](quint64, const zjdesk::ShareStats &stats) { sent = stats; });
        QString offeredSdp, answeredSdp;
        connect(&sender, &zjdesk::ScreenShare::negotiationReady, &receiver,
            [&](quint64, const QString &type, const QJsonObject &message) {
                if (type == "offer")
                    offeredSdp = message.value("sdp").toString();
                receiver.negotiate(type, message, 9);
            });
        connect(&receiver, &zjdesk::ScreenShare::negotiationReady, &sender,
            [&](quint64, const QString &type, const QJsonObject &message) {
                if (type == "answer")
                    answeredSdp = message.value("sdp").toString();
                sender.negotiate(type, message, 7);
            });
        receiver.start(false, {}, 9);
        sender.start(true, "test", 7);
        QTRY_VERIFY_WITH_TIMEOUT(!senderReady.isEmpty() && !receiverReady.isEmpty(), 15000);
        QVERIFY(senderReady.last().at(1).toBool());
        QVERIFY(receiverReady.last().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(sent.controlRttMs >= 0 && sent.transportExcessMs >= 0, 5000);
        QCOMPARE(atSender.size(), 0);
        QCOMPARE(atReceiver.size(), 0);
        if (qEnvironmentVariable("ZJDESK_WEBRTC_GCC") == "1"
            || qEnvironmentVariable("ZJDESK_WEBRTC_GCC") == "tcp")
        {
            for (const auto &sdp : {offeredSdp, answeredSdp})
                QVERIFY2(sdp.contains("transport-wide-cc-extensions-01")
                    && sdp.contains("a=rtcp-fb:96 transport-cc"), qPrintable(sdp));
            QVERIFY(sent.congestionBitrate >= 2000000 && sent.congestionBitrate <= 40000000);
            QVERIFY(sent.pacedBytes >= 0);
            qInfo() << "Negotiated TWCC with GCC estimate" << sent.congestionBitrate;
        }
        for (const auto &sdp : {offeredSdp, answeredSdp})
        {
            QVERIFY2(sdp.contains("a=rtcp-fb:96 nack\r\n"), qPrintable(sdp));
            QVERIFY2(sdp.contains("rtx/90000"), qPrintable(sdp));
            QVERIFY2(sdp.contains("red/90000") && sdp.contains("ulpfec/90000"), qPrintable(sdp));
        }
        sender.sendControl({{"action", "capabilities"}, {"control", true}}, 7);
        QTRY_COMPARE(atReceiver.size(), 1);
        QCOMPARE(atReceiver.last().at(0).toULongLong(), quint64(9));
        QVERIFY(atReceiver.last().at(1).toJsonObject().value("control").toBool());
        receiver.sendControl({{"action", "request"}}, 9);
        QTRY_COMPARE(atSender.size(), 1);
        QCOMPARE(atSender.last().at(1).toJsonObject().value("action").toString(), QString("request"));
        // Position bursts may collapse, but the last position must precede each discrete event.
        atReceiver.clear();
        for (int i = 1; i <= 1000; ++i)
            sender.sendControl({{"action", "input"}, {"kind", "move"}, {"epoch", "3"}, {"sequence", i}}, 7);
        sender.sendControl({{"action", "input"}, {"kind", "button"}, {"epoch", "3"}, {"sequence", 1001}}, 7);
        for (int i = 1002; i <= 2000; ++i)
            sender.sendControl({{"action", "input"}, {"kind", "move"}, {"epoch", "3"}, {"sequence", i}}, 7);
        sender.sendControl({{"action", "input"}, {"kind", "release"}, {"epoch", "3"}, {"sequence", 2001}}, 7);
        QTRY_VERIFY_WITH_TIMEOUT(!atReceiver.isEmpty()
            && atReceiver.last().at(1).toJsonObject().value("sequence").toInt() == 2001, 5000);
        QVERIFY(atReceiver.size() < 2001);
        int previous = 0;
        bool button = false, release = false;
        for (const auto &arguments : atReceiver)
        {
            const auto message = arguments.at(1).toJsonObject();
            const int sequence = message.value("sequence").toInt();
            QVERIFY(sequence > previous);
            if (message.value("kind") == "button")
            {
                QCOMPARE(previous, 1000);
                button = true;
            }
            if (message.value("kind") == "release")
            {
                QCOMPARE(previous, 2000);
                release = true;
            }
            previous = sequence;
        }
        QVERIFY(button && release);
        const int receivedCount = atReceiver.size();
        sender.sendControl({{"action", "old-stream"}}, 6);
        QTest::qWait(100);
        QCOMPARE(atReceiver.size(), receivedCount);
        receiver.stop(10);
        sender.stop(8);
        QTest::qWait(100);
    }

    void temporaryStartupDisplayDelayKeepsNativeFrameRate()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator sender, receiver;
        QVERIFY(sender.join(url, "warmup", "Sender"));
        QVERIFY(receiver.join(url, "warmup", "Receiver"));
        QTRY_COMPARE(sender.memberCount(), 2);
        quint64 delivered = 0;
        connect(&receiver, &zjdesk::SessionCoordinator::shareFrame, this,
            [&](const QImage &image) { if (!image.isNull()) ++delivered; });
        QVERIFY(sender.startScreenShare("test"));
        QTRY_COMPARE_WITH_TIMEOUT(receiver.shareState(), Preview::Running, 15000);
        QTimer display;
        bool warmingUp = true;
        bool displayActive = true;
        connect(&display, &QTimer::timeout, &receiver,
            [&] { receiver.reportSharePresentation(warmingUp ? 0 : delivered, displayActive); });
        display.start(1000);
        QTest::qWait(6500);
        QCOMPARE(sender.shareStats().targetFps, 60);
        warmingUp = false;
        QTest::qWait(5000);
        QCOMPARE(sender.shareStats().targetFps, 60);
        QCOMPARE(receiver.shareStats().size, QSize(640, 480));
        displayActive = false;
        warmingUp = true;
        QTest::qWait(6500);
        QCOMPARE(sender.shareStats().targetFps, 60);
        QCOMPARE(receiver.shareStats().size, QSize(640, 480));
        QCOMPARE(receiver.shareStats().presentedFps, -1);
        displayActive = true;
        warmingUp = false;
        QTest::qWait(2000);
        QCOMPARE(sender.shareStats().targetFps, 60);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.shareStats().presentedFps > 50, 4000);
        display.stop();
        sender.stopScreenShare();
        QTRY_COMPARE(receiver.shareState(), Preview::Stopped);
    }

    void slowReceiverChangesFrameRateAndResolutionWithoutRestart()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator sender, receiver;
        QVERIFY(sender.join(url, "adaptive", "Sender"));
        QVERIFY(receiver.join(url, "adaptive", "Receiver"));
        QTRY_COMPARE(sender.memberCount(), 2);
        QSize lastFrameSize;
        quint64 delivered = 0;
        QDir().mkpath("output");
        QFile trace("output/share-resolution-recovery.txt");
        QVERIFY(trace.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer sampling;
        connect(&sampling, &QTimer::timeout, this, [&] {
            const auto sent = sender.shareStats();
            const auto received = receiver.shareStats();
            trace.write(QString("%1 ms sender=%2x%3@%4 encoded=%5 receiver=%6x%7@%8 decoded=%9 frame=%10x%11 %12 | %13\n")
                .arg(elapsed.elapsed()).arg(sent.size.width()).arg(sent.size.height()).arg(sent.targetFps)
                .arg(sent.processedFps).arg(received.size.width()).arg(received.size.height())
                .arg(received.targetFps).arg(received.processedFps).arg(lastFrameSize.width())
                .arg(lastFrameSize.height()).arg(sender.shareDetail(), receiver.shareDetail()).toUtf8());
            trace.flush();
        });
        sampling.start(1000);
        connect(&receiver, &zjdesk::SessionCoordinator::shareFrame, this,
            [&](const QImage &image) {
                if (!image.isNull())
                {
                    lastFrameSize = image.size();
                    ++delivered;
                }
            });
        QVERIFY(sender.startScreenShare("test"));
        QTRY_COMPARE_WITH_TIMEOUT(receiver.shareState(), Preview::Running, 20000);
        QTimer slowDisplay;
        connect(&slowDisplay, &QTimer::timeout, &receiver,
            [&receiver] { receiver.reportSharePresentation(0); });
        slowDisplay.start(1000);
        QTRY_COMPARE_WITH_TIMEOUT(sender.shareStats().targetFps, 24, 45000);
        QTRY_COMPARE(receiver.shareStats().targetFps, 24);
        QCOMPARE(lastFrameSize, QSize(640, 480));
        QTRY_VERIFY_WITH_TIMEOUT(lastFrameSize == QSize(480, 360), 30000);
        QCOMPARE(sender.shareState(), Preview::Running);
        QCOMPARE(receiver.shareState(), Preview::Running);
        QVERIFY(sender.screenActive());
        slowDisplay.stop();
        QTimer healthyDisplay;
        connect(&healthyDisplay, &QTimer::timeout, &receiver,
            [&] { receiver.reportSharePresentation(delivered); });
        healthyDisplay.start(1000);
        QTRY_VERIFY_WITH_TIMEOUT(lastFrameSize == QSize(640, 480), 45000);
        QCOMPARE(receiver.shareState(), Preview::Running);
        healthyDisplay.stop();
        sender.stopScreenShare();
        QTRY_COMPARE(receiver.shareState(), Preview::Stopped);
    }

    void cameraModeUsesResolutionBeforeFrameRate()
    {
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        auto mode = zjdesk::maximumCameraMode("video/x-raw,format=NV12,width=640,height=480,framerate=120/1;"
            "video/x-raw,format=YUY2,width=1920,height=1080,framerate=(fraction){15/1,30/1};"
            "video/x-raw,format=NV12,width=1920,height=1080,framerate=30000/1001");
        QCOMPARE(mode.size, QSize(1920, 1080));
        QCOMPARE(mode.fpsNumerator, 30);
        QCOMPARE(mode.fpsDenominator, 1);
        QVERIFY(mode.caps.contains("YUY2"));
        mode = zjdesk::maximumCameraMode("image/jpeg,width=3840,height=2160,framerate=15/1;"
            "video/x-raw,width=1920,height=1080,framerate=60/1");
        QCOMPARE(mode.size, QSize(3840, 2160));
        QVERIFY(mode.jpeg);
        mode = zjdesk::maximumCameraMode("video/x-raw,width=(int)[320,1280],height=(int)[240,720],"
            "framerate=(fraction)[1/1,60/1]");
        QCOMPARE(mode.size, QSize(1280, 720));
        QCOMPARE(mode.fpsNumerator, 60);
        QVERIFY(zjdesk::maximumCameraMode("video/x-raw").caps.isEmpty());
    }

    void screenVideoTravelsThroughWebRtcAndRestarts()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator alice, bob;
        QVERIFY(!alice.startScreenShare("test"));
        QVERIFY(alice.join(url, "share", "Alice"));
        QVERIFY(bob.join(url, "share", "Bob"));
        QTRY_COMPARE(alice.memberCount(), 2);
        QSignalSpy frames(&bob, &zjdesk::SessionCoordinator::shareFrame);
        QSignalSpy negotiations(&bob, &zjdesk::SessionCoordinator::negotiationReceived);
        QString oldId;
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            QVERIFY(alice.startScreenShare("test"));
            QTRY_VERIFY_WITH_TIMEOUT(bob.shareState() == Preview::Running
                || bob.shareState() == Preview::Failed, 20000);
            QVERIFY2(bob.shareState() == Preview::Running, qPrintable(bob.shareDetail()));
            QTRY_COMPARE(alice.shareState(), Preview::Running);
            QVERIFY(bob.receivingScreen());
            QVERIFY(!bob.startScreenShare("test"));
            if (!oldId.isEmpty())
            {
                QVERIFY(alice.sendNegotiation("offer", {{"streamId", oldId}, {"sdp", "invalid-old-sdp"}}));
                QTest::qWait(100);
                QCOMPARE(bob.shareState(), Preview::Running);
            }
            QTRY_VERIFY(!frames.isEmpty());
            const auto first = qvariant_cast<QImage>(frames.last().at(0));
            QCOMPARE(first.size(), QSize(640, 480));
            QTest::qWait(250);
            QVERIFY(first != qvariant_cast<QImage>(frames.last().at(0)));
            QVERIFY(!negotiations.isEmpty());
            for (const auto &event : negotiations)
            {
                const auto message = qvariant_cast<zjdesk::protocol::Message>(event.at(0));
                if (message.type == "offer")
                    oldId = message.payload.value("streamId").toString();
            }
            QVERIFY(!oldId.isEmpty());
            alice.stopScreenShare();
            QTRY_COMPARE(alice.shareState(), Preview::Stopped);
            QTRY_COMPARE(bob.shareState(), Preview::Stopped);
            frames.clear();
            QVERIFY(alice.sendNegotiation("ice", {{"streamId", oldId}, {"candidate", "old"}, {"sdpMLineIndex", 0}}));
            QTest::qWait(100);
            QCOMPARE(frames.size(), 0);
            negotiations.clear();
        }
        QVERIFY(bob.startScreenShare("test"));
        QTRY_COMPARE_WITH_TIMEOUT(alice.shareState(), Preview::Running, 20000);
        bob.leave();
        QTRY_COMPARE(alice.memberCount(), 1);
        QTRY_COMPARE(alice.shareState(), Preview::Stopped);
    }

    void simultaneousShareAndCancelAreStable()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator alice, bob;
        QVERIFY(alice.join(url, "collision", "Alice"));
        QVERIFY(bob.join(url, "collision", "Bob"));
        QTRY_COMPARE(alice.memberCount(), 2);
        QVERIFY(alice.startScreenShare("test"));
        QVERIFY(bob.startScreenShare("test"));
        QTRY_COMPARE_WITH_TIMEOUT(alice.shareState(), Preview::Running, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(bob.shareState(), Preview::Running, 20000);
        QVERIFY(alice.receivingScreen() != bob.receivingScreen());
        alice.stopScreenShare();
        QTRY_COMPARE(alice.shareState(), Preview::Stopped);
        QTRY_COMPARE(bob.shareState(), Preview::Stopped);
        QVERIFY(alice.startScreenShare("test"));
        alice.stopScreenShare();
        QTRY_COMPARE(alice.shareState(), Preview::Stopped);
        QTRY_COMPARE(bob.shareState(), Preview::Stopped);
        QTest::qWait(200);
        QVERIFY(!alice.screenActive());
        QVERIFY(!bob.screenActive());
        QVERIFY(alice.startScreenShare("test"));
        QTRY_COMPARE_WITH_TIMEOUT(bob.shareState(), Preview::Running, 20000);
        server.close();
        QTRY_COMPARE(alice.state(), zjdesk::SessionCoordinator::State::Failed);
        QTRY_COMPARE(bob.state(), zjdesk::SessionCoordinator::State::Failed);
        QTRY_COMPARE(alice.shareState(), Preview::Stopped);
        QTRY_COMPARE(bob.shareState(), Preview::Stopped);
    }
    void testSourceProducesOwnedFramesAndRestarts()
    {
        zjdesk::SessionCoordinator session;
        QSignalSpy frames(&session, &zjdesk::SessionCoordinator::previewFrame);
        for (int i = 0; i < 5; ++i)
        {
            QVERIFY(session.startPreview("test"));
            QTRY_COMPARE_WITH_TIMEOUT(session.previewState(), Preview::Running, 10000);
            QTRY_VERIFY(!frames.isEmpty());
            const auto frame = qvariant_cast<QImage>(frames.last().at(0));
            QCOMPARE(frame.size(), QSize(640, 480));
            QVERIFY(frame.pixelColor(10, 10).isValid());
            session.stopPreview();
            QTRY_COMPARE(session.previewState(), Preview::Stopped);
            QCOMPARE(session.previewStats().pendingFrames, 0);
            frames.clear();
            QTest::qWait(100);
            QCOMPARE(frames.count(), 0);
            QVERIFY(!frame.isNull());
        }
    }

    void slowConsumerKeepsLatestFrameOnly()
    {
        zjdesk::SessionCoordinator session;
        QVERIFY(session.startPreview("test"));
        QTRY_COMPARE(session.previewState(), Preview::Running);
        QThread::msleep(500);
        const auto stats = session.previewStats();
        QCOMPARE(stats.pendingFrames, 1);
        QVERIFY(stats.droppedFrames > 0);
        session.stopPreview();
        QTRY_COMPARE(session.previewState(), Preview::Stopped);
    }

    void cancelAndInvalidDeviceDoNotReviveOldPreview()
    {
        zjdesk::SessionCoordinator session;
        QVERIFY(session.startPreview("test"));
        session.stopPreview();
        QTRY_COMPARE(session.previewState(), Preview::Stopped);
        QTest::qWait(100);
        QCOMPARE(session.previewState(), Preview::Stopped);
        QVERIFY(session.startPreview("missing-camera"));
        QTRY_COMPARE(session.previewState(), Preview::Failed);
        QVERIFY(!session.previewDetail().isEmpty());
        QVERIFY(session.startPreview("test"));
        QTRY_COMPARE(session.previewState(), Preview::Running);
        session.leave();
        QTRY_COMPARE(session.previewState(), Preview::Stopped);
    }

    void devicesAndSilentAudioTest()
    {
        zjdesk::SessionCoordinator session;
        QSignalSpy devices(&session, &zjdesk::SessionCoordinator::devicesChanged);
        session.refreshDevices();
        QTRY_VERIFY_WITH_TIMEOUT(devices.count() > 0, 15000);
        for (const auto &device : session.devices())
        {
            QVERIFY(!device.id.isEmpty());
            QVERIFY(!device.name.isEmpty());
            QVERIFY(!device.kind.isEmpty());
        }
        QVERIFY(session.startAudioTest(false));
        QTRY_COMPARE(session.audioState(), Preview::Running);
        session.stopAudioTest();
        QTRY_COMPARE(session.audioState(), Preview::Stopped);
    }

    void cameraReleasesDevice()
    {
        if (!qEnvironmentVariableIsSet("ZJDESK_CAMERA_TEST"))
            QSKIP("Physical camera test requires ZJDESK_CAMERA_TEST=1");
        zjdesk::SessionCoordinator session;
        QSignalSpy devices(&session, &zjdesk::SessionCoordinator::devicesChanged);
        session.refreshDevices();
        QTRY_VERIFY_WITH_TIMEOUT(devices.count() > 0, 15000);
        QString camera;
        QSize expectedSize;
        for (const auto &device : session.devices())
        {
            qInfo().noquote() << device.kind << device.name;
            if (camera.isEmpty() && device.kind.contains("Video/Source"))
            {
                camera = device.id;
                expectedSize = device.maximumMode.size;
                qInfo().noquote() << "Maximum camera mode:" << device.maximumMode.description()
                                 << device.maximumMode.caps;
            }
        }
        QVERIFY2(!camera.isEmpty(), "No camera was enumerated");
        QSignalSpy frames(&session, &zjdesk::SessionCoordinator::previewFrame);
        for (int i = 0; i < 3; ++i)
        {
            QVERIFY(session.startPreview(camera));
            QTRY_VERIFY_WITH_TIMEOUT(session.previewState() == Preview::Running
                || session.previewState() == Preview::Failed, 15000);
            QVERIFY2(session.previewState() == Preview::Running, qPrintable(session.previewDetail()));
            QTRY_VERIFY(!frames.isEmpty());
            QTRY_VERIFY(session.previewStats().capturedFrames >= 3);
            const auto image = qvariant_cast<QImage>(frames.last().at(0));
            QCOMPARE(image.size(), expectedSize);
            qInfo().noquote() << session.previewDetail();
            qInfo() << "Camera cycle" << i + 1 << "captured" << session.previewStats().capturedFrames;
            session.stopPreview();
            QTRY_COMPARE(session.previewState(), Preview::Stopped);
            frames.clear();
        }
    }
};

QTEST_GUILESS_MAIN(MediaTests)
#include "tst_media.moc"
