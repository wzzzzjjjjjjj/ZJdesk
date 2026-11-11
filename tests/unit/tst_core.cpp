#include "diagnostics/event_log.h"
#include "platform/platform_info.h"
#include "platform/camera_permission_p.h"
#include "protocol/message.h"
#include "protocol/annotation.h"
#include "remote-control/control_session.h"
#include "remote-control/input_receiver.h"
#include "session/session_coordinator.h"
#include "media/share_quality.h"
#include "media/congestion_budget.h"
#include "media/test_frame_marker.h"
#include <QFile>
#include <QBuffer>
#include <QScopeGuard>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <limits>
#include <algorithm>
#include <QtTest>
#include "remote-control/input_injector_win_p.h"

class CoreTests : public QObject
{
    Q_OBJECT
private slots:
    void latencyFrameMarkerRejectsDamageAndPreservesEpoch()
    {
        QImage image(640, 480, QImage::Format_RGB888);
        image.fill(Qt::gray);
        QVERIFY(!zjdesk::TestFrameMarker::read(image));
        for (quint32 sequence : {quint32(1), quint32(0x80000000), quint32(0xffffffff)})
        {
            zjdesk::TestFrameMarker::draw(image, {0xabcdef01, sequence});
            const auto decoded = zjdesk::TestFrameMarker::read(image);
            QVERIFY(decoded);
            QCOMPARE(decoded->epoch, quint32(0xabcdef01));
            QCOMPARE(decoded->sequence, sequence);
        }
        QPainter painter(&image);
        painter.fillRect(0, 0, 8, 8, Qt::gray);
        painter.end();
        QVERIFY(!zjdesk::TestFrameMarker::read(image));
        zjdesk::TestFrameMarker::draw(image, {0, 1});
        QVERIFY(!zjdesk::TestFrameMarker::read(image));
        QVERIFY(!zjdesk::TestFrameMarker::read(QImage(10, 10, QImage::Format_RGB32)));
    }

    void annotationBoundsAndTools()
    {
        using namespace zjdesk::protocol;
        QJsonArray singlePoint;
        singlePoint.append(QJsonArray{0, 1});
        QJsonObject stroke{{"strokeId", "stroke1"}, {"tool", "pen"}, {"points", singlePoint}};
        QVERIFY(isAnnotationStrokeValid(stroke));
        QJsonArray points;
        for (int i = 0; i < MaximumAnnotationPoints; ++i) points.append(QJsonArray{0.5, 0.5});
        stroke.insert("points", points);
        QVERIFY(isAnnotationStrokeValid(stroke));
        points.append(QJsonArray{0.5, 0.5});
        stroke.insert("points", points);
        QVERIFY(!isAnnotationStrokeValid(stroke));
        for (const QString &tool : {QString("circle"), QString("arrow")})
        {
            stroke.insert("tool", tool);
            stroke.insert("points", QJsonArray{QJsonArray{0.2, 0.2}, QJsonArray{0.8, 0.8}});
            QVERIFY(isAnnotationStrokeValid(stroke));
            stroke.insert("points", QJsonArray{QJsonArray{0.2, 0.2}});
            QVERIFY(!isAnnotationStrokeValid(stroke));
        }
        stroke.insert("tool", "pen");
        for (const auto &invalid : {QJsonArray{QJsonArray{-0.01, 0.5}}, QJsonArray{QJsonArray{0.5, 1.01}},
                QJsonArray{QJsonArray{"0.5", 0.5}}, QJsonArray{QJsonArray{0.5, 0.5, 0.5}}, QJsonArray{}})
        {
            stroke.insert("points", invalid);
            QVERIFY(!isAnnotationStrokeValid(stroke));
        }
    }

