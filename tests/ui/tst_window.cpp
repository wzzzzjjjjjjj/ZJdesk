#include "main_window.h"
#include "video_surface.h"
#include "annotation_layer.h"
#include "signaling/room_server.h"
#include "platform/platform_info.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QColor>
#include <QLabel>
#include <QMessageBox>
#include <QComboBox>
#include <QToolButton>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QScreen>
#include <QWindow>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QVBoxLayout>
#include <QFocusEvent>
#include <QSpinBox>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QCheckBox>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QClipboard>
#include <ctime>
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
// Slightly lossy 4:4:4: preserve native pixels and bound RGB error on text/one-pixel colour lines.
constexpr int MaximumDesktopChannelError = 24;
constexpr double MaximumDesktopMeanError = 2;

// 取区域中最深的像素颜色，用来确认提示文字确实是灰色而不是普通文字颜色。
QColor darkestPixelColor(const QImage &image, const QRect &region)
{
    QColor darkest(255, 255, 255);
    int best = 255 * 3;
    const QRect area = region.intersected(image.rect());
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            const int score = color.red() + color.green() + color.blue();
            if (score < best)
            {
                best = score;
                darkest = color;
            }
        }
    return darkest;
}

int colorDistance(const QColor &left, const QColor &right)
{
    return qMax(qAbs(left.red() - right.red()),
        qMax(qAbs(left.green() - right.green()), qAbs(left.blue() - right.blue())));
}

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
        {"localAddress", stats.localAddress}, {"remoteAddress", stats.remoteAddress},
        {"networkMbps", stats.networkMbps}, {"packetsReceived", stats.packetsReceived},
        {"packetsLost", stats.packetsLost}, {"jitterMs", stats.jitterMs},
        {"controlRttMs", stats.controlRttMs}, {"transportExcessMs", stats.transportExcessMs},
        {"controlBufferedBytes", qint64(stats.controlBufferedBytes)}, {"revision", stats.revision},
        {"congestionBitrate", stats.congestionBitrate}, {"pacedBytes", stats.pacedBytes},
        {"droppedBeforeEncode", qint64(stats.droppedBeforeEncode)},
        {"presentedGapMaxMs", stats.presentedGapMaxMs},
        {"processedFps", stats.processedFps}, {"deliveredFps", stats.deliveredFps},
        {"codecMeanMs", stats.codecMeanMs}, {"codecMaxMs", stats.codecMaxMs},
        {"outputGapMaxMs", stats.outputGapMaxMs}, {"copyMaxMs", stats.copyMaxMs},
        {"maxEncodedFrameBytes", qint64(stats.maxEncodedFrameBytes)},
        {"maxKeyframeBytes", qint64(stats.maxKeyframeBytes)}, {"keyframes", qint64(stats.keyframes)},
        {"overwrittenFrames", qint64(stats.overwrittenFrames)},
        {"width", stats.size.width()}, {"height", stats.size.height()}, {"timings", stats.timingDiagnostics()}};
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

class CameraSettingsHandler : public QObject
{
public:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Polish && (object->objectName() == "cameraPermissionDialog"
            || object->objectName() == "cameraSettingsError"))
        {
            // Exercise dialog logic without showing a system window or taking focus.
            static_cast<QWidget *>(object)->setAttribute(Qt::WA_DontShowOnScreen);
        }
        return false;
    }
};

class WindowTests : public QObject
{
    Q_OBJECT
    QTemporaryDir preferenceDirectory_;
private slots:
    void windowsSharedDesktopExcludesOwnPreview()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows capture exclusion uses native display affinity");
#else
        using Session = zjdesk::SessionCoordinator;
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        MainWindow window(url.toString(), {}, "Owner"), viewer(url.toString(), {}, "Viewer");
        auto &owner = window.session_;
        owner.setConferenceAutoConnect(false);
        viewer.session_.setConferenceAutoConnect(false);
        if (!owner.mediaEnvironment().ready) QSKIP("GStreamer is not enabled");
        window.resize(1280, 840);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("视频会议")) { button->click(); break; }
        const auto affinity = [](MainWindow &target) {
            DWORD value = DWORD(-1);
            if (!GetWindowDisplayAffinity(reinterpret_cast<HWND>(target.winId()), &value)) return DWORD(-1);
            return value;
        };
        QCOMPARE(affinity(window), DWORD(WDA_NONE));
        QVERIFY(owner.join(url, "capture-exclusion", "Owner", Session::RoomType::Meeting));
        QVERIFY(viewer.session_.join(url, "capture-exclusion", "Viewer", Session::RoomType::Meeting));
        QTRY_COMPARE(owner.memberCount(), 2);
        QVERIFY(owner.startScreenShare("test", 15));
        QTRY_VERIFY_WITH_TIMEOUT(window.meetingShareVideo_->hasFrame(), 30000);
        QCOMPARE(affinity(window), DWORD(WDA_NONE)); // Synthetic sources remain capturable.
        owner.stopScreenShare();
        QTRY_VERIFY(!owner.screenActive());
        QVERIFY(owner.startScreenShare("desktop", 15));
        QTRY_VERIFY(owner.isDesktopShare());
        QCOMPARE(affinity(window), DWORD(0x00000011)); // WDA_EXCLUDEFROMCAPTURE, not black-window WDA_MONITOR.
        QTRY_VERIFY_WITH_TIMEOUT(window.meetingShareVideo_->hasFrame(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.meetingShareVideo_->hasFrame(), 30000);
        QCOMPARE(affinity(viewer), DWORD(WDA_NONE));
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            window.expandMeetingVideo(window.meetingShareVideo_);
            QTRY_VERIFY(window.isFullScreen());
            QCOMPARE(affinity(window), DWORD(0x00000011));
            // The viewer is hidden, so count received rather than painted frames.
            QSignalSpy received(&viewer.session_, &Session::shareFrame);
            QTRY_VERIFY_WITH_TIMEOUT(received.size() >= 3, 10000);
            if (cycle == 0) QTest::keyClick(window.meetingShareVideo_, Qt::Key_Escape);
            else owner.stopScreenShare(); // Ending an expanded share restores capture too.
            QTRY_VERIFY(!window.isFullScreen());
            QCOMPARE(affinity(window), DWORD(cycle == 0 ? 0x00000011 : WDA_NONE));
        }
        QVERIFY(owner.startScreenShare("desktop", 15));
        QTRY_VERIFY(owner.isDesktopShare());
        QCOMPARE(affinity(window), DWORD(0x00000011));
        owner.leave();
        QCOMPARE(affinity(window), DWORD(WDA_NONE));
#endif
    }

    void annotationToolsStayAlignedWithSharedFrame()
    {
        VideoSurface surface;
        surface.resize(800, 600);
        QImage frame(1600, 900, QImage::Format_RGB32);
        frame.fill(Qt::black);
        surface.setFrame(frame);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QVERIFY(surface.isValid());
        auto *layer = surface.annotationLayer();
        QCOMPARE(layer->geometry(), QRect(0, 75, 800, 450));
        const QJsonObject author{{"authorId", "a"}, {"name", "Alice"}, {"color", "#e5484d"}};
        layer->setAnnotations({}, author);
        QSignalSpy strokes(layer, &AnnotationLayer::strokeReady);
        QSignalSpy input(&surface, &VideoSurface::remoteInput);
        for (const QString &tool : {QString("circle"), QString("arrow"), QString("pen")})
        {
            layer->setTool(tool);
            QTest::mousePress(layer, Qt::LeftButton, Qt::NoModifier, QPoint(160, 90));
            QTest::mouseMove(layer, QPoint(400, 225));
            QTest::mouseRelease(layer, Qt::LeftButton, Qt::NoModifier, QPoint(640, 360));
            QVERIFY(!strokes.isEmpty());
            const auto stroke = strokes.takeLast().first().toJsonObject();
            QCOMPARE(stroke.value("tool").toString(), tool);
            const auto points = stroke.value("points").toArray();
            QCOMPARE(points.first().toArray(), (QJsonArray{0.2, 0.2}));
            QCOMPARE(points.last().toArray(), (QJsonArray{0.8, 0.8}));
            layer->setAnnotations({stroke}, author);
        }
        QCOMPARE(input.size(), 0); // Drawing does not grant or inject remote input.
        surface.resize(640, 640);
        QTRY_COMPARE(layer->geometry(), QRect(0, 140, 640, 360));
        layer->setTool({});
        QVERIFY(layer->testAttribute(Qt::WA_TransparentForMouseEvents));
        surface.setFrame({});
        QVERIFY(!layer->isVisible());
    }

    void annotationLegendListsEachAuthorOnce()
    {
        AnnotationLayer layer;
        layer.resize(800, 400);
        layer.setAnnotations({}, {{"authorId", "author"}, {"name", "Author"}, {"color", "#e5484d"}});
        layer.setTool("pen");
        layer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&layer));
        QSignalSpy strokes(&layer, &AnnotationLayer::strokeReady);
        QTest::mousePress(&layer, Qt::LeftButton, Qt::NoModifier, QPoint(20, 200));
        for (int x = 21; x < 620; ++x)
        {
            QMouseEvent event(QEvent::MouseMove, QPointF(x, 200), QPointF(layer.mapToGlobal(QPoint(x, 200))),
                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&layer, &event);
        }
        QTest::mouseRelease(&layer, Qt::LeftButton, Qt::NoModifier, QPoint(620, 200));
        QCOMPARE(strokes.size(), 3);
        QJsonArray canvas;
        for (int i = 0; i < strokes.size(); ++i)
        {
            const auto stroke = strokes.at(i).first().toJsonObject();
            canvas.append(stroke);
        }
        layer.setAnnotations(canvas);
        QImage image(layer.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        layer.render(&painter, {}, {}, QWidget::DrawChildren);
        painter.end();
        QCOMPARE(image.pixelColor(30, 182).alpha(), 0);
        QCOMPARE(image.pixelColor(285, 182).alpha(), 0);
        QCOMPARE(image.pixelColor(540, 182).alpha(), 0);
        QVERIFY(image.pixelColor(780, 54).alpha() > 0);
        QCOMPARE(image.pixelColor(780, 80).alpha(), 0); // Three segments, one author row.
        auto other = canvas.first().toObject();
        other.insert("strokeId", "other");
        other.insert("authorId", "other-author");
        other.insert("name", "Other");
        other.insert("color", "#2474e5");
        canvas.append(other);
        layer.setAnnotations(canvas);
        image.fill(Qt::transparent);
        painter.begin(&image);
        layer.render(&painter, {}, {}, QWidget::DrawChildren);
        painter.end();
        QVERIFY(image.pixelColor(780, 80).alpha() > 0);
        QCOMPARE(image.pixelColor(780, 106).alpha(), 0);
        layer.setAnnotationsInVideo(true);
        image.fill(Qt::transparent);
        painter.begin(&image);
        layer.render(&painter, {}, {}, QWidget::DrawChildren);
        painter.end();
        QCOMPARE(image.pixelColor(780, 54).alpha(), 0); // Embedded X11 legend is not painted twice.
        strokes.clear();
        QTest::mousePress(&layer, Qt::LeftButton, Qt::NoModifier, QPoint(20, 300));
        QTest::mouseRelease(&layer, Qt::LeftButton, Qt::NoModifier, QPoint(50, 300));
        QCOMPARE(strokes.size(), 1);
    }

    void meetingAnnotationsReachEveryViewerAndLateJoiner()
    {
        using Session = zjdesk::SessionCoordinator;
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        MainWindow window(url.toString(), {}, "Owner");
        auto &owner = window.session_;
        owner.setConferenceAutoConnect(false);
        Session first, second, late;
        if (!owner.mediaEnvironment().ready) QSKIP("GStreamer is not enabled");
        int frames[3] = {};
        const QList<Session *> viewers{&first, &second, &late};
        for (int i = 0; i < viewers.size(); ++i)
            connect(viewers[i], &Session::shareFrame, this, [&, i](const QImage &frame) { if (!frame.isNull()) ++frames[i]; });
        QVERIFY(owner.join(url, "annotations", "Owner", Session::RoomType::Meeting));
        QVERIFY(first.join(url, "annotations", "First", Session::RoomType::Meeting));
        QVERIFY(second.join(url, "annotations", "Second", Session::RoomType::Meeting));
        QTRY_COMPARE(owner.memberCount(), 3);
        QVERIFY(owner.startScreenShare("test", 15));
        QTRY_VERIFY_WITH_TIMEOUT(frames[0] >= 3 && frames[1] >= 3, 30000);
        QVERIFY(owner.canAnnotate() && first.canAnnotate() && second.canAnnotate());
        const QJsonObject circle{{"strokeId", "owner-circle"}, {"tool", "circle"},
            {"points", QJsonArray{QJsonArray{0.2, 0.2}, QJsonArray{0.8, 0.8}}}};
        auto arrow = circle;
        arrow.insert("strokeId", "viewer-arrow");
        arrow.insert("tool", "arrow");
        QVERIFY(owner.submitAnnotation(circle));
        QVERIFY(second.submitAnnotation(arrow));
        QTRY_COMPARE(first.annotations().size(), 2);
        QTRY_COMPARE(owner.annotations(), first.annotations());
        QTRY_COMPARE(second.annotations(), first.annotations());
        QVERIFY(!second.clearAnnotations(true));
        QVERIFY(late.join(url, "annotations", "Late", Session::RoomType::Meeting));
        QTRY_COMPARE(late.annotations(), first.annotations());
        QTRY_VERIFY_WITH_TIMEOUT(frames[2] >= 3, 30000);
        // Losing the first/primary viewer must not stop another member's sharing.
        first.leave();
        QTRY_COMPARE(owner.memberCount(), 3);
        const int before = frames[1];
        QTRY_VERIFY_WITH_TIMEOUT(frames[1] >= before + 3, 10000);
        QVERIFY(second.clearAnnotations());
        QTRY_COMPARE(owner.annotations().size(), 1);
        QTRY_COMPARE(late.annotations(), owner.annotations());
        QCOMPARE(owner.annotations().first().toObject().value("strokeId").toString(), "owner-circle");
        QVERIFY(!window.clearAllAnnotations_->isHidden());
        QVERIFY(window.annotationTool_->isEnabled());
        // The native desktop window paints the same model and remains click-through.
        owner.desktopSource_ = true;
        window.refreshAnnotations();
        QVERIFY(window.desktopAnnotations_->isVisible());
        QVERIFY(window.desktopAnnotations_->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(window.desktopAnnotations_->testAttribute(Qt::WA_ShowWithoutActivating));
        const auto rendered = window.desktopAnnotations_->grab().toImage();
        const auto mark = rendered.pixelColor(qRound(rendered.width() * 0.2), qRound(rendered.height() * 0.5));
        const QColor expected(owner.annotations().first().toObject().value("color").toString());
        QVERIFY(mark.alpha() > 100);
        QVERIFY(qAbs(mark.red() - expected.red()) < 20 && qAbs(mark.blue() - expected.blue()) < 20);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.showMinimized();
        QTest::qWait(250);
        QVERIFY(window.desktopAnnotations_->windowHandle()->isExposed());
        QVERIFY(owner.clearAnnotations(true));
        QTRY_VERIFY(late.annotations().isEmpty() && owner.annotations().isEmpty());
        QVERIFY(!window.desktopAnnotations_->isVisible());
        owner.leave();
        QTRY_VERIFY(!late.canAnnotate() && !second.canAnnotate());
        QVERIFY(!window.annotationTool_->isEnabled());
    }

    void meetingAnnotationWanPeer()
    {
        if (!qEnvironmentVariableIsSet("ZJDESK_ANNOTATION_WAN_URL")) QSKIP("Requires a coordinated cross-network peer");
        using Session = zjdesk::SessionCoordinator;
        const bool owner = qEnvironmentVariable("ZJDESK_ANNOTATION_WAN_ROLE") == "owner";
        const auto output = qEnvironmentVariable("ZJDESK_ANNOTATION_WAN_OUTPUT");
        QVERIFY(QDir().mkpath(output));
        const QUrl url(qEnvironmentVariable("ZJDESK_ANNOTATION_WAN_URL"));
        MainWindow window(url.toString(), {}, owner ? "WAN owner" : "WAN viewer");
        window.resize(1280, 840);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("视频会议")) { button->click(); break; }
        auto &session = window.session_;
        session.setConferenceAutoConnect(false);
        int frames = 0;
        QImage latest;
        connect(&session, &Session::shareFrame, this, [&](const QImage &frame) {
            if (!frame.isNull()) { ++frames; latest = frame; }
        });
        QVERIFY(session.join(url, qEnvironmentVariable("ZJDESK_ANNOTATION_WAN_ROOM"),
            owner ? "WAN owner" : "WAN viewer", Session::RoomType::Meeting));
        QTRY_COMPARE_WITH_TIMEOUT(session.memberCount(), 2, 45000);
        if (owner) QVERIFY(session.startScreenShare("desktop", 15));
        QTRY_VERIFY_WITH_TIMEOUT(session.canAnnotate() && frames >= 5, 45000);
        auto *layer = window.meetingShareVideo_->annotationLayer();
        window.annotationTool_->setCurrentIndex(owner ? 2 : 3);
        const auto start = QPoint(layer->width() / 5, layer->height() / 5);
        const auto end = QPoint(layer->width() * 4 / 5, layer->height() * 4 / 5);
        QTest::mousePress(layer, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(layer, end);
        QTest::mouseRelease(layer, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE_WITH_TIMEOUT(session.annotations().size(), 2, 15000);
        const auto strokes = session.annotations();
        QVERIFY(strokes[0].toObject().value("authorId") != strokes[1].toObject().value("authorId"));
        QVERIFY(strokes[0].toObject().value("color") != strokes[1].toObject().value("color"));
        QTest::qWait(1200); // Include the X11 desktop markings in the captured video.
        QVERIFY(latest.save(output + "/shared-frame.png"));
        QVERIFY(window.grab().save(output + "/meeting.png"));
        QFile model(output + "/annotations.json");
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write(QJsonDocument(strokes).toJson());
        model.close();
        if (owner)
        {
            QVERIFY(window.desktopAnnotations_->isVisible());
            QVERIFY(window.desktopAnnotations_->grab().save(output + "/desktop-layer.png"));
            QTRY_COMPARE_WITH_TIMEOUT(session.annotations().size(), 1, 20000);
            QVERIFY(session.clearAnnotations(true));
            QTRY_VERIFY_WITH_TIMEOUT(session.annotations().isEmpty(), 10000);
            QTest::qWait(2000);
            session.stopScreenShare();
        }
        else
        {
            QVERIFY(session.clearAnnotations());
            QTRY_VERIFY_WITH_TIMEOUT(session.annotations().isEmpty(), 20000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!session.canAnnotate(), 15000);
        QVERIFY(!window.desktopAnnotations_->isVisible());
        qInfo() << "WAN native desktop and annotations passed; role" << (owner ? "owner" : "viewer")
            << "frames" << frames << "size" << latest.size();
        session.leave();
    }

    void startupNetworkServerRemainsVerified()
    {
        const QString url = "ws://192.0.2.10:8765";
        MainWindow window(url, {}, "Network test");
        auto *server = window.findChild<QLineEdit *>("serverAddress");
        QVERIFY(server->isReadOnly());
        server->selectAll();
        QTest::keyClicks(server, "ws://192.0.2.11:8765");
        QCOMPARE(server->text(), url);
        QCOMPARE(window.findChild<QLineEdit *>("meetingServerAddress")->text(), url);
    }

    void startupNetworkCannotSkipVerification()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "network");
        settings.clear();
        MainWindow window({}, {}, "Network test");
        window.enableNetworkSetup();
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *peer = window.findChild<QLineEdit *>("networkPeerAddress");
        QVERIFY(peer->text().isEmpty());
        QVERIFY(peer->property("networkInvalid").toBool());
        QVERIFY(peer->styleSheet().contains("#dc3545"));
        QVERIFY(!window.joinButton_->isEnabled());
        QVERIFY(!window.meetingJoinButton_->isEnabled());
        window.startRemoteAccess();
        QCOMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::Idle);
        peer->setText("invalid");
        window.joinButton_->click();
        QVERIFY(!window.networkSetup_.verified());
        QVERIFY(peer->property("networkInvalid").toBool());
        QVERIFY(window.joinButton_->isEnabled());
        QVERIFY(window.findChild<QLabel *>("networkStatus")->toolTip().contains(QStringLiteral("对端")));
        const auto *panel = window.findChild<QWidget *>("startupNetworkPanel");
        QCOMPARE(panel->parentWidget(), window.connectionLayout_->parentWidget());
        QVERIFY(panel->isAncestorOf(window.joinButton_));
        QCOMPARE(panel->findChildren<QPushButton *>().size(), 2);
        QVERIFY(!window.networkColumn_->isVisible());
        QVERIFY(window.remoteHistory_->isHidden());
        QVERIFY(!window.serverEdit_->isVisible());
        QVERIFY(!window.findChild<QLineEdit *>("displayName"));
        for (auto *label : panel->parentWidget()->findChildren<QLabel *>())
            QVERIFY(!label->isVisible() || label->text() != QStringLiteral("服务地址"));
        QVERIFY(!window.findChild<QFrame *>("sidebar")->isAncestorOf(panel));
        for (auto *widget : QApplication::topLevelWidgets())
            if (qobject_cast<QDialog *>(widget)) QVERIFY(!widget->isVisible());
        QVERIFY(QDir().mkpath("output"));
        QVERIFY(window.grab().save("output/ui-startup-network.png"));
        peer->clear();
        QVERIFY(peer->property("networkInvalid").toBool());
    }

    void startupNetworkPersistsInputsButRequiresNewVerification()
    {
        const auto addresses = zjdesk::NetworkSetup::localAddresses();
        if (addresses.isEmpty()) QSKIP("No active IPv4 interface");
        // 与产品逻辑一致：按到对端的路由选择本机地址，避免多网卡 / VPN 机器上
        // 服务器地址与自动选出的本机地址不一致导致验证被拒。
        QUdpSocket route;
        route.connectToHost(QHostAddress("203.0.113.9"), 9);
        const auto routed = route.localAddress().toString();
        const auto serverAddress = addresses.contains(routed) ? routed : addresses.first();
        const auto originalPath = qgetenv("PATH");
        const auto originalBinding = qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        const auto originalPing = qgetenv("ZJDESK_TEST_PING");
        const auto restore = qScopeGuard([=] {
            qputenv("PATH", originalPath);
            if (originalBinding.isNull()) qunsetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
            else qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", originalBinding);
            if (originalPing.isNull()) qunsetenv("ZJDESK_TEST_PING");
            else qputenv("ZJDESK_TEST_PING", originalPing);
        });
        QTemporaryDir pingDirectory;
#ifdef Q_OS_WIN
        const auto suffix = QStringLiteral(".exe");
#else
        const auto suffix = QString();
#endif
        const auto destination = pingDirectory.filePath("ping" + suffix);
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/zjdesk-network-ping-stub" + suffix, destination));
        QFile::setPermissions(destination, QFile::permissions(destination) | QFile::ExeOwner);
        qputenv("PATH", pingDirectory.path().toUtf8() + QDir::listSeparator().toLatin1() + originalPath);
        qputenv("ZJDESK_TEST_PING", "ok");
        zjdesk::RoomServer server(nullptr, pingDirectory.filePath("codes.ini"));
        QVERIFY(server.listen(QHostAddress(serverAddress), 0));
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "network");
        settings.clear();
        const auto url = QString("ws://%1:%2").arg(serverAddress).arg(server.port());
        MainWindow window(url, {}, "Network test");
        window.enableNetworkSetup();
