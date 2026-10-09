#include "main_window.h"
#include "video_surface.h"
#include "signaling/room_server.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QScreen>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QProcess>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScopeGuard>
#include <QVBoxLayout>
#include <QFocusEvent>
#include <QSpinBox>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <ctime>
#include <algorithm>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
// Slightly lossy 4:4:4: preserve native pixels and bound RGB error on text/one-pixel colour lines.
constexpr int MaximumDesktopChannelError = 24;
constexpr double MaximumDesktopMeanError = 2;

void waitForPresentation(int milliseconds)
{
    // Run the normal event dispatcher; QtTest's polling sleeps bias high-FPS measurements.
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QJsonObject shareDiagnostics(const zjdesk::ShareStats &stats)
{
    return {{"codec", stats.codec}, {"hardwareEncoder", stats.hardwareEncoder},
        {"fallbackReason", stats.codecFallbackReason}, {"bitrateBudget", stats.encoderBitrate},
        {"hardwareQuantizer", stats.encoderQuantizer}, {"targetFps", stats.targetFps},
        {"sendBufferBytes", stats.sendBufferBytes}, {"receiveBufferBytes", stats.receiveBufferBytes},
        {"rtcpFeedbackBudget", stats.rtcpFeedbackBudget}, {"transport", stats.transport},
        {"processedFps", stats.processedFps}, {"deliveredFps", stats.deliveredFps},
        {"codecMeanMs", stats.codecMeanMs}, {"codecMaxMs", stats.codecMaxMs},
        {"outputGapMaxMs", stats.outputGapMaxMs}, {"copyMaxMs", stats.copyMaxMs},
        {"maxEncodedFrameBytes", qint64(stats.maxEncodedFrameBytes)},
        {"maxKeyframeBytes", qint64(stats.maxKeyframeBytes)}, {"keyframes", qint64(stats.keyframes)},
        {"overwrittenFrames", qint64(stats.overwrittenFrames)},
        {"width", stats.size.width()}, {"height", stats.size.height()}};
}

double processCpuSeconds()
{
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0;
    const quint64 ticks = (quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime
        + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime;
    return ticks / 10000000.0;
#else
    return double(std::clock()) / CLOCKS_PER_SEC;
#endif
}

class MovingDesktop final : public QWidget
{
public:
    MovingDesktop() : QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint)
    {
        connect(&timer_, &QTimer::timeout, this, [this] { ++phase_; update(); });
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.start(16);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#e5efff"));
        for (int y = 0; y < height(); y += 50)
            for (int x = -50; x < width(); x += 100)
                painter.fillRect(QRect(x + phase_ % 100, y, 50, 50), QColor("#176ee8"));
        painter.setPen(Qt::black);
        painter.drawText(rect(), Qt::AlignCenter, QString("Moving desktop frame %1").arg(phase_));
        const auto ratio = devicePixelRatioF();
        QImage detail(qRound(width() * ratio), qRound(64 * ratio), QImage::Format_RGB32);
        detail.fill(Qt::white);
        QPainter textPainter(&detail);
        auto font = textPainter.font();
        font.setPixelSize(qRound(12 * ratio));
        textPainter.setFont(font);
        textPainter.setPen(Qt::black);
        textPainter.drawText(detail.rect().adjusted(8, 4, -8, -16), Qt::AlignVCenter,
            QStringLiteral("AaBbCc 0123456789 thin text 中文细字"));
        textPainter.end();
        for (int x = 0; x < detail.width(); ++x)
            for (int y = detail.height() - 8; y < detail.height(); ++y)
                detail.setPixelColor(x, y, x % 3 == 0 ? Qt::red : x % 3 == 1 ? Qt::green : Qt::blue);
        detail.setDevicePixelRatio(ratio);
        painter.drawImage(QPointF(0, height() - 64), detail);
    }
private:
    QTimer timer_;
    int phase_ = 0;
};
} // namespace