    void inputReceiverRejectsStaleDuplicateAndRevokedInput()
    {
        zjdesk::InputReceiver receiver;
        QSignalSpy processed(&receiver, &zjdesk::InputReceiver::processed);
        const auto receive = [&](quint64 sequence, quint64 controlEpoch = 42, quint64 shareEpoch = 7) {
            receiver.receive(shareEpoch, {{"action", "input"}, {"epoch", QString::number(controlEpoch)},
                {"sequence", QString::number(sequence)}, {"kind", "unsupported-test-event"}, {"probe", true}});
        };
        receive(1); // Before local permission.
        receiver.authorize(7, 42);
        receive(1, 41);
        receive(1, 42, 6);
        receive(0);
        receive(1);
        QTRY_COMPARE(processed.size(), 1);
        receive(1); // A duplicate must not execute again.
        receiver.authorize(7, 42); // A retransmitted grant must not reset ordering.
        receive(1);
        receiver.revoke(6); // Closing a stale share must not revoke the new one.
        receive(2);
        QTRY_COMPARE(processed.size(), 2);
        receiver.receive(7, {{"action", "revoke"}});
        receive(3);
        receiver.authorize(8, 43);
        receive(4);
        receive(1, 43, 8);
        QTRY_COMPARE(processed.size(), 3);
        QCOMPARE(processed.last().at(1).toJsonObject().value("sequence").toString(), QString("1"));
        receiver.revoke();
        receive(2, 43, 8);
        // A fresh grant and valid event act as an ordering barrier for the rejection above.
        receiver.authorize(9, 44);
        receive(1, 44, 9);
        QTRY_COMPARE(processed.size(), 4);
    }

    void shareQualityUsesBothDisplayLimits()
    {
        using Quality = zjdesk::ShareQuality;
        QCOMPARE(Quality::compatibleMaximumFps(144, 60), 60);
        QCOMPARE(Quality::compatibleMaximumFps(60, 144), 60);
        QCOMPARE(Quality::compatibleMaximumFps(144, 120), 120);
        QCOMPARE(Quality::compatibleMaximumFps(0, 30), 30);
        QCOMPARE(Quality::compatibleMaximumFps(500, 0), 240);
        QCOMPARE(Quality::compatibleMaximumFps(144, -1), 144);
        QCOMPARE(Quality::compatibleMaximumFps(144, 500), 144);
        zjdesk::ShareQuality quality(QSize(2560, 1440), Quality::compatibleMaximumFps(144, 60));
        quality.observe(1, 1);
        QVERIFY(quality.observe(1, 1));
        for (int i = 0; i < 120; ++i) quality.observe(240, 240);
        QCOMPARE(quality.profile().fps, 60);
        QCOMPARE(quality.profile().size, QSize(2560, 1440));
    }

#ifdef Q_OS_WIN
    void windowsSystemCursorImages_data()
    {
        QTest::addColumn<int>("resource");
        QTest::newRow("arrow") << 32512;
        QTest::newRow("text") << 32513;
        QTest::newRow("wait") << 32514;
        QTest::newRow("hand") << 32649;
    }

