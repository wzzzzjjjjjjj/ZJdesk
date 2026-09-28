#include "session/session_coordinator.h"
#include <QThread>
#include <QtTest>

using Preview = zjdesk::SessionCoordinator::PreviewState;

class MediaTests : public QObject
{
    Q_OBJECT
private slots:
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
        for (const auto &device : session.devices())
        {
            qInfo().noquote() << device.kind << device.name;
            if (camera.isEmpty() && device.kind.contains("Video/Source"))
                camera = device.id;
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
            QCOMPARE(image.size(), QSize(640, 480));
            qInfo() << "Camera cycle" << i + 1 << "captured" << session.previewStats().capturedFrames;
            session.stopPreview();
            QTRY_COMPARE(session.previewState(), Preview::Stopped);
            frames.clear();
        }
    }
};

QTEST_GUILESS_MAIN(MediaTests)
#include "tst_media.moc"
