#include "diagnostics/event_log.h"
#include "platform/platform_info.h"
#include "protocol/message.h"
#include "remote-control/control_session.h"
#include "session/session_coordinator.h"
#include "media/share_quality.h"
#include "media/congestion_budget.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <limits>
#include <QtTest>

class CoreTests : public QObject
{
    Q_OBJECT
private slots:
    void congestionBudgetCapsStartupAndAllowsLowBandwidth()
    {
        const auto defaults = zjdesk::CongestionBudget::fromMaximum({}, 8294400);
        QVERIFY(defaults);
        QCOMPARE(defaults->minimum, 8000000);
        QCOMPARE(defaults->maximum, 40000000);
        QCOMPARE(defaults->initial, 40000000);
        const auto limited = zjdesk::CongestionBudget::fromMaximum("512000", 8294400);
        QVERIFY(limited);
        QCOMPARE(limited->minimum, 128000);
        QCOMPARE(limited->maximum, 512000);
        QCOMPARE(limited->initial, 512000);
        for (const auto &value : {"128000", "8000000", "40000000"})
        {
            const auto budget = zjdesk::CongestionBudget::fromMaximum(value, 2000000);
            QVERIFY(budget);
            QVERIFY(budget->minimum <= budget->initial && budget->initial <= budget->maximum);
        }
        for (const auto &value : {"-1", "0", "127999", "40000001", "4294967296", "1Mbps", "nan"})
            QVERIFY(!zjdesk::CongestionBudget::fromMaximum(value, 8294400));
    }