    void windowsSystemCursorImages()
    {
        QFETCH(int, resource);
        const auto cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(resource));
        QVERIFY(cursor);
        ICONINFO icon{};
        QVERIFY(GetIconInfo(cursor, &icon));
        const auto cleanup = qScopeGuard([&] {
            if (icon.hbmColor) DeleteObject(icon.hbmColor);
            if (icon.hbmMask) DeleteObject(icon.hbmMask);
        });
        const auto image = zjdesk::windowsCursorImage(cursor, icon);
        QVERIFY(!image.isNull());
        QVERIFY(image.width() <= 128 && image.height() <= 128);
        QVERIFY(image.rect().contains(QPoint(int(icon.xHotspot), int(icon.yHotspot))));
        bool visible = false, transparent = false;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
            {
                const int alpha = image.pixelColor(x, y).alpha();
                visible |= alpha > 0;
                transparent |= alpha == 0;
            }
        QVERIFY(visible && transparent);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        QVERIFY(png.size() <= 2400); // Must fit the existing cursor shape message.
        QCOMPARE(QImage::fromData(png).size(), image.size());
    }

    void windowsMonochromeCursorPreservesMask()
    {
        BYTE andMask[32], xorMask[32]{};
        std::fill_n(andMask, 32, BYTE(0xff));
        andMask[0] = 0x3f;
        xorMask[0] = 0x40;
        xorMask[8] = 0x02; // Invert the background at (6, 4).
        const auto cursor = CreateCursor(nullptr, 1, 2, 16, 16, andMask, xorMask);
        QVERIFY(cursor);
        const auto destroy = qScopeGuard([&] { DestroyCursor(cursor); });
        ICONINFO icon{};
        QVERIFY(GetIconInfo(cursor, &icon));
        const auto cleanup = qScopeGuard([&] {
            if (icon.hbmColor) DeleteObject(icon.hbmColor);
            if (icon.hbmMask) DeleteObject(icon.hbmMask);
        });
        const auto image = zjdesk::windowsCursorImage(cursor, icon);
        QCOMPARE(image.size(), QSize(16, 16));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::black));
        QCOMPARE(image.pixelColor(1, 0), QColor(Qt::white));
        QCOMPARE(image.pixelColor(2, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(6, 4), QColor(Qt::black));
        QCOMPARE(image.pixelColor(5, 4), QColor(Qt::white));
        QCOMPARE(image.pixelColor(3, 4).alpha(), 0);
    }

    void windowsColorCursorPreservesAlpha()
    {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), 2, -2, 1, 32, BI_RGB};
        void *pixels = nullptr;
        const auto color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        QVERIFY(color);
        auto *rgba = static_cast<quint32 *>(pixels);
        rgba[0] = 0xffff0000;
        rgba[1] = 0x80ff0000; // Straight alpha source for CreateIconIndirect.
        rgba[2] = rgba[3] = 0;
        const BYTE mask[4]{};
        const auto bitmap = CreateBitmap(2, 2, 1, 1, mask);
        const auto cleanup = qScopeGuard([&] { DeleteObject(color); if (bitmap) DeleteObject(bitmap); });
        QVERIFY(bitmap);
        ICONINFO icon{};
        icon.hbmColor = color;
        icon.hbmMask = bitmap;
        const auto cursor = HCURSOR(CreateIconIndirect(&icon));
        QVERIFY(cursor);
        const auto destroy = qScopeGuard([&] { DestroyCursor(cursor); });
        const auto image = zjdesk::windowsCursorImage(cursor, icon);
        QCOMPARE(image.size(), QSize(2, 2));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(image.pixelColor(1, 0), QColor(255, 0, 0, 128));
        QCOMPARE(image.pixelColor(0, 1).alpha(), 0);
    }

    void windowsInputTranslatesKeys_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("native");
        QTest::addColumn<bool>("extended");
        QTest::newRow("letter") << int(Qt::Key_A) << int('A') << false;
        QTest::newRow("digit") << int(Qt::Key_7) << int('7') << false;
        QTest::newRow("function") << int(Qt::Key_F12) << int(VK_F12) << false;
        QTest::newRow("control") << int(Qt::Key_Control) << int(VK_LCONTROL) << false;
        QTest::newRow("shift") << int(Qt::Key_Shift) << int(VK_LSHIFT) << false;
        QTest::newRow("alt") << int(Qt::Key_Alt) << int(VK_LMENU) << false;
        QTest::newRow("windows") << int(Qt::Key_Meta) << int(VK_LWIN) << true;
        QTest::newRow("return") << int(Qt::Key_Return) << int(VK_RETURN) << false;
        QTest::newRow("tab") << int(Qt::Key_Tab) << int(VK_TAB) << false;
        QTest::newRow("delete") << int(Qt::Key_Delete) << int(VK_DELETE) << true;
        QTest::newRow("left") << int(Qt::Key_Left) << int(VK_LEFT) << true;
        QTest::newRow("home") << int(Qt::Key_Home) << int(VK_HOME) << true;
        QTest::newRow("page-down") << int(Qt::Key_PageDown) << int(VK_NEXT) << true;
        QTest::newRow("num-lock") << int(Qt::Key_NumLock) << int(VK_NUMLOCK) << true;
    }

    void windowsInputTranslatesKeys()
    {
        QFETCH(int, key);
        QFETCH(int, native);
        QFETCH(bool, extended);
        for (bool pressed : {true, false})
        {
            const auto events = zjdesk::windowsInputEvents({{"kind", "key"}, {"key", key}, {"pressed", pressed}}, {}, {});
            QCOMPARE(events.size(), 1);
            const auto &event = events.first();
            QCOMPARE(event.type, DWORD(INPUT_KEYBOARD));
            QCOMPARE(event.ki.wVk, WORD(native));
            QVERIFY(event.ki.wScan != 0);
            QVERIFY(event.ki.dwFlags & KEYEVENTF_SCANCODE);
            QCOMPARE(bool(event.ki.dwFlags & KEYEVENTF_EXTENDEDKEY), extended);
            QCOMPARE(bool(event.ki.dwFlags & KEYEVENTF_KEYUP), !pressed);
        }
    }

    void windowsInputUsesPhysicalPrimaryInsideVirtualDesktop()
    {
        const QRect screen(0, 0, 3840, 2160), desktop(-1920, -1080, 5760, 3240);
        auto events = zjdesk::windowsInputEvents({{"kind", "move"}, {"x", 0}, {"y", 0}}, screen, desktop);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().mi.dx, qRound(1920.0 * 65535 / 5759));
        QCOMPARE(events.first().mi.dy, qRound(1080.0 * 65535 / 3239));
        QCOMPARE(events.first().mi.dwFlags, DWORD(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK));
        events = zjdesk::windowsInputEvents({{"kind", "move"}, {"x", 1}, {"y", 1}}, screen, desktop);
        QCOMPARE(events.first().mi.dx, LONG(65535));
        QCOMPARE(events.first().mi.dy, LONG(65535));
        for (const auto button : {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton})
        {
            const auto down = zjdesk::windowsInputEvents({{"kind", "button"}, {"button", int(button)},
                {"pressed", true}, {"x", 0.5}, {"y", 0.5}}, screen, desktop);
            const auto up = zjdesk::windowsInputEvents({{"kind", "button"}, {"button", int(button)},
                {"pressed", false}, {"x", 0.5}, {"y", 0.5}}, screen, desktop);
            QCOMPARE(down.size(), 2);
            QCOMPARE(up.size(), 2);
            QCOMPARE(down.last().mi.dwFlags << 1, up.last().mi.dwFlags);
        }
        for (int steps : {-10, -1, 1, 10})
        {
            events = zjdesk::windowsInputEvents({{"kind", "wheel"}, {"steps", steps}, {"x", 0.5}, {"y", 0.5}}, screen, desktop);
            QCOMPARE(events.size(), 2);
            QCOMPARE(events.last().mi.dwFlags, DWORD(MOUSEEVENTF_WHEEL));
            QCOMPARE(LONG(events.last().mi.mouseData), LONG(steps * WHEEL_DELTA));
        }
    }

    void windowsSpecialKeysUseVirtualKeySequences()
    {
        for (unsigned int key : {VK_PAUSE, VK_SNAPSHOT})
            for (bool pressed : {true, false})
            {
                const auto event = zjdesk::windowsKeyEvent(key, pressed);
                QCOMPARE(event.ki.wVk, WORD(key));
                QVERIFY(!(event.ki.dwFlags & KEYEVENTF_SCANCODE));
                QCOMPARE(bool(event.ki.dwFlags & KEYEVENTF_KEYUP), !pressed);
            }
    }

    void windowsInputRejectsMalformedEventsWithoutMovingPointer()
    {
        const QRect screen(0, 0, 1920, 1080);
        for (const QJsonObject &input : QList<QJsonObject>{
                {{"kind", "move"}, {"x", -0.1}, {"y", 0}},
                {{"kind", "move"}, {"x", 1.1}, {"y", 0}},
                {{"kind", "move"}, {"x", "0.5"}, {"y", 0}},
                {{"kind", "button"}, {"x", 0}, {"y", 0}, {"button", int(Qt::LeftButton)}},
                {{"kind", "button"}, {"x", 0}, {"y", 0}, {"button", 999}, {"pressed", true}},
                {{"kind", "wheel"}, {"x", 0}, {"y", 0}, {"steps", 0}},
                {{"kind", "wheel"}, {"x", 0}, {"y", 0}, {"steps", 11}},
                {{"kind", "wheel"}, {"x", 0}, {"y", 0}, {"steps", std::numeric_limits<int>::min()}},
                {{"kind", "key"}, {"key", int(Qt::Key_A)}},
                {{"kind", "key"}, {"key", int(Qt::Key_unknown)}, {"pressed", true}},
                {{"kind", "unknown"}}})
            QVERIFY(zjdesk::windowsInputEvents(input, screen, screen).isEmpty());
    }
