#include "session/session_coordinator.h"
#include "signaling/room_server.h"
#include <QThread>
#include <QtTest>

using Preview = zjdesk::SessionCoordinator::PreviewState;

class MediaTests : public QObject
{
    Q_OBJECT
private slots:
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