#ifdef Q_OS_WIN
        const QString peerKey = "linuxAddress";
#else
        const QString peerKey = "windowsAddress";
#endif
        auto *peer = window.findChild<QLineEdit *>("networkPeerAddress");
        peer->setText("203.0.113.9");
        window.joinButton_->click();
        QTRY_VERIFY2(window.networkSetup_.verified(),
            qPrintable(window.findChild<QLabel *>("networkStatus")->toolTip()));
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QVERIFY(window.remoteCodeLabel_->isHidden());
        QVERIFY(window.remoteHistory_->isHidden());
        QTest::qWait(300);
        QVERIFY(!window.session_.detail().contains(QStringLiteral("对端未在线")));
        QVERIFY(!window.session_.remoteConnecting());
        QCOMPARE(window.session_.remoteState(), zjdesk::SessionCoordinator::RemoteState::Idle);
        QVERIFY(!window.session_.screenActive());
        QVERIFY(!window.joinButton_->isEnabled());
        auto *remote = window.findChild<QPushButton *>("remoteConnectButton");
        QVERIFY(remote && remote->isEnabled());
        QCOMPARE(remote->text(), QStringLiteral("远程控制"));
        remote->click();
        // Only the explicit control action attempts to pair with the offline fixture.
        QTRY_VERIFY2(window.session_.detail().contains(QStringLiteral("对端未在线")),
            qPrintable(window.session_.detail()));
        QVERIFY(remote->isEnabled());
        zjdesk::SessionCoordinator host;
        QVERIFY(host.setRemoteCodePolicy("startup", {}));
        QVERIFY(host.startRemoteAccess(QUrl(url), "Host", serverAddress));
        QTRY_COMPARE(host.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        // Re-register the fixture with one real interface for both endpoints, then use the actual button.
        window.session_.leave();
        QVERIFY(window.session_.startRemoteAccess(QUrl(url), "Network test", serverAddress));
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QTest::qWait(100);
        QCOMPARE(host.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        remote->click();
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::PeerAvailable);
        QTRY_COMPARE(host.state(), zjdesk::SessionCoordinator::State::PeerAvailable);
        QVERIFY(!remote->isEnabled());
        if (host.input_.available() && host.mediaEnvironment().ready)
        {
            QTRY_COMPARE(host.remoteState(), zjdesk::SessionCoordinator::RemoteState::Pending);
            QCOMPARE(window.session_.remoteState(), zjdesk::SessionCoordinator::RemoteState::Requesting);
            QVERIFY(!host.screenActive());
        }
        window.session_.disconnectRemote();
        QTRY_COMPARE(host.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QVERIFY(remote->isEnabled());
        QCOMPARE(window.serverEdit_->text(), url);
        QVERIFY(window.meetingJoinButton_->isEnabled());
        settings.sync();
        QCOMPARE(settings.value("networkSetup/" + peerKey).toString(), QString("203.0.113.9"));
        peer->setText("203.0.113.10");
        QVERIFY(!window.networkSetup_.verified());
        QCOMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::Idle);
        QVERIFY(!window.meetingJoinButton_->isEnabled());
        QVERIFY(!remote->isEnabled());
        MainWindow restarted(url, {}, "Network test");
        restarted.enableNetworkSetup();
        QCOMPARE(restarted.networkPeer_->text(), QString("203.0.113.9"));
        QVERIFY(!restarted.networkSetup_.verified());
        QVERIFY(!restarted.meetingJoinButton_->isEnabled());
    }

    void remoteCodeWaitsForPreviousStreamsToStop_data()
    {
        QTest::addColumn<bool>("controllerFirst");
        QTest::newRow("controller-stops-first") << true;
        QTest::newRow("host-stops-first") << false;
    }

    void remoteCodeWaitsForPreviousStreamsToStop()
    {
        QFETCH(bool, controllerFirst);
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator controller, host;
        if (!host.input_.available() || !host.mediaEnvironment().ready) QSKIP("Native desktop backend required");
        using Preview = zjdesk::SessionCoordinator::PreviewState;
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        controller.shareState_ = host.shareState_ = Preview::Stopping;
        QVERIFY(controller.startRemoteAccess(url, "Controller"));
        QVERIFY(host.startRemoteAccess(url, "Host"));
        QTRY_VERIFY(!controller.remoteCode().isEmpty() && !host.remoteCode().isEmpty());
        QVERIFY(controller.connectRemote(host.remoteCode()));
        QTRY_COMPARE(controller.state(), zjdesk::SessionCoordinator::State::PeerAvailable);
        QTRY_COMPARE(host.state(), zjdesk::SessionCoordinator::State::PeerAvailable);
        auto &first = controllerFirst ? controller : host;
        auto &last = controllerFirst ? host : controller;
        first.screen_.statusChanged(first.shareEpoch_, "stopped", "test previous stream stopped");
        QTest::qWait(50);
        QCOMPARE(host.remoteState(), Remote::Idle);
        last.screen_.statusChanged(last.shareEpoch_, "stopped", "test previous stream stopped");
        QTRY_COMPARE(host.remoteState(), Remote::Pending);
        QCOMPARE(controller.remoteState(), Remote::Requesting);
        QVERIFY(!host.screenActive());
    }

    void remoteCodeConnectionAutomaticallyRequestsConsent()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator controller, host;
        if (!host.input_.available() || !host.mediaEnvironment().ready) QSKIP("Native desktop backend required");
        QVERIFY(controller.startRemoteAccess(url, "Controller"));
        QVERIFY(host.startRemoteAccess(url, "Host"));
        QTRY_VERIFY(!controller.remoteCode().isEmpty() && !host.remoteCode().isEmpty());
        const auto hostCode = host.remoteCode();
        QVERIFY(controller.connectRemote(hostCode));
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        QTRY_COMPARE(host.remoteState(), Remote::Pending);
        QTRY_COMPARE(controller.remoteState(), Remote::Requesting);
        QVERIFY(!host.screenActive()); // No capture or input before consent.
        host.stopControl();
        QTRY_COMPARE(controller.remoteState(), Remote::Idle);
        QTRY_COMPARE(controller.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QTRY_COMPARE(host.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QCOMPARE(host.remoteCode(), hostCode);
    }

    void recentRemotePeersPersistWithoutReconnecting()
    {
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
        preferences.clear();
        const auto reset = qScopeGuard([&] { preferences.clear(); });
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow window(url, {}, "Controller");
        zjdesk::SessionCoordinator host;
        // Prevent capture while exercising actual pairing, history and UI disconnect wiring.
        window.session_.shareState_ = zjdesk::SessionCoordinator::PreviewState::Stopping;
        QVERIFY(host.startRemoteAccess(QUrl(url), "Host"));
        window.startRemoteAccess();
        QTRY_VERIFY(!window.session_.remoteCode().isEmpty() && !host.remoteCode().isEmpty());
        const auto code = host.remoteCode();
        window.roomEdit_->setText(code);
        QTest::mouseClick(window.joinButton_, Qt::LeftButton);
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::PeerAvailable);
        auto *history = window.findChild<QComboBox *>("recentRemoteCodes");
        QVERIFY(history);
        QCOMPARE(history->count(), 1);
        QCOMPARE(history->itemData(0).toString(), code);
        QVERIFY(history->itemText(0).contains("Host"));
        QVERIFY(!history->itemIcon(0).isNull());
        QVERIFY(!history->isEnabled());
        window.session_.remoteState_ = zjdesk::SessionCoordinator::RemoteState::Controlling;
        window.refreshControl();
        QCOMPARE(window.stopControlButton_->text(), QStringLiteral("结束控制"));
        QTest::mouseClick(window.stopControlButton_, Qt::LeftButton);
        QTRY_COMPARE(host.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QVERIFY(window.roomEdit_->text().isEmpty());
        QVERIFY(!window.joinButton_->isEnabled());
        QVERIFY(history->isEnabled());
        window.session_.leave();
        MainWindow reopened(url, {}, "Controller");
        auto *saved = reopened.findChild<QComboBox *>("recentRemoteCodes");
        QCOMPARE(saved->count(), 1);
        QVERIFY(reopened.roomEdit_->text().isEmpty());
        const auto snapshot = qEnvironmentVariable("ZJDESK_REMOTE_HISTORY_SNAPSHOT");
        if (!snapshot.isEmpty())
        {
            reopened.show();
            QVERIFY(QTest::qWaitForWindowExposed(&reopened));
            QVERIFY(reopened.grab().save(snapshot + "-window.png"));
            auto *historyButton = reopened.roomEdit_->findChild<QToolButton *>();
            QVERIFY(historyButton);
            QTest::mouseClick(historyButton, Qt::LeftButton);
            QTest::qWait(50);
            QVERIFY(QApplication::activePopupWidget());
            QVERIFY(QApplication::activePopupWidget()->grab().save(snapshot + "-popup.png"));
            saved->hidePopup();
        }
        saved->setCurrentIndex(0);
        QVERIFY(QMetaObject::invokeMethod(saved, "activated", Q_ARG(int, 0)));
        QCOMPARE(reopened.roomEdit_->text(), code);
        QVERIFY(reopened.roomEdit_->hasAcceptableInput());
        QCOMPARE(reopened.session_.state(), zjdesk::SessionCoordinator::State::Idle);
        // Reconnecting refreshes the existing entry instead of duplicating it.
        reopened.session_.remotePeerConnected(code, "Renamed host");
        QCOMPARE(saved->count(), 1);
        QVERIFY(saved->itemText(0).contains("Renamed host"));
        for (int i = 0; i < 12; ++i)
            reopened.session_.remotePeerConnected(QString::number(100000000 + i), QString("Host %1").arg(i));
        QCOMPARE(saved->count(), 10);
        QCOMPARE(saved->itemData(0).toString(), "100000011");
        reopened.serverEdit_->setText("ws://127.0.0.1:1");
        QCOMPARE(saved->count(), 0); // Codes from another server must not be suggested.
        reopened.serverEdit_->setText(url);
        QCOMPARE(saved->count(), 10);
        QCOMPARE(saved->itemData(0).toString(), "100000011");
    }

    void remoteCodeSettingsPersistWithoutRoomEntry()
    {
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
        preferences.clear();
        const auto reset = qScopeGuard([&] { preferences.clear(); });
        MainWindow window("ws://127.0.0.1:8765", "old-room", "Tester");
        QVERIFY(!window.findChild<QLineEdit *>("roomId"));
        auto *code = window.findChild<QLineEdit *>("remoteCode");
        QVERIFY(code && code->text().isEmpty());
        code->setText("123456789");
        QVERIFY(code->hasAcceptableInput());
        code->setText("12345678");
        QVERIFY(!code->hasAcceptableInput());
        auto *approval = window.findChild<QCheckBox *>("requireRemoteApproval");
        QVERIFY(approval && approval->isChecked());
        QVERIFY(window.session_.requiresRemoteApproval());
        auto *policy = window.findChild<QComboBox *>("remoteCodePolicy");
        QVERIFY(policy);
        QCOMPARE(policy->count(), 3);
        QCOMPARE(policy->currentData().toString(), "startup");
        policy->setCurrentIndex(1);
        QCOMPARE(window.session_.remoteCodePolicy(), "three_days");
        policy->setCurrentIndex(2);
        approval->setChecked(false);
        MainWindow reopened("ws://127.0.0.1:8765", "", "Tester");
        QVERIFY(!reopened.findChild<QCheckBox *>("requireRemoteApproval")->isChecked());
        QVERIFY(!reopened.session_.requiresRemoteApproval());
        QCOMPARE(reopened.findChild<QComboBox *>("remoteCodePolicy")->currentData().toString(), "never");
        QCOMPARE(reopened.session_.remoteCodePolicy(), "never");
        QVERIFY(zjdesk::protocol::isRemoteIdentityValid(preferences.value("identity").toString()));
    }

    void remoteCodePolicyDoesNotClaimPersistenceWhenSettingsFail()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath("ZJdesk/remote-control.ini"));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        const auto restore = qScopeGuard([this] {
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferenceDirectory_.path());
        });
        MainWindow window("ws://127.0.0.1:8765", "", "Tester");
        auto *policy = window.findChild<QComboBox *>("remoteCodePolicy");
        QVERIFY(policy);
        policy->setCurrentIndex(2);
        QCOMPARE(window.session_.remoteCodePolicy(), "startup");
        QCOMPARE(policy->currentData().toString(), "startup");
        auto *error = window.findChild<QLabel *>("remoteCodePolicyError");
        QVERIFY(error && !error->isHidden() && !error->text().isEmpty());
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
        QCOMPARE(preferences.value("codePolicy", "startup").toString(), "startup");
    }

    void remoteCodeApprovalCanBeAutomaticWithoutDesktopInput()
    {
        zjdesk::SessionCoordinator host;
        if (!host.input_.available() || !host.mediaEnvironment().ready) QSKIP("Native desktop backend required");
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        host.state_ = zjdesk::SessionCoordinator::State::PeerAvailable;
        host.peerId_ = "controller";
        host.remoteAccess_ = true;
        // Supply an existing fake stream so approval neither captures nor injects.
        host.streamId_ = "fixture";
        host.desktopSource_ = true;
        host.shareState_ = zjdesk::SessionCoordinator::PreviewState::Running;
        host.controlChannelReady_ = true;
        const QJsonObject request{{"action", "request"}, {"requestId", "first"}};
        host.receiveControlRequest(request);
        QCOMPARE(host.remoteState(), Remote::Pending);
        host.clearControl();
        host.setRequiresRemoteApproval(false);
        host.receiveControlRequest(request);
        QCOMPARE(host.remoteState(), Remote::Controlled);
        host.clearControl();
        host.remoteAccess_ = false;
        host.receiveControlRequest(request);
        QCOMPARE(host.remoteState(), Remote::Pending); // Legacy rooms cannot bypass consent.
        host.streamId_.clear();
    }

    void remoteCodeAutomaticApprovalCreatesNewAuthorizationAfterReconnect()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator controller, host;
        if (!host.input_.available() || !host.mediaEnvironment().ready) QSKIP("Native desktop backend required");
        using Session = zjdesk::SessionCoordinator;
        using Gate = zjdesk::ControlSession;
        host.setRequiresRemoteApproval(false);
        QVERIFY(controller.startRemoteAccess(url, "Controller"));
        QVERIFY(host.startRemoteAccess(url, "Host"));
        QTRY_VERIFY(!controller.remoteCode().isEmpty() && !host.remoteCode().isEmpty());
        quint64 previousEpoch = 0;
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            controller.shareState_ = Session::PreviewState::Stopping;
            QVERIFY(controller.connectRemote(host.remoteCode()));
            QTRY_COMPARE(controller.state(), Session::State::PeerAvailable);
            QTRY_COMPARE(host.state(), Session::State::PeerAvailable);
            const auto peerId = controller.ownPeerId_;
            // A fake established stream exercises authorization without capture or input injection.
            host.streamId_ = "fixture";
            host.desktopSource_ = true;
            host.shareState_ = Session::PreviewState::Running;
            host.controlChannelReady_ = true;
            host.receiveControlRequest({{"action", "request"}, {"requestId", QString("request-%1").arg(cycle)}});
            QCOMPARE(host.remoteState(), Session::RemoteState::Controlled);
            QCOMPARE(host.control_.state(), Gate::State::Granted);
            const auto epoch = host.control_.epoch();
            QVERIFY(epoch > previousEpoch);
            host.remoteControlEpoch_ = epoch;
            host.inputSequence_ = 7;
            host.controlChannelReady_ = false;
            (cycle == 0 ? controller : host).stopControl();
            QTRY_COMPARE(controller.state(), Session::State::WaitingForPeer);
            QTRY_COMPARE(host.state(), Session::State::WaitingForPeer);
            QCOMPARE(host.control_.state(), Gate::State::Idle);
            QCOMPARE(host.remoteState(), Session::RemoteState::Idle);
            QCOMPARE(host.remoteControlEpoch_, quint64(0));
            QCOMPARE(host.inputSequence_, quint64(0));
            QVERIFY(host.controlRequestId_.isEmpty());
            QVERIFY(!host.screenActive() && !controller.screenActive());
            QVERIFY(!host.control_.acceptInput(peerId, epoch, 8));
            previousEpoch = epoch;
        }
    }

    void remoteDisconnectDoesNotClaimOnlineWhenSendFails()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator controller, host;
        using Session = zjdesk::SessionCoordinator;
        controller.shareState_ = Session::PreviewState::Stopping;
        QVERIFY(controller.startRemoteAccess(url, "Controller"));
        QVERIFY(host.startRemoteAccess(url, "Host"));
        QTRY_VERIFY(!controller.remoteCode().isEmpty() && !host.remoteCode().isEmpty());
        QVERIFY(controller.connectRemote(host.remoteCode()));
        QTRY_COMPARE(controller.state(), Session::State::PeerAvailable);
        QTRY_COMPARE(host.state(), Session::State::PeerAvailable);
        controller.signaling_.close(); // Make the disconnect command fail to send.
        controller.stopControl();
        QCOMPARE(controller.state(), Session::State::Failed);
        QVERIFY(controller.remoteCode().isEmpty());
        QVERIFY(controller.peerId().isEmpty());
        QVERIFY(!controller.remoteConnecting_ && !controller.remoteDialPending_);
        QTRY_COMPARE(host.state(), Session::State::WaitingForPeer);
        QCOMPARE(host.remoteState(), Session::RemoteState::Idle);
        QCOMPARE(server.roomCount(), 0);
    }

    void meetingEmptyExpansionRestoresOnLeave()
    {
        using Session = zjdesk::SessionCoordinator;
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow window(url, {}, "Local");
        window.setWindowFlag(Qt::Tool);
        window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.move(window.screen()->virtualGeometry().left() - 2000, 0);
        window.show();
        window.findChildren<QPushButton *>("navButton").at(1)->click();
        for (auto *surface : {window.meetingLocalVideo_, window.meetingShareVideo_})
        {
            QVERIFY(window.session_.join(QUrl(url), "empty-expansion", "Local", Session::RoomType::Meeting));
            QTRY_COMPARE(window.session_.state(), Session::State::WaitingForPeer);
            QVERIFY(!surface->hasFrame());
            surface->findChild<QPushButton *>("expandFrameButton")->click();
            QTRY_VERIFY(window.isFullScreen());
            if (surface == window.meetingLocalVideo_) window.session_.leave();
            else server.close();
            QTRY_VERIFY(!window.isFullScreen());
            QVERIFY(window.findChild<QPushButton *>("meetingJoinButton")->isVisible());
        }
    }

    void meetingEarlyStopKeepsOtherMembers()
    {
        using Session = zjdesk::SessionCoordinator;
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        Session a, b, c;
        for (auto *peer : {&a, &b, &c})
        {
            QVERIFY(peer->setConferenceDevices({"test", "test", "test"}));
            QVERIFY(peer->join(url, "early-stop", "Peer", Session::RoomType::Meeting));
        }
        for (auto *peer : {&a, &b, &c}) QTRY_COMPARE(peer->memberCount(), 3);
        a.setConferenceAutoConnect(true);
        QVERIFY(!a.meshReady_);
        // Deliver a stop while capture startup is still queued on the media thread.
        a.receiveMeshConference({{"fromPeerId", b.ownPeerId_}, {"action", "stop"},
            {"sessionId", a.meshSessionId(b.ownPeerId_)}});
        QVERIFY(a.conferenceActive());
        c.setConferenceAutoConnect(true);
        QTRY_COMPARE_WITH_TIMEOUT(a.conferencePeerStatus().value(c.ownPeerId_), "running", 15000);
        QCOMPARE(a.conferencePeerStatus().value(b.ownPeerId_), "stopped");
        for (auto *peer : {&a, &b, &c}) peer->leave();
        for (auto *peer : {&a, &b, &c}) QTRY_VERIFY(!peer->conferenceDevicesBusy());

        // When the only remote stops during startup, release capture after ready.
        a.setConferenceAutoConnect(false);
        QVERIFY(a.join(url, "early-stop", "A", Session::RoomType::Meeting));
        QVERIFY(b.join(url, "early-stop", "B", Session::RoomType::Meeting));
        QTRY_COMPARE(a.memberCount(), 2);
        a.setConferenceAutoConnect(true);
        a.receiveMeshConference({{"fromPeerId", b.ownPeerId_}, {"action", "stop"},
            {"sessionId", a.meshSessionId(b.ownPeerId_)}});
        QTRY_COMPARE(a.conferenceState(), Session::ConferenceState::Idle);
        QTRY_VERIFY(!a.conferenceDevicesBusy());
        a.leave(); b.leave();
    }

    void meetingFourVideoTiles()
    {
        using Session = zjdesk::SessionCoordinator;
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow window(url, {}, "Local");
        window.setWindowFlag(Qt::Tool);
        window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.move(window.screen()->virtualGeometry().left() - 2000, 0);
        window.resize(1040, 720);
        window.show();
        window.findChildren<QPushButton *>("navButton").at(1)->click();
        QVERIFY(window.session_.setConferenceDevices({"test", "test", "test"}));
        QVERIFY(window.session_.join(QUrl(url), "four-tiles", "Local", Session::RoomType::Meeting));
        Session b, c, d;
        for (auto *peer : {&b, &c, &d})
        {
            peer->setConferenceAutoConnect(true);
            QVERIFY(peer->setConferenceDevices({"test", "test", "test"}));
            QVERIFY(peer->join(QUrl(url), "four-tiles", "Remote", Session::RoomType::Meeting));
        }
        QTRY_COMPARE_WITH_TIMEOUT(window.session_.conferencePeerStatus().values().count("running"), 3, 15000);
        QCOMPARE(window.findChild<QLabel *>("meetingMemberCount")->text(), QStringLiteral("4 人参与"));
        QVERIFY(window.findChild<QPushButton *>("shareButton")->isEnabled());
        for (auto *surface : QList<VideoSurface *>{window.meetingLocalVideo_} + window.meetingRemoteVideos_)
        {
            QTRY_VERIFY(surface->isVisible() && surface->hasFrame());
            QVERIFY(window.rect().contains(QRect(surface->mapTo(&window, QPoint()), surface->size())));
            QVERIFY(surface->isValid());
            QVERIFY(!surface->grabFramebuffer().isNull());
        }
        const auto surfaces = QList<VideoSurface *>{window.meetingLocalVideo_} + window.meetingRemoteVideos_;
        for (int i = 0; i < surfaces.size(); ++i)
            for (int j = i + 1; j < surfaces.size(); ++j)
                QTRY_VERIFY2(!surfaces.at(i)->geometry().intersects(surfaces.at(j)->geometry()),
                    qPrintable(QString("%1 (%2,%3 %4x%5), %6 (%7,%8 %9x%10), parent height=%11, window height=%12")
                        .arg(surfaces.at(i)->objectName()).arg(surfaces.at(i)->x()).arg(surfaces.at(i)->y())
                        .arg(surfaces.at(i)->width()).arg(surfaces.at(i)->height()).arg(surfaces.at(j)->objectName())
                        .arg(surfaces.at(j)->x()).arg(surfaces.at(j)->y()).arg(surfaces.at(j)->width()).arg(surfaces.at(j)->height())
                        .arg(surfaces.at(i)->parentWidget()->height()).arg(window.height())));
        QDir().mkpath("output");
        QVERIFY(window.grab().save("output/ui-meeting-four-small.png"));
        QImage sharedFrame(1280, 720, QImage::Format_RGB32);
        sharedFrame.fill(QColor("#176ee8"));
        window.meetingShareVideo_->setFrame(sharedFrame);
        const auto normalGeometry = window.geometry();
        for (auto *surface : surfaces + QList<VideoSurface *>{window.meetingShareVideo_})
        {
            auto *parent = surface->parentWidget();
            const auto normalSize = surface->size();
            auto *button = surface->findChild<QPushButton *>("expandFrameButton");
            QVERIFY(button && button->isVisible());
            const auto epoch = window.session_.conferenceEpoch_;
            button->click();
            QTRY_VERIFY(window.isFullScreen());
            QCOMPARE(surface->parentWidget(), parent);
            QTRY_VERIFY(surface->width() > normalSize.width() && surface->height() > normalSize.height());
            QTRY_VERIFY(surface->width() >= window.width() - 40);
            QTRY_VERIFY2(surface->height() >= window.height() - 40,
                qPrintable(QString("%1 height %2 of %3").arg(surface->objectName()).arg(surface->height()).arg(window.height())));
            QVERIFY(button->isVisible());
            QCOMPARE(button->text(), QStringLiteral("缩小"));
            window.refreshMeetingParticipants();
            QTest::qWait(100);
            QTRY_VERIFY2(surface->height() >= window.height() - 40,
                qPrintable(QString("%1 height %2 of %3").arg(surface->objectName()).arg(surface->height()).arg(window.height())));
            for (auto *other : surfaces + QList<VideoSurface *>{window.meetingShareVideo_})
                QCOMPARE(other->isVisible(), other == surface);
            if (surface == surfaces.at(2))
            {
                // A local hide moves the primary remote tile above the local avatar.
                window.session_.setConferenceMuted(false, true, false);
                QTRY_VERIFY(window.session_.conferenceCameraHidden());
                QTest::qWait(100);
                QTRY_VERIFY(surface->height() >= window.height() - 40);
                window.session_.setConferenceMuted(false, false, false);
                QTRY_VERIFY(!window.session_.conferenceCameraHidden());
                QTest::qWait(100);
                QTRY_VERIFY(surface->height() >= window.height() - 40);
            }
            if (surface != window.meetingShareVideo_)
            {
                const auto frames = surface->presentedFrames();
                QTRY_VERIFY(surface->presentedFrames() > frames);
            }
            QVERIFY(!surface->grabFramebuffer().isNull());
            if (surface == window.meetingShareVideo_)
            {
                const auto image = surface->grabFramebuffer();
                QCOMPARE(image.pixelColor(image.rect().center()), QColor("#176ee8"));
                QVERIFY(window.grab().save("output/ui-meeting-expanded-share.png"));
            }
            if (surface == window.meetingLocalVideo_)
                QVERIFY(window.grab().save("output/ui-meeting-expanded-camera.png"));
            button->click();
            QTRY_VERIFY(!window.isFullScreen());
            QTRY_COMPARE(window.geometry(), normalGeometry);
            QTRY_COMPARE(surface->size(), normalSize);
            for (auto *other : surfaces) QVERIFY(other->isVisible());
            QCOMPARE(window.session_.conferenceEpoch_, epoch);
            QCOMPARE(window.session_.conferencePeerStatus().values().count("running"), 3);
            button->click();
            QTRY_VERIFY(window.isFullScreen());
            QTest::keyClick(surface, Qt::Key_Escape);
            QTRY_VERIFY(!window.isFullScreen());
        }
        window.meetingShareVideo_->findChild<QPushButton *>("expandFrameButton")->click();
        QTRY_VERIFY(window.isFullScreen());
        window.meetingShareVideo_->setFrame({});
        QTRY_VERIFY(!window.isFullScreen());
        window.showMaximized();
        QTRY_VERIFY(window.isMaximized());
        window.meetingLocalVideo_->findChild<QPushButton *>("expandFrameButton")->click();
        QTRY_VERIFY(window.isFullScreen());
        window.meetingLocalVideo_->findChild<QPushButton *>("expandFrameButton")->click();
        QTRY_VERIFY(window.isMaximized() && !window.isFullScreen());
        window.showNormal();
        window.setGeometry(normalGeometry);
        auto *leavingSurface = *std::find_if(window.meetingRemoteVideos_.begin(), window.meetingRemoteVideos_.end(),
            [&b](VideoSurface *surface) { return surface->property("peerId").toString() == b.ownPeerId_; });
        leavingSurface->findChild<QPushButton *>("expandFrameButton")->click();
        QTRY_VERIFY(window.isFullScreen());
        b.leave();
        QTRY_COMPARE(window.session_.memberCount(), 3);
        QTRY_VERIFY(!window.isFullScreen());
        QTRY_COMPARE(window.session_.conferencePeerStatus().values().count("running"), 2);
        QTRY_VERIFY(window.meetingRemoteVideos_.last()->isHidden());
        for (auto *surface : window.meetingRemoteVideos_.mid(0, 2)) QTRY_VERIFY(surface->hasFrame());
        const auto timedOut = c.ownPeerId_;
        window.session_.meshDeadlines_[timedOut] = QDeadlineTimer(0);
        QVERIFY(QMetaObject::invokeMethod(&window.session_.meshDeadline_, "timeout", Qt::DirectConnection));
        QTRY_COMPARE(window.session_.conferencePeerStatus().values().count("running"), 1);
        QCOMPARE(window.session_.conferencePeerStatus().value(timedOut), "failed");
        QCOMPARE(window.session_.conferencePeerStatus().value(d.ownPeerId_), "running");
        QCOMPARE(window.session_.conferenceState(), Session::ConferenceState::Running);
        window.session_.leave(); c.leave(); d.leave();
    }

    void fileClipboardNativePasteAndLocalCopyCancels()
    {
        auto *cb = QGuiApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        for (const auto &format : cb->mimeData()->formats()) saved->setData(format, cb->mimeData()->data(format));
        const auto restore = qScopeGuard([&] { cb->setMimeData(saved.release()); });
        cb->setText("ZJdesk native clipboard fixture");
        QTest::qWait(20);
        zjdesk::FileClipboard outbound;
        outbound.start(true, 1);
        QSignalSpy echoed(&outbound, &zjdesk::FileClipboard::sendMessage);
        zjdesk::FileClipboard receiver;
        receiver.start(false, 1);
        QSignalSpy ready(&receiver, &zjdesk::FileClipboard::filesReady);
        const QJsonObject offer{{"action", "offer"}, {"epoch", "1"}, {"id", "batch"}, {"count", 1}};
        receiver.receive(offer);
        receiver.receive({{"action", "entry"}, {"epoch", "1"}, {"id", "batch"}, {"index", 0},
            {"path", QStringLiteral("中文 empty.txt")}, {"size", "0"}, {"offset", "0"}, {"data", ""}, {"eof", true}});
        QCOMPARE(ready.size(), 1);
        const auto urls = qvariant_cast<QList<QUrl>>(ready.first().first());
        QCOMPARE(cb->mimeData()->urls(), urls);
        QByteArray gnome = "copy\n";
        for (const auto &url : urls) gnome += url.toEncoded() + '\n';
        QCOMPARE(cb->mimeData()->data("x-special/gnome-copied-files"), gnome);
        QTest::qWait(20);
        QCOMPARE(echoed.size(), 0);
#ifdef Q_OS_WIN
        QVERIFY(IsClipboardFormatAvailable(CF_HDROP));
#endif
        receiver.receive(offer);
        cb->setText("local clipboard wins");
        QTRY_VERIFY(receiver.status().contains(QStringLiteral("本机剪贴板已改变")));
        receiver.receive({{"action", "entry"}, {"epoch", "1"}, {"id", "batch"}, {"index", 0},
            {"path", "discarded"}, {"size", "0"}, {"offset", "0"}, {"data", ""}, {"eof", true}});
        QCOMPARE(ready.size(), 1);
        QCOMPARE(cb->text(), QString("local clipboard wins"));
    }

    void fileClipboardBusyDiscardsUnpublishedCache()
    {
#ifdef Q_OS_WIN
        auto *cb = QGuiApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        for (const auto &format : cb->mimeData()->formats()) saved->setData(format, cb->mimeData()->data(format));
        const auto restore = qScopeGuard([&] { cb->setMimeData(saved.release()); });
        cb->setText("ZJdesk clipboard busy fixture");
        QTest::qWait(20); // Deliver the fixture's clipboard change before starting a transfer.
        zjdesk::FileClipboard receiver;
        receiver.start(false, 1);
        QSignalSpy ready(&receiver, &zjdesk::FileClipboard::filesReady);
        const QDir temp(QDir::tempPath());
        const auto previous = temp.entryList({"zjdesk-files-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        receiver.receive({{"action", "offer"}, {"epoch", "1"}, {"id", "busy"}, {"count", 1}});
        auto created = temp.entryList({"zjdesk-files-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &path : previous) created.removeAll(path);
        QCOMPARE(created.size(), 1);
        const auto cache = temp.filePath(created.first());
        std::atomic<bool> release = false;
        std::atomic<int> opened = -1;
        std::thread lockClipboard([&] {
            const bool locked = OpenClipboard(nullptr);
            opened = locked ? 1 : 0;
            if (!locked) return;
            while (!release.load()) Sleep(1);
            CloseClipboard();
        });
        const auto unlock = qScopeGuard([&] { release = true; if (lockClipboard.joinable()) lockClipboard.join(); });
        QTRY_COMPARE(opened.load(), 1);
        receiver.receive({{"action", "entry"}, {"epoch", "1"}, {"id", "busy"}, {"index", 0},
            {"path", "unpublished.txt"}, {"size", "0"}, {"offset", "0"}, {"data", ""}, {"eof", true}});
        release = true;
        lockClipboard.join();
        QTest::qWait(20);
        QCOMPARE(ready.size(), 0);
        QVERIFY(receiver.status().contains(QStringLiteral("剪贴板被占用")));
        QVERIFY(!QFileInfo::exists(cache));
#else
        QSKIP("Windows native clipboard contention only");
#endif
    }

    void fileClipboardRequiresGrantAndCurrentEpoch()
    {
        auto *cb = QGuiApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        for (const auto &format : cb->mimeData()->formats()) saved->setData(format, cb->mimeData()->data(format));
        const auto restore = qScopeGuard([&] { cb->setMimeData(saved.release()); });
        cb->setText("ZJdesk authorization fixture");
        zjdesk::SessionCoordinator session;
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        session.controlChannelReady_ = true;
        session.startFileClipboard();
        QVERIFY(!session.fileClipboard_.active()); // Ordinary sharing is not authorization.
        session.remoteState_ = Remote::Requesting;
        session.receivingScreen_ = true;
        session.controlRequestId_ = "current";
        session.receiveControl({{"action", "grant"}, {"epoch", "8"}, {"requestId", "stale"}, {"fileClipboard", true}});
        QVERIFY(!session.fileClipboard_.active());
        session.controlChannelReady_ = false;
        session.receiveControl({{"action", "grant"}, {"epoch", "8"}, {"requestId", "current"}, {"fileClipboard", true}, {"fileClipboardReverse", true}});
        QVERIFY(!session.fileClipboard_.active());
        session.controlChannelReady_ = true;
        session.startFileClipboard();
        QVERIFY(session.fileClipboard_.active());
        QVERIFY(session.reverseFileClipboard_.active());
        session.fileClipboard_.receive({{"action", "offer"}, {"epoch", "8"}, {"id", "forward"}, {"count", 1}});
        QVERIFY(session.remoteDetail().contains(QStringLiteral("对方 → 本机：")));
        session.clearControl();
        QVERIFY(!session.fileClipboard_.active());
        QVERIFY(!session.reverseFileClipboard_.active());
        QVERIFY(session.control_.request("peer"));
        QVERIFY(session.control_.grant());
        session.remoteState_ = Remote::Controlled;
        session.receivingScreen_ = false;
        session.startFileClipboard();
        QVERIFY(!session.fileClipboard_.active()); // Peer must explicitly acknowledge file support.
        session.receiveControl({{"action", "file_ready"}, {"epoch", QString::number(session.control_.epoch() + 1)}});
        QVERIFY(!session.fileClipboard_.active());
        session.receiveControl({{"action", "file_ready"}, {"epoch", QString::number(session.control_.epoch())}});
        QVERIFY(session.fileClipboard_.active());
        QVERIFY(!session.reverseFileClipboard_.active()); // Older peers retain one-way support.
        session.receiveControl({{"action", "file_ready"}, {"epoch", QString::number(session.control_.epoch())}, {"reverse", true}});
        QVERIFY(session.reverseFileClipboard_.active());
        session.reverseFileClipboard_.receive({{"action", "offer"}, {"epoch", QString::number(session.control_.epoch())},
            {"id", "reverse"}, {"count", 1}});
        QVERIFY(session.remoteDetail().contains(QStringLiteral("对方 → 本机：")));
        session.clearControl();
        QVERIFY(!session.fileClipboard_.active());
        QVERIFY(!session.reverseFileClipboard_.active());
    }

    void fileClipboardAcrossMachines()
    {
        const auto role = qEnvironmentVariable("ZJDESK_FILE_CLIPBOARD_FIXTURE");
        if (role != "source" && role != "controller") QSKIP("Explicit Linux/Windows clipboard validation only");
        auto *cb = QGuiApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        for (const auto &format : cb->mimeData()->formats()) saved->setData(format, cb->mimeData()->data(format));
        const auto restore = qScopeGuard([&] { cb->setMimeData(saved.release()); });
        cb->setText("ZJdesk file clipboard fixture");
        zjdesk::SessionCoordinator session;
        zjdesk::RoomServer server;
        connect(&session.fileClipboard_, &zjdesk::FileClipboard::statusChanged, this,
            [&] { qInfo().noquote() << "File clipboard:" << session.fileClipboard_.status(); });
        connect(&session.reverseFileClipboard_, &zjdesk::FileClipboard::statusChanged, this,
            [&] { qInfo().noquote() << "Reverse file clipboard:" << session.reverseFileClipboard_.status(); });
        if (role == "source") QVERIFY(server.listen(QHostAddress::AnyIPv4, 18767));
        const QUrl url(role == "source" ? "ws://127.0.0.1:18767" : qEnvironmentVariable("ZJDESK_TEST_SERVER"));
        QVERIFY(session.join(url, "file-clipboard-test", role));
        const auto failure = qScopeGuard([&] { if (QTest::currentTestFailed()) qWarning().noquote() << session.detail() << session.remoteDetail() << session.shareDetail(); });
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), zjdesk::SessionCoordinator::State::PeerAvailable, 60000);
        const QByteArray payload = QByteArray::fromHex("00ff12ab") + QByteArray(300000, '\x91');
        if (role == "source")
        {
            // Automatic consent exists only in this explicitly selected test fixture.
            QTRY_COMPARE_WITH_TIMEOUT(session.remoteState(), zjdesk::SessionCoordinator::RemoteState::Pending, 30000);
            QVERIFY(session.grantControl());
            QTRY_VERIFY_WITH_TIMEOUT(session.fileClipboard_.active(), 60000);
            QVERIFY(session.reverseFileClipboard_.active());
            QSignalSpy returned(&session.reverseFileClipboard_, &zjdesk::FileClipboard::filesReady);
            QSignalSpy forwardMessages(&session.fileClipboard_, &zjdesk::FileClipboard::sendMessage);
            QTemporaryDir source;
            QVERIFY(QDir().mkpath(source.filePath(QStringLiteral("中文 文件夹/empty"))));
            QFile file(source.filePath(QStringLiteral("中文 文件夹/binary.bin")));
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(payload), payload.size()); file.close();
            auto *mime = new QMimeData;
            mime->setData("x-special/gnome-copied-files", "copy\n" + QUrl::fromLocalFile(source.filePath(QStringLiteral("中文 文件夹"))).toEncoded() + '\n');
            cb->setMimeData(mime);
            QTRY_VERIFY_WITH_TIMEOUT(returned.size() == 1 || session.remoteState() == zjdesk::SessionCoordinator::RemoteState::Idle, 60000);
            QCOMPARE(returned.size(), 1);
            const auto urls = qvariant_cast<QList<QUrl>>(returned.first().first());
            QCOMPARE(urls.size(), 2);
            QFile received(QDir(urls[0].toLocalFile()).filePath("binary.bin"));
            QVERIFY(received.open(QIODevice::ReadOnly)); QCOMPARE(received.readAll(), payload); received.close();
            QVERIFY(QFileInfo(QDir(urls[0].toLocalFile()).filePath("empty")).isDir());
            QCOMPARE(QFileInfo(urls[1].toLocalFile()).size(), qint64(0));
            QCOMPARE(cb->mimeData()->urls(), urls);
#ifdef Q_OS_LINUX
            // GTK reads the X11 selection in another process, then GIO copies its files.
            QTemporaryDir pasted;
            QProcess paste;
            paste.start("python3", {"-c", QString::fromLatin1(R"PY(
import gi, sys
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, Gio
selection = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD).wait_for_contents(Gdk.Atom.intern('x-special/gnome-copied-files', False))
assert selection is not None
data = selection.get_data()
lines = data.splitlines()
assert lines[0] == b'copy' and len(lines) == 3
destination = Gio.File.new_for_path(sys.argv[1])
for uri in lines[1:]:
    source = Gio.File.new_for_uri(uri.decode('utf-8'))
    target = destination.get_child(source.get_basename())
    if source.query_file_type(Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, None) == Gio.FileType.DIRECTORY:
        target.make_directory(None)
        source.get_child('binary.bin').copy(target.get_child('binary.bin'), Gio.FileCopyFlags.NONE, None, None, None)
        assert source.get_child('empty').query_file_type(Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, None) == Gio.FileType.DIRECTORY
        target.get_child('empty').make_directory(None)
    else:
        source.copy(target, Gio.FileCopyFlags.NONE, None, None, None)
sys.stdout.buffer.write(data)
)PY"), pasted.path()});
            QVERIFY(paste.waitForStarted());
            QTRY_COMPARE_WITH_TIMEOUT(paste.state(), QProcess::NotRunning, 15000);
            QVERIFY2(paste.exitCode() == 0, paste.readAllStandardError().constData());
            QCOMPARE(paste.readAllStandardOutput(), cb->mimeData()->data("x-special/gnome-copied-files"));
            const auto pastedRoot = QDir(pasted.path()).filePath(QFileInfo(urls[0].toLocalFile()).fileName());
            QFile pastedFile(QDir(pastedRoot).filePath("binary.bin"));
            QVERIFY(pastedFile.open(QIODevice::ReadOnly)); QCOMPARE(pastedFile.readAll(), payload);
            QVERIFY(QFileInfo(QDir(pastedRoot).filePath("empty")).isDir());
            QCOMPARE(QFileInfo(QDir(pasted.path()).filePath("zero")).size(), qint64(0));
#endif
            QTRY_COMPARE_WITH_TIMEOUT(session.remoteState(), zjdesk::SessionCoordinator::RemoteState::Idle, 60000);
            QVERIFY(QFileInfo::exists(file.fileName())); // Copy never deletes the Linux source.
            QVERIFY(QFileInfo::exists(urls[0].toLocalFile())); // Imported files survive revoke.
            int offers = 0;
            for (const auto &message : forwardMessages)
                if (message.first().toJsonObject().value("action") == "offer") ++offers;
            QCOMPARE(offers, 1); // Received Windows files do not echo back.
            qInfo() << "Windows -> Linux X11 GTK clipboard and GIO copy: binary, Unicode folder, empty directory and empty file verified";
        }
        else
        {
            QTRY_VERIFY_WITH_TIMEOUT(session.canRequestControl(), 30000);
            QVERIFY(session.requestControl());
            QSignalSpy ready(&session.fileClipboard_, &zjdesk::FileClipboard::filesReady);
            QTRY_VERIFY_WITH_TIMEOUT(ready.size() == 1 || session.remoteState() == zjdesk::SessionCoordinator::RemoteState::Idle
                || session.shareState() == zjdesk::SessionCoordinator::PreviewState::Failed
                || session.fileClipboard_.status().contains(QStringLiteral("取消"))
                || session.fileClipboard_.status().contains(QStringLiteral("超时")), 60000);
            QCOMPARE(ready.size(), 1);
            const auto urls = qvariant_cast<QList<QUrl>>(ready.first().first());
            QCOMPARE(urls.size(), 1);
            QFile file(QDir(urls[0].toLocalFile()).filePath("binary.bin"));
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), payload); file.close();
            QVERIFY(QFileInfo(QDir(urls[0].toLocalFile()).filePath("empty")).isDir());
            QCOMPARE(cb->mimeData()->urls(), urls);
#ifdef Q_OS_WIN
            QVERIFY(IsClipboardFormatAvailable(CF_HDROP));
            QString nativePath;
            {
                bool opened = false;
                QTRY_VERIFY(opened || (opened = OpenClipboard(nullptr)));
                auto close = qScopeGuard([] { CloseClipboard(); });
                const auto drop = reinterpret_cast<HDROP>(GetClipboardData(CF_HDROP));
                QVERIFY(drop);
                QCOMPARE(DragQueryFileW(drop, 0xffffffff, nullptr, 0), UINT(1));
                const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
                nativePath = QString(int(length + 1), QChar(0));
                QCOMPARE(DragQueryFileW(drop, 0, reinterpret_cast<wchar_t *>(nativePath.data()), length + 1), length);
                nativePath.resize(int(length));
                QCOMPARE(QDir::fromNativeSeparators(nativePath), urls[0].toLocalFile());
                QVERIFY(CloseClipboard());
                close.dismiss();
            }
            QTemporaryDir pasted;
            const auto from = nativePath + QChar(0) + QChar(0);
            const auto to = QDir::toNativeSeparators(pasted.path()) + QChar(0) + QChar(0);
            SHFILEOPSTRUCTW operation{};
            operation.wFunc = FO_COPY;
            operation.pFrom = reinterpret_cast<const wchar_t *>(from.utf16());
            operation.pTo = reinterpret_cast<const wchar_t *>(to.utf16());
            operation.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI;
            QCOMPARE(SHFileOperationW(&operation), 0);
            QVERIFY(!operation.fAnyOperationsAborted);
            const auto pastedRoot = QDir(pasted.path()).filePath(QFileInfo(nativePath).fileName());
            QFile pastedFile(QDir(pastedRoot).filePath("binary.bin"));
            QVERIFY(pastedFile.open(QIODevice::ReadOnly));
            QCOMPARE(pastedFile.readAll(), payload);
            QVERIFY(QFileInfo(QDir(pastedRoot).filePath("empty")).isDir());
#endif
            QVERIFY(session.reverseFileClipboard_.active());
            QTemporaryDir windowsSource;
            const auto folder = windowsSource.filePath(QStringLiteral("Windows 中文文件夹"));
            QVERIFY(QDir().mkpath(QDir(folder).filePath("empty")));
            QFile original(QDir(folder).filePath("binary.bin"));
            QVERIFY(original.open(QIODevice::WriteOnly)); QCOMPARE(original.write(payload), payload.size()); original.close();
            QFile zero(windowsSource.filePath("zero")); QVERIFY(zero.open(QIODevice::WriteOnly)); zero.close();
            QTest::qWait(100); // Let native clipboard notifications after the Shell copy finish.
            auto *files = new QMimeData;
            files->setUrls({QUrl::fromLocalFile(folder), QUrl::fromLocalFile(zero.fileName())});
            cb->setMimeData(files);
            QVERIFY(cb->ownsClipboard());
            QTRY_VERIFY_WITH_TIMEOUT(session.reverseFileClipboard_.status().contains(QStringLiteral("文件已到对方"))
                || session.remoteState() == zjdesk::SessionCoordinator::RemoteState::Idle, 60000);
            QVERIFY(session.reverseFileClipboard_.status().contains(QStringLiteral("文件已到对方")));
            QVERIFY(QFileInfo::exists(original.fileName()));
            session.stopControl();
            QVERIFY(QFileInfo::exists(file.fileName()));
            qInfo() << "Linux X11 GNOME clipboard -> authorized WebRTC file channel -> Windows native file clipboard and Shell copy: 300004 binary bytes, Unicode folder and empty directory verified";
        }
        session.leave();
    }

    void init()
    {
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "meeting");
        preferences.clear();
    }

    void meetingSettingsDeviceRowsDoNotOverlap_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("normal") << 660;
        QTest::newRow("minimum") << 1;
    }

    void meetingSettingsDeviceRowsDoNotOverlap()
    {
        QFETCH(int, width);
        MainWindow window("ws://127.0.0.1:8765", {}, "Tester");
        auto *settings = window.findChild<QWidget *>("meetingSettings");
        QVERIFY(settings);
        settings->resize(width, 1);
        settings->setAttribute(Qt::WA_ShowWithoutActivating);
        settings->show();
        QVERIFY(QTest::qWaitForWindowExposed(settings));
        QVERIFY(QDir().mkpath("output"));
        QVERIFY(settings->grab().save(width == 660 ? "output/ui-settings-device-rows.png"
                                                   : "output/ui-settings-device-rows-minimum.png"));
        for (const auto &device : {QStringLiteral("Camera"), QStringLiteral("Microphone"), QStringLiteral("Speaker")})
        {
            auto *combo = window.findChild<QComboBox *>("conference" + device);
            auto *check = window.findChild<QCheckBox *>("join" + device + "Enabled");
            QVERIFY(combo && check && combo->isVisible() && check->isVisible());
            const QRect comboRect(combo->mapTo(settings, QPoint()), combo->size());
            const QRect checkRect(check->mapTo(settings, QPoint()), check->size());
            QVERIFY2(comboRect.bottom() + 6 <= checkRect.top(), qPrintable(
                QString("%1: combo y=%2 height=%3 hint=%4 min=%5; checkbox y=%6 height=%7 hint=%8; card height=%9 hint=%10")
                    .arg(device).arg(comboRect.y()).arg(comboRect.height()).arg(combo->sizeHint().height())
                    .arg(combo->minimumHeight()).arg(checkRect.y()).arg(checkRect.height())
                    .arg(check->sizeHint().height()).arg(combo->parentWidget()->height())
                    .arg(combo->parentWidget()->sizeHint().height())));
            QVERIFY2(check->height() >= check->fontMetrics().height(), qPrintable(device));
            QVERIFY2(check->width() >= check->sizeHint().width(), qPrintable(device));
        }
        QList<QRect> occupied;
        for (auto *widget : settings->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (!widget->isVisible())
                continue;
            QVERIFY(settings->rect().contains(widget->geometry()));
            for (const auto &rect : occupied)
                QVERIFY2(!rect.intersects(widget->geometry()), qPrintable(widget->objectName()));
            occupied.append(widget->geometry());
        }
    }

    void meetingJoinDefaultsPersist()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Tester");
        for (const auto *name : {"joinCameraEnabled", "joinMicrophoneEnabled", "joinSpeakerEnabled"})
        {
            auto *check = window.findChild<QCheckBox *>(name);
            QVERIFY(check && check->isChecked());
            check->setChecked(false);
        }
        MainWindow reopened("ws://127.0.0.1:8765", "demo", "Tester");
        for (const auto *name : {"joinCameraEnabled", "joinMicrophoneEnabled", "joinSpeakerEnabled"})
            QVERIFY(!reopened.findChild<QCheckBox *>(name)->isChecked());
        QVERIFY(!reopened.session_.conferenceJoinCameraEnabled_);
        QVERIFY(!reopened.session_.conferenceJoinMicrophoneEnabled_);
        QVERIFY(!reopened.session_.conferenceJoinSpeakerEnabled_);
        for (const auto *name : {"conferenceMuteMicrophone", "conferenceMuteSpeaker", "conferenceHideCamera"})
            QVERIFY(reopened.findChild<QPushButton *>(name)->isChecked());
    }

    void roomEntriesAreMutuallyExclusive()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow window(url, "room", "Tester");
        auto *assistance = window.findChild<QPushButton *>("primaryButton");
        auto *meeting = window.findChild<QPushButton *>("meetingJoinButton");
        auto *meetingRoom = window.findChild<QLineEdit *>("meetingRoomId");
        QVERIFY(assistance && meeting && meetingRoom);
        meeting->click();
        QCOMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::Failed);
        QCOMPARE(window.session_.roomType(), zjdesk::SessionCoordinator::RoomType::Meeting);
        auto *meetingStatus = window.findChild<QLabel *>("conferenceStatus");
        QVERIFY(meetingStatus);
        QVERIFY(meetingStatus->text().contains(window.session_.detail()));
        meetingRoom->setText("room");
        assistance->click();
        QVERIFY(!meeting->isEnabled());
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        QVERIFY(!window.session_.remoteCode().isEmpty());
        QVERIFY(window.findChild<QLabel *>("localRemoteCode")->text().contains(window.session_.remoteCode()));
        QVERIFY(meetingRoom->isEnabled());
        QVERIFY(meeting->isEnabled());
        meeting->click();
        QVERIFY(!assistance->isEnabled());
        QTRY_COMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
        meeting->click();
        QTRY_VERIFY(window.session_.remoteAccess() && !window.session_.remoteCode().isEmpty());
        QCOMPARE(window.session_.state(), zjdesk::SessionCoordinator::State::WaitingForPeer);
    }

    void enabledDesktopCameraSettingsDoNotPromptAgain()
    {
#ifdef Q_OS_WIN
        const QSettings device(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam"), QSettings::NativeFormat);
        const QSettings desktop(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam\\NonPackaged"), QSettings::NativeFormat);
        if (device.value("Value").toString() != "Allow" || desktop.value("Value").toString() != "Allow")
            QSKIP("Requires enabled device and desktop camera settings; never changes system permissions");
        CameraSettingsHandler handler;
        qApp->installEventFilter(&handler);
        const auto cleanup = qScopeGuard([&handler] { qApp->removeEventFilter(&handler); });
        MainWindow window("ws://127.0.0.1:1", {}, "Enabled desktop camera");
        QSignalSpy required(&window.session_, &zjdesk::SessionCoordinator::cameraPermissionRequired);
        QSignalSpy devices(&window.session_, &zjdesk::SessionCoordinator::devicesChanged);
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            devices.clear();
            window.refreshDevicesButton_->click();
            QCOMPARE(window.session_.cameraPermissionStatus(), zjdesk::CameraPermission::Allowed);
            QCOMPARE(required.size(), 0);
            QVERIFY(!window.findChild<QMessageBox *>("cameraPermissionDialog"));
            QTRY_VERIFY(!devices.isEmpty());
        }
        QCOMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
#else
        QSKIP("Windows desktop camera settings regression");
#endif
    }

    void cameraPermissionDialogDoesNotGrantAccessItself_data()
    {
        QTest::addColumn<bool>("launchSucceeded");
        QTest::newRow("settings-launch-accepted") << true;
        QTest::newRow("settings-launch-failed") << false;
    }

    void cameraPermissionDialogDoesNotGrantAccessItself()
    {
        QFETCH(bool, launchSucceeded);
        CameraSettingsHandler handler;
        qApp->installEventFilter(&handler);
        const auto cleanup = qScopeGuard([&handler] {
            qApp->removeEventFilter(&handler);
        });
        MainWindow window("ws://127.0.0.1:1", {}, "Camera permission");
        int settingsRequests = 0;
        window.session_.cameraSettingsOpener_ = [&](QObject *context, zjdesk::CameraSettingsCompletion completed) {
            ++settingsRequests;
            QTimer::singleShot(0, context, [completed, launchSucceeded] {
                completed(launchSucceeded, launchSucceeded ? QString{} : QStringLiteral("模拟系统启动错误"));
            });
        };
        window.session_.cameraPermissionRequired();
        auto *dialog = window.findChild<QMessageBox *>("cameraPermissionDialog");
        QVERIFY(dialog);
        window.session_.cameraPermissionRequired();
        QCOMPARE(window.findChildren<QMessageBox *>("cameraPermissionDialog").size(), 1);
        for (auto *button : dialog->buttons())
            if (dialog->buttonRole(button) == QMessageBox::RejectRole) button->click();
        QTRY_VERIFY(!window.findChild<QMessageBox *>("cameraPermissionDialog"));
        QCOMPARE(settingsRequests, 0);
        QVERIFY(!window.awaitingCameraPermission_);
        QCOMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
        QVERIFY(window.findChild<QComboBox *>("conferenceCamera")->findData("test") < 0);
        window.session_.cameraPermissionRequired();
        dialog = window.findChild<QMessageBox *>("cameraPermissionDialog");
        QVERIFY(dialog);
        QSignalSpy settingsFinished(&window.session_, &zjdesk::SessionCoordinator::cameraPrivacySettingsFinished);
        QSignalSpy devices(&window.session_, &zjdesk::SessionCoordinator::devicesChanged);
        for (auto *button : dialog->buttons())
            if (dialog->buttonRole(button) == QMessageBox::AcceptRole) button->click();
        QCOMPARE(settingsRequests, 1);
        QVERIFY(window.awaitingCameraPermission_);
        QVERIFY(settingsFinished.isEmpty());
        window.session_.openCameraPrivacySettings();
        QCOMPARE(settingsRequests, 1);
        QTRY_COMPARE(settingsFinished.size(), 1);
        QCOMPARE(settingsFinished.first().first().toBool(), launchSucceeded);
        if (launchSucceeded)
        {
            QTRY_VERIFY(!devices.isEmpty());
        }
        else
        {
            auto *error = window.findChild<QMessageBox *>("cameraSettingsError");
            QVERIFY(error);
            QVERIFY(error->text().contains(QStringLiteral("无法打开")));
            QVERIFY(error->text().contains(QStringLiteral("模拟系统启动错误")));
            if (window.session_.cameraPermissionStatus() != zjdesk::CameraPermission::Allowed)
                QVERIFY(window.awaitingCameraPermission_);
            error->button(QMessageBox::Ok)->click();
            QTRY_VERIFY(!window.findChild<QMessageBox *>("cameraSettingsError"));
        }
        const bool awaitingPermission = window.awaitingCameraPermission_;
        devices.clear();
        qApp->applicationStateChanged(Qt::ApplicationActive);
        QCOMPARE(window.session_.cameraPermissionStatus(), zjdesk::cameraPermission());
        QCOMPARE(window.awaitingCameraPermission_,
            window.session_.cameraPermissionStatus() != zjdesk::CameraPermission::Allowed);
        if (awaitingPermission)
            QTRY_VERIFY(!devices.isEmpty());
        QCOMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
        window.session_.openCameraPrivacySettings();
        QCOMPARE(settingsRequests, 2);
        QTRY_COMPARE(settingsFinished.size(), 2);
    }

    void physicalCameraRefreshAndSelection()
    {
        if (!qEnvironmentVariableIsSet("ZJDESK_CAMERA_TEST"))
            QSKIP("Physical camera test requires ZJDESK_CAMERA_TEST=1");
        MainWindow window("ws://127.0.0.1:1", {}, "Camera refresh");
        QSignalSpy devices(&window.session_, &zjdesk::SessionCoordinator::devicesChanged);
        QTRY_VERIFY_WITH_TIMEOUT(!devices.isEmpty(), 15000);
        devices.clear();
        QTRY_VERIFY(window.refreshDevicesButton_->isEnabled());
        window.refreshDevicesButton_->click();
        QTRY_VERIFY_WITH_TIMEOUT(!devices.isEmpty(), 15000);
        QString camera;
        for (const auto &device : window.session_.devices())
            if (camera.isEmpty() && device.kind == "Video/Source")
            {
                camera = device.name;
            }
        QVERIFY2(!camera.isEmpty(), "Refresh did not enumerate a physical camera");
        auto *cameraCombo = window.findChild<QComboBox *>("conferenceCamera");
        QVERIFY(cameraCombo);
        const int cameraIndex = cameraCombo->findData(camera);
        QVERIFY2(cameraIndex >= 0, "Physical camera was not added to the source selector");
        cameraCombo->setCurrentIndex(cameraIndex);
        QCOMPARE(cameraCombo->currentData().toString(), camera);
        QCOMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
        QTRY_VERIFY(window.refreshDevicesButton_->isEnabled());
    }

    void cameraPermissionRefreshResumesAfterPreview()
    {
        MainWindow window("ws://127.0.0.1:1", {}, "Deferred camera refresh");
        if (!window.session_.mediaEnvironment().ready)
            QSKIP("Media SDK is required for the synthetic preview");
        QVERIFY(window.session_.startPreview("test"));
        QTRY_COMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Running);
        window.awaitingCameraPermission_ = true;
        QSignalSpy devices(&window.session_, &zjdesk::SessionCoordinator::devicesChanged);
        qApp->applicationStateChanged(Qt::ApplicationActive);
        QTest::qWait(100);
        QVERIFY(devices.isEmpty());
        window.session_.stopPreview();
        QTRY_COMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
        QTRY_VERIFY(!devices.isEmpty());
        QCOMPARE(window.session_.cameraPermissionStatus(), zjdesk::cameraPermission());
        QCOMPARE(window.awaitingCameraPermission_,
            window.session_.cameraPermissionStatus() != zjdesk::CameraPermission::Allowed);
    }

    void physicalDesktopAcrossMachines()
    {
        if (qEnvironmentVariable("ZJDESK_PHYSICAL_RECEIVER") != "1")
            QSKIP("Explicit physical desktop validation only");
        MainWindow window(qEnvironmentVariable("ZJDESK_TEST_SERVER"), "physical-first-frame", "Windows");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.session_.join(QUrl(qEnvironmentVariable("ZJDESK_TEST_SERVER")), "physical-first-frame", "Windows"));
        QTRY_VERIFY_WITH_TIMEOUT(window.session_.memberCount() == 2
            || window.session_.state() == zjdesk::SessionCoordinator::State::Failed, 30000);
        QVERIFY2(window.session_.memberCount() == 2, qPrintable(window.session_.detail()));
        auto *surface = window.findChild<VideoSurface *>();
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            QElapsedTimer startup;
            startup.start();
            QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(), 15000);
            const auto firstFrameMs = startup.elapsed();
            QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareStats().transport,
                qEnvironmentVariable("ZJDESK_TEST_TRANSPORT", "TCP"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(window.session_.shareStats().processedFps > 0, 5000);
            waitForPresentation(2000);
            const auto stats = window.session_.shareStats();
            // Keep the last receiver measurements even when a following assertion fails.
            qInfo().noquote() << "Physical desktop cycle" << cycle + 1 << "first frame ms" << firstFrameMs
                << QJsonDocument(shareDiagnostics(stats)).toJson(QJsonDocument::Compact);
            QCOMPARE(stats.size, QSize(1920, 1080));
            QVERIFY(stats.deliveredFps > 0);
            QVERIFY(stats.presentedFps > 0);
            QCOMPARE(stats.decoder, QString("vp9dec"));
            QVERIFY(!stats.hardwareDecoder);
            QVERIFY(stats.codecTiming.samples > 0);
            QVERIFY(stats.decodedToSinkTiming.samples > 0);
            QVERIFY(stats.mailboxTiming.samples > 0);
            QVERIFY(stats.presentation.receiveToSwap.samples > 0);
            QVERIFY(!surface->grabFramebuffer().isNull());
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
        MainWindow window(server, "remote-test", zjdesk::PlatformInfo::current().system);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.session_.join(QUrl(server), "remote-test", zjdesk::PlatformInfo::current().system));
        auto *share = window.findChild<QPushButton *>("shareButton");
        auto *allow = window.findChild<QPushButton *>("allowControlButton");
        auto *stop = window.findChild<QPushButton *>("stopControlButton");
        auto *status = window.findChild<QLabel *>("controlStatus");
        QTRY_COMPARE_WITH_TIMEOUT(window.session_.state(), zjdesk::SessionCoordinator::State::PeerAvailable, 120000);
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
        int dragUpdates = 0;
        qint64 dragGapMaxMs = 0;
        QElapsedTimer dragClock;
        connect(&drag, &QSlider::valueChanged, this, [&](int value) {
            if (dragClock.isValid())
                dragGapMaxMs = qMax(dragGapMaxMs, dragClock.elapsed());
            dragClock.restart();
            ++dragUpdates;
            if (qEnvironmentVariable("ZJDESK_DRAG_LATENCY_TEST") == "1")
            {
                QImage code(224, 64, QImage::Format_RGB32);
                QPainter painter(&code);
                for (int bit = 0; bit < 7; ++bit)
                    painter.fillRect(bit * 32, 0, 32, 64, value & (1 << bit) ? Qt::white : Qt::black);
                painter.end();
                result.setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                result.setPixmap(QPixmap::fromImage(code));
            }
        });
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
        bool edgeStressActive = false;
        marker.setGeometry(QGuiApplication::primaryScreen()->geometry().right() - 500, 60, 480, 280);
        if (qEnvironmentVariable("ZJDESK_DRAG_LATENCY_TEST") == "1")
            marker.setGeometry(60, 380, 1800, 640);
        marker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        QTimer observations;
        const auto screen = QGuiApplication::primaryScreen();
        connect(&observations, &QTimer::timeout, this, [&] {
            QDir().mkpath("output");
            const bool stress = QFile::exists("output/edge-stress-active");
            if (stress != edgeStressActive)
            {
                edgeStressActive = stress;
                marker.setVisible(!stress);
                target.setVisible(!stress);
                if (!stress)
                {
                    target.raise();
                    target.activateWindow();
                }
            }
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
                {"dragUpdates", dragUpdates}, {"dragGapMaxMs", dragGapMaxMs},
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
        const int fixtureLimit = qMax(240000, qEnvironmentVariableIntValue("ZJDESK_PERFORMANCE_DURATION_MS") + 180000);
        while (!completed && completionClock.elapsed() < fixtureLimit)
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

    void inputProbesKeepOverlappingAcknowledgementsAndTraceClicks()
    {
        zjdesk::SessionCoordinator controller;
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        controller.remoteState_ = Remote::Requesting;
        controller.controlChannelReady_ = controller.receivingScreen_ = true;
        controller.controlRequestId_ = "current";
        controller.receiveControl({{"action", "grant"}, {"epoch", "42"}, {"requestId", "current"},
            {"fileClipboard", true}});
        QCOMPARE(controller.remoteState(), Remote::Controlling);
        QVERIFY(controller.fileClipboard_.active());
        QSignalSpy acknowledgements(&controller, &zjdesk::SessionCoordinator::inputProbeAcknowledged);
        controller.sendRemoteInput({{"kind", "unsupported-test-event"}, {"probe", true}});
        controller.sendRemoteInput({{"kind", "unsupported-test-event"}, {"probe", true}});
        const auto acknowledge = [&](int sequence, int epoch = 42, bool accepted = true) {
            controller.receiveControl({{"action", "probe_ack"}, {"epoch", QString::number(epoch)},
                {"sequence", QString::number(sequence)}, {"injectUs", 12}, {"accepted", accepted}});
        };
        acknowledge(1, 41);
        QCOMPARE(acknowledgements.size(), 0);
        acknowledge(1);
        acknowledge(1); // Duplicate replies must not produce duplicate results.
        acknowledge(2);
        QCOMPARE(acknowledgements.size(), 2);
        controller.sendRemoteInput({{"kind", "button"}, {"button", int(Qt::LeftButton)},
            {"pressed", true}, {"x", 0.5}, {"y", 0.5}});
        acknowledge(3, 42, false);
        QCOMPARE(acknowledgements.size(), 3);
        QCOMPARE(acknowledgements.last().at(3).toBool(), false);
        QVERIFY(std::any_of(controller.events()->entries().cbegin(), controller.events()->entries().cend(),
            [](const auto &event) { return event.category == "control-input" && event.message.contains("rttUs="); }));
        controller.sendRemoteInput({{"kind", "button"}, {"button", int(Qt::LeftButton)},
            {"pressed", false}, {"x", 0.5}, {"y", 0.5}, {"probe", false}});
        acknowledge(4);
        QCOMPARE(acknowledgements.size(), 3);
        controller.sendRemoteInput({{"kind", "unsupported-test-event"}, {"probe", true}});
        controller.clearControl();
        QVERIFY(!controller.fileClipboard_.active());
        acknowledge(5);
        QCOMPARE(acknowledgements.size(), 3);
    }

    void motionInputReportsRejectionTimeoutAndRecovery()
    {
        zjdesk::SessionCoordinator controller;
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        controller.remoteState_ = Remote::Requesting;
        controller.controlChannelReady_ = controller.receivingScreen_ = true;
        controller.controlRequestId_ = "current";
        controller.receiveControl({{"action", "grant"}, {"epoch", "42"}, {"requestId", "current"}});
        VideoSurface surface;
        surface.resize(640, 480);
        QImage frame(640, 480, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        connect(&surface, &VideoSurface::remoteInput, &controller, &zjdesk::SessionCoordinator::sendRemoteInput);
        QSignalSpy acknowledgements(&controller, &zjdesk::SessionCoordinator::inputProbeAcknowledged);
        const auto move = [&](int x) {
            QMouseEvent event(QEvent::MouseMove, QPointF(x, 240), QPointF(x, 240),
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&surface, &event);
        };
        move(100);
        QTRY_COMPARE(controller.inputSequence_, quint64(1));
        // Movement must be checked even when the user never clicks.
        QCOMPARE(controller.inputProbeClocks_.size(), 1);
        const auto acknowledge = [&](quint64 sequence, bool accepted, const QString &error = {}, int epoch = 42) {
            controller.receiveControl({{"action", "probe_ack"}, {"epoch", QString::number(epoch)},
                {"sequence", QString::number(sequence)}, {"accepted", accepted}, {"error", error}});
        };
        acknowledge(1, false, "SendInput 0/1 Win32=5", 41);
        QVERIFY(!controller.remoteDetail().contains("SendInput"));
        acknowledge(1, false, "SendInput 0/1 Win32=5");
        QCOMPARE(acknowledgements.size(), 1);
        QVERIFY(controller.remoteDetail().contains("SendInput 0/1 Win32=5"));
        move(110);
        QTRY_COMPARE(controller.inputSequence_, quint64(2));
        QCOMPARE(controller.inputProbeClocks_.size(), 0); // Do not probe every movement.
        controller.sendRemoteInput({{"kind", "move"}, {"x", 0.5}, {"y", 0.5}, {"probe", true}});
        acknowledge(3, true);
        QVERIFY(!controller.remoteDetail().contains("SendInput"));
        controller.sendRemoteInput({{"kind", "move"}, {"x", 0.6}, {"y", 0.5}, {"probe", true}});
        QTRY_VERIFY_WITH_TIMEOUT(controller.remoteDetail().contains(QStringLiteral("未收到输入回执")), 3500);
        QCOMPARE(controller.remoteState(), Remote::Controlling);
        controller.sendRemoteInput({{"kind", "move"}, {"x", 0.7}, {"y", 0.5}, {"probe", true}});
        acknowledge(5, true);
        QVERIFY(!controller.remoteDetail().contains(QStringLiteral("未收到输入回执")));
        controller.stopControl();
        QVERIFY(controller.inputProbeClocks_.isEmpty());
    }

    void inputExecutionDoesNotWaitForHostUiEvents()
    {
        QSemaphore executed;
        zjdesk::SessionCoordinator host;
        host.peerId_ = "controller";
        QVERIFY(host.control_.request(host.peerId_));
        QVERIFY(host.control_.grant());
        host.remoteState_ = zjdesk::SessionCoordinator::RemoteState::Controlled;
        host.sendControlGrant();
        connect(&host.inputReceiver_, &zjdesk::InputReceiver::processed, &host,
            [&](quint64, const QJsonObject &) { executed.release(); }, Qt::DirectConnection);
        std::thread incoming([&] {
            host.screen_.controlReceived(host.shareEpoch_, {{"action", "input"},
                {"epoch", QString::number(host.control_.epoch())}, {"sequence", "1"},
                {"kind", "unsupported-test-event"}, {"probe", true}});
        });
        // No Qt event processing on the host UI thread, as in a native modal loop.
        const bool completed = executed.tryAcquire(1, 2000);
        incoming.join();
        QVERIFY(completed);
        QTRY_VERIFY(host.remoteDetail().contains(QStringLiteral("输入未执行")));
    }

    void inputReceiverReleasesEmptyStateWithoutUiEvents()
    {
        zjdesk::InputInjector capability;
        if (!capability.available()) QSKIP("Native input is unavailable on this desktop");
        QJsonObject result;
        QSemaphore executed;
        zjdesk::InputReceiver receiver;
        receiver.authorize(7, 42);
        connect(&receiver, &zjdesk::InputReceiver::processed, this,
            [&](quint64, const QJsonObject &message) { result = message; executed.release(); }, Qt::DirectConnection);
        // No keys or buttons were pressed; release performs no native key/mouse injection.
        receiver.receive(7, {{"action", "input"}, {"epoch", "42"}, {"sequence", "1"},
            {"kind", "release"}, {"probe", true}});
        QVERIFY(executed.tryAcquire(1, 2000));
        QVERIFY(result.value("accepted").toBool());
    }

    void rejectedHostInputExplainsFailureWithoutNativeInput()
    {
        zjdesk::SessionCoordinator host;
        host.peerId_ = "controller";
        QVERIFY(host.control_.request(host.peerId_));
        QVERIFY(host.control_.grant());
        host.remoteState_ = zjdesk::SessionCoordinator::RemoteState::Controlled;
        host.sendControlGrant();
        host.receiveControl({{"action", "input"}, {"epoch", QString::number(host.control_.epoch())},
            {"sequence", "1"}, {"kind", "unsupported-test-event"}, {"probe", true}});
        QTRY_VERIFY(host.remoteDetail().contains(QStringLiteral("输入未执行")));
        QVERIFY(std::any_of(host.events()->entries().cbegin(), host.events()->entries().cend(),
            [](const auto &event) { return event.category == "control-input" && event.message.contains("accepted=0"); }));
        host.clearControl();
        QVERIFY(!host.remoteDetail().contains(QStringLiteral("输入未执行")));
    }

    void windowsControlHostWithoutDesktopInput()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows native desktop host only");
#else
        if (qEnvironmentVariable("ZJDESK_WINDOWS_CONTROL_HOST") != "1")
            QSKIP("Explicit cross-machine backend validation only");
        using Remote = zjdesk::SessionCoordinator::RemoteState;
        zjdesk::SessionCoordinator host;
        const bool clickAcceptance = qEnvironmentVariable("ZJDESK_WINDOWS_CLICK_ACCEPTANCE") == "1";
        QPushButton clickTarget(QStringLiteral("ZJdesk 远程点击测试"));
        clickTarget.setWindowFlag(Qt::WindowStaysOnTopHint);
        clickTarget.resize(320, 160);
        clickTarget.move(QGuiApplication::primaryScreen()->geometry().center() - clickTarget.rect().center());
        int clicks = 0;
        connect(&clickTarget, &QPushButton::clicked, this, [&] { ++clicks; });
        const auto reportFailure = qScopeGuard([&] {
            if (QTest::currentTestFailed())
                for (const auto &event : host.events()->entries())
                    qWarning().noquote() << event.category << event.message;
        });
        QVERIFY(host.input_.available());
        QVERIFY(host.join(QUrl(qEnvironmentVariable("ZJDESK_TEST_SERVER")), "windows-host-test", "Windows"));
        QSignalSpy probes(&host, &zjdesk::SessionCoordinator::inputProbeInjected);
        // The default path shows no window and injects no native input.
        // Native clicks require the dedicated test target and explicit gate.
        QTRY_COMPARE_WITH_TIMEOUT(host.remoteState(), Remote::Pending, 60000);
        host.stopControl();
        QTRY_COMPARE_WITH_TIMEOUT(host.remoteState(), Remote::Pending, 15000);
        QVERIFY(!host.screenActive());
        if (clickAcceptance)
        {
            clickTarget.show();
            QVERIFY(QTest::qWaitForWindowExposed(&clickTarget));
            clickTarget.raise();
            clickTarget.activateWindow();
        }
        QVERIFY(host.grantControl());
        QTRY_VERIFY_WITH_TIMEOUT(host.shareState() == zjdesk::SessionCoordinator::PreviewState::Running
            || host.shareState() == zjdesk::SessionCoordinator::PreviewState::Failed, 45000);
        QCOMPARE(host.shareState(), zjdesk::SessionCoordinator::PreviewState::Running);
        QTRY_VERIFY_WITH_TIMEOUT(!probes.isEmpty(), 45000);
        QCOMPARE(probes.first().at(2).toBool(), false);
        QTRY_COMPARE_WITH_TIMEOUT(host.remoteState(), Remote::Idle, 45000);
        if (clickAcceptance)
            QCOMPARE(clicks, 20);
        QTRY_VERIFY_WITH_TIMEOUT(!host.screenActive(), 5000);
        host.leave();
#endif
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
                qWarning().noquote() << QJsonDocument(shareDiagnostics(window.session_.shareStats())).toJson(QJsonDocument::Compact);
                qWarning().noquote() << observer.readAllStandardError();
                window.grab().save("output/remote-control-failed.png");
            }
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.session_.join(QUrl(qEnvironmentVariable("ZJDESK_TEST_SERVER")), "remote-test", "Windows"));
        auto *request = window.findChild<QPushButton *>("controlButton");
        auto *status = window.findChild<QLabel *>("controlStatus");
        auto *surface = window.findChild<VideoSurface *>();
        QTRY_VERIFY_WITH_TIMEOUT(request->isEnabled(), 30000);
        request->click();
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY(request->isEnabled());
        request->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("正在控制对方电脑")), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(surface->hasRemoteCursor(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareStats().transport,
            qEnvironmentVariable("ZJDESK_TEST_TRANSPORT", "TCP"), 5000);
        if (!qEnvironmentVariable("ZJDESK_WEBRTC_LOCAL_ADDRESS").isEmpty())
        {
            QTRY_COMPARE_WITH_TIMEOUT(window.session_.shareStats().localAddress,
                qEnvironmentVariable("ZJDESK_WEBRTC_LOCAL_ADDRESS"), 5000);
            QCOMPARE(window.session_.shareStats().remoteAddress,
                qEnvironmentVariable("ZJDESK_TEST_MEDIA_REMOTE_ADDRESS", QUrl(qEnvironmentVariable("ZJDESK_TEST_SERVER")).host()));
        }
        QTRY_VERIFY_WITH_TIMEOUT(observation().contains("fieldX"), 10000);
        const auto normalSize = surface->size();
        auto *expand = window.findChild<QPushButton *>("expandVideoButton");
        expand->click();
        QTRY_VERIFY(window.isFullScreen());
        QTRY_VERIFY(surface->width() > normalSize.width() && surface->height() > normalSize.height());
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto data = observation();
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
                    {"x", x}, {"y", y}, {"pressed", false}, {"probe", false}});
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
            QList<qint64> presentationGapsUs;
            QElapsedTimer swapClock;
            quint64 lastSwapFrames = surface->presentedFrames();
            const auto swapConnection = connect(surface, &QOpenGLWidget::frameSwapped, this, [&] {
                const auto frames = surface->presentedFrames();
                if (frames == lastSwapFrames)
                    return;
                if (swapClock.isValid())
                    presentationGapsUs.append(swapClock.nsecsElapsed() / 1000);
                swapClock.restart();
                lastSwapFrames = frames;
            });
            const auto stopSwaps = qScopeGuard([&] { QObject::disconnect(swapConnection); });
            QElapsedTimer measurement;
            const int performanceDuration = qBound(30000,
                qEnvironmentVariableIntValue("ZJDESK_PERFORMANCE_DURATION_MS"), 600000);
            QProcess edgeStress;
            if (qEnvironmentVariable("ZJDESK_EDGE_STRESS_TEST") == "1")
            {
                const auto display = qEnvironmentVariable("ZJDESK_EDGE_STRESS_DISPLAY", ":77");
                QVERIFY(QRegularExpression(QStringLiteral("^:[0-9]+(?:\\.[0-9]+)?$")).match(display).hasMatch());
                edgeStress.start("C:/Windows/System32/OpenSSH/ssh.exe", {"-o", "BatchMode=yes", host,
                    "python3 " + root + "/tests/scripts/edge_stress.py "
                        + QString::number(performanceDuration / 1000 - 5) + " --display " + display});
                QVERIFY(edgeStress.waitForStarted(5000));
            }
            const auto stopEdgeStress = qScopeGuard([&] {
                if (edgeStress.state() != QProcess::NotRunning)
                {
                    edgeStress.terminate();
                    edgeStress.waitForFinished(3000);
                }
            });
            measurement.start();
            auto previousPresented = surface->presentedFrames();
            qint64 previousTime = 0;
            while (measurement.elapsed() < performanceDuration)
            {
                waitForPresentation(1000);
                if (qEnvironmentVariable("ZJDESK_EDGE_STRESS_TEST") == "1"
                    && edgeStress.state() == QProcess::NotRunning && edgeStress.exitCode() != 0)
                    QFAIL(edgeStress.readAllStandardError().constData());
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
            if (qEnvironmentVariable("ZJDESK_EDGE_STRESS_TEST") == "1")
            {
                if (edgeStress.state() != QProcess::NotRunning)
                    QVERIFY(edgeStress.waitForFinished(10000));
                qInfo().noquote() << edgeStress.readAllStandardOutput();
                QVERIFY2(edgeStress.exitCode() == 0, edgeStress.readAllStandardError().constData());
                waitForPresentation(1000);
            }
            // GNOME may reposition tool windows when Edge stress hides and restores them.
            // Sample the current fixture geometry rather than the pre-stress coordinates.
            const auto beforeStressGeometry = data;
            data = observation();
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
                {"maximumError", maximumDetailError}, {"singlePixelColumns", detailWidth},
                {"beforeStressMarkerX", beforeStressGeometry.value("markerX")},
                {"beforeStressMarkerY", beforeStressGeometry.value("markerY")},
                {"markerX", data.value("markerX")}, {"markerY", data.value("markerY")}}).toJson());
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
                    {"receiverMedia", shareDiagnostics(network)}, {"senderMedia", observation().value("media")},
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
            std::sort(presentationGapsUs.begin(), presentationGapsUs.end());
            const int stalls = int(std::count_if(presentationGapsUs.begin(), presentationGapsUs.end(),
                [](qint64 gap) { return gap > 200000; }));
            report.write(QJsonDocument(QJsonObject{{"samples", samples},
                {"presentedFps", (surface->presentedFrames() - qualityPresentedBefore) * 1000.0 / duration.elapsed()},
                {"presentationGapSamples", presentationGapsUs.size()},
                {"presentationGapP95Us", presentationGapsUs.isEmpty() ? -1
                    : presentationGapsUs.at(presentationGapsUs.size() * 95 / 100)},
                {"presentationGapMaxUs", presentationGapsUs.isEmpty() ? -1 : presentationGapsUs.last()},
                {"presentationStallsOver200Ms", stalls},
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
        QElapsedTimer dragTimeline;
        QList<qint64> commandedAt(101, -1), dragVisibleUs;
        QHash<int, qint64> cursorCommandedAt;
        QList<qint64> cursorReceivedUs;
        const auto cursorUpdates = connect(&window.session_, &zjdesk::SessionCoordinator::remoteCursorChanged, this,
            [&](const zjdesk::RemoteCursor &cursor) {
                const int x = qRound(cursor.position.x() * (cursor.screenSize.width() - 1));
                if (cursor.visible && dragTimeline.isValid() && cursorCommandedAt.contains(x))
                    cursorReceivedUs.append(dragTimeline.nsecsElapsed() / 1000 - cursorCommandedAt.take(x));
            });
        const auto stopCursorUpdates = qScopeGuard([&] { QObject::disconnect(cursorUpdates); });
        int lastDecodedDrag = -1;
        const auto dragFrames = connect(&window.session_, &zjdesk::SessionCoordinator::shareFrame, this,
            [&](const QImage &image) {
                if (qEnvironmentVariable("ZJDESK_DRAG_LATENCY_TEST") != "1"
                    || image.isNull() || !dragTimeline.isValid())
                    return;
                int value = 0;
                for (int bit = 0; bit < 7; ++bit)
                {
                    const auto color = image.pixelColor(data.value("resultX").toInt() + 4 + bit * 32,
                        data.value("resultY").toInt());
                    if (color.red() > 180 && color.green() > 180 && color.blue() > 180)
                        value |= 1 << bit;
                    else if (color.red() > 70 || color.green() > 70 || color.blue() > 70)
                        return;
                }
                if (value > 100 || value <= lastDecodedDrag || commandedAt.at(value) < 0)
                    return;
                lastDecodedDrag = value;
                dragVisibleUs.append(dragTimeline.nsecsElapsed() / 1000 - commandedAt.at(value));
            });
        const auto stopDragFrames = qScopeGuard([&] { QObject::disconnect(dragFrames); });
        dragTimeline.start();
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, point("dragX", "dragY"));
        const auto dragStart = point("dragX", "dragY"), dragEnd = point("dragEndX", "dragEndY");
        for (int step = 1; step <= 120; ++step)
        {
            const auto position = dragStart + (dragEnd - dragStart) * step / 120;
            const int value = qMin(100, qRound(step * 100.0 / 120));
            if (commandedAt.at(value) < 0)
                commandedAt[value] = dragTimeline.nsecsElapsed() / 1000;
            const QSize source(data.value("width").toInt(), data.value("height").toInt());
            const auto size = source.scaled(surface->size(), Qt::KeepAspectRatio);
            const double x = (position.x() - (surface->width() - size.width()) / 2.0) / size.width();
            cursorCommandedAt.insert(qRound(x * (source.width() - 1)), dragTimeline.nsecsElapsed() / 1000);
            QMouseEvent move(QEvent::MouseMove, position, surface->mapToGlobal(position),
                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(surface, &move);
            waitForPresentation(8);
        }
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, point("dragEndX", "dragEndY"));
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("drag").toInt() >= 80, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("dragUpdates").toInt() >= 20, 10000);
        qInfo() << "Remote drag updates" << observation().value("dragUpdates").toInt()
                << "maximum update gap ms" << observation().value("dragGapMaxMs").toInteger();
        QCOMPARE(surface->cursor().shape(), Qt::BlankCursor);
        QTRY_VERIFY_WITH_TIMEOUT(cursorReceivedUs.size() >= 10, 10000);
        std::sort(cursorReceivedUs.begin(), cursorReceivedUs.end());
        qInfo() << "Cursor input to confirmed remote position samples" << cursorReceivedUs.size()
            << "median ms" << cursorReceivedUs.at(cursorReceivedUs.size() / 2) / 1000.0
            << "p95 ms" << cursorReceivedUs.at(cursorReceivedUs.size() * 95 / 100) / 1000.0;
        if (qEnvironmentVariable("ZJDESK_DRAG_LATENCY_TEST") == "1")
        {
            waitForPresentation(1000);
            QVERIFY(dragVisibleUs.size() >= 10);
            std::sort(dragVisibleUs.begin(), dragVisibleUs.end());
            qInfo() << "Drag input to decoded frame samples" << dragVisibleUs.size()
                << "median ms" << dragVisibleUs.at(dragVisibleUs.size() / 2) / 1000.0
                << "p95 ms" << dragVisibleUs.at(dragVisibleUs.size() * 95 / 100) / 1000.0
                << "maximum ms" << dragVisibleUs.last() / 1000.0;
        }
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
        QVERIFY(!surface->hasRemoteCursor());
        QTRY_COMPARE_WITH_TIMEOUT(observation().value("status").toString(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        QTest::keyClicks(surface, "blocked", Qt::NoModifier, 20);
        QTest::qWait(300);
        QCOMPARE(observation().value("text").toString(), QString("zjdesk123"));
        QTRY_VERIFY(!surface->hasFrame());
        QTRY_VERIFY(request->isEnabled());
        request->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("正在控制对方电脑")), 10000);
        QTest::keyPress(surface, Qt::Key_Control);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("controlHeld").toBool(), 10000);
        QVERIFY(observer.write("remote-revoke\n") > 0);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        QTRY_VERIFY(!surface->hasFrame());
        QTRY_VERIFY(request->isEnabled());
        request->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("正在控制对方电脑")), 10000);
        QTest::keyPress(surface, Qt::Key_Control);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("controlHeld").toBool(), 10000);
        window.findChild<QPushButton *>("primaryButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(observation().value("status").toString(), QStringLiteral("远程控制未开启"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!observation().value("controlHeld").toBool(), 10000);
        QVERIFY(observer.write("remote-completed\n") > 0);
        QTRY_VERIFY_WITH_TIMEOUT(observation().value("fixtureCompleted").toBool(), 10000);
    }

    void expandedVideoEscHidesButton()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Viewer");
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        auto *surface = window.findChild<VideoSurface *>();
        auto *expand = window.findChild<QPushButton *>("expandVideoButton");
        QVERIFY(surface);
        QVERIFY(expand && expand->isVisible());
        for (const auto modifiers : {Qt::NoModifier, Qt::ControlModifier})
        {
            expand->click();
            QTRY_VERIFY(window.isFullScreen());
            QVERIFY(!expand->isVisible());
            QTest::keyClick(surface, Qt::Key_Escape, modifiers);
            QTRY_VERIFY(!window.isFullScreen());
            QVERIFY(expand->isVisible());
        }
    }

    void expandedVideoKeepsSurfaceAndRestoresWindow()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Viewer");