class WindowTests : public QObject
{
    Q_OBJECT
private slots:
    void physicalDesktopAcrossMachines()
    {
        if (qEnvironmentVariable("ZJDESK_PHYSICAL_RECEIVER") != "1")
            QSKIP("Explicit physical desktop validation only");
        MainWindow window(qEnvironmentVariable("ZJDESK_TEST_SERVER"), "physical-first-frame", "Windows");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QPushButton *>("primaryButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(window.session_.memberCount(), 2, 30000);
        auto *surface = window.findChild<VideoSurface *>();
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            QElapsedTimer startup;
            startup.start();
            QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(), 15000);
            const auto firstFrameMs = startup.elapsed();
            QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareStats().transport, QString("TCP"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(window.session_.shareStats().processedFps > 0, 5000);
            waitForPresentation(2000);
            const auto stats = window.session_.shareStats();
            QCOMPARE(stats.size, QSize(1920, 1080));
            QVERIFY(stats.deliveredFps > 0);
            QVERIFY(stats.presentedFps > 0);
            QVERIFY(!surface->grabFramebuffer().isNull());
            qInfo().noquote() << "Physical desktop cycle" << cycle + 1 << "first frame ms" << firstFrameMs
                << QJsonDocument(shareDiagnostics(stats)).toJson(QJsonDocument::Compact);
            window.session_.stopScreenShare();
            QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareState(), zjdesk::SessionCoordinator::PreviewState::Stopped, 5000);
            QVERIFY(!surface->hasFrame());
        }
        window.session_.leave();
    }

    void remoteControlledDesktopFixture()
    {
        if (qEnvironmentVariable("ZJDESK_REMOTE_FIXTURE") != "1")
            QSKIP("Explicit two-machine validation only");
        QDir().mkpath("output");
        QFile::remove("output/remote-revoke");
        QFile::remove("output/remote-completed");
        const auto server = qEnvironmentVariable("ZJDESK_TEST_SERVER");
        MainWindow window(server, "remote-test", "Linux");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QPushButton *>("primaryButton")->click();
        auto *share = window.findChild<QPushButton *>("shareButton");
        auto *allow = window.findChild<QPushButton *>("allowControlButton");
        auto *stop = window.findChild<QPushButton *>("stopControlButton");
        auto *status = window.findChild<QLabel *>("controlStatus");
        QTRY_VERIFY_WITH_TIMEOUT(share->isEnabled(), 120000);
        QWidget target(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
        target.setWindowTitle("ZJdesk remote input validation");
        target.setGeometry(50, 50, 600, 300);
        auto *layout = new QVBoxLayout(&target);
        QLineEdit field;
        QPushButton button("Remote click target");
        QLabel result("Waiting for Windows input");
        result.setMinimumHeight(80);
        result.setAlignment(Qt::AlignCenter);
        result.setStyleSheet("background: #ff00ff; color: white;");
        QSpinBox wheel;
        wheel.setValue(10);
        QSlider drag(Qt::Horizontal);
        drag.setRange(0, 100);
        layout->addWidget(&field);
        layout->addWidget(&button);
        layout->addWidget(&wheel);
        layout->addWidget(&drag);
        layout->addWidget(&result);
        int clicks = 0;
        QElapsedTimer fixtureClock;
        fixtureClock.start();
        qint64 injectedAtUs = -1;
        qint64 uiDispatchUs = -1;
        quint64 probeSequence = 0;
        bool completed = false;
        bool repeatGrant = false;
        connect(&window.session_, &zjdesk::SessionCoordinator::inputProbeInjected, this,
            [&](quint64 sequence, qint64, bool accepted) {
                if (accepted)
                {
                    probeSequence = sequence;
                    injectedAtUs = fixtureClock.nsecsElapsed() / 1000;
                }
            });
        connect(&button, &QPushButton::clicked, this, [&] {
            ++clicks;
            result.setText(QString::number(clicks));
            result.setStyleSheet(clicks % 2 ? "background: #00ff00; color: black;"
                                             : "background: #ff00ff; color: white;");
            if (injectedAtUs >= 0)
                uiDispatchUs = fixtureClock.nsecsElapsed() / 1000 - injectedAtUs;
        });
        target.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        MovingDesktop marker;
        marker.setGeometry(QGuiApplication::primaryScreen()->geometry().right() - 500, 60, 480, 280);
        marker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        QTimer observations;
        const auto screen = QGuiApplication::primaryScreen();
        connect(&observations, &QTimer::timeout, this, [&] {
            QDir().mkpath("output");
            const auto shareStats = window.session_.shareStats();
            const auto editPoint = field.mapToGlobal(field.rect().center());
            const auto clickPoint = button.mapToGlobal(button.rect().center());
            const auto resultPoint = result.mapToGlobal(QPoint(12, result.height() / 2));
            const auto wheelPoint = wheel.mapToGlobal(wheel.rect().center());
            const auto dragStart = drag.mapToGlobal(QPoint(8, drag.height() / 2));
            const auto dragEnd = drag.mapToGlobal(QPoint(drag.width() - 8, drag.height() / 2));
            if (repeatGrant && allow->isVisible())
                allow->click();
            if (QFile::exists("output/remote-revoke"))
            {
                stop->click();
                QFile::remove("output/remote-revoke");
            }
            QJsonObject data{{"text", field.text()}, {"clicks", clicks}, {"status", status->text()},
                {"probeSequence", QString::number(probeSequence)}, {"uiDispatchUs", uiDispatchUs},
                {"shareStatus", window.findChild<QLabel *>("shareStatus")->text()},
                {"sentMbps", shareStats.networkMbps}, {"webRtcRttMs", shareStats.webRtcRttMs},
                {"media", shareDiagnostics(shareStats)},
                {"controlHeld", QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ControlModifier)},
                {"width", screen->size().width()}, {"height", screen->size().height()},
                {"fieldX", editPoint.x()}, {"fieldY", editPoint.y()},
                {"buttonX", clickPoint.x()}, {"buttonY", clickPoint.y()},
                {"resultX", resultPoint.x()}, {"resultY", resultPoint.y()},
                {"wheelX", wheelPoint.x()}, {"wheelY", wheelPoint.y()}, {"wheel", wheel.value()},
                {"dragX", dragStart.x()}, {"dragY", dragStart.y()},
                {"dragEndX", dragEnd.x()}, {"dragEndY", dragEnd.y()}, {"drag", drag.value()},
                {"markerX", marker.mapToGlobal(QPoint()).x()}, {"markerY", marker.mapToGlobal(QPoint()).y()},
                {"markerWidth", marker.width()}, {"markerHeight", marker.height()}};
            QSaveFile file("output/remote-observations.json");
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(QJsonDocument(data).toJson());
                file.commit();
            }
            completed = QFile::exists("output/remote-completed");
        });
        observations.start(100);
        QTRY_VERIFY_WITH_TIMEOUT(allow->isVisible(), 30000);
        QCOMPARE(share->text(), QStringLiteral("共享主屏幕"));
        QVERIFY(!window.findChild<VideoSurface *>()->hasFrame());
        stop->click();
        QTRY_VERIFY_WITH_TIMEOUT(allow->isVisible(), 30000);
        QCOMPARE(share->text(), QStringLiteral("共享主屏幕"));
        allow->click();
        repeatGrant = true;
        target.raise();
        target.activateWindow();
        QElapsedTimer completionClock;
        completionClock.start();
        while (!completed && completionClock.elapsed() < 240000)
            waitForPresentation(100);
        QVERIFY(completed);
        QVERIFY(clicks >= 1);
        QCOMPARE(wheel.value(), 11);
        QVERIFY(drag.value() >= 80);
        QCOMPARE(field.text(), QString("zjdesk123"));
        QCOMPARE(status->text(), QStringLiteral("远程控制未开启"));
        QVERIFY(target.screen()->grabWindow(target.winId()).save("output/remote-input-linux.png"));
        window.findChild<QPushButton *>("primaryButton")->click();
    }

    void remoteControlAcrossMachines()
    {
        if (qEnvironmentVariable("ZJDESK_REMOTE_CONTROLLER") != "1")
            QSKIP("Explicit two-machine validation only");
#ifdef Q_OS_WIN
        const auto executionState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        QVERIFY(executionState != 0);
        const auto restoreExecutionState = qScopeGuard([executionState] { SetThreadExecutionState(executionState); });
#endif
        const auto host = qEnvironmentVariable("ZJDESK_TEST_SSH");
        const auto root = qEnvironmentVariable("ZJDESK_TEST_LINUX_ROOT");
        QProcess observer;
        QJsonObject latest;
        QByteArray pending;
        connect(&observer, &QProcess::readyReadStandardOutput, this, [&] {
            pending += observer.readAllStandardOutput();
            int end;
            while ((end = pending.indexOf('\n')) >= 0)
            {
                const auto data = QJsonDocument::fromJson(pending.left(end));
                pending.remove(0, end + 1);
                if (data.isObject())
                    latest = data.object();
            }
        });
        observer.start("C:/Windows/System32/OpenSSH/ssh.exe", {"-o", "BatchMode=yes", host,
            "python3 -u -c \"import json,select,sys,pathlib; p=pathlib.Path('" + root
                + "/build/linux-release/output/remote-observations.json');\nwhile True:\n"
                  " if select.select([sys.stdin],[],[],0.1)[0]:\n"
                  "  command=sys.stdin.readline().strip()\n"
                  "  if command in ('remote-revoke','remote-completed'): (p.parent/command).touch()\n"
                  "  elif not command: break\n"
                  " try:\n  data=json.loads(p.read_text()); data['fixtureCompleted']=(p.parent/'remote-completed').exists()\n"
                  "  print(json.dumps(data),flush=True)\n except (OSError,ValueError): pass\""});
        const auto stopObserver = qScopeGuard([&] { observer.kill(); observer.waitForFinished(3000); });
        QVERIFY(observer.waitForStarted(5000));
        const auto observation = [&] { return latest; };
        QImage receivedDesktop;
        MainWindow window(qEnvironmentVariable("ZJDESK_TEST_SERVER"), "remote-test", "Windows");
        connect(&window.session_, &zjdesk::SessionCoordinator::shareFrame, &window,
            [&](const QImage &image) { receivedDesktop = image; });
        const auto failureReport = qScopeGuard([&] {
            if (QTest::currentTestFailed())
            {
                qWarning().noquote() << window.findChild<QPlainTextEdit *>()->toPlainText();
                qWarning().noquote() << observer.readAllStandardError();
                window.grab().save("output/remote-control-failed.png");
            }
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.findChild<QPushButton *>("primaryButton")->click();
        auto *request = window.findChild<QPushButton *>("controlButton");
        auto *status = window.findChild<QLabel *>("controlStatus");
        auto *surface = window.findChild<VideoSurface *>();
        QTRY_VERIFY_WITH_TIMEOUT(request->isEnabled(), 30000);
        request->click();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY(request->isEnabled());
        request->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("正在控制 Linux")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareStats().transport, QString("TCP"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(observation().contains("fieldX"), 10000);
        const auto normalSize = surface->size();
        auto *expand = window.findChild<QPushButton *>("expandVideoButton");
        expand->click();
        QTRY_VERIFY(window.isFullScreen());
        QTRY_VERIFY(surface->width() > normalSize.width() && surface->height() > normalSize.height());
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const auto data = observation();
        const auto point = [&](const char *x, const char *y) {
            const QSize source(data.value("width").toInt(), data.value("height").toInt());
            const auto size = source.scaled(surface->size(), Qt::KeepAspectRatio);
            return QPoint((surface->width() - size.width()) / 2
                    + qRound(data.value(x).toDouble() / source.width() * size.width()),
                (surface->height() - size.height()) / 2
                    + qRound(data.value(y).toDouble() / source.height() * size.height()));
        };
        if (qEnvironmentVariable("ZJDESK_LATENCY_TEST") == "1")
        {
            const QSize source(data.value("width").toInt(), data.value("height").toInt());
            QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareStats().size, source, 10000);
            QSignalSpy acknowledgements(&window.session_,
                &zjdesk::SessionCoordinator::inputProbeAcknowledged);
            QElapsedTimer timeline;
            QElapsedTimer frameGapClock;
            QJsonArray samples;
            QList<qint64> frameGapsUs;
            qint64 firstFrameUs = -1, visibleUs = -1;
            quint64 paintedBefore = 0;
            bool measuring = false;
            bool expectGreen = false;
            const auto frameConnection = connect(&window.session_, &zjdesk::SessionCoordinator::shareFrame, this,
                [&](const QImage &image) {
                    if (image.isNull())
                        return;
                    if (frameGapClock.isValid())
                        frameGapsUs.append(frameGapClock.nsecsElapsed() / 1000);
                    frameGapClock.restart();
                    if (!measuring || firstFrameUs >= 0)
                        return;
                    const auto pixel = image.pixelColor(data.value("resultX").toInt(),
                        data.value("resultY").toInt());
                    const bool matched = expectGreen
                        ? pixel.green() > 190 && pixel.red() < 90 && pixel.blue() < 90
                        : pixel.red() > 190 && pixel.blue() > 190 && pixel.green() < 90;
                    if (matched)
                    {
                        firstFrameUs = timeline.nsecsElapsed() / 1000;
                        paintedBefore = surface->presentedFrames();
                    }
                });
            const auto paintConnection = connect(surface, &QOpenGLWidget::frameSwapped, this, [&] {
                if (measuring && firstFrameUs >= 0 && visibleUs < 0
                    && surface->presentedFrames() > paintedBefore)
                    visibleUs = timeline.nsecsElapsed() / 1000;
            });
            const auto stopMeasurement = qScopeGuard([&] {
                QObject::disconnect(frameConnection);
                QObject::disconnect(paintConnection);
            });
            QTest::qWait(500);
            for (int i = 0; i < 12; ++i)
            {
                acknowledgements.clear();
                firstFrameUs = visibleUs = -1;
                expectGreen = i % 2 == 0;
                const double x = data.value("buttonX").toDouble() / (source.width() - 1);
                const double y = data.value("buttonY").toDouble() / (source.height() - 1);
                timeline.start();
                measuring = true;
                window.session_.sendRemoteInput({{"kind", "button"}, {"button", int(Qt::LeftButton)},
                    {"x", x}, {"y", y}, {"pressed", true}, {"probe", true}});
                window.session_.sendRemoteInput({{"kind", "button"}, {"button", int(Qt::LeftButton)},
                    {"x", x}, {"y", y}, {"pressed", false}});
                QTRY_VERIFY_WITH_TIMEOUT(visibleUs >= 0 && !acknowledgements.isEmpty()
                    && observation().value("probeSequence").toString()
                        == QString::number(acknowledgements.first().at(0).toULongLong()), 10000);
                measuring = false;
                const auto ack = acknowledgements.first();
                QVERIFY(ack.at(3).toBool());
                const auto network = window.session_.shareStats();
                samples.append(QJsonObject{{"sequence", QString::number(ack.at(0).toULongLong())},
                    {"inputRoundTripUs", ack.at(1).toLongLong()}, {"linuxInjectUs", ack.at(2).toLongLong()},
                    {"linuxUiDispatchUs", observation().value("uiDispatchUs").toInteger()},
                    {"firstDecodedFrameUs", firstFrameUs}, {"visibleFrameUs", visibleUs},
                    {"receivedMbps", network.networkMbps}, {"packetsReceived", network.packetsReceived},
                    {"packetsLost", network.packetsLost},
                    {"jitterMs", network.jitterMs}, {"sentMbps", observation().value("sentMbps").toDouble(-1)},
                    {"webRtcRttMs", observation().value("webRtcRttMs").toDouble(-1)}});
                QTest::qWait(150);
            }
            std::sort(frameGapsUs.begin(), frameGapsUs.end());
            bool configured = false;
            const int requested = qEnvironmentVariableIntValue("ZJDESK_WEBRTC_LATENCY_MS", &configured);
            const int bufferMs = configured ? requested : 200;
            QSaveFile report(QString("output/cross-input-latency-%1ms.json").arg(bufferMs));
            QVERIFY(report.open(QIODevice::WriteOnly));
            report.write(QJsonDocument(QJsonObject{{"jitterBufferMs", bufferMs},
                {"frameGapP95Us", frameGapsUs.isEmpty() ? -1
                    : frameGapsUs.at(frameGapsUs.size() * 95 / 100)}, {"samples", samples}}).toJson());
            QVERIFY(report.commit());
        }
        if (qEnvironmentVariable("ZJDESK_VIDEO_QUALITY_TEST") == "1")
        {
            QJsonArray performance;
            QElapsedTimer measurement;
            measurement.start();
            auto previousPresented = surface->presentedFrames();
            qint64 previousTime = 0;
            while (measurement.elapsed() < 30000)
            {
                waitForPresentation(1000);
                const auto elapsed = measurement.elapsed();
                const auto presented = surface->presentedFrames();
                const auto network = window.session_.shareStats();
                performance.append(QJsonObject{{"milliseconds", elapsed},
                    {"presentedFps", (presented - previousPresented) * 1000.0 / (elapsed - previousTime)},
                    {"receiver", window.findChild<QLabel *>("shareStatus")->text()},
                    {"sender", observation().value("shareStatus")},
                    {"receiverMedia", shareDiagnostics(network)}, {"senderMedia", observation().value("media")},
                    {"receivedMbps", network.networkMbps}, {"packetsReceived", network.packetsReceived},
                    {"packetsLost", network.packetsLost},
                    {"jitterMs", network.jitterMs}, {"sentMbps", observation().value("sentMbps").toDouble(-1)},
                    {"webRtcRttMs", observation().value("webRtcRttMs").toDouble(-1)}});
                previousPresented = presented;
                previousTime = elapsed;
            }
            QSaveFile performanceReport("output/cross-video-performance.json");
            QVERIFY(performanceReport.open(QIODevice::WriteOnly));
            performanceReport.write(QJsonDocument(performance).toJson());
            QVERIFY(performanceReport.commit());
            const auto source = QSize(data.value("width").toInt(), data.value("height").toInt());
            const auto nativeFrame = surface->grabFramebuffer();
            QCOMPARE(nativeFrame.size(), source);
            double detailError = 0;
            int maximumDetailError = 0;
            const int detailWidth = data.value("markerWidth").toInt();
            for (int x = 0; x < detailWidth; ++x)
            {
                const QColor expected(x % 3 == 0 ? Qt::red : x % 3 == 1 ? Qt::green : Qt::blue);
                const auto actual = nativeFrame.pixelColor(data.value("markerX").toInt() + x,
                    data.value("markerY").toInt() + data.value("markerHeight").toInt() - 4);
                for (int error : {qAbs(expected.red() - actual.red()), qAbs(expected.green() - actual.green()),
                                  qAbs(expected.blue() - actual.blue())})
                {
                    detailError += error;
                    maximumDetailError = qMax(maximumDetailError, error);
                }
            }
            detailError /= detailWidth * 3;
            QSaveFile detailReport("output/cross-text-detail.json");
            QVERIFY(detailReport.open(QIODevice::WriteOnly));
            detailReport.write(QJsonDocument(QJsonObject{{"meanError", detailError},
                {"maximumError", maximumDetailError}, {"singlePixelColumns", detailWidth}}).toJson());
            QVERIFY(detailReport.commit());
            QVERIFY2(detailError < MaximumDesktopMeanError && maximumDetailError <= MaximumDesktopChannelError,
                "Single-pixel colour detail exceeded the slightly lossy quality budget across machines");
            QElapsedTimer duration;
            duration.start();
            const auto qualityPresentedBefore = surface->presentedFrames();
            QJsonArray samples;
            int unhealthy = 0;
            int changed = 0;
            QImage previous;
            while (duration.elapsed() < 60000)
            {
                // Repeated GPU readback stalls presentation and feeds the test's own delay into adaptation.
                // Check decoded pixels continuously; native OpenGL pixels are checked before/after this loop.
                const auto image = receivedDesktop;
                QCOMPARE(image.size(), source);
                const QRect markerRect(data.value("markerX").toInt(), data.value("markerY").toInt(),
                    data.value("markerWidth").toInt(), data.value("markerHeight").toInt());
                const auto thumbnail = image.copy(markerRect).scaled(32, 16);
                if (!previous.isNull() && previous != thumbnail)
                    ++changed;
                previous = thumbnail;
                int matching = 0, count = 0;
                for (int y = markerRect.top() + 8; y < markerRect.bottom() - 8; y += 4)
                    for (int x = markerRect.left() + 8; x < markerRect.right() - 8; x += 4)
                    {
                        const auto color = image.pixelColor(x, y);
                        if ((color.blue() > 170 && color.red() > 140 && color.green() > 140)
                            || (color.blue() > 150 && color.red() < 80 && color.green() > 50 && color.green() < 160))
                            ++matching;
                        ++count;
                    }
                const double healthy = count ? double(matching) / count : 0;
                const auto network = window.session_.shareStats();
                samples.append(QJsonObject{{"milliseconds", duration.elapsed()}, {"healthyPixels", healthy},
                    {"receiver", window.findChild<QLabel *>("shareStatus")->text()},
                    {"receivedMbps", network.networkMbps}, {"packetsReceived", network.packetsReceived},
                    {"packetsLost", network.packetsLost},
                    {"jitterMs", network.jitterMs}});
                if (samples.size() == 1)
                    image.save("output/cross-video-native.png");
                if (healthy < 0.90)
                {
                    ++unhealthy;
                    if (unhealthy <= 3)
                        image.save(QString("output/cross-video-unhealthy-%1.png").arg(unhealthy));
                }
                waitForPresentation(250);
            }
            QSaveFile report("output/cross-video-quality.json");
            QVERIFY(report.open(QIODevice::WriteOnly));
            report.write(QJsonDocument(QJsonObject{{"samples", samples},
                {"presentedFps", (surface->presentedFrames() - qualityPresentedBefore) * 1000.0 / duration.elapsed()},
                {"unhealthySamples", unhealthy},
                {"changedSamples", changed}}).toJson());
            QVERIFY(report.commit());
            QVERIFY2(unhealthy <= 2, "Decoded desktop pixels were corrupted during the two-machine test");
            QVERIFY2(changed >= 30, "The received desktop stopped updating");
        }
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, point("buttonX", "buttonY"));
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("clicks").toInt() >= 1, 10000);
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, point("fieldX", "fieldY"));
        QTest::keyClicks(surface, "zjdesk123", Qt::NoModifier, 30);
        QTRY_COMPARE_WITH_TIMEOUT(observation().value("text").toString(), QString("zjdesk123"), 10000);
        const auto wheelPoint = point("wheelX", "wheelY");
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, wheelPoint);
        QWheelEvent scroll(wheelPoint, surface->mapToGlobal(wheelPoint), QPoint(), QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(surface, &scroll);
        QTRY_COMPARE_WITH_TIMEOUT(observation().value("wheel").toInt(), 11, 10000);
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, point("dragX", "dragY"));
        QTest::mouseMove(surface, point("dragEndX", "dragEndY"), 100);
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, point("dragEndX", "dragEndY"));
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("drag").toInt() >= 80, 10000);
        QVERIFY(QDir().mkpath("output"));
        const auto controlledImage = surface->grabFramebuffer();
        QCOMPARE(controlledImage.size(), surface->size() * surface->devicePixelRatioF());
        if (qEnvironmentVariable("ZJDESK_VIDEO_QUALITY_TEST") == "1")
            for (int x = 0; x < data.value("markerWidth").toInt(); ++x)
            {
                const QColor expected(x % 3 == 0 ? Qt::red : x % 3 == 1 ? Qt::green : Qt::blue);
                const auto actual = controlledImage.pixelColor(data.value("markerX").toInt() + x,
                    data.value("markerY").toInt() + data.value("markerHeight").toInt() - 4);
                QVERIFY2(qAbs(expected.red() - actual.red()) <= MaximumDesktopChannelError
                    && qAbs(expected.green() - actual.green()) <= MaximumDesktopChannelError
                    && qAbs(expected.blue() - actual.blue()) <= MaximumDesktopChannelError,
                    "Text detail exceeded the slightly lossy quality budget after remote input");
            }
        QVERIFY(controlledImage.save("output/remote-control-windows.png"));
        QTest::keyPress(surface, Qt::Key_Control);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("controlHeld").toBool(), 10000);
        QTest::keyClick(surface, Qt::Key_Escape, Qt::ControlModifier);
        QTRY_VERIFY(!window.isFullScreen());
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        // Held modifiers must be released when control is revoked.
        QTest::keyPress(surface, Qt::Key_Control);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("controlHeld").toBool(), 10000);
        window.findChild<QPushButton *>("stopControlButton")->click();
        QTRY_COMPARE(status->text(), QStringLiteral("远程控制未开启"));
        QTRY_COMPARE_WITH_TIMEOUT(observation().value("status").toString(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        QTest::keyClicks(surface, "blocked", Qt::NoModifier, 20);
        QTest::qWait(300);
        QCOMPARE(observation().value("text").toString(), QString("zjdesk123"));
        QTRY_VERIFY(!surface->hasFrame());
        QTRY_VERIFY(request->isEnabled());
        request->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("正在控制 Linux")), 10000);
        QTest::keyPress(surface, Qt::Key_Control);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("controlHeld").toBool(), 10000);
        QVERIFY(observer.write("remote-revoke\n") > 0);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        QTRY_VERIFY(!surface->hasFrame());
        QTRY_VERIFY(request->isEnabled());
        request->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("正在控制 Linux")), 10000);
        QTest::keyPress(surface, Qt::Key_Control);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("controlHeld").toBool(), 10000);
        window.findChild<QPushButton *>("primaryButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(observation().value("status").toString(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        QVERIFY(observer.write("remote-completed\n") > 0);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("fixtureCompleted").toBool(), 10000);
    }

    void expandedVideoKeepsSurfaceAndRestoresWindow()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Viewer");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *surface = window.findChild<VideoSurface *>();
        auto *expand = window.findChild<QPushButton *>("expandVideoButton");
        const auto normalSize = surface->size();
        const auto normalGeometry = window.geometry();
        QImage frame(window.screen()->size() * window.screen()->devicePixelRatio(), QImage::Format_RGB32);
        frame.fill(QColor("#176ee8"));
        const QRect detail(300, 300, 120, 120);
        for (int y = detail.top(); y <= detail.bottom(); ++y)
            for (int x = detail.left(); x <= detail.right(); ++x)
                frame.setPixelColor(x, y, x % 2 ? Qt::white : Qt::black);
        surface->setFrame(frame);
        surface->setControlEnabled(true);
        QSignalSpy input(surface, &VideoSurface::remoteInput);
        expand->click();
        QTRY_VERIFY(window.isFullScreen());
        QTRY_VERIFY(surface->width() > normalSize.width() && surface->height() > normalSize.height());
        QCOMPARE(window.findChild<VideoSurface *>(), surface);
        surface->setControlEnabled(true);
        QTest::mouseClick(surface, Qt::LeftButton, Qt::NoModifier, surface->rect().center());
        QVERIFY(input.size() >= 2);
        const auto click = input.at(input.size() - 2).at(0).toJsonObject();
        QVERIFY(qAbs(click.value("x").toDouble() - 0.5) < 0.005);
        QVERIFY(qAbs(click.value("y").toDouble() - 0.5) < 0.005);
        QTRY_COMPARE_WITH_TIMEOUT(surface->grabFramebuffer().size(),
            window.screen()->size() * window.screen()->devicePixelRatio(), 5000);
        const auto displayed = surface->grabFramebuffer();
        auto displayedDetail = displayed.copy(detail).convertToFormat(QImage::Format_RGB32);
        displayedDetail.setDevicePixelRatio(frame.devicePixelRatio());
        QCOMPARE(displayedDetail, frame.copy(detail));
        QCOMPARE(displayed.pixelColor(displayed.rect().center()), QColor("#176ee8"));
        QVERIFY(QDir().mkpath("output"));
        QVERIFY(window.grab().save("output/ui-expanded-video.png"));
        QTest::keyClick(surface, Qt::Key_Escape, Qt::ControlModifier);
        QTRY_VERIFY(!window.isFullScreen());
        QTRY_COMPARE(window.geometry(), normalGeometry);
        QVERIFY(window.findChild<QPushButton *>("primaryButton")->isVisible());
    }

    void controlSurfaceRejectsLetterboxAndReleasesOnFocusLoss()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        surface.setFrame(QImage(1920, 1080, QImage::Format_RGB32));
        QSignalSpy input(&surface, &VideoSurface::remoteInput);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(320, 240));
        QCOMPARE(input.size(), 0);
        surface.setControlEnabled(true);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(320, 20));
        QCOMPARE(input.size(), 0);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(320, 240));
        QCOMPARE(input.size(), 2);
        const auto click = input.first().at(0).toJsonObject();
        QCOMPARE(click.value("x").toDouble(), 0.5);
        QCOMPARE(click.value("y").toDouble(), 0.5);
        QTest::keyPress(&surface, Qt::Key_Control);
        QFocusEvent lost(QEvent::FocusOut);
        QApplication::sendEvent(&surface, &lost);
        QCOMPARE(input.last().at(0).toJsonObject().value("kind").toString(), QString("release"));
        surface.setControlEnabled(false);
    }

    void sharedTextRetainsSinglePixelDetail()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        zjdesk::SessionCoordinator sender, receiver;
        if (!sender.mediaEnvironment().ready)
            QSKIP("GStreamer is not enabled");
        MovingDesktop marker;
        marker.resize(900, 650);
        marker.move(QGuiApplication::primaryScreen()->geometry().topLeft());
        marker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        QImage original, decoded;
        connect(&sender, &zjdesk::SessionCoordinator::shareFrame, this,
            [&](const QImage &image) { if (!image.isNull()) original = image; });
        connect(&receiver, &zjdesk::SessionCoordinator::shareFrame, this,
            [&](const QImage &image) { if (!image.isNull()) decoded = image; });
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        QVERIFY(sender.join(url, "detail", "Sender"));
        QVERIFY(receiver.join(url, "detail", "Receiver"));
        QTRY_COMPARE(sender.memberCount(), 2);
        QVERIFY(sender.startScreenShare("desktop", 30));
        QTRY_VERIFY_WITH_TIMEOUT(!original.isNull() && !decoded.isNull(), 20000);
        QTest::qWait(1000);
        QCOMPARE(decoded.size(), original.size());
        const auto ratio = marker.screen()->devicePixelRatio();
        const auto origin = marker.mapToGlobal(QPoint(0, marker.height() - 64))
            - marker.screen()->geometry().topLeft();
        const QRect region = QRect(origin * ratio, QSize(marker.width(), 64) * ratio).adjusted(2, 2, -2, -2);
        QVERIFY(original.rect().contains(region));
        qint64 totalError = 0;
        int maximumError = 0;
        QPoint worstPixel;
        for (int y = region.top(); y <= region.bottom(); ++y)
            for (int x = region.left(); x <= region.right(); ++x)
            {
                const auto source = original.pixelColor(x, y), result = decoded.pixelColor(x, y);
                for (const int error : {qAbs(source.red() - result.red()), qAbs(source.green() - result.green()),
                                       qAbs(source.blue() - result.blue())})
                {
                    totalError += error;
                    if (error > maximumError)
                    {
                        maximumError = error;
                        worstPixel = QPoint(x, y);
                    }
                }
            }
        const double meanError = double(totalError) / (region.width() * region.height() * 3);
        qInfo() << "Native text RGB error: mean" << meanError << "maximum" << maximumError;
        qInfo() << "Worst pixel" << worstPixel << original.pixelColor(worstPixel) << decoded.pixelColor(worstPixel);
        QVERIFY(QDir().mkpath("output"));
        original.copy(region).save("output/text-original.png");
        decoded.copy(region).save("output/text-decoded.png");
        QVERIFY2(meanError < MaximumDesktopMeanError && maximumError <= MaximumDesktopChannelError,
            "Text or single-pixel color detail exceeded the slightly lossy quality budget");
        sender.stopScreenShare();
        QTRY_COMPARE(receiver.shareState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
    }

    void screenShareFrameRates_data()
    {
        QTest::addColumn<int>("fps");
        QTest::newRow("30fps") << 30;
        QTest::newRow("60fps") << 60;
        QTest::newRow("auto") << 0;
    }

    void screenShareFrameRates()
    {
        QFETCH(int, fps);
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        zjdesk::SessionCoordinator sender, receiver;
        if (!sender.mediaEnvironment().ready)
            QSKIP("GStreamer is not enabled");
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        MovingDesktop marker;
        marker.resize(900, 650);
        marker.move(QGuiApplication::primaryScreen()->geometry().topLeft());
        VideoSurface surface;
        surface.resize(700, 400);
        surface.move(marker.pos() + QPoint(930, 50));
        marker.show();
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        connect(&receiver, &zjdesk::SessionCoordinator::shareFrame, &surface, &VideoSurface::setFrame);
        QVERIFY(sender.join(url, "fps", "Sender"));
        QVERIFY(receiver.join(url, "fps", "Receiver"));
        QTRY_COMPARE(sender.memberCount(), 2);
        QVERIFY(sender.startScreenShare("desktop", fps));
        using Preview = zjdesk::SessionCoordinator::PreviewState;
        QTRY_VERIFY_WITH_TIMEOUT(receiver.shareState() == Preview::Running
            || receiver.shareState() == Preview::Failed, 20000);
        QVERIFY2(receiver.shareState() == Preview::Running, qPrintable(receiver.shareDetail()));
        waitForPresentation(fps == 0 ? 15000 : 2000);
        QJsonArray samples;
        double receivedFrames = 0, deliveredFrames = 0, sampleSeconds = 0;
        double encodedFrames = 0, encodedSeconds = 0;
        connect(&sender, &zjdesk::SessionCoordinator::shareStatisticsChanged, this,
            [&](const zjdesk::ShareStats &stats) {
                encodedFrames += stats.processedFps * stats.intervalSeconds;
                encodedSeconds += stats.intervalSeconds;
            });
        connect(&receiver, &zjdesk::SessionCoordinator::shareStatisticsChanged, &surface,
            [&](const zjdesk::ShareStats &) { receiver.reportSharePresentation(surface.presentedFrames()); });
        connect(&receiver, &zjdesk::SessionCoordinator::shareStatisticsChanged, this,
            [&](const zjdesk::ShareStats &stats) {
                samples.append(QJsonObject{{"receivedFps", stats.processedFps},
                    {"deliveredFps", stats.deliveredFps}, {"seconds", stats.intervalSeconds}});
                receivedFrames += stats.processedFps * stats.intervalSeconds;
                deliveredFrames += stats.deliveredFps * stats.intervalSeconds;
                sampleSeconds += stats.intervalSeconds;
            });
        const auto presentedBefore = surface.presentedFrames();
        const auto cpuBefore = processCpuSeconds();
        const auto firstImage = surface.grabFramebuffer();
        QElapsedTimer elapsed;
        elapsed.start();
        waitForPresentation(10000);
        const double seconds = elapsed.elapsed() / 1000.0;
        const double cpuPercent = 100 * (processCpuSeconds() - cpuBefore)
            / seconds / qMax(1, QThread::idealThreadCount());
        const double presentedFps = (surface.presentedFrames() - presentedBefore) / seconds;
        const auto stats = receiver.shareStats();
        QVERIFY(sampleSeconds >= 8);
        const double receivedFps = receivedFrames / sampleSeconds;
        QJsonObject report{{"targetFps", fps}, {"width", stats.size.width()}, {"height", stats.size.height()},
            {"receivedFps", receivedFps}, {"deliveredFps", deliveredFrames / sampleSeconds},
            {"selectedFps", stats.targetFps}, {"encodedFps", encodedFrames / encodedSeconds},
            {"presentedFps", presentedFps}, {"processCpuPercent", cpuPercent},
            {"seconds", seconds}, {"samples", samples}};
        QVERIFY(QDir().mkpath("output"));
        QFile result(QString("output/share-%1fps.json").arg(fps));
        QVERIFY(result.open(QIODevice::WriteOnly));
        result.write(QJsonDocument(report).toJson());
        qInfo().noquote() << QJsonDocument(report).toJson(QJsonDocument::Compact);
        QVERIFY(receivedFps > 5);
        QVERIFY(presentedFps > 5);
        if (fps == 0)
            QVERIFY2(stats.processedFps >= stats.targetFps * 0.80, qPrintable(receiver.shareDetail()));
        QVERIFY(firstImage != surface.grabFramebuffer());
        sender.stopScreenShare();
        QTRY_COMPARE(receiver.shareState(), Preview::Stopped);
        sender.leave();
        receiver.leave();
    }

    void initTestCase()
    {
        QApplication::setStyle("Fusion");
        QFont font;
        font.setFamilies({"Microsoft YaHei UI", "Noto Sans CJK SC", "sans-serif"});
        font.setPointSize(10);
        QApplication::setFont(font);
        QFile style(":/resources/styles.qss");
        QVERIFY(style.open(QIODevice::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }

    void actualWindowsJoinAndLeave()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow alice(url, "demo", "Alice");
        MainWindow bob(url, "demo", "Bob");
        alice.show();
        QVERIFY(QTest::qWaitForWindowExposed(&alice));
        bob.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bob));
        auto *aliceJoin = alice.findChild<QPushButton *>("primaryButton");
        auto *bobJoin = bob.findChild<QPushButton *>("primaryButton");
        auto *aliceStatus = alice.findChild<QLabel *>("statusBadge");
        auto *bobStatus = bob.findChild<QLabel *>("statusBadge");
        auto *members = alice.findChild<QLabel *>("memberCount");
        auto *surface = alice.findChild<QOpenGLWidget *>();
        QVERIFY(aliceJoin && bobJoin && aliceStatus && bobStatus && members && surface);
        QTRY_VERIFY(surface->isValid());
        QTest::mouseClick(aliceJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("等待成员"));
        QTest::mouseClick(bobJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("双方已入会"));
        QTRY_COMPARE(bobStatus->text(), QStringLiteral("双方已入会"));
        QCOMPARE(members->text(), QStringLiteral("2 / 2 人"));
        QVERIFY(QDir().mkpath("output"));
        alice.raise();
        QTest::qWait(150);
        // Capture the composed native window, including the OpenGL framebuffer.
        QVERIFY(alice.screen()->grabWindow(alice.winId()).save("output/ui-workspace.png"));
        const auto navButtons = alice.findChildren<QPushButton *>("navButton");
        QVERIFY(navButtons.size() == 2);
        QTest::mouseClick(navButtons.at(1), Qt::LeftButton);
        QTest::qWait(100);
        QVERIFY(alice.screen()->grabWindow(alice.winId()).save("output/ui-diagnostics.png"));
        QTest::mouseClick(bobJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("等待成员"));
        QTest::mouseClick(aliceJoin, Qt::LeftButton);
        QTRY_COMPARE(server.roomCount(), 0);
    }

    void workspaceFitsSmallWindowAndLongNames()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        const QString longName(40, QChar(0x534f));
        MainWindow window(url, "demo", longName);
        window.resize(1040, 720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.size(), QSize(1040, 720));
        auto *surface = window.findChild<VideoSurface *>();
        QVERIFY(surface->width() >= 450);
        QVERIFY(surface->height() >= 300);
        for (auto *widget : {static_cast<QWidget *>(surface),
                 window.findChild<QWidget *>("previewSource"),
                 window.findChild<QWidget *>("previewButton"),
                 window.findChild<QWidget *>("audioTestButton"),
                 window.findChild<QWidget *>("primaryButton")})
        {
            QVERIFY(widget);
            const QRect bounds(widget->mapTo(&window, QPoint()), widget->size());
            QVERIFY2(window.rect().contains(bounds), qPrintable(widget->objectName()));
        }
        auto *panel = window.findChild<QScrollArea *>("sessionPanel");
        auto *stop = window.findChild<QPushButton *>("stopControlButton");
        panel->ensureWidgetVisible(stop);
        QTest::qWait(100);
        QVERIFY(panel->viewport()->rect().contains(QRect(stop->mapTo(panel->viewport(), QPoint()), stop->size())));
        panel->verticalScrollBar()->setValue(0);
        QTest::qWait(100);
        QVERIFY(QDir().mkpath("output"));
        QVERIFY(window.screen()->grabWindow(window.winId()).save("output/ui-workspace-small.png"));
        window.findChild<QLineEdit *>("serverAddress")->setText("http://invalid");
        QTest::mouseClick(window.findChild<QPushButton *>("primaryButton"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel *>("statusBadge")->text(), QStringLiteral("连接失败"));
        QVERIFY(window.findChild<QLineEdit *>("serverAddress")->isEnabled());
        QTest::qWait(200);
        QVERIFY(window.screen()->grabWindow(window.winId()).save("output/ui-connection-error.png"));
        window.findChild<QLineEdit *>("serverAddress")->setText(url);
        QTest::mouseClick(window.findChild<QPushButton *>("primaryButton"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel *>("statusBadge")->text(), QStringLiteral("等待成员"));
        zjdesk::SessionCoordinator peer;
        QVERIFY(peer.join(QUrl(url), "demo", longName));
        QTRY_COMPARE(window.session_.memberCount(), 2);
        QCOMPARE(window.size(), QSize(1040, 720));
        window.resize(1280, 820);
        QTest::qWait(200);
        QVERIFY(window.screen()->grabWindow(window.winId()).save("output/ui-workspace-large.png"));
        const auto nav = window.findChildren<QPushButton *>("navButton");
        nav.at(1)->setFocus();
        QTest::keyClick(nav.at(1), Qt::Key_Space);
        QTRY_COMPARE(window.findChild<QLabel *>("pageTitle")->text(), QStringLiteral("运行诊断"));
        QTest::qWait(100);
        QVERIFY(window.screen()->grabWindow(window.winId()).save("output/ui-diagnostics-large.png"));
        QTest::mouseClick(nav.at(0), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel *>("pageTitle")->text(), QStringLiteral("协作工作台"));
    }

    void localPreviewDisplaysRealFramesAndStops()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Tester");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *button = window.findChild<QPushButton *>("previewButton");
        auto *surface = dynamic_cast<VideoSurface *>(window.findChild<QOpenGLWidget *>());
        QVERIFY(button && surface);
        if (!button->isEnabled())
            QSKIP("GStreamer is not enabled in this build");
        QTRY_VERIFY(surface->isValid());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(), 10000);
        QCOMPARE(button->text(), QStringLiteral("停止预览"));
        const auto first = surface->grabFramebuffer();
        QTest::qWait(200);
        const auto second = surface->grabFramebuffer();
        QVERIFY(!first.isNull());
        QVERIFY(first != second);
        QVERIFY(QDir().mkpath("output"));
        window.raise();
        QTest::qWait(100);
        QVERIFY(window.screen()->grabWindow(window.winId()).save("output/ui-local-preview.png"));
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_COMPARE(button->text(), QStringLiteral("开始预览"));
        QVERIFY(!surface->hasFrame());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(surface->hasFrame());
    }

    void screenShareCapturesDesktopAndDisplaysAtPeer()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow alice(url, "desktop", "Alice"), bob(url, "desktop", "Bob");
        alice.show();
        bob.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bob));
        auto *aliceShare = alice.findChild<QPushButton *>("shareButton");
        auto *bobShare = bob.findChild<QPushButton *>("shareButton");
        auto *bobStatus = bob.findChild<QLabel *>("shareStatus");
        auto *surface = dynamic_cast<VideoSurface *>(bob.findChild<QOpenGLWidget *>());
        QVERIFY(aliceShare && bobShare && bobStatus && surface);
        if (!alice.findChild<QPushButton *>("previewButton")->isEnabled())
            QSKIP("GStreamer is not enabled in this build");
        QVERIFY(!aliceShare->isEnabled());
        alice.findChild<QPushButton *>("primaryButton")->click();
        bob.findChild<QPushButton *>("primaryButton")->click();
        QTRY_VERIFY(aliceShare->isEnabled());
        // A known visible desktop marker proves the peer receives captured pixels.
        QWidget marker(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
        marker.resize(400, 300);
        marker.move(QGuiApplication::primaryScreen()->geometry().topLeft());
        marker.setStyleSheet("background: #ff00ff;");
        marker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        aliceShare->click();
        QTRY_VERIFY_WITH_TIMEOUT(bobStatus->text().contains(QStringLiteral("正在接收对方屏幕"))
            && surface->hasFrame(), 20000);
#ifdef Q_OS_LINUX
        QTRY_COMPARE_WITH_TIMEOUT(alice.session_.shareStats().transport, QString("TCP"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(bob.session_.shareStats().transport, QString("TCP"), 5000);
#endif
        QVERIFY(!bobShare->isEnabled());
        const auto hasColor = [surface](bool green) {
            const auto image = surface->grabFramebuffer();
            int count = 0;
            for (int y = 0; y < image.height(); y += 4)
                for (int x = 0; x < image.width(); x += 4)
                {
                    const auto color = image.pixelColor(x, y);
                    // Desktop system dialogs can dim the marker; compare chroma.
                    if (green ? color.green() - qMax(color.red(), color.blue()) > 80
                              : qMin(color.red(), color.blue()) - color.green() > 80)
                        ++count;
                }
            return count > 100;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasColor(false), 10000);
        marker.setStyleSheet("background: #00ff00;");
        QTRY_VERIFY_WITH_TIMEOUT(hasColor(true), 10000);
        qInfo().noquote() << "Desktop receiver:" << bobStatus->text();
        marker.hide();
        bob.raise();
        QTest::qWait(150);
        QVERIFY(QDir().mkpath("output"));
        QVERIFY(bob.screen()->grabWindow(bob.winId()).save("output/ui-screen-share.png"));
        aliceShare->click();
        QTRY_COMPARE(aliceShare->text(), QStringLiteral("共享主屏幕"));
        QTRY_COMPARE(bobShare->text(), QStringLiteral("共享主屏幕"));
        QVERIFY(!surface->hasFrame());
        QTRY_VERIFY(bobShare->isEnabled());
        bobShare->click();
        auto *aliceSurface = dynamic_cast<VideoSurface *>(alice.findChild<QOpenGLWidget *>());
        QTRY_VERIFY_WITH_TIMEOUT(aliceSurface->hasFrame(), 20000);
        bob.findChild<QPushButton *>("primaryButton")->click();
        QTRY_VERIFY(!aliceSurface->hasFrame());
        QVERIFY(!aliceShare->isEnabled());
    }
};

QTEST_MAIN(WindowTests)
#include "tst_window.moc"