    void frameTimingKeepsLongStallsAndUsesConservativePercentiles()
    {
        zjdesk::FrameTiming timing;
        QCOMPARE(timing.summary().samples, quint64(0));
        QCOMPARE(timing.summary().p99Ms, -1.0);
        for (int i = 0; i < 99; ++i)
            timing.record(10);
        timing.record(907.258);
        timing.record(-1);
        timing.record(std::numeric_limits<double>::quiet_NaN());
        timing.record(std::numeric_limits<double>::infinity());
        const auto stats = timing.summary();
        QCOMPARE(stats.samples, quint64(100));
        QCOMPARE(stats.p50Ms, 10.0);
        QCOMPARE(stats.p95Ms, 10.0);
        QCOMPARE(stats.p99Ms, 10.0);
        QCOMPARE(stats.maxMs, 907.258);
        QCOMPARE(stats.over100Ms, quint64(1));
        QCOMPARE(stats.over200Ms, quint64(1));
        timing.record(100);
        QCOMPARE(timing.summary().p99Ms, 100.0);
        QCOMPARE(timing.summary().over100Ms, quint64(1));
        timing.record(201);
        timing.record(201);
        QCOMPARE(timing.summary().p99Ms, 907.258);
        QCOMPARE(timing.summary().over200Ms, quint64(3));
        timing = {};
        timing.record(1.01);
        QCOMPARE(timing.summary().p99Ms, 1.1);
        QCOMPARE(timing.summary().meanMs, 1.01);
    }

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
        QCOMPARE(quality.profile().fps, 50);
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        for (int i = 0; i < 4; ++i)
        {
            QVERIFY(!quality.observe(1, 1));
            QVERIFY(quality.observe(1, 1));
        }
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
        QVERIFY(!quality.observe(1, 1));
        QVERIFY(quality.observe(1, 1));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 60);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(60, 60));
        QVERIFY(quality.observe(60, 60));
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
    }

    void shareQualityRestoresResolutionFromLowerFrameRate()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        for (int i = 0; i < 34; ++i)
            quality.observe(1, 1);
        QCOMPARE(quality.profile().size, QSize(1280, 720));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(24, 24));
        QVERIFY(quality.observe(24, 24));
        QCOMPARE(quality.profile().size, QSize(1600, 900));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
    }

    void shareQualityHandlesSmallAndUltrawideDisplays()
    {
        zjdesk::ShareQuality small(QSize(320, 240));
        for (int i = 0; i < 20; ++i)
            small.observe(1, 1);
        QCOMPARE(small.profile().size, QSize(320, 240));
        QCOMPARE(small.profile().fps, 5);
        zjdesk::ShareQuality wide(QSize(3440, 1440));
        for (int i = 0; i < 12; ++i)
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
        QCOMPARE(quality.profile().fps, 120);
        QCOMPARE(quality.profile().size, QSize(2560, 1440));
        QCOMPARE(zjdesk::ShareQuality(QSize(1920, 1080), 0).profile().fps, 60);
        QCOMPARE(zjdesk::ShareQuality(QSize(1920, 1080), 500).profile().fps, 240);
    }

    void shareQualityChangesOnlyOneFrameRateStep()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        QVERIFY(!quality.observe(60, 45));
        QVERIFY(quality.observe(60, 35));
        QCOMPARE(quality.profile().fps, 50);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
    }

    void shareQualityRestoresResolutionBeforeChasingFrameRate()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        for (int i = 0; i < 12; ++i)
            quality.observe(1, 1);
        QCOMPARE(quality.profile().size, QSize(1600, 900));
        QCOMPARE(quality.profile().fps, 60);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(52, 52));
        QVERIFY(quality.observe(52, 52));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
    }

    void networkLossReducesBitrateWithoutReducingResolution()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        const int initial = quality.profile().bitrate();
        QVERIFY(!quality.observeNetwork(-1, -1));
        QVERIFY(!quality.observeNetwork(1000, 0));
        QVERIFY(!quality.observeNetwork(1980, 20));
        QVERIFY(quality.observeNetwork(2960, 40));
        QCOMPARE(quality.profile().bitrate(), initial * 85 / 100);
        QCOMPARE(quality.profile().fps, 60);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        const int reduced = quality.profile().bitrate();
        for (int i = 1; i <= 14; ++i)
            QVERIFY(!quality.observeNetwork(2960 + i * 1000, 40));
        QVERIFY(quality.observeNetwork(17960, 40));
        QVERIFY(quality.profile().bitrate() > reduced);
        QVERIFY(quality.profile().bitrate() < initial);
    }

    void networkCountersResetAndSparseTrafficDoNotTriggerCongestion()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        const int initial = quality.profile().bitrate();
        const auto maximum = std::numeric_limits<qint64>::max();
        QVERIFY(!quality.observeNetwork(maximum, maximum));
        QVERIFY(!quality.observeNetwork(1000, 10));
        QVERIFY(!quality.observeNetwork(1980, 30));
        QVERIFY(!quality.observeNetwork(1981, 31));
        QVERIFY(!quality.observeNetwork(2961, 51));
        QCOMPARE(quality.profile().bitrate(), initial);
        QVERIFY(!quality.observeNetwork(10, 0));
        QVERIFY(!quality.observeNetwork(990, 20));
        QCOMPARE(quality.profile().bitrate(), initial);
        QVERIFY(quality.observeNetwork(1970, 40));
        qint64 received = 1970, lost = 40;
        for (int i = 0; i < 100; ++i)
            quality.observeNetwork(received += 900, lost += 100);
        QCOMPARE(quality.profile().bitrate(), 2000000);
    }
    void transportDelayReducesBudgetWithoutChangingNativeProfile()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        const int initial = quality.profile().bitrate();
        QVERIFY(!quality.observeNetwork(1000, 0, 100));
        QVERIFY(quality.observeNetwork(2000, 0, 100));
        QCOMPARE(quality.profile().bitrate(), initial * 85 / 100);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 60);
        const int reduced = quality.profile().bitrate();
        for (int i = 1; i <= 20; ++i)
            QVERIFY(!quality.observeNetwork(2000 + i * 1000, 0, 40));
        QCOMPARE(quality.profile().bitrate(), reduced);
        for (int i = 1; i <= 14; ++i)
            QVERIFY(!quality.observeNetwork(22000 + i * 1000, 0, 10));
        QVERIFY(quality.observeNetwork(37000, 0, 10));
        QVERIFY(quality.profile().bitrate() > reduced);
    }

    void delaySpikesAndUnsupportedPeersDoNotReduceBudget()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        const int initial = quality.profile().bitrate();
        for (double delay : {100., 10., 100., -1., 100., 10.})
            QVERIFY(!quality.observeNetwork(-1, -1, delay));
        QCOMPARE(quality.profile().bitrate(), initial);
        QVERIFY(!quality.observeNetwork(-1, -1, 100));
        QVERIFY(quality.observeNetwork(-1, -1, 100));
        QCOMPARE(quality.profile().bitrate(), initial * 85 / 100);
        for (int i = 0; i < 15; ++i)
            quality.observeNetwork(-1, -1, 10);
        QVERIFY(quality.profile().bitrate() > initial * 85 / 100);
    }

    void queuedTransportDoesNotTriggerFrameRateUpgrade()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        quality.observe(10, 10);
        QVERIFY(quality.observe(10, 10));
        QCOMPARE(quality.profile().fps, 50);
        for (int i = 0; i < 30; ++i)
        {
            quality.observeNetwork(-1, -1, 40);
            QVERIFY(!quality.observe(50, 50));
        }
        QCOMPARE(quality.profile().fps, 50);
        quality.observeNetwork(-1, -1, -1);
        for (int i = 0; i < 29; ++i)
            QVERIFY(!quality.observe(50, 50));
        QVERIFY(quality.observe(50, 50));
        QCOMPARE(quality.profile().fps, 60);
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