#endif

    void desktopCameraConsent_data()
    {
        QTest::addColumn<QString>("deviceConsent");
        QTest::addColumn<QString>("desktopConsent");
        QTest::addColumn<zjdesk::CameraPermission>("expected");
        using Permission = zjdesk::CameraPermission;
        QTest::newRow("both-enabled") << QStringLiteral("Allow") << QStringLiteral("Allow") << Permission::Allowed;
        QTest::newRow("device-disabled") << QStringLiteral("Deny") << QStringLiteral("Allow") << Permission::Denied;
        QTest::newRow("desktop-disabled") << QStringLiteral("Allow") << QStringLiteral("Deny") << Permission::Denied;
        QTest::newRow("both-disabled") << QStringLiteral("Deny") << QStringLiteral("Deny") << Permission::Denied;
        QTest::newRow("missing-device") << QString{} << QStringLiteral("Allow") << Permission::Unknown;
        QTest::newRow("missing-desktop") << QStringLiteral("Allow") << QString{} << Permission::Unknown;
        QTest::newRow("missing-both") << QString{} << QString{} << Permission::Unknown;
        QTest::newRow("unrecognized-consent") << QStringLiteral("Prompt") << QStringLiteral("Allow") << Permission::Unknown;
        QTest::newRow("denial-with-unknown") << QString{} << QStringLiteral("Deny") << Permission::Denied;
    }

    void desktopCameraConsent()
    {
        QFETCH(QString, deviceConsent);
        QFETCH(QString, desktopConsent);
        QFETCH(zjdesk::CameraPermission, expected);
        QCOMPARE(zjdesk::detail::desktopCameraPermission(deviceConsent, desktopConsent), expected);
    }

    void cameraPermissionRequestReflectsSystemAccess()
    {
        zjdesk::SessionCoordinator session;
        QSignalSpy required(&session, &zjdesk::SessionCoordinator::cameraPermissionRequired);
        session.requestCameraPermission();
        const auto permission = zjdesk::cameraPermission();
        QCOMPARE(session.cameraPermissionStatus(), permission);
        QCOMPARE(required.size(), permission == zjdesk::CameraPermission::Denied ? 1 : 0);
        QCOMPARE(session.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
        session.requestCameraPermission();
        QCOMPARE(required.size(), permission == zjdesk::CameraPermission::Denied ? 2 : 0);
    }

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

    void lowBandwidthQualityCapsAndRecoversWithinBudget()
    {
        zjdesk::ShareQuality quality(QSize(3840, 2160), 144, 24, zjdesk::ShareQuality::Mode::LowBandwidth);
        QCOMPARE(quality.profile().size, QSize(960, 540));
        QCOMPARE(quality.profile().fps, 15);
        QCOMPARE(quality.profile().bitrate(), 800000);
        for (int i = 0; i < 60; ++i) quality.observeNetwork(-1, -1, 300);
        QCOMPARE(quality.profile().bitrate(), 128000);
        for (int i = 0; i < 300; ++i) quality.observeNetwork(-1, -1, 0);
        QCOMPARE(quality.profile().bitrate(), 800000);
        for (int i = 0; i < 60; ++i) quality.observe(0, 0);
        QCOMPARE(quality.profile().size, QSize(640, 360));
        QCOMPARE(quality.profile().fps, 5);
        for (int i = 0; i < 300; ++i) quality.observe(60, 60);
        QVERIFY(quality.profile().size.width() <= 960);
        QVERIFY(quality.profile().fps <= 15);
        QVERIFY(quality.profile().bitrate() <= 800000);
        zjdesk::ShareQuality small(QSize(320, 240), 60, 24, zjdesk::ShareQuality::Mode::LowBandwidth);
        QCOMPARE(small.profile().size, QSize(320, 240));
    }

    void wideAreaQualityTargets1080p30AndRecovers()
    {
        using Mode = zjdesk::ShareQuality::Mode;
        zjdesk::ShareQuality quality(QSize(3840, 2160), 144, 24, Mode::WideArea);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 30);
        const int bitrate = quality.profile().bitrate();
        QVERIFY(bitrate >= 4000000 && bitrate <= 12000000);
        for (int i = 0; i < 100; ++i) quality.observeNetwork(-1, -1, 300);
        QCOMPARE(quality.profile().bitrate(), 128000);
        for (int i = 0; i < 900; ++i) quality.observeNetwork(-1, -1, 0);
        QCOMPARE(quality.profile().bitrate(), bitrate);
        QVERIFY(!quality.observe(1, 1));
        QVERIFY(quality.observe(1, 1));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 24);
        QVERIFY(!quality.observe(1, 1));
        QVERIFY(quality.observe(1, 1));
        QCOMPARE(quality.profile().size, QSize(1600, 900));
        for (int i = 0; i < 60; ++i) quality.observe(0, 0);
        QCOMPARE(quality.profile().size, QSize(640, 360));
        QCOMPARE(quality.profile().fps, 5);
        for (int i = 0; i < 900; ++i) quality.observe(30, 30);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 30);
        zjdesk::ShareQuality small(QSize(320, 240), 15, 24, Mode::WideArea);
        QCOMPARE(small.profile().size, QSize(320, 240));
        QCOMPARE(small.profile().fps, 15);
        zjdesk::ShareQuality portrait(QSize(1080, 1920), 60, 24, Mode::WideArea);
        QCOMPARE(portrait.profile().size, QSize(606, 1080));
    }

    void wideAreaBudgetStartsConservativelyAndHonorsExplicitCap()
    {
        const auto budget = zjdesk::CongestionBudget::fromMaximum({}, 4147200, true);
        QVERIFY(budget);
        QCOMPARE(budget->minimum, 128000);
        QCOMPARE(budget->initial, 4000000);
        QCOMPARE(budget->maximum, 12000000);
        for (const auto &value : {"128000", "512000", "8000000", "40000000"})
        {
            const auto capped = zjdesk::CongestionBudget::fromMaximum(value, 4147200, true);
            QVERIFY(capped);
            QCOMPARE(capped->minimum, 128000);
            QCOMPARE(capped->maximum, QString(value).toInt());
            QCOMPARE(capped->initial, qMin(4000000, capped->maximum));
        }
        QVERIFY(!zjdesk::CongestionBudget::fromMaximum("invalid", 4147200, true));
    }

    void shareQualityPreservesResolutionAndAvoidsOscillation()
    {
        zjdesk::ShareQuality quality(QSize(3840, 2160));
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, 60);
        QVERIFY(!quality.observe(60, 60));
        for (int i = 0; i < 3; ++i)
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
        for (int i = 0; i < 14; ++i)
            QVERIFY(!quality.observe(60, 60));
        QVERIFY(quality.observe(60, 60));
        QCOMPARE(quality.profile().size, QSize(3840, 2160));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
    }

    void cameraQualityPreservesClarityBeforeReducingSize()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080), 60, 15);
        QCOMPARE(quality.profile().fps, 60);
        QVERIFY(!quality.observe(1, 1));
        QVERIFY(quality.observe(1, 1));
        QCOMPARE(quality.profile().fps, 50);
        while (quality.profile().fps > 15)
        {
            QCOMPARE(quality.profile().size, QSize(1920, 1080));
            QVERIFY(!quality.observe(1, 1));
            QVERIFY(quality.observe(1, 1));
        }
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QVERIFY(!quality.observe(1, 1));
        QVERIFY(quality.observe(1, 1));
        QCOMPARE(quality.profile().size, QSize(1600, 900));
        for (int i = 0; i < 14; ++i) QVERIFY(!quality.observe(60, 60));
        QVERIFY(quality.observe(60, 60));
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 15);
        for (int i = 0; i < 14; ++i) QVERIFY(!quality.observe(15, 15));
        QVERIFY(quality.observe(15, 15));
        QCOMPARE(quality.profile().fps, 20);
    }

    void shareQualityRestoresResolutionFromLowerFrameRate()
    {
        zjdesk::ShareQuality quality(QSize(1920, 1080));
        for (int i = 0; i < 34; ++i)
            quality.observe(1, 1);
        QCOMPARE(quality.profile().size, QSize(1280, 720));
        QCOMPARE(quality.profile().fps, zjdesk::ShareQuality::MinimumFps);
        for (int i = 0; i < 14; ++i)
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
        for (int i = 0; i < 3; ++i)
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
        for (int i = 0; i < 3; ++i) QVERIFY(!quality.observe(60, 45));
        QVERIFY(quality.observe(60, 45));
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
        for (int i = 0; i < 14; ++i)
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
        QVERIFY(!quality.observeNetwork(1000, 0, 300));
        QVERIFY(!quality.observeNetwork(2000, 0, 300));
        QVERIFY(quality.observeNetwork(3000, 0, 300));
        QCOMPARE(quality.profile().bitrate(), initial * 85 / 100);
        QCOMPARE(quality.profile().size, QSize(1920, 1080));
        QCOMPARE(quality.profile().fps, 60);
        const int reduced = quality.profile().bitrate();
        for (int i = 1; i <= 20; ++i)
            QVERIFY(!quality.observeNetwork(2000 + i * 1000, 0, 100));
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
        QVERIFY(!quality.observeNetwork(-1, -1, 300));
        QVERIFY(!quality.observeNetwork(-1, -1, 300));
        QVERIFY(quality.observeNetwork(-1, -1, 300));
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
            quality.observeNetwork(-1, -1, 100);
            QVERIFY(!quality.observe(50, 50));
        }
        QCOMPARE(quality.profile().fps, 50);
        quality.observeNetwork(-1, -1, -1);
        for (int i = 0; i < 14; ++i)
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