#ifdef Q_OS_WIN
        window.setWindowFlag(Qt::Tool);
        window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        window.setAttribute(Qt::WA_ShowWithoutActivating);
#endif
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *surface = window.findChild<VideoSurface *>("videoSurface");
        auto *expand = window.findChild<QPushButton *>("expandVideoButton");
        auto *code = window.findChild<QLabel *>("localRemoteCode");
        QVERIFY(code && code->isVisible());
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
        QVERIFY(!expand->isVisible());
        QVERIFY(!code->isVisible());
        auto *shrink = surface->findChild<QPushButton *>("expandFrameButton");
        QVERIFY(!shrink || !shrink->isVisible());
        QTRY_VERIFY(surface->width() > normalSize.width() && surface->height() > normalSize.height());
        QCOMPARE(window.findChild<VideoSurface *>("videoSurface"), surface);
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
        QVERIFY(expand->isVisible());
        QVERIFY(code->isVisible());
        QVERIFY(window.findChild<QPushButton *>("primaryButton")->isVisible());
        expand->click();
        QTRY_VERIFY(window.isFullScreen());
        QVERIFY(!expand->isVisible());
        QVERIFY(!code->isVisible());
        shrink = surface->findChild<QPushButton *>("expandFrameButton");
        QVERIFY(!shrink || !shrink->isVisible());
        QTest::keyClick(surface, Qt::Key_Escape);
        QTRY_VERIFY(!window.isFullScreen());
        QVERIFY(expand->isVisible());
        QVERIFY(code->isVisible());
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

    void controlSurfaceForwardsTabAndDeleteWithoutLosingFocus()
    {
        QWidget window;
        VideoSurface surface(&window);
        QPushButton next(QStringLiteral("Next"), &window);
        QPushButton other(QStringLiteral("Other"), &window);
        window.resize(700, 500);
        surface.resize(640, 400);
        next.move(0, 420);
        other.move(100, 420);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        surface.setFrame(QImage(1920, 1080, QImage::Format_RGB32));
        surface.setControlEnabled(true);
        surface.setFocus();
        QTRY_VERIFY(surface.hasFocus());
        QSignalSpy input(&surface, &VideoSurface::remoteInput);
        const auto verifyKey = [&](int key) {
            QCOMPARE(input.size(), 2);
            for (int i = 0; i < 2; ++i)
            {
                const auto message = input.at(i).at(0).toJsonObject();
                QCOMPARE(message.value("kind").toString(), QStringLiteral("key"));
                QCOMPARE(message.value("key").toInt(), key);
                QCOMPARE(message.value("pressed").toBool(), i == 0);
            }
            input.clear();
        };
        QTest::keyClick(&surface, Qt::Key_Tab);
        QVERIFY(surface.hasFocus());
        verifyKey(Qt::Key_Tab);
        QTest::keyClick(&surface, Qt::Key_Delete);
        QVERIFY(surface.hasFocus());
        verifyKey(Qt::Key_Delete);
        QTest::keyClick(&surface, Qt::Key_Backtab);
        QVERIFY(surface.hasFocus());
        verifyKey(Qt::Key_Backtab);
        QTest::keyPress(&surface, Qt::Key_Shift);
        QKeyEvent backtabPress(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QKeyEvent backtabRelease(QEvent::KeyRelease, Qt::Key_Backtab, Qt::ShiftModifier);
        QApplication::sendEvent(&surface, &backtabPress);
        QApplication::sendEvent(&surface, &backtabRelease);
        QTest::keyRelease(&surface, Qt::Key_Shift);
        QVERIFY(surface.hasFocus());
        QCOMPARE(input.size(), 4);
        QCOMPARE(input.at(0).at(0).toJsonObject().value("key").toInt(), int(Qt::Key_Shift));
        QCOMPARE(input.at(1).at(0).toJsonObject().value("key").toInt(), int(Qt::Key_Backtab));
        QCOMPARE(input.at(1).at(0).toJsonObject().value("pressed").toBool(), true);
        QCOMPARE(input.at(2).at(0).toJsonObject().value("key").toInt(), int(Qt::Key_Backtab));
        QCOMPARE(input.at(2).at(0).toJsonObject().value("pressed").toBool(), false);
        QCOMPARE(input.at(3).at(0).toJsonObject().value("key").toInt(), int(Qt::Key_Shift));
        input.clear();
        surface.setControlEnabled(false);
        next.setFocus();
        QTRY_VERIFY(next.hasFocus());
        QTest::keyClick(&next, Qt::Key_Tab);
        QTRY_VERIFY(other.hasFocus());
        QCOMPARE(input.size(), 1); // disabling control releases any remote keys
        QCOMPARE(input.first().at(0).toJsonObject().value("kind").toString(), QStringLiteral("release"));
    }

    void controlSurfaceUsesLocalCursorWithoutWaitingForHostPosition()
    {
        // No visible window or native input: exercise the controller event path only.
        VideoSurface surface;
        surface.resize(640, 480);
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        zjdesk::RemoteCursor cursor;
        cursor.image = QImage(48, 48, QImage::Format_ARGB32);
        cursor.image.fill(Qt::red);
        cursor.screenSize = frame.size();
        cursor.hotspot = QPoint(6, 9);
        cursor.position = QPointF(0.1, 0.1);
        cursor.serial = 1;
        cursor.visible = true;
        surface.setRemoteCursor(cursor);
        const auto move = [&](QPoint position) {
            QMouseEvent event(QEvent::MouseMove, position, position,
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&surface, &event);
        };
        move(QPoint(320, 240));
        QVERIFY(!surface.cursor().pixmap().isNull());
        QCOMPARE(surface.cursor().pixmap().size(), QSize(16, 16));
        QCOMPARE(surface.cursor().hotSpot(), QPoint(2, 3));
        const auto shape = surface.cursor().pixmap().cacheKey();
        move(QPoint(500, 240)); // No cursor message or frame arrives from the host.
        QCOMPARE(surface.cursor().pixmap().cacheKey(), shape);
        cursor.position = QPointF(0.2, 0.2); // A delayed position cannot move the local pointer.
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().pixmap().cacheKey(), shape);
        cursor.serial = 2;
        cursor.image.fill(Qt::green);
        surface.setRemoteCursor(cursor);
        QVERIFY(surface.cursor().pixmap().cacheKey() != shape);
        QCOMPARE(surface.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::green));
        QSignalSpy input(&surface, &VideoSurface::remoteInput);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(500, 240));
        QCOMPARE(input.last().at(0).toJsonObject().value("pressed").toBool(), false);
        QCOMPARE(input.last().at(0).toJsonObject().value("x").toDouble(), 500.0 / 640);
        move(QPoint(320, 20)); // Letterbox margins restore the local default cursor.
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        move(QPoint(320, 240));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&surface, &leave);
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        move(QPoint(320, 240));
        cursor.visible = false;
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().shape(), Qt::BlankCursor);
        cursor.visible = true;
        surface.setRemoteCursor(cursor);
        surface.setFrame({});
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        surface.setControlEnabled(false);
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
    }

    void controlSurfaceRefreshesCursorAcrossSessions()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        zjdesk::RemoteCursor cursor;
        cursor.image = QImage(48, 48, QImage::Format_ARGB32);
        cursor.image.fill(Qt::red);
        cursor.screenSize = frame.size();
        cursor.hotspot = QPoint(6, 9);
        cursor.serial = 1;
        cursor.visible = true;
        const auto move = [&] {
            QMouseEvent event(QEvent::MouseMove, QPointF(320, 240), QPointF(320, 240),
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&surface, &event);
        };
        surface.setRemoteCursor(cursor);
        move();
        QCOMPARE(surface.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::red));
        surface.setControlEnabled(false);
        surface.setControlEnabled(true);
        cursor.image.fill(Qt::green); // A new host can reuse the same serial and dimensions.
        surface.setRemoteCursor(cursor);
        move();
        QCOMPARE(surface.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::green));
        surface.setFrame({});
        surface.setFrame(frame);
        cursor.image.fill(Qt::yellow);
        surface.setRemoteCursor(cursor);
        move();
        QCOMPARE(surface.cursor().pixmap().toImage().pixelColor(0, 0), QColor(Qt::yellow));
        cursor.hotspot = QPoint(12, 15);
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().hotSpot(), QPoint(4, 5));
        cursor.hotspot = QPoint(23, 9);
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().hotSpot(), QPoint(8, 3));
        cursor.screenSize.setWidth(1970); // Same rounded bitmap size, different rounded hotspot.
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().pixmap().size(), QSize(16, 16));
        QCOMPARE(surface.cursor().hotSpot(), QPoint(7, 3));
    }

    void controlSurfaceBoundsLocalCursorScaling()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        zjdesk::RemoteCursor cursor;
        cursor.image = QImage(48, 48, QImage::Format_ARGB32);
        cursor.image.fill(Qt::red);
        cursor.screenSize = QSize(48, 48); // Valid protocol data with an oversized display scale.
        cursor.hotspot = QPoint(6, 9);
        cursor.serial = 1;
        cursor.visible = true;
        surface.setRemoteCursor(cursor);
        QMouseEvent event(QEvent::MouseMove, QPointF(320, 240), QPointF(320, 240),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&surface, &event);
        QCOMPARE(surface.cursor().pixmap().size(), QSize(128, 128));
        QCOMPARE(surface.cursor().hotSpot(), QPoint(16, 24));
        cursor.screenSize = QSize(1, 1);
        surface.setRemoteCursor(cursor);
        QCOMPARE(surface.cursor().pixmap().size(), QSize(128, 128));
    }

    void controlSurfaceUpdatesStationaryCursorWhenFrameAspectChanges()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        zjdesk::RemoteCursor cursor;
        cursor.image = QImage(48, 48, QImage::Format_ARGB32);
        cursor.image.fill(Qt::red);
        cursor.screenSize = frame.size();
        cursor.visible = true;
        surface.setRemoteCursor(cursor);
        QMouseEvent event(QEvent::MouseMove, QPointF(320, 100), QPointF(320, 100),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&surface, &event);
        QVERIFY(!surface.cursor().pixmap().isNull());
        QImage wideFrame(1920, 400, QImage::Format_RGB32);
        wideFrame.fill(Qt::blue);
        surface.setFrame(wideFrame); // The stationary pointer is now in a letterbox margin.
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        surface.setFrame(frame);
        QVERIFY(!surface.cursor().pixmap().isNull());
    }

    void controlSurfaceCoalescesMotionAndRestoresCursor()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        QSignalSpy input(&surface, &VideoSurface::remoteInput);
        const auto move = [&](const QPoint &position) {
            QMouseEvent event(QEvent::MouseMove, position, surface.mapToGlobal(position),
                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&surface, &event);
        };
        QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(120, 240));
        input.clear();
        for (int x = 121; x <= 500; ++x)
            move(QPoint(x, 240));
        QTRY_VERIFY_WITH_TIMEOUT(!input.isEmpty(), 200);
        const auto lastMove = input.last().at(0).toJsonObject();
        QCOMPARE(lastMove.value("kind").toString(), QString("move"));
        QCOMPARE(lastMove.value("x").toDouble(), 500.0 / 640);
        QVERIFY(input.size() <= 2);
        QCOMPARE(surface.cursor().shape(), Qt::BlankCursor);
        input.clear();
        move(QPoint(510, 240));
        QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(520, 240));
        QCOMPARE(input.last().at(0).toJsonObject().value("kind").toString(), QString("button"));
        QCOMPARE(input.last().at(0).toJsonObject().value("x").toDouble(), 520.0 / 640);
        const auto releasedCount = input.size();
        QTest::qWait(30);
        QCOMPARE(input.size(), releasedCount);
        move(QPoint(320, 20));
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        move(QPoint(300, 240));
        QFocusEvent lost(QEvent::FocusOut);
        QApplication::sendEvent(&surface, &lost);
        const auto lostCount = input.size();
        QTest::qWait(30);
        QCOMPARE(input.size(), lostCount);
        QCOMPARE(input.last().at(0).toJsonObject().value("kind").toString(), QString("release"));
        move(QPoint(310, 240));
        surface.setControlEnabled(false);
        const auto stoppedCount = input.size();
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        QTest::qWait(30);
        QCOMPARE(input.size(), stoppedCount);
        surface.setControlEnabled(true);
        surface.setFrame({});
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
    }

    void presentationCountsSwapsAndIgnoresCursorOnlyRedraws()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QImage frame(640, 480, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        QCOMPARE(surface.presentedFrames(), quint64(0));
        QTRY_COMPARE(surface.presentedFrames(), quint64(1));
        for (int i = 0; i < 3; ++i)
        {
            surface.update();
            waitForPresentation(30);
        }
        QCOMPARE(surface.presentedFrames(), quint64(1));
        surface.setFrame(frame);
        QTRY_COMPARE(surface.presentedFrames(), quint64(2));
        QVERIFY(surface.takePresentationGapMaxMs() > 0);
        QCOMPARE(surface.takePresentationGapMaxMs(), -1.0);
        surface.setFrame(frame);
        surface.setFrame({});
        waitForPresentation(50);
        QCOMPARE(surface.presentedFrames(), quint64(2));
        surface.setFrame(frame);
        QTRY_COMPARE(surface.presentedFrames(), quint64(3));
        QCOMPARE(surface.takePresentationGapMaxMs(), -1.0);
    }

    void presentationMeasuresBlockedGuiAndResetsOnClear()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QImage frame(640, 480, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setFrame(frame);
        surface.setFrame(frame);
        // Deliberately block this GUI thread to reproduce delivery before a late paint.
        QTest::qSleep(80);
        QTRY_COMPARE(surface.presentedFrames(), quint64(1));
        const auto stats = surface.presentationStatistics();
        QCOMPARE(stats.supersededFrames, quint64(2));
        QCOMPARE(stats.lastFrameId, quint64(3));
        QCOMPARE(stats.receiveToPaint.samples, quint64(1));
        QVERIFY(stats.receiveToPaint.maxMs >= 70);
        QCOMPARE(stats.receiveToSwap.samples, quint64(1));
        QVERIFY(stats.receiveToSwap.maxMs >= stats.receiveToPaint.maxMs);
        QCOMPARE(stats.paint.samples, quint64(1));
        QCOMPARE(stats.gaps.samples, quint64(0));
        surface.update();
        waitForPresentation(50);
        QCOMPARE(surface.presentationStatistics().receiveToSwap.samples, quint64(1));
        surface.setFrame({});
        const auto cleared = surface.presentationStatistics();
        QCOMPARE(cleared.receiveToSwap.samples, quint64(0));
        QCOMPARE(cleared.receiveToSwap.maxMs, -1.0);
        QCOMPARE(cleared.lastFrameId, quint64(0));
        surface.setFrame(frame);
        QTRY_COMPARE(surface.presentedFrames(), quint64(2));
        QCOMPARE(surface.presentationStatistics().lastFrameId, quint64(1));
        QCOMPARE(surface.presentationStatistics().gaps.samples, quint64(0));
    }

    void remoteCursorOverlayUsesHostShapeAndClearsOnStop()
    {
        VideoSurface surface;
        surface.resize(640, 480);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(Qt::blue);
        surface.setFrame(frame);
        surface.setControlEnabled(true);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&surface, &leave); // Host-position overlay applies outside local control.
        const auto center = QPoint(320, 240) * surface.devicePixelRatioF();
        const auto movedCenter = QPoint(480, 240) * surface.devicePixelRatioF();
        QCOMPARE(surface.grabFramebuffer().pixelColor(center), QColor(Qt::blue));
        const auto presented = surface.presentedFrames();
        zjdesk::RemoteCursor cursor;
        cursor.image = QImage(48, 48, QImage::Format_ARGB32_Premultiplied);
        cursor.image.fill(Qt::red);
        cursor.hotspot = QPoint(24, 24);
        cursor.screenSize = frame.size();
        cursor.position = QPointF(0.5, 0.5);
        cursor.visible = true;
        surface.setRemoteCursor(cursor);
        QVERIFY(surface.hasRemoteCursor());
        QCOMPARE(surface.grabFramebuffer().pixelColor(center), QColor(Qt::red));
        QCOMPARE(surface.presentedFrames(), presented);
        cursor.position = QPointF(0.75, 0.5);
        surface.setRemoteCursor(cursor);
        const auto moved = surface.grabFramebuffer();
        QCOMPARE(moved.pixelColor(center), QColor(Qt::blue));
        QCOMPARE(moved.pixelColor(movedCenter), QColor(Qt::red));
        cursor.visible = false;
        surface.setRemoteCursor(cursor);
        QVERIFY(!surface.hasRemoteCursor());
        QCOMPARE(surface.grabFramebuffer().pixelColor(movedCenter), QColor(Qt::blue));
        cursor.visible = true;
        surface.setRemoteCursor(cursor);
        surface.setControlEnabled(false);
        QVERIFY(!surface.hasRemoteCursor());
        QCOMPARE(surface.cursor().shape(), Qt::ArrowCursor);
        QCOMPARE(surface.grabFramebuffer().pixelColor(movedCenter), QColor(Qt::blue));
    }

    void desktopSharingStartsWithFreshKeyframeAfterDelayedNegotiation()
    {
#ifndef Q_OS_LINUX
        QSKIP("Linux X11 capture startup regression");
#else
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        QWidget desktop(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        const auto fill = [&](const QColor &color) {
            auto palette = desktop.palette();
            palette.setColor(QPalette::Window, color);
            desktop.setPalette(palette);
        };
        desktop.setAutoFillBackground(true);
        fill(Qt::red);
        desktop.showFullScreen();
        QVERIFY(QTest::qWaitForWindowExposed(&desktop));
        zjdesk::ScreenShare sender, receiver;
        QList<QPair<QString, QJsonObject>> pending;
        bool forward = false;
        connect(&sender, &zjdesk::ScreenShare::negotiationReady, &receiver,
            [&](quint64, const QString &type, const QJsonObject &message) {
                if (forward)
                    receiver.negotiate(type, message, 9);
                else
                    pending.append({type, message});
            });
        connect(&receiver, &zjdesk::ScreenShare::negotiationReady, &sender,
            [&](quint64, const QString &type, const QJsonObject &message) { sender.negotiate(type, message, 7); });
        QImage captured, firstDecoded;
        connect(&sender, &zjdesk::ScreenShare::frameReady, this,
            [&](quint64, const QImage &image) { captured = image; });
        connect(&receiver, &zjdesk::ScreenShare::frameReady, this,
            [&](quint64, const QImage &image) { if (firstDecoded.isNull()) firstDecoded = image; });
        receiver.start(false, {}, 9);
        sender.start(true, "desktop", 7, 30);
        QTRY_VERIFY_WITH_TIMEOUT(!captured.isNull() && captured.pixelColor(100, 100).red() > 220, 10000);
        QTest::qWait(1000);
        fill(Qt::blue);
        QTRY_VERIFY_WITH_TIMEOUT(captured.pixelColor(100, 100).blue() > 220, 5000);
        QTest::qWait(500);
        QVERIFY(firstDecoded.isNull());
        QVERIFY(!pending.isEmpty());
        forward = true;
        for (const auto &message : pending)
            receiver.negotiate(message.first, message.second, 9);
        QTRY_VERIFY_WITH_TIMEOUT(!firstDecoded.isNull(), 10000);
        const auto pixel = firstDecoded.pixelColor(100, 100);
        QVERIFY2(pixel.blue() > 220 && pixel.red() < 24, qPrintable(pixel.name()));
        sender.stop(8);
        receiver.stop(10);
#endif
    }

    void desktopCursorEmbeddingCanBeDisabledAndRestored()
    {
#ifndef Q_OS_LINUX
        QSKIP("Linux X11 cursor capture only");
#else
        QVERIFY(zjdesk::MediaEnvironment::inspect().ready);
        QWidget desktop(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        auto palette = desktop.palette();
        palette.setColor(QPalette::Window, Qt::blue);
        desktop.setPalette(palette);
        desktop.setAutoFillBackground(true);
        desktop.setCursor(Qt::CrossCursor);
        desktop.showFullScreen();
        QVERIFY(QTest::qWaitForWindowExposed(&desktop));
        const auto originalPointer = QCursor::pos();
        const auto restorePointer = qScopeGuard([&] { QCursor::setPos(originalPointer); });
        QCursor::setPos(desktop.mapToGlobal(desktop.rect().center()));
        zjdesk::InputInjector input;
        const auto cursor = input.cursor();
        QVERIFY(cursor.visible && !cursor.image.isNull());
        zjdesk::ScreenShare sender, receiver;
        connect(&sender, &zjdesk::ScreenShare::negotiationReady, &receiver,
            [&](quint64, const QString &type, const QJsonObject &message) { receiver.negotiate(type, message, 9); });
        connect(&receiver, &zjdesk::ScreenShare::negotiationReady, &sender,
            [&](quint64, const QString &type, const QJsonObject &message) { sender.negotiate(type, message, 7); });
        QImage captured;
        int frames = 0;
        connect(&sender, &zjdesk::ScreenShare::frameReady, this, [&](quint64, const QImage &image) {
            captured = image;
            ++frames;
        });
        receiver.start(false, {}, 9);
        sender.start(true, "desktop", 7, 30);
        QTRY_VERIFY_WITH_TIMEOUT(!captured.isNull(), 20000);
        const auto hasCursor = [&] {
            const auto center = captured.rect().center();
            int changed = 0;
            for (int y = center.y() - 32; y <= center.y() + 32; ++y)
                for (int x = center.x() - 32; x <= center.x() + 32; ++x)
                    if (captured.pixelColor(x, y) != QColor(Qt::blue))
                        ++changed;
            return changed > 4;
        };
        QTRY_VERIFY(hasCursor());
        sender.setCursorEmbedded(false, 6);
        const int beforeOldEpoch = frames;
        QTRY_VERIFY(frames > beforeOldEpoch + 3);
        QVERIFY(hasCursor());
        sender.setCursorEmbedded(false, 7);
        QTRY_VERIFY_WITH_TIMEOUT(!hasCursor(), 5000);
        sender.setCursorEmbedded(true, 7);
        QTRY_VERIFY_WITH_TIMEOUT(hasCursor(), 5000);
        sender.stop(8);
        receiver.stop(10);
#endif
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
        QVERIFY(preferenceDirectory_.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferenceDirectory_.path());
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
        MainWindow alice(url, "", "Alice");
        MainWindow bob(url, "", "Bob");
        alice.show();
        QVERIFY(QTest::qWaitForWindowExposed(&alice));
        bob.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bob));
        auto *aliceRoom = alice.findChild<QLineEdit *>("remoteCode");
        auto *bobRoom = bob.findChild<QLineEdit *>("remoteCode");
        QVERIFY(aliceRoom && bobRoom);
        QVERIFY(aliceRoom->text().isEmpty() && bobRoom->text().isEmpty());
        auto *aliceJoin = alice.findChild<QPushButton *>("primaryButton");
        auto *bobJoin = bob.findChild<QPushButton *>("primaryButton");
        auto *aliceStatus = alice.findChild<QLabel *>("statusBadge");
        auto *bobStatus = bob.findChild<QLabel *>("statusBadge");
        auto *members = alice.findChild<QLabel *>("memberCount");
        auto *surface = alice.findChild<QOpenGLWidget *>();
        QVERIFY(aliceJoin && bobJoin && aliceStatus && bobStatus && members && surface);
        QTRY_VERIFY(surface->isValid());
        QTest::mouseClick(aliceJoin, Qt::LeftButton);
        QCOMPARE(server.roomCount(), 0);
        QTest::mouseClick(bobJoin, Qt::LeftButton);
        QTRY_VERIFY(!alice.session_.remoteCode().isEmpty() && !bob.session_.remoteCode().isEmpty());
        aliceRoom->setText(bob.session_.remoteCode());
        QTest::mouseClick(aliceJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("协作 · 已连接对方"));
        QTRY_COMPARE(bobStatus->text(), QStringLiteral("协作 · 已连接对方"));
        QCOMPARE(members->text(), QStringLiteral("2 / 2 人"));
        QVERIFY(QDir().mkpath("output"));
        alice.raise();
        QTest::qWait(150);
        // Capture the composed native window, including the OpenGL framebuffer.
        QVERIFY(alice.screen()->grabWindow(alice.winId()).save("output/ui-workspace.png"));
        const auto navButtons = alice.findChildren<QPushButton *>("navButton");
        QCOMPARE(navButtons.size(), 3);
        QTest::mouseClick(navButtons.at(2), Qt::LeftButton);
        QTest::qWait(100);
        QVERIFY(alice.screen()->grabWindow(alice.winId()).save("output/ui-diagnostics.png"));
        QTest::mouseClick(bobJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("协作 · 远控已在线"));
        alice.session_.leave();
        QTRY_COMPARE(server.roomCount(), 0);
    }

    void workspaceFitsSmallWindowAndLongNames()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        const QString longName(40, QChar(0x534f));
        MainWindow window(url, "demo", longName);
        const bool backgroundSnapshots = qEnvironmentVariableIntValue("ZJDESK_UI_BACKGROUND_SNAPSHOTS") == 1;
        if (backgroundSnapshots)
        {
            // Opt-in native snapshots without activating the user's desktop.
            window.setWindowFlag(Qt::Tool);
            window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            window.setAttribute(Qt::WA_ShowWithoutActivating);
            window.move(window.screen()->virtualGeometry().left() - 2000, 0);
        }
        window.resize(1040, 720);
        window.show();
        if (backgroundSnapshots)
        {
            QTRY_VERIFY(window.isVisible());
        }
        else
        {
            QVERIFY(QTest::qWaitForWindowExposed(&window));
        }
        QCOMPARE(window.size(), QSize(1040, 720));
        auto *profile = window.findChild<QWidget *>("userProfile");
        auto *profileName = window.findChild<QLineEdit *>("meetingDisplayName");
        auto *profileAvatar = window.findChild<QLabel *>("profileAvatar");
        QVERIFY(profile && profile->isVisible());
        QVERIFY(profile->mapTo(&window, QPoint()).x() > window.width() / 2);
        QVERIFY(window.rect().contains(QRect(profile->mapTo(&window, QPoint()), profile->size())));
        profileName->setText("Zoe");
        QCOMPARE(profileAvatar->text(), QString("Z"));
        QCOMPARE(window.findChild<QLabel *>("localName")->text(), QString("Zoe"));
        profileName->setText(longName);
        QCOMPARE(profileAvatar->text(), longName.left(1));
        QCOMPARE(window.findChild<QLabel *>("localName")->text(), longName);
        auto *settingsButton = window.findChild<QPushButton *>("settingsButton");
        QVERIFY(settingsButton && settingsButton->isVisible());
        QVERIFY(!window.findChild<QPushButton *>("meetingSettingsButton"));
        auto *sidebar = window.findChild<QFrame *>("sidebar");
        const QRect settingsBounds(settingsButton->mapTo(sidebar, QPoint()), settingsButton->size());
        QVERIFY(sidebar->rect().contains(settingsBounds));
        QVERIFY(settingsBounds.center().x() > sidebar->width() / 2);
        QVERIFY(settingsBounds.bottom() > sidebar->height() - 60);
        auto *surface = window.findChild<VideoSurface *>();
        QVERIFY(!surface->grabFramebuffer().isNull());
        QTRY_VERIFY(surface->isValid());
        QVERIFY(surface->width() >= 450);
        QVERIFY(surface->height() >= 300);
        for (auto *widget : {static_cast<QWidget *>(surface), window.findChild<QWidget *>("primaryButton")})
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
        QVERIFY(window.grab().save("output/ui-workspace-small.png"));
        window.findChild<QLineEdit *>("serverAddress")->setText("http://invalid");
        QTest::mouseClick(window.findChild<QPushButton *>("primaryButton"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel *>("statusBadge")->text(), QStringLiteral("连接失败"));
        QVERIFY(window.findChild<QLineEdit *>("serverAddress")->isEnabled());
        QTest::qWait(200);
        QVERIFY(window.grab().save("output/ui-connection-error.png"));
        window.findChild<QLineEdit *>("serverAddress")->setText(url);
        QTest::mouseClick(window.findChild<QPushButton *>("primaryButton"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel *>("statusBadge")->text(), QStringLiteral("协作 · 远控已在线"));
        zjdesk::SessionCoordinator peer;
        QVERIFY(peer.startRemoteAccess(QUrl(url), longName));
        QTRY_VERIFY(!peer.remoteCode().isEmpty());
        QVERIFY(peer.connectRemote(window.session_.remoteCode()));
        QTRY_COMPARE(window.session_.memberCount(), 2);
        QCOMPARE(window.size(), QSize(1040, 720));
        window.resize(1280, 820);
        QTest::qWait(200);
        QVERIFY(window.grab().save("output/ui-workspace-large.png"));
        const auto nav = window.findChildren<QPushButton *>("navButton");
        QCOMPARE(nav.size(), 3);
        nav.at(2)->setFocus();
        QTest::keyClick(nav.at(2), Qt::Key_Space);
        QTRY_COMPARE(window.findChild<QLabel *>("pageTitle")->text(), QStringLiteral("运行诊断"));
        QTest::qWait(100);
        QVERIFY(window.grab().save("output/ui-diagnostics-large.png"));
        window.resize(1040, 720);
        QTest::qWait(100);
        QCOMPARE(window.size(), QSize(1040, 720));
        auto *log = window.findChild<QPlainTextEdit *>();
        QVERIFY(window.rect().contains(QRect(log->mapTo(&window, QPoint()), log->size())));
        QVERIFY(window.grab().save("output/ui-diagnostics-small.png"));
        QTest::mouseClick(nav.at(0), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel *>("pageTitle")->text(), QStringLiteral("协作工作台"));
        QSignalSpy meetingDevices(&window.session_, &zjdesk::SessionCoordinator::devicesChanged);
        QTest::mouseClick(nav.at(1), Qt::LeftButton);
        // 进入页面会自动刷新一次设备，等刷新完成后再注入长名称，避免被刷新覆盖。
        QTRY_VERIFY_WITH_TIMEOUT(!meetingDevices.isEmpty(), 15000);
        // Assistance-room membership must not appear as meeting participants.
        auto *count = window.findChild<QLabel *>("meetingMemberCount");
        QCOMPARE(count->text(), QStringLiteral("尚未入会"));
        QVERIFY(window.findChild<QWidget *>("meetingRemoteMember")->isHidden());
        QVERIFY(window.findChild<VideoSurface *>("conferenceRemoteVideo")->isHidden());
        window.session_.leave();
        peer.leave();
        QVERIFY(window.session_.join(QUrl(url), "demo", longName, zjdesk::SessionCoordinator::RoomType::Meeting));
        QTRY_COMPARE(count->text(), QStringLiteral("1 人参与"));
        QVERIFY(window.findChild<QWidget *>("meetingRemoteMember")->isHidden());
        QVERIFY(peer.join(QUrl(url), "demo", longName, zjdesk::SessionCoordinator::RoomType::Meeting));
        QTRY_COMPARE(count->text(), QStringLiteral("2 人参与"));
        window.resize(1040, 720);
        for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
        {
            auto *combo = window.findChild<QComboBox *>(name);
            combo->addItem(QStringLiteral("USB 音视频设备 · 高清摄像头与立体声麦克风阵列（长设备名称）"), "test");
            combo->setCurrentIndex(combo->count() - 1);
        }
        QTest::qWait(100);
        QCOMPARE(window.size(), QSize(1040, 720));
        for (const auto *name : {"conferenceMuteMicrophone", "conferenceHideCamera",
                 "conferenceMuteSpeaker", "conferenceLocalVideo", "conferenceRemoteVideo", "meetingShareVideo",
                 "meetingRoomId", "meetingJoinButton", "settingsButton", "userProfile", "meetingLocalMember", "meetingRemoteMember", "shareButton"})
        {
            auto *widget = window.findChild<QWidget *>(name);
            QVERIFY(widget && widget->isVisible());
            QVERIFY2(window.rect().contains(QRect(widget->mapTo(&window, QPoint()), widget->size())), name);
        }
        QVERIFY(!window.findChild<VideoSurface *>("conferenceLocalVideo")->grabFramebuffer().isNull());
        QVERIFY(!window.findChild<VideoSurface *>("conferenceRemoteVideo")->grabFramebuffer().isNull());
        auto *stage = window.findChild<VideoSurface *>("meetingShareVideo");
        auto *local = window.findChild<VideoSurface *>("conferenceLocalVideo");
        auto *remote = window.findChild<VideoSurface *>("conferenceRemoteVideo");
        QVERIFY(stage->width() > local->width() * 2);
        QVERIFY(local->mapTo(&window, QPoint()).x() > stage->mapTo(&window, QPoint()).x() + stage->width());
        QVERIFY(remote->mapTo(&window, QPoint()).y() > local->mapTo(&window, QPoint()).y());
        QVERIFY(window.grab().save("output/ui-meeting-small.png"));
        window.resize(1280, 820);
        QTest::qWait(100);
        QVERIFY(window.grab().save("output/ui-meeting-large.png"));
        auto *settings = window.findChild<QWidget *>("meetingSettings");
        QVERIFY(settings && settings->isHidden());
        if (backgroundSnapshots)
        {
            settings->setWindowFlag(Qt::Tool);
            settings->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            settings->setAttribute(Qt::WA_ShowWithoutActivating);
            settings->move(window.screen()->virtualGeometry().left() - 2000, 0);
        }
        settingsButton->click();
        QTRY_VERIFY(settings->isVisible());
        QVERIFY(!settings->isAncestorOf(profileName));
        for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
        {
            auto *combo = window.findChild<QComboBox *>(name);
            combo->addItem(QStringLiteral("USB 音视频设备 · 高清摄像头与立体声麦克风阵列（长设备名称）"), "test");
            combo->setCurrentIndex(combo->count() - 1);
        }
        QTest::qWait(100);
        for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker", "audioTestButton",
                 "joinCameraEnabled", "joinMicrophoneEnabled", "joinSpeakerEnabled"})
        {
            auto *widget = window.findChild<QWidget *>(name);
            QVERIFY(widget && widget->isVisible());
            QVERIFY2(settings->rect().contains(QRect(widget->mapTo(settings, QPoint()), widget->size())), name);
        }
        QVERIFY(settings->grab().save("output/ui-meeting-settings.png"));
        QVERIFY(window.findChild<QLineEdit *>("meetingServerAddress")->isHidden());
        settings->hide();
        // UI fixtures cover the future roster boundary, without adding fake room members.
        window.session_.stopConference();
        QTRY_VERIFY_WITH_TIMEOUT(!window.session_.conferenceDevicesBusy(), 15000);
        window.resize(1040, 720);
        const QStringList fourNames{QStringLiteral("张三"), QStringLiteral("李四"), QStringLiteral("王五"), QStringLiteral("赵六")};
        window.setMeetingRoster(fourNames);
        QVERIFY(window.meetingRosterStack_->isHidden());
        QVERIFY(window.meetingRosterScroll_->isVisible());
        QCOMPARE(count->text(), QStringLiteral("4 人参与"));
        window.setMeetingRoster(fourNames + QStringList{QStringLiteral("孙七")});
        QVERIFY(window.meetingRosterStack_->isVisible());
        QVERIFY(window.meetingRosterStack_->text().isEmpty());
        QVERIFY(!window.meetingRosterStack_->icon().isNull());
        QVERIFY(window.meetingRosterScroll_->isHidden());
        QCOMPARE(count->text(), QStringLiteral("5 人参与"));
        QTest::qWait(100);
        QVERIFY(window.grab().save("output/ui-meeting-roster-folded.png"));
        auto *rosterPopup = window.meetingRosterPopup_;
        if (qEnvironmentVariableIntValue("ZJDESK_UI_BACKGROUND_SNAPSHOTS"))
        {
            rosterPopup->setWindowFlags(Qt::Tool | Qt::WindowDoesNotAcceptFocus);
            rosterPopup->setAttribute(Qt::WA_ShowWithoutActivating);
        }
        window.meetingRosterStack_->click();
        QTRY_VERIFY(rosterPopup->isVisible());
        QVERIFY(window.meetingRosterScroll_->isHidden());
        QCOMPARE(rosterPopup->findChildren<QWidget *>("meetingPopupMember").size(), 5);
        for (int i = 0; i < 5; ++i)
        {
            auto *badge = window.meetingRosterPopupMembers_.at(i);
            QVERIFY(badge->isVisible());
            QVERIFY(!badge->findChild<QLabel *>("memberName")->text().isEmpty());
            QVERIFY(rosterPopup->rect().contains(QRect(badge->mapTo(rosterPopup, QPoint()), badge->size())));
        }
        QCOMPARE(window.size(), QSize(1040, 720));
        QTest::qWait(100);
        QVERIFY(rosterPopup->grab().save("output/ui-meeting-roster-popup.png"));
        rosterPopup->findChild<QPushButton *>("meetingRosterClose")->click();
        QVERIFY(rosterPopup->isHidden());
        window.meetingRosterStack_->click();
        QVERIFY(rosterPopup->isVisible());
        window.setMeetingRoster(fourNames);
        QVERIFY(window.meetingRosterStack_->isHidden());
        QVERIFY(window.meetingRosterScroll_->isVisible());
        QVERIFY(rosterPopup->isHidden());
        window.refreshMeetingParticipants();
        QCOMPARE(count->text(), QStringLiteral("2 人参与"));
        window.session_.leave();
        peer.leave();
        window.findChild<QLineEdit *>("meetingDisplayName")->setText(QStringLiteral("本机用户"));
        window.findChild<QLineEdit *>("serverAddress")->setText("ws://127.0.0.1:8765");
        QTest::mouseClick(nav.at(0), Qt::LeftButton);
        panel->verticalScrollBar()->setValue(0);
        QTest::qWait(100);
        QVERIFY(window.grab().save("output/ui-workspace-preview.png"));
    }

    void conferenceControlsAndFrames()
    {
        using CS = zjdesk::SessionCoordinator::ConferenceState;
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow alice(url, "conference-ui", "Alice");
        auto bobOwner = std::make_unique<MainWindow>(url, "conference-ui", "Bob");
        auto &bob = *bobOwner;
        const bool backgroundSnapshots = qEnvironmentVariableIntValue("ZJDESK_UI_BACKGROUND_SNAPSHOTS") == 1;
        if (backgroundSnapshots)
        {
            for (auto *window : {&alice, &bob})
            {
                window->setWindowFlag(Qt::Tool);
                window->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
                window->setAttribute(Qt::WA_ShowWithoutActivating);
                window->move(window->screen()->virtualGeometry().left() - 2000, 0);
            }
        }
        if (!alice.session_.mediaEnvironment().ready) QSKIP("GStreamer is not enabled in this build");
        QSignalSpy aliceDevices(&alice.session_, &zjdesk::SessionCoordinator::devicesChanged);
        QSignalSpy bobDevices(&bob.session_, &zjdesk::SessionCoordinator::devicesChanged);
        alice.show(); bob.show();
        if (backgroundSnapshots)
        {
            QTRY_VERIFY(alice.isVisible() && bob.isVisible());
        }
        else
        {
            QVERIFY(QTest::qWaitForWindowExposed(&alice));
            QVERIFY(QTest::qWaitForWindowExposed(&bob));
        }
        QTRY_VERIFY(!aliceDevices.isEmpty() && !bobDevices.isEmpty());
        for (auto *window : {&alice, &bob})
        {
            for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
                QVERIFY(window->findChild<QComboBox *>(name)->currentData().toString().isEmpty());
            for (const auto *name : {"conferenceMuteMicrophone", "conferenceMuteSpeaker", "conferenceHideCamera"})
            {
                const auto *button = window->findChild<QPushButton *>(name);
                QVERIFY(!button->isChecked());
                QVERIFY(button->text().endsWith(QStringLiteral("开")));
            }
            QVERIFY(!window->session_.conferenceMicMuted_);
            QVERIFY(!window->session_.conferenceSpeakerMuted_);
            QVERIFY(!window->session_.conferenceCameraHidden());
        }
        aliceDevices.clear();
        alice.findChildren<QPushButton *>("navButton").at(1)->click();
        QVERIFY(!alice.findChild<VideoSurface *>("conferenceLocalVideo")->hasFrame());
        QTRY_VERIFY_WITH_TIMEOUT(!aliceDevices.isEmpty(), 15000);
        for (auto *window : {&alice, &bob})
        {
            auto *settings = window->findChild<QDialog *>("meetingSettings");
            if (backgroundSnapshots)
            {
                settings->setWindowFlag(Qt::Tool);
                settings->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
                settings->setAttribute(Qt::WA_ShowWithoutActivating);
                settings->move(window->screen()->virtualGeometry().left() - 2000, 0);
            }
            window->findChild<QPushButton *>("settingsButton")->click();
            QTRY_VERIFY(settings->isVisible());
            for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
            {
                auto *combo = window->findChild<QComboBox *>(name);
                QVERIFY(combo);
                QVERIFY(combo->isEnabled());
                combo->addItem("Test device", "test");
                combo->setCurrentIndex(combo->count() - 1);
            }
            QCOMPARE(window->session_.conferenceDevices_.camera, QString("test"));
            QCOMPARE(window->session_.conferenceDevices_.microphone, QString("test"));
            QCOMPARE(window->session_.conferenceDevices_.speaker, QString("test"));
            settings->hide();
            window->findChild<QLineEdit *>("meetingRoomId")->setText("conference-ui");
            window->findChild<QPushButton *>("meetingJoinButton")->click();
        }
        QTRY_COMPARE(alice.session_.memberCount(), 2);
        QVERIFY(!alice.findChild<QPushButton *>("conferenceCall"));
        QVERIFY(!bob.findChild<QPushButton *>("conferenceAccept"));
        QVERIFY(!alice.findChild<QPushButton *>("conferenceHangup"));
        QTRY_COMPARE_WITH_TIMEOUT(alice.session_.conferenceState(), CS::Running, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(bob.session_.conferenceState(), CS::Running, 15000);
        for (auto *window : {&alice, &bob})
        {
            auto *local = window->findChild<VideoSurface *>("conferenceLocalVideo");
            auto *remote = window->findChild<VideoSurface *>("conferenceRemoteVideo");
            QVERIFY(!local->grabFramebuffer().isNull());
            QVERIFY(!remote->grabFramebuffer().isNull());
            QTRY_VERIFY(local->isValid() && remote->isValid());
            QTRY_VERIFY(local->hasFrame() && remote->hasFrame());
            QVERIFY(!remote->grabFramebuffer().isNull());
            QVERIFY(!window->findChild<QComboBox *>("conferenceCamera")->isEnabled());
        }
        auto *hide = alice.findChild<QPushButton *>("conferenceHideCamera");
        QVERIFY(QDir().mkpath("output"));
        QTest::qWait(100);
        QVERIFY(alice.grab().save("output/ui-meeting-two-cameras.png"));
        QVERIFY(alice.findChild<QPushButton *>("shareButton")->isEnabled());
        QVERIFY(alice.session_.startScreenShare("test", 15));
        QTRY_VERIFY_WITH_TIMEOUT(alice.findChild<VideoSurface *>("meetingShareVideo")->hasFrame(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(bob.findChild<VideoSurface *>("meetingShareVideo")->hasFrame(), 15000);
        QVERIFY(!bob.findChild<QPushButton *>("shareButton")->isEnabled());
        alice.session_.stopScreenShare();
        QTRY_VERIFY(!bob.findChild<VideoSurface *>("meetingShareVideo")->hasFrame());
        QCOMPARE(alice.session_.conferenceState(), CS::Running);
        QVERIFY(hide->isEnabled());
        hide->click();
        QVERIFY(hide->isChecked());
        auto *local = alice.findChild<VideoSurface *>("conferenceLocalVideo");
        auto *remote = alice.findChild<VideoSurface *>("conferenceRemoteVideo");
        QTRY_VERIFY(bob.session_.conferenceRemoteCameraHidden());
        QTRY_COMPARE(bob.findChild<VideoSurface *>("conferenceRemoteVideo")->grabFramebuffer().pixelColor(10, 30), QColor("#eef3f6"));
        QTRY_COMPARE(local->grabFramebuffer().pixelColor(10, 30), QColor("#eef3f6"));
        QTRY_VERIFY(remote->mapTo(&alice, QPoint()).y() < local->mapTo(&alice, QPoint()).y());
        QCOMPARE(alice.findChild<QLabel *>("meetingMemberCount")->text(), QStringLiteral("2 人参与"));
        QTest::qWait(100);
        QVERIFY(alice.grab().save("output/ui-meeting-running.png"));
        QVERIFY(alice.grab().save("output/ui-meeting-one-camera.png"));
        auto *bobHide = bob.findChild<QPushButton *>("conferenceHideCamera");
        bobHide->click();
        QTRY_VERIFY(alice.session_.conferenceRemoteCameraHidden());
        QTRY_COMPARE(remote->grabFramebuffer().pixelColor(10, 30), QColor("#eef3f6"));
        // Late camera-state messages from a previous call cannot replace current state.
        alice.session_.receiveConference({{"action", "camera"}, {"sessionId", "stale-call"}, {"hidden", false}});
        QVERIFY(alice.session_.conferenceRemoteCameraHidden());
        auto *mic = alice.findChild<QPushButton *>("conferenceMuteMicrophone");
        auto *speaker = alice.findChild<QPushButton *>("conferenceMuteSpeaker");
        mic->click();
        speaker->click();
        alice.resize(1040, 720);
        QTest::qWait(100);
        QCOMPARE(alice.size(), QSize(1040, 720));
        const auto *roomEntry = alice.findChild<QLineEdit *>("meetingRoomId");
        QVERIFY(roomEntry->mapTo(&alice, roomEntry->rect().topRight()).x() < mic->mapTo(&alice, QPoint()).x());
        QVERIFY(alice.rect().contains(hide->mapTo(&alice, hide->rect().bottomRight())));
        QVERIFY(alice.grab().save("output/ui-meeting-no-cameras.png"));
        mic->click();
        speaker->click();
        hide->click();
        QVERIFY(!hide->isChecked());
        QTRY_VERIFY(local->mapTo(&alice, QPoint()).y() < remote->mapTo(&alice, QPoint()).y());
        QTRY_VERIFY(local->grabFramebuffer().pixelColor(10, 30) != QColor("#eef3f6"));
        bobHide->click();
        QTRY_VERIFY(!alice.session_.conferenceRemoteCameraHidden());
        QTRY_VERIFY(remote->grabFramebuffer().pixelColor(10, 30) != QColor("#eef3f6"));
        // Defaults configured during a call apply to the following connection only.
        for (const auto *name : {"joinCameraEnabled", "joinMicrophoneEnabled", "joinSpeakerEnabled"})
            alice.findChild<QCheckBox *>(name)->setChecked(false);
        QVERIFY(!alice.session_.conferenceCameraHidden());
        QVERIFY(!alice.session_.conferenceMicrophoneMuted());
        QVERIFY(!alice.session_.conferenceSpeakerMuted());
        QVERIFY(!hide->isChecked() && !mic->isChecked() && !speaker->isChecked());
        // A delayed device enumeration can refresh disabled selectors during a call.
        // Those updates must not change the active capture, but must apply on next join.
        alice.session_.devices_.clear();
        alice.refreshDevices();
        QVERIFY(alice.findChild<QComboBox *>("conferenceCamera")->currentData().toString().isEmpty());
        QCOMPARE(alice.session_.conferenceDevices_.camera, QString("test"));
        bob.findChild<QPushButton *>("meetingJoinButton")->click();
        QTRY_COMPARE(alice.session_.conferenceState(), CS::Idle);
        QTRY_VERIFY(!alice.findChild<VideoSurface *>("conferenceRemoteVideo")->hasFrame());
        QVERIFY(hide->isChecked());
        QTRY_COMPARE(alice.findChild<QLabel *>("meetingMemberCount")->text(), QStringLiteral("1 人参与"));
        QVERIFY(remote->isHidden());
        alice.session_.leave();
        QCOMPARE(alice.findChild<QLabel *>("meetingMemberCount")->text(), QStringLiteral("尚未入会"));
        QVERIFY(alice.findChild<QWidget *>("meetingLocalMember")->isHidden());

        alice.findChild<QPushButton *>("meetingJoinButton")->click();
        QCOMPARE(alice.session_.conferenceDevices_.camera, QString());
        QCOMPARE(alice.session_.conferenceDevices_.microphone, QString());
        QCOMPARE(alice.session_.conferenceDevices_.speaker, QString());
        alice.session_.leave();

        // Destroy a window during an active call, without explicitly hanging up or leaving first.
        // Its coordinator emits cleanup signals after MainWindow's derived members are destroyed.
        for (auto *window : {&alice, &bob})
        {
            QVERIFY(window->session_.setConferenceDevices({"test", "test", "test"}));
            QVERIFY(window->session_.join(QUrl(url), "conference-ui", window == &alice ? "Alice" : "Bob",
                zjdesk::SessionCoordinator::RoomType::Meeting));
        }
        QTRY_COMPARE(alice.session_.memberCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(alice.session_.conferenceState(), CS::Running, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(bob.session_.conferenceState(), CS::Running, 15000);
        QTRY_VERIFY(bob.findChild<VideoSurface *>("conferenceRemoteVideo")->hasFrame());
        QVERIFY(alice.session_.conferenceCameraHidden());
        QVERIFY(alice.session_.conferenceMicrophoneMuted());
        QVERIFY(alice.session_.conferenceSpeakerMuted());
        QTRY_VERIFY(bob.session_.conferenceRemoteCameraHidden());
        for (const auto *name : {"conferenceMuteMicrophone", "conferenceMuteSpeaker", "conferenceHideCamera"})
            QVERIFY(alice.findChild<QPushButton *>(name)->isChecked());
        QPointer<MainWindow> closedWindow(&bob);
        bobOwner.reset();
        QVERIFY(closedWindow.isNull());
        QTRY_COMPARE(alice.session_.conferenceState(), CS::Idle);
        QTRY_COMPARE(alice.session_.memberCount(), 1);
        QVERIFY(!alice.findChild<VideoSurface *>("conferenceRemoteVideo")->hasFrame());
        alice.session_.leave();
    }

    void localPreviewIsNotAvailable()
    {
        MainWindow window("ws://127.0.0.1:1", "demo", "Tester");
        QVERIFY(!window.findChild<QPushButton *>("previewButton"));
        QVERIFY(!window.findChild<QComboBox *>("previewSource"));
        QVERIFY(!window.findChild<QLabel *>("previewStatus"));
        for (const auto *label : window.findChildren<QLabel *>())
            QVERIFY(!label->text().contains(QStringLiteral("本地预览")));
        for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
            QVERIFY(window.findChild<QComboBox *>(name));
        QVERIFY(window.findChild<QPushButton *>("audioTestButton"));
        QVERIFY(window.refreshDevicesButton_);
        auto *localVideo = window.findChild<VideoSurface *>("conferenceLocalVideo");
        auto *remoteVideo = window.findChild<VideoSurface *>("conferenceRemoteVideo");
        QVERIFY(localVideo && remoteVideo);
        QImage frame(32, 24, QImage::Format_RGB32);
        frame.fill(Qt::red);
        window.session_.previewFrame(frame);
        QVERIFY(!localVideo->hasFrame());
        QVERIFY(!remoteVideo->hasFrame());
        QCOMPARE(window.session_.previewState(), zjdesk::SessionCoordinator::PreviewState::Stopped);
    }

    void meetingEntryRefreshesCameraAndHintsMissingDevice()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Tester");
        QSignalSpy devices(&window.session_, &zjdesk::SessionCoordinator::devicesChanged);
        const bool backgroundSnapshots = qEnvironmentVariableIntValue("ZJDESK_UI_BACKGROUND_SNAPSHOTS") == 1;
        if (backgroundSnapshots)
        {
            window.setWindowFlag(Qt::Tool);
            window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            window.setAttribute(Qt::WA_ShowWithoutActivating);
            window.move(window.screen()->virtualGeometry().left() - 2000, 0);
        }
        window.show();
        if (backgroundSnapshots)
        {
            QTRY_VERIFY(window.isVisible());
        }
        else
        {
            QVERIFY(QTest::qWaitForWindowExposed(&window));
        }
        // 先等启动时的设备枚举结束，避免与“进入页面”的刷新重叠、互相清空组合框。
        QTRY_VERIFY_WITH_TIMEOUT(!devices.isEmpty(), 15000);
        QTest::qWait(200);
        auto *camera = window.findChild<QComboBox *>("conferenceCamera");
        auto *title = window.findChild<QLabel *>("pageTitle");
        QVERIFY(camera && title);
        const auto nav = window.findChildren<QPushButton *>("navButton");
        QCOMPARE(nav.size(), 3);
        devices.clear();
        QTest::mouseClick(nav.at(1), Qt::LeftButton);
        QCOMPARE(title->text(), QStringLiteral("视频会议"));
        // 进入视频会议页时自动枚举一次设备，用于检测摄像头是否存在。
        QTRY_VERIFY_WITH_TIMEOUT(!devices.isEmpty(), 15000);
        QTest::qWait(200);
        QCOMPARE(devices.size(), 1);
        int cameras = 0;
        for (const auto &device : window.session_.devices())
            if (device.kind == QLatin1String("Video/Source")) ++cameras;
        QVERIFY2(camera->findData("test") < 0, "会议摄像头不应提供视频测试源");
        QVERIFY(camera->findText(QStringLiteral("视频测试源")) < 0);
        if (cameras == 0)
        {
            QCOMPARE(camera->count(), 0);
            QCOMPARE(camera->currentIndex(), -1);
            QCOMPARE(camera->placeholderText(), QStringLiteral("无可用设备"));
            QVERIFY2(!camera->toolTip().isEmpty(), "无设备提示需要说明原因");
            qInfo() << "No camera enumerated; camera hint:" << camera->toolTip();
        }
        else
        {
            QCOMPARE(camera->itemText(0), QStringLiteral("默认设备"));
            QCOMPARE(camera->count(), cameras + 1);
        }
        // 枚举结果没有摄像头时，摄像头选择框用灰色字体提示“无可用设备”。
        // 开发机可能始终能枚举到摄像头，这里用只保留音频设备的枚举结果覆盖该分支。
        const auto realDevices = window.session_.devices();
        QList<zjdesk::MediaDevice> audioOnly;
        for (const auto &device : realDevices)
            if (device.kind != QLatin1String("Video/Source")) audioOnly.append(device);
        window.session_.devices_ = audioOnly;
        window.refreshDevices();
        QCOMPARE(camera->count(), 0);
        QCOMPARE(camera->currentIndex(), -1);
        QCOMPARE(camera->placeholderText(), QStringLiteral("无可用设备"));
        const QImage hintImage = camera->grab().toImage();
        const QColor hintColor = darkestPixelColor(hintImage,
            QRect(0, 0, hintImage.width() / 3, hintImage.height()));
        QVERIFY2(colorDistance(hintColor, QColor("#8492a1")) <= 40,
            qPrintable(QStringLiteral("无设备提示应为灰色，实际 RGB %1,%2,%3")
                .arg(hintColor.red()).arg(hintColor.green()).arg(hintColor.blue())));
        QVERIFY2(!camera->toolTip().isEmpty(), "无设备提示需要说明原因");
        QVERIFY(QDir().mkpath("output"));
        QTest::qWait(100);
        QVERIFY(window.grab().save("output/ui-meeting-no-device.png"));
        window.session_.devices_ = realDevices;
        window.refreshDevices();
        // 再次进入仍然只刷新一次，确认刷新挂在“进入页面”这条路径上。
        devices.clear();
        QTest::mouseClick(nav.at(0), Qt::LeftButton);
        QTRY_COMPARE(title->text(), QStringLiteral("协作工作台"));
        QTest::mouseClick(nav.at(1), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!devices.isEmpty(), 15000);
        QTest::qWait(200);
        QCOMPARE(devices.size(), 1);
        if (cameras == 0)
        {
            QCOMPARE(camera->count(), 0);
            QCOMPARE(camera->placeholderText(), QStringLiteral("无可用设备"));
        }
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
        auto *surface = bob.findChild<VideoSurface *>("meetingShareVideo");
        QVERIFY(aliceShare && bobShare && bobStatus && surface);
        if (!alice.session_.mediaEnvironment().ready)
            QSKIP("GStreamer is not enabled in this build");
        QVERIFY(!aliceShare->isEnabled());
        for (auto *window : {&alice, &bob})
        {
            window->findChildren<QPushButton *>("navButton").at(1)->click();
            window->findChild<QLineEdit *>("meetingRoomId")->setText("desktop");
            window->findChild<QPushButton *>("meetingJoinButton")->click();
        }
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
        auto *aliceSurface = alice.findChild<VideoSurface *>("meetingShareVideo");
        QTRY_VERIFY_WITH_TIMEOUT(aliceSurface->hasFrame(), 20000);
        bob.findChild<QPushButton *>("meetingJoinButton")->click();
        QTRY_VERIFY(!aliceSurface->hasFrame());
        QVERIFY(!aliceShare->isEnabled());
    }
};

QTEST_MAIN(WindowTests)
#include "tst_window.moc"
