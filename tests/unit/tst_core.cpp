#include "diagnostics/event_log.h"
#include "platform/platform_info.h"
#include "protocol/message.h"
#include "remote-control/control_session.h"
#include "session/session_coordinator.h"
#include "media/share_quality.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class CoreTests : public QObject
{
    Q_OBJECT
private slots:
    void shareQualityPreservesResolutionAndAvoidsOscillation()
    {
        zjdesk::ShareQuality quality(QSize(3840, 2160));
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, 60);
        QVERIFY(!quality.observe(60, 60));
        QVERIFY(!quality.observe(60, 20));
        QVERIFY(!quality.observe(60, 60));
        QVERIFY(!quality.observe(60, 20));
        QVERIFY(quality.observe(60, 20));
        QCOMPARE(quality.profile().fps, 20);
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QVERIFY(!quality.observe(10, 30));
        QVERIFY(quality.observe(10, 30));
        QCOMPARE(quality.profile().fps, 10);
        QVERIFY(!quality.observe(5, 5));
        QVERIFY(quality.observe(5, 5));
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, 5);
        QVERIFY(!quality.observe(1, 1));
        QVERIFY(quality.observe(1, 1));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 60);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(60, 60));
        QVERIFY(quality.observe(60, 60));
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, 5);
    }

    void shareQualityRestoresResolutionFromLowerFrameRate()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        for (int i = 0; i < 4; ++i)
            quality.observe(1, 1);
        quality.observe(20, 20);
        QVERIFY(quality.observe(20, 20));
        QCOMPARE(quality.profile().size, QSize(1280, 720));
        QCOMPARE(quality.profile().fps, 20);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(20, 20));
        QVERIFY(quality.observe(20, 20));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 5);
    }

    void shareQualityHandlesSmallAndUltrawideDisplays()
    {
        zjdesk::ShareQuality small(QSize(320, 240));
        for (int i = 0; i < 20; ++i)
            small.observe(1, 1);
        QCOMPARE(small.profile().size, QSize(320, 240));
        QCOMPARE(small.profile().fps, 5);
        zjdesk::ShareQuality wide(QSize(3440, 1440));
        for (int i = 0; i < 4; ++i)
            wide.observe(1, 1);
        QCOMPARE(wide.profile().size.width(), 1920);
        QCOMPARE(wide.profile().size.height() % 2, 0);
        QVERIFY(qAbs(double(wide.profile().size.width()) / wide.profile().size.height() - 3440.0 / 1440) < 0.01);
    }

    void slowScreenShareKeepsEnoughBitrateForText()
    {
        const zjdesk::ShareProfile slow{QSize(1920, 1080), 5};
        const zjdesk::ShareProfile normal{QSize(1920, 1080), 30};
        const zjdesk::ShareProfile uhd{QSize(3840, 2160), 60};
        QCOMPARE(slow.bitrate(), normal.bitrate());
        QVERIFY(slow.bitrate() >= 4000000);
        QVERIFY(uhd.bitrate() > 12000000);
        QVERIFY(uhd.bitrate() <= 40000000);
    }

    void shareQualityStartsAtDisplayRefreshRate()
    {
        zjdesk::ShareQuality quality(QSize(2560, 1440), 144);
        QCOMPARE(quality.profile().fps, 144);
        QVERIFY(!quality.observe(144, 55));
        QVERIFY(quality.observe(144, 55));
        QCOMPARE(quality.profile().fps, 50);
        QCOMPARE(quality.profile().size, QSize(2560, 1440));
        QCOMPARE(zjdesk::ShareQuality(QSize(1920, 1080), 0).profile().fps, 60);
        QCOMPARE(zjdesk::ShareQuality(QSize(1920, 1080), 500).profile().fps, 240);
    }

    void shareQualityUsesBothSlowSamplesForFrameRate()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        QVERIFY(!quality.observe(60, 45));
        QVERIFY(quality.observe(60, 35));
        QCOMPARE(quality.profile().fps, 40);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
    }

    void shareQualityRestoresResolutionBeforeChasingFrameRate()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        for (int i = 0; i < 4; ++i)
            quality.observe(1, 1);
        QCOMPARE(quality.profile().size, QSize(1280, 720));
        QCOMPARE(quality.profile().fps, 60);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(52, 52));
        QVERIFY(quality.observe(52, 52));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 5);
    }
    void messageRoundTrip()
    {
        const zjdesk::protocol::Message original{"join", {{"roomId", "demo"}, {"name", QStringLiteral("测试用户")}}};
        const auto decoded = zjdesk::protocol::decode(zjdesk::protocol::encode(original));
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->type, original.type);
        QCOMPARE(decoded->payload, original.payload);
    }

    void rejectInvalidMessages_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("broken-json") << "{";
        QTest::newRow("array") << "[]";
        QTest::newRow("wrong-version") << "{\"version\":2,\"type\":\"join\",\"payload\":{}}";
        QTest::newRow("fractional-version") << "{\"version\":1.5,\"type\":\"join\",\"payload\":{}}";
        QTest::newRow("no-type") << "{\"version\":1,\"payload\":{}}";
        QTest::newRow("wrong-payload") << "{\"version\":1,\"type\":\"join\",\"payload\":[]}";
        QTest::newRow("oversize") << QString(zjdesk::protocol::MaxMessageBytes + 1, 'x');
    }

    void rejectInvalidMessages()
    {
        QFETCH(QString, input);
        QString error;
        QVERIFY(!zjdesk::protocol::decode(input, &error));
        QVERIFY(!error.isEmpty());
    }

    void validateRoomAndName()
    {
        QVERIFY(zjdesk::protocol::isRoomIdValid("demo_room-01"));
        QVERIFY(!zjdesk::protocol::isRoomIdValid("../room"));
        QVERIFY(!zjdesk::protocol::isRoomIdValid("demo\n"));
        QVERIFY(!zjdesk::protocol::isRoomIdValid(QString(33, 'x')));
        QVERIFY(!zjdesk::protocol::isPeerNameValid("   "));
        QVERIFY(!zjdesk::protocol::isPeerNameValid("a\nb"));
        QVERIFY(zjdesk::protocol::isPeerNameValid(QStringLiteral("用户 A")));
    }

    void controlConsentAndEpoch()
    {
        zjdesk::ControlSession control;
        QVERIFY(!control.grant());
        QVERIFY(!control.acceptInput("peer", 0, 1));
        QVERIFY(control.request("peer"));
        QVERIFY(!control.acceptInput("peer", control.epoch(), 1));
        QVERIFY(control.grant());
        const auto oldEpoch = control.epoch();
        QVERIFY(!control.acceptInput("other", oldEpoch, 1));
        QVERIFY(control.acceptInput("peer", oldEpoch, 1));
        QVERIFY(!control.acceptInput("peer", oldEpoch, 1));
        QVERIFY(!control.acceptInput("peer", oldEpoch, 0));
        control.revoke();
        QVERIFY(!control.acceptInput("peer", oldEpoch, 2));
        QVERIFY(control.request("peer"));
        QVERIFY(control.grant());
        QVERIFY(control.epoch() > oldEpoch);
        QVERIFY(!control.acceptInput("peer", oldEpoch, 2));
        QVERIFY(control.acceptInput("peer", control.epoch(), 1));
    }

    void eventLogBoundedAndExported()
    {
        zjdesk::EventLog events;
        for (int i = 0; i < zjdesk::EventLog::MaxEntries + 10; ++i)
            events.append("test", QString::number(i));
        QCOMPARE(events.entries().size(), zjdesk::EventLog::MaxEntries);
        QCOMPARE(events.entries().first().message, "10");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("events.jsonl");
        QString error;
        QVERIFY2(events.exportJsonLines(path, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), zjdesk::EventLog::MaxEntries + 1);
        QCOMPARE(QJsonDocument::fromJson(lines.first()).object().value("kind").toString(), "environment");
        QCOMPARE(QJsonDocument::fromJson(lines.last()).object().value("message").toString(), "309");
    }

    void invalidJoinDoesNotOpenConnection()
    {
        zjdesk::SessionCoordinator session;
        QVERIFY(!session.join(QUrl("http://127.0.0.1"), "demo", "Tester"));
        QCOMPARE(session.state(), zjdesk::SessionCoordinator::State::Failed);
        QCOMPARE(session.memberCount(), 0);
        session.leave();
        QCOMPARE(session.state(), zjdesk::SessionCoordinator::State::Idle);
    }

    void platformInformationIsReal()
    {
        const auto platform = zjdesk::PlatformInfo::current();
        QVERIFY(!platform.system.isEmpty());
        QVERIFY(!platform.architecture.isEmpty());
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
