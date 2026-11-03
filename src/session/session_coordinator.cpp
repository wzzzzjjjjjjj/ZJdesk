#include "session_coordinator.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QBuffer>
#include <QImageReader>
#include <cmath>

namespace zjdesk
{
SessionCoordinator::SessionCoordinator(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<State>();
    qRegisterMetaType<PreviewState>();
    qRegisterMetaType<RemoteCursor>();
    qRegisterMetaType<ConferenceState>();
    conferenceDeadline_.setSingleShot(true);
    connect(&conferenceDeadline_, &QTimer::timeout, this, [this] {
        clearConference(true);
        conferenceState_ = ConferenceState::Failed;
        conferenceDetail_ = QStringLiteral("会议呼叫或连接超时");
        emit conferenceChanged();
    });
    connect(&conference_, &ConferenceMedia::negotiationReady, this,
        [this](quint64 epoch, const QString &type, QJsonObject payload) {
            if (epoch != conferenceEpoch_ || !conferenceActive()) return;
            payload.insert("action", type); payload.insert("sessionId", conferenceId_);
            if (!sendNegotiation("conference", payload)) stopConference();
        });
    connect(&conference_, &ConferenceMedia::statusChanged, this,
        [this](quint64 epoch, const QString &status, const QString &detail) {
            if (status == "stopped" && epoch == conferenceEpoch_)
            {
                conferenceCleanupPending_ = false;
                emit conferenceChanged();
                return;
            }
            if (epoch != conferenceEpoch_ || !conferenceActive()) return;
            if (status == "failed")
            {
                clearConference(true);
                conferenceState_ = ConferenceState::Failed;
            }
            else if (status == "running")
            {
                conferenceState_ = ConferenceState::Running;
                conferenceDeadline_.stop();
            }
            else return;
            conferenceDetail_ = detail;
            events_.append("conference", detail);
            emit conferenceChanged();
        });
    connect(&conference_, &ConferenceMedia::localFrame, this, [this](quint64 epoch, const QImage &image) {
        if (epoch == conferenceEpoch_ && conferenceActive()) emit conferenceLocalFrame(image);
    });
    connect(&conference_, &ConferenceMedia::remoteFrame, this, [this](quint64 epoch, const QImage &image) {
        if (epoch == conferenceEpoch_ && conferenceActive()) emit conferenceRemoteFrame(image);
    });
    connect(&conference_, &ConferenceMedia::receivedAudio, this, [this](quint64 epoch, quint64 count, double peak) {
        if (epoch == conferenceEpoch_ && conferenceActive()) emit conferenceAudioReceived(count, peak);
    });
    cursorTimer_.setInterval(16);
    cursorTimer_.setTimerType(Qt::PreciseTimer);
    connect(&cursorTimer_, &QTimer::timeout, this, &SessionCoordinator::sendCursor);
    connect(&screen_, &ScreenShare::controlChannelChanged, this, [this](quint64 epoch, bool ready) {
        if (epoch != shareEpoch_ || !screenActive())
            return;
        controlChannelReady_ = ready;
        if (ready)
        {
            screen_.sendControl({{"action", "capabilities"},
                {"control", roomType_ == RoomType::Assistance && !receivingScreen_ && desktopSource_ && input_.available()}}, shareEpoch_);
            if (remoteState_ == RemoteState::Controlled)
                sendControlGrant();
        }
        else
            stopControl();
        emit remoteChanged();
    });
    connect(&screen_, &ScreenShare::controlReceived, this, [this](quint64 epoch, const QJsonObject &message) {
        if (epoch == shareEpoch_ && screenActive() && controlChannelReady_)
            receiveControl(message);
    });
    controlDeadline_.setSingleShot(true);
    connect(&controlDeadline_, &QTimer::timeout, this, &SessionCoordinator::stopControl);
    connect(&screen_, &ScreenShare::statisticsReady, this, [this](quint64 epoch, const ShareStats &stats) {
        if (epoch != shareEpoch_ || !screenActive())
            return;
        if (stats.size != shareStats_.size || stats.targetFps != shareStats_.targetFps
            || stats.encoderQuantizer != shareStats_.encoderQuantizer
            || stats.transportExcessMs >= 80 || stats.presentedGapMaxMs > 200 || stats.outputGapMaxMs > 200)
            events_.append("screen-performance", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"revision", stats.revision}, {"width", stats.size.width()}, {"height", stats.size.height()},
                {"targetFps", stats.targetFps}, {"processedFps", stats.processedFps}, {"presentedFps", stats.presentedFps},
                {"codec", stats.codec}, {"bitrateBudget", stats.encoderBitrate}, {"quantizer", stats.encoderQuantizer},
                {"controlRttMs", stats.controlRttMs}, {"transportExcessMs", stats.transportExcessMs},
                {"outputGapMaxMs", stats.outputGapMaxMs}, {"presentedGapMaxMs", stats.presentedGapMaxMs},
                {"packetsLost", stats.packetsLost}, {"transport", stats.transport},
                {"timings", stats.timingDiagnostics()}}).toJson(QJsonDocument::Compact)));
        shareStats_ = stats;
        emit shareStatisticsChanged(stats);
        if (shareState_ == PreviewState::Running)
        {
            shareDetail_ = QStringLiteral("%1 · %2 × %3 · 目标 %4 fps · %5 %6 fps")
                .arg(receivingScreen_ ? QStringLiteral("正在接收对方屏幕") : QStringLiteral("正在共享主屏幕"))
                .arg(stats.size.width()).arg(stats.size.height()).arg(stats.targetFps)
                .arg(receivingScreen_ ? QStringLiteral("接收") : QStringLiteral("编码"))
                .arg(stats.processedFps, 0, 'f', 1);
            if (!receivingScreen_ && stats.peerFps >= 0)
                shareDetail_ += QStringLiteral(" · 对方 %1 fps").arg(stats.peerFps, 0, 'f', 1);
            if (receivingScreen_ && stats.presentedFps >= 0)
                shareDetail_ += QStringLiteral(" · 显示 %1 fps").arg(stats.presentedFps, 0, 'f', 1);
            if (!stats.transport.isEmpty())
                shareDetail_ += QStringLiteral(" · %1").arg(stats.transport);
            emit shareChanged();
        }
    });
    connect(&screen_, &ScreenShare::negotiationReady, this,
        [this](quint64 epoch, const QString &type, QJsonObject payload) {
            if (epoch != shareEpoch_ || streamId_.isEmpty())
                return;
            payload.insert("streamId", streamId_);
            if (!sendNegotiation(type, payload))
                stopScreenShare();
        });
    connect(&screen_, &ScreenShare::statusChanged, this,
        [this](quint64 epoch, const QString &status, const QString &detail) {
            if (epoch != shareEpoch_ || (status == "stopped" && shareState_ == PreviewState::Failed))
                return;
            if (status == "failed")
            {
                const auto failedStreamId = streamId_;
                stopScreenShare();
                failedStreamId_ = failedStreamId;
                shareState_ = PreviewState::Failed;
            }
            else
                shareState_ = status == "running" ? PreviewState::Running : PreviewState::Stopped;
            shareDetail_ = detail;
            events_.append("screen", detail);
            emit shareChanged();
        });
    connect(&screen_, &ScreenShare::frameReady, this, [this](quint64 epoch, const QImage &image) {
        if (epoch == shareEpoch_ && screenActive())
            emit shareFrame(image);
    });
    connect(&media_, &MediaEngine::devicesReady, this, [this](const QList<MediaDevice> &devices, const QString &error) {
        devices_ = devices;
        events_.append("media", error.isEmpty()
            ? QStringLiteral("发现 %1 个音视频设备").arg(devices.size()) : error);
        emit devicesChanged();
    });
    connect(&media_, &MediaEngine::videoStatus, this, [this](quint64 epoch, const QString &status, const QString &detail) {
        if (epoch != previewEpoch_)
            return;
        if (status == "stopped" && previewState_ == PreviewState::Failed)
            return;
        previewState_ = status == "running" ? PreviewState::Running
            : status == "stopped" ? PreviewState::Stopped : PreviewState::Failed;
        previewDetail_ = detail;
        if (previewState_ == PreviewState::Failed)
            media_.stopVideo(++previewEpoch_);
        events_.append("media", detail);
        emit previewChanged();
        if (deviceRefreshPending_ && (previewState_ == PreviewState::Stopped || previewState_ == PreviewState::Failed))
            refreshDevices();
        startConferenceMediaWhenReady();
    });
    connect(&media_, &MediaEngine::audioStatus, this, [this](quint64 epoch, const QString &status, const QString &detail) {
        if (epoch != audioEpoch_)
            return;
        audioState_ = status == "running" ? PreviewState::Running
            : status == "stopped" ? PreviewState::Stopped : PreviewState::Failed;
        audioDetail_ = detail;
        events_.append("media", detail);
        emit audioChanged();
        startConferenceMediaWhenReady();
    });
    connect(&media_, &MediaEngine::frameReady, this, [this](quint64 epoch, const QImage &image) {
        if (epoch == previewEpoch_ && previewState_ == PreviewState::Running)
            emit previewFrame(image);
    });
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { fail(QStringLiteral("入会超时，请检查信令服务")); });
    connect(&signaling_, &SignalingClient::connected, this, [this] {
        if (state_ != State::Connecting)
            return;
        changeState(State::Joining, QStringLiteral("已连接，正在加入房间"));
        if (!signaling_.send({"join", {{"roomId", roomId_}, {"name", name_},
            {"roomType", roomType_ == RoomType::Meeting ? "meeting" : "assistance"}}}))
            fail(QStringLiteral("入会请求发送失败"));
    });
    connect(&signaling_, &SignalingClient::messageReceived, this, &SessionCoordinator::receive);
    connect(&signaling_, &SignalingClient::failed, this, &SessionCoordinator::fail);
    connect(&signaling_, &SignalingClient::disconnected, this, [this] {
        if (state_ != State::Idle && state_ != State::Failed)
            fail(QStringLiteral("信令连接已断开"));
    });
    connect(&signaling_, &SignalingClient::traffic, this, [this](bool outgoing) {
        outgoing ? ++sent_ : ++received_;
        emit trafficChanged();
    });
}

SessionCoordinator::~SessionCoordinator()
{
    clearConference(true);
    clearControl();
    signaling_.close();
}

int SessionCoordinator::memberCount() const
{
    if (state_ == State::WaitingForPeer)
        return 1;
    return state_ == State::PeerAvailable ? 2 : 0;
}

bool SessionCoordinator::join(const QUrl &url, const QString &roomId, const QString &name, RoomType type)
{
    if (state_ != State::Idle && state_ != State::Failed)
        return false;
    roomType_ = type;
    if (!url.isValid() || (url.scheme() != "ws" && url.scheme() != "wss")
        || url.host().isEmpty() || !url.userInfo().isEmpty()
        || !protocol::isRoomIdValid(roomId) || !protocol::isPeerNameValid(name))
    {
        fail(QStringLiteral("请填写有效的服务地址、房间码（1–32 位字母、数字、_ 或 -）和昵称"));
        return false;
    }
    roomId_ = roomId;
    name_ = name.trimmed();
    sent_ = received_ = 0;
    emit trafficChanged();
    changeState(State::Connecting, QStringLiteral("正在连接信令服务"));
    deadline_.start(8000);
    signaling_.open(url);
    return true;
}

void SessionCoordinator::clearMembership()
{
    clearConference(false);
    stopScreenShare();
    failedStreamId_.clear();
    clearControl();
    peerControllable_ = false;
    ownPeerId_.clear();
    peerId_.clear();
    peerName_.clear();
    control_.revoke();
}

void SessionCoordinator::leave()
{
    stopConference();
    stopScreenShare();
    stopPreview();
    stopAudioTest();
    deadline_.stop();
    signaling_.close();
    clearMembership();
    changeState(State::Idle, QStringLiteral("尚未加入房间"));
    emit membersChanged();
}

void SessionCoordinator::changeState(State state, const QString &detail)
{
    state_ = state;
    detail_ = detail;
    events_.append("session", detail);
    emit stateChanged(state);
    if (state == State::PeerAvailable)
    {
        sendNegotiation("control", {{"action", "capabilities"},
            {"control", roomType_ == RoomType::Assistance && input_.available() && media_.environment().ready}});
        if (conferenceAutoConnect_ && roomType_ == RoomType::Meeting)
            startConference();
    }
}

void SessionCoordinator::fail(const QString &reason)
{
    clearConference(false);
    stopScreenShare();
    stopPreview();
    stopAudioTest();
    deadline_.stop();
    signaling_.close();
    clearMembership();
    changeState(State::Failed, reason);
    emit membersChanged();
}

void SessionCoordinator::refreshDevices()
{
    if (previewState_ == PreviewState::Starting || previewState_ == PreviewState::Running
        || previewState_ == PreviewState::Stopping)
    {
        deviceRefreshPending_ = true;
        return;
    }
    deviceRefreshPending_ = false;
    cameraPermission_ = zjdesk::cameraPermission();
    media_.refreshDevices();
}

void SessionCoordinator::requestCameraPermission()
{
    refreshDevices();
    if (cameraPermission_ == CameraPermission::Denied)
        emit cameraPermissionRequired();
}

void SessionCoordinator::openCameraPrivacySettings()
{
    if (cameraSettingsOpening_) return;
    cameraSettingsOpening_ = true;
    cameraSettingsOpener_(this, [this](bool opened, const QString &error) {
        cameraSettingsOpening_ = false;
        emit cameraPrivacySettingsFinished(opened, error);
    });
}

bool SessionCoordinator::startPreview(const QString &sourceId)
{
    if (conferenceDevicesBusy()) return false;
    if (previewState_ != PreviewState::Stopped && previewState_ != PreviewState::Failed)
        return false;
    if (!media_.environment().ready)
    {
        previewState_ = PreviewState::Failed;
        previewDetail_ = media_.environment().description;
        emit previewChanged();
        return false;
    }
    previewState_ = PreviewState::Starting;
    previewDetail_ = QStringLiteral("正在启动本地预览");
    emit previewChanged();
    media_.startVideo(sourceId, ++previewEpoch_);
    return true;
}

void SessionCoordinator::stopPreview()
{
    if (previewState_ == PreviewState::Stopped || previewState_ == PreviewState::Stopping)
        return;
    previewState_ = PreviewState::Stopping;
    previewDetail_ = QStringLiteral("正在停止本地预览");
    media_.stopVideo(++previewEpoch_);
    emit previewFrame({});
    emit previewChanged();
}

bool SessionCoordinator::startAudioTest(bool audible)
{
    if (conferenceDevicesBusy()) return false;
    if (audioState_ != PreviewState::Stopped && audioState_ != PreviewState::Failed)
        return false;
    if (!media_.environment().ready)
    {
        audioState_ = PreviewState::Failed;
        audioDetail_ = media_.environment().description;
        emit audioChanged();
        return false;
    }
    audioState_ = PreviewState::Starting;
    audioDetail_ = QStringLiteral("正在启动音频测试");
    emit audioChanged();
    media_.startAudio(audible, ++audioEpoch_);
    return true;
}

void SessionCoordinator::stopAudioTest()
{
    if (audioState_ == PreviewState::Stopped || audioState_ == PreviewState::Stopping)
        return;
    audioState_ = PreviewState::Stopping;
    audioDetail_ = QStringLiteral("正在停止音频测试");
    media_.stopAudio(++audioEpoch_);
    emit audioChanged();
}

bool SessionCoordinator::sendNegotiation(const QString &type, const QJsonObject &payload)
{
    return state_ == State::PeerAvailable && protocol::isRelayType(type)
           && signaling_.send({type, payload});
}

bool SessionCoordinator::startScreenShare(const QString &source, int targetFps)
{
    if (targetFps != 0 && targetFps != 15 && targetFps != 30 && targetFps != 60)
        return false;
    if (state_ != State::PeerAvailable || screenActive() || shareState_ == PreviewState::Stopping)
        return false;
    failedStreamId_.clear();
    if (!media_.environment().ready)
    {
        shareState_ = PreviewState::Failed;
        shareDetail_ = media_.environment().description;
        emit shareChanged();
        return false;
    }
    streamId_ = QUuid::createUuid().toString(QUuid::Id128);
    desktopSource_ = source == "desktop";
    receivingScreen_ = false;
    if (!sendNegotiation("share", {{"action", "start"}, {"streamId", streamId_}}))
    {
        streamId_.clear();
        return false;
    }
    shareState_ = PreviewState::Starting;
    shareDetail_ = QStringLiteral("正在连接屏幕共享");
    shareStats_ = {};
    screen_.start(true, source, ++shareEpoch_, targetFps);
    emit shareChanged();
    return true;
}

void SessionCoordinator::stopScreenShare()
{
    clearScreenShare(true);
}

void SessionCoordinator::reportSharePresentation(quint64 frames, bool active, double gapMaxMs,
    const VideoPresentationStats &presentation)
{
    if (screenActive())
        screen_.reportPresentation(frames, shareEpoch_, active, gapMaxMs, presentation);
}

void SessionCoordinator::clearScreenShare(bool notifyPeer)
{
    if (!screenActive())
        return;
    controlStartedShare_ = false;
    clearControl();
    controlChannelReady_ = false;
    if (notifyPeer)
        sendNegotiation("share", {{"action", "stop"}, {"streamId", streamId_}});
    streamId_.clear();
    receivingScreen_ = false;
    shareState_ = PreviewState::Stopping;
    shareDetail_ = QStringLiteral("正在停止屏幕共享");
    screen_.stop(++shareEpoch_);
    emit shareFrame({});
    emit shareChanged();
}

bool SessionCoordinator::canRequestControl() const
{
    return roomType_ == RoomType::Assistance && state_ == State::PeerAvailable && media_.environment().ready
        && peerControllable_ && remoteState_ == RemoteState::Idle
        && ((!screenActive() && shareState_ != PreviewState::Stopping)
            || (receivingScreen_ && shareState_ == PreviewState::Running && controlChannelReady_));
}

bool SessionCoordinator::requestControl()
{
    if (!canRequestControl())
        return false;
    controlRequestId_ = QUuid::createUuid().toString(QUuid::Id128);
    if (!sendNegotiation("control", {{"action", "request"}, {"requestId", controlRequestId_}}))
    {
        controlRequestId_.clear();
        return false;
    }
    remoteState_ = RemoteState::Requesting;
    remoteDetail_ = QStringLiteral("等待 Linux 端允许控制");
    controlDeadline_.start(60000);
    emit remoteChanged();
    return true;
}

bool SessionCoordinator::grantControl()
{
    if (remoteState_ != RemoteState::Pending
        || !input_.available() || !control_.grant())
        return false;
    remoteState_ = RemoteState::Controlled;
    remoteDetail_ = QStringLiteral("已允许控制，正在建立桌面连接");
    controlDeadline_.start(60000);
    if (!screenActive())
    {
        controlStartedShare_ = true;
        if (!startScreenShare())
        {
            stopControl();
            return false;
        }
    }
    if (controlChannelReady_)
        sendControlGrant();
    events_.append("control", QStringLiteral("本机允许远程控制"));
    emit remoteChanged();
    return true;
}

void SessionCoordinator::sendControlGrant()
{
    controlDeadline_.stop();
    remoteDetail_ = QStringLiteral("对方正在控制本机，点击停止控制可立即收回");
    screen_.sendControl({{"action", "grant"}, {"epoch", QString::number(control_.epoch())},
        {"requestId", controlRequestId_}, {"cursorSync", true}}, shareEpoch_);
    emit remoteChanged();
}

void SessionCoordinator::clearControl()
{
    controlDeadline_.stop();
    cursorTimer_.stop();
    if (cursorSyncActive_)
        screen_.setCursorEmbedded(true, shareEpoch_);
    cursorSyncActive_ = false;
    sentCursor_ = remoteCursor_ = {};
    cursorSequence_ = remoteCursorSequence_ = 0;
    emit remoteCursorChanged({});
    control_.revoke();
    input_.releaseAll();
    remoteState_ = RemoteState::Idle;
    remoteControlEpoch_ = inputSequence_ = 0;
    pendingProbeSequence_ = 0;
    inputProbeClock_.invalidate();
    controlRequestId_.clear();
    controlStartedShare_ = false;
    remoteDetail_ = QStringLiteral("远程控制未开启");
    emit remoteChanged();
}

void SessionCoordinator::sendCursor()
{
    if (remoteState_ != RemoteState::Controlled || !controlChannelReady_ || !cursorTimer_.isActive())
        return;
    const auto cursor = input_.cursor();
    QJsonObject message{{"action", "cursor"}, {"epoch", QString::number(control_.epoch())},
        {"sequence", QString::number(cursorSequence_ + 1)}, {"visible", cursor.visible}};
    QByteArray png;
    const bool newShape = cursor.serial != sentCursor_.serial || sentCursor_.image.isNull();
    if (!cursor.image.isNull() && newShape)
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        cursor.image.save(&buffer, "PNG");
    }
    if (cursor.image.isNull() || (newShape && png.isEmpty()) || png.size() > 2400)
    {
        if (cursorSyncActive_)
        {
            screen_.setCursorEmbedded(true, shareEpoch_);
            message.insert("visible", false);
            screen_.sendControl(message, shareEpoch_);
            ++cursorSequence_;
            cursorSyncActive_ = false;
            sentCursor_ = {};
        }
        return;
    }
    if (!cursor.visible)
    {
        if (cursorSyncActive_ && sentCursor_.visible)
        {
            screen_.sendControl(message, shareEpoch_);
            ++cursorSequence_;
            sentCursor_.visible = false;
        }
        return;
    }
    if (cursorSyncActive_ && cursor.serial == sentCursor_.serial && cursor.position == sentCursor_.position
        && cursor.visible == sentCursor_.visible)
        return;
    if (!cursorSyncActive_)
    {
        screen_.setCursorEmbedded(false, shareEpoch_);
        cursorSyncActive_ = true;
    }
    message.insert("serial", QString::number(cursor.serial));
    message.insert("x", cursor.position.x());
    message.insert("y", cursor.position.y());
    if (!png.isEmpty())
    {
        message.insert("image", QString::fromLatin1(png.toBase64()));
        message.insert("hotX", cursor.hotspot.x());
        message.insert("hotY", cursor.hotspot.y());
        message.insert("width", cursor.screenSize.width());
        message.insert("height", cursor.screenSize.height());
    }
    screen_.sendControl(message, shareEpoch_);
    ++cursorSequence_;
    sentCursor_ = cursor;
}

void SessionCoordinator::stopControl()
{
    const bool stopDesktop = controlStartedShare_;
    if (state_ == State::PeerAvailable && remoteState_ != RemoteState::Idle)
        sendNegotiation("control", {{"action", "revoke"}, {"requestId", controlRequestId_}});
    if (controlChannelReady_ && remoteState_ != RemoteState::Idle)
        screen_.sendControl({{"action", "revoke"}}, shareEpoch_);
    clearControl();
    if (stopDesktop)
        stopScreenShare();
}

void SessionCoordinator::sendRemoteInput(const QJsonObject &input)
{
    if (remoteState_ != RemoteState::Controlling || !controlChannelReady_)
        return;
    QJsonObject message = input;
    message.insert("action", "input");
    message.insert("epoch", QString::number(remoteControlEpoch_));
    message.insert("sequence", QString::number(++inputSequence_));
    if (message.value("probe").toBool())
    {
        pendingProbeSequence_ = inputSequence_;
        inputProbeClock_.restart();
    }
    screen_.sendControl(message, shareEpoch_);
}

void SessionCoordinator::receiveControl(const QJsonObject &message)
{
    const auto action = message.value("action").toString();
    if (action == "capabilities")
    {
        peerControllable_ = message.value("control").toBool();
        emit remoteChanged();
    }
    else if (action == "grant" && remoteState_ == RemoteState::Requesting && receivingScreen_
        && message.value("requestId").toString() == controlRequestId_)
    {
        bool valid = false;
        const auto epoch = message.value("epoch").toString().toULongLong(&valid);
        if (!valid || epoch == 0)
            return;
        controlDeadline_.stop();
        remoteControlEpoch_ = epoch;
        inputSequence_ = 0;
        pendingProbeSequence_ = 0;
        inputProbeClock_.invalidate();
        remoteState_ = RemoteState::Controlling;
        remoteDetail_ = QStringLiteral("正在控制 Linux；点击共享画面后操作键鼠");
        if (message.value("cursorSync").toBool())
            screen_.sendControl({{"action", "cursor_ready"}, {"epoch", QString::number(epoch)}}, shareEpoch_);
        emit remoteChanged();
    }
    else if (action == "revoke")
        stopControl();
    else if (action == "cursor_ready" && remoteState_ == RemoteState::Controlled && !receivingScreen_)
    {
        if (message.value("epoch").toString() == QString::number(control_.epoch()))
        {
            cursorTimer_.start();
            sendCursor();
        }
    }
    else if (action == "cursor" && remoteState_ == RemoteState::Controlling && receivingScreen_)
    {
        bool valid = false;
        const auto sequence = message.value("sequence").toString().toULongLong(&valid);
        if (!valid || sequence <= remoteCursorSequence_
            || message.value("epoch").toString() != QString::number(remoteControlEpoch_)
            || !message.value("visible").isBool())
            return;
        auto cursor = remoteCursor_;
        cursor.visible = message.value("visible").toBool();
        if (cursor.visible)
        {
            const double x = message.value("x").toDouble(-1), y = message.value("y").toDouble(-1);
            const auto serial = message.value("serial").toString().toULongLong(&valid);
            if (!valid || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
                return;
            if (serial != cursor.serial || cursor.image.isNull())
            {
                const auto encoded = message.value("image").toString().toLatin1();
                if (encoded.isEmpty() || encoded.size() > 3200)
                    return;
                auto png = QByteArray::fromBase64(encoded);
                QBuffer buffer(&png);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer, "PNG");
                const auto size = reader.size();
                const QSize screen(message.value("width").toInt(), message.value("height").toInt());
                const QPoint hotspot(message.value("hotX").toInt(-1), message.value("hotY").toInt(-1));
                if (size.isEmpty() || size.width() > 128 || size.height() > 128 || screen.isEmpty()
                    || screen.width() > 32768 || screen.height() > 32768 || !QRect(QPoint(), size).contains(hotspot))
                    return;
                cursor.image = reader.read();
                if (cursor.image.isNull())
                    return;
                cursor.hotspot = hotspot;
                cursor.screenSize = screen;
                cursor.serial = serial;
            }
            cursor.position = QPointF(x, y);
        }
        remoteCursorSequence_ = sequence;
        remoteCursor_ = cursor;
        emit remoteCursorChanged(cursor);
    }
    else if (action == "probe_ack" && remoteState_ == RemoteState::Controlling && receivingScreen_)
    {
        bool epochValid = false, sequenceValid = false;
        const auto epoch = message.value("epoch").toString().toULongLong(&epochValid);
        const auto sequence = message.value("sequence").toString().toULongLong(&sequenceValid);
        if (epochValid && sequenceValid && epoch == remoteControlEpoch_
            && sequence == pendingProbeSequence_ && inputProbeClock_.isValid())
        {
            const auto roundTripUs = inputProbeClock_.nsecsElapsed() / 1000;
            pendingProbeSequence_ = 0;
            inputProbeClock_.invalidate();
            emit inputProbeAcknowledged(sequence, roundTripUs,
                message.value("injectUs").toInteger(), message.value("accepted").toBool());
        }
    }
    else if (action == "input" && remoteState_ == RemoteState::Controlled && !receivingScreen_)
    {
        bool epochValid = false, sequenceValid = false;
        const auto epoch = message.value("epoch").toString().toULongLong(&epochValid);
        const auto sequence = message.value("sequence").toString().toULongLong(&sequenceValid);
        if (epochValid && sequenceValid && control_.acceptInput(peerId_, epoch, sequence))
        {
            if (message.value("probe").toBool())
            {
                QElapsedTimer injectClock;
                injectClock.start();
                const bool accepted = input_.inject(message);
                const auto injectUs = injectClock.nsecsElapsed() / 1000;
                emit inputProbeInjected(sequence, injectUs, accepted);
                screen_.sendControl({{"action", "probe_ack"}, {"epoch", QString::number(epoch)},
                    {"sequence", QString::number(sequence)}, {"injectUs", injectUs},
                    {"accepted", accepted}}, shareEpoch_);
            }
            else
                input_.inject(message);
            sendCursor();
        }
    }
}

void SessionCoordinator::receiveControlRequest(const QJsonObject &message)
{
    if (roomType_ != RoomType::Assistance)
        return;
    const auto action = message.value("action").toString();
    const auto requestId = message.value("requestId").toString();
    if (action == "capabilities")
    {
        peerControllable_ = message.value("control").toBool();
        emit remoteChanged();
    }
    else if (action == "request" && protocol::isRoomIdValid(requestId)
        && input_.available() && media_.environment().ready && remoteState_ == RemoteState::Idle
        && (!screenActive() || (!receivingScreen_ && desktopSource_
            && shareState_ == PreviewState::Running && controlChannelReady_))
        && control_.request(peerId_))
    {
        controlRequestId_ = requestId;
        remoteState_ = RemoteState::Pending;
        remoteDetail_ = peerName_ + QStringLiteral(" 请求控制本机，请选择允许或拒绝");
        controlDeadline_.start(30000);
        emit remoteChanged();
    }
    else if (action == "revoke" && !controlRequestId_.isEmpty() && requestId == controlRequestId_)
        stopControl();
}

void SessionCoordinator::receiveShare(const protocol::Message &message)
{
    const auto id = message.payload.value("streamId").toString();
    if (!protocol::isRoomIdValid(id))
        return;
    if (message.type == "share")
    {
        const auto action = message.payload.value("action").toString();
        if (action == "stop" && id == streamId_)
        {
            clearScreenShare(false);
        }
        else if (action == "stop" && id == failedStreamId_ && shareState_ == PreviewState::Failed)
        {
            // ICE can fail before the peer's normal stop reaches signaling.
            failedStreamId_.clear();
            shareState_ = PreviewState::Stopped;
            shareDetail_ = QStringLiteral("屏幕共享已停止");
            events_.append("screen", shareDetail_);
            emit shareChanged();
        }
        else if (action == "start" && id != streamId_)
        {
            // Simultaneous clicks choose the same sender at both ends.
            if (screenActive() && !receivingScreen_ && ownPeerId_ < peerId_)
                return;
            stopScreenShare();
            failedStreamId_.clear();
            streamId_ = id;
            receivingScreen_ = true;
            shareState_ = PreviewState::Starting;
            shareDetail_ = QStringLiteral("正在接收对方屏幕");
            screen_.start(false, {}, ++shareEpoch_);
            emit shareChanged();
        }
        else if (id == streamId_ && (action == "profile" || action == "stats"))
            screen_.negotiate(message.type, message.payload, shareEpoch_);
    }
    else if (id == streamId_)
        screen_.negotiate(message.type, message.payload, shareEpoch_);
}

bool SessionCoordinator::setConferenceDevices(const ConferenceDevices &devices)
{
    if (conferenceState_ == ConferenceState::Connecting || conferenceState_ == ConferenceState::Running)
        return false;
    conferenceDevices_ = devices;
    return true;
}

bool SessionCoordinator::startConference()
{
    if (roomType_ != RoomType::Meeting || state_ != State::PeerAvailable || conferenceActive() || !media_.environment().ready)
        return false;
    conferenceId_ = QUuid::createUuid().toString(QUuid::Id128);
    conferenceState_ = ConferenceState::Calling;
    conferenceDetail_ = conferenceAutoConnect_ ? QStringLiteral("正在连接会议音视频")
        : QStringLiteral("正在呼叫对方，等待接听");
    if (!sendNegotiation("conference", {{"action", "start"}, {"sessionId", conferenceId_}}))
    { clearConference(false); return false; }
    conferenceDeadline_.start(45000);
    emit conferenceChanged();
    return true;
}

void SessionCoordinator::setConferenceAutoConnect(bool enabled)
{
    conferenceAutoConnect_ = enabled;
    if (!enabled || roomType_ != RoomType::Meeting) return;
    if (conferenceState_ == ConferenceState::Incoming)
        acceptConference();
    else if (state_ == State::PeerAvailable && !conferenceActive())
        startConference();
}

void SessionCoordinator::setConferenceJoinDefaults(bool microphoneEnabled, bool cameraEnabled, bool speakerEnabled)
{
    conferenceJoinMicrophoneEnabled_ = microphoneEnabled;
    conferenceJoinCameraEnabled_ = cameraEnabled;
    conferenceJoinSpeakerEnabled_ = speakerEnabled;
    emit conferenceChanged();
}

void SessionCoordinator::beginConferenceMedia(bool offerer)
{
    conferenceMediaPending_ = true;
    conferenceCleanupPending_ = false;
    conferenceOfferer_ = offerer;
    ++conferenceEpoch_;
    conferenceMicMuted_ = !conferenceJoinMicrophoneEnabled_;
    conferenceCameraHidden_ = !conferenceJoinCameraEnabled_;
    conferenceSpeakerMuted_ = !conferenceJoinSpeakerEnabled_;
    conferenceRemoteCameraHidden_ = false;
    conferenceState_ = ConferenceState::Connecting;
    conferenceDetail_ = QStringLiteral("正在连接双向音视频");
    conferenceDeadline_.start(45000);
    stopPreview();
    stopAudioTest();
    emit conferenceChanged();
    // Announce the initial camera state, including a default-hidden camera.
    sendNegotiation("conference", {{"action", "camera"}, {"sessionId", conferenceId_}, {"hidden", conferenceCameraHidden_}});
    startConferenceMediaWhenReady();
}

void SessionCoordinator::startConferenceMediaWhenReady()
{
    if (!conferenceMediaPending_ || !conferenceActive()) return;
    const auto released = [](PreviewState state) { return state == PreviewState::Stopped || state == PreviewState::Failed; };
    if (!released(previewState_) || !released(audioState_)) return;
    conferenceMediaPending_ = false;
    conference_.start(conferenceOfferer_, conferenceDevices_, conferenceEpoch_,
        conferenceMicMuted_, conferenceCameraHidden_, conferenceSpeakerMuted_);
    const auto pending = conferencePendingSignals_; conferencePendingSignals_.clear();
    for (const auto &payload : pending)
        conference_.negotiate(payload.value("action").toString(), payload, conferenceEpoch_);
}

bool SessionCoordinator::acceptConference()
{
    if (conferenceState_ != ConferenceState::Incoming || !conferenceActive() || !media_.environment().ready)
        return false;
    if (!sendNegotiation("conference", {{"action", "accept"}, {"sessionId", conferenceId_}}))
    { stopConference(); return false; }
    beginConferenceMedia(false);
    return true;
}

void SessionCoordinator::setConferenceMuted(bool microphone, bool camera, bool speaker)
{
    const bool cameraChanged = conferenceCameraHidden_ != camera;
    conferenceMicMuted_ = microphone;
    conferenceCameraHidden_ = camera;
    conferenceSpeakerMuted_ = speaker;
    if (conferenceState_ == ConferenceState::Connecting || conferenceState_ == ConferenceState::Running)
    {
        conference_.setMuted(microphone, camera, speaker, conferenceEpoch_);
        if (cameraChanged)
            sendNegotiation("conference", {{"action", "camera"}, {"sessionId", conferenceId_}, {"hidden", camera}});
    }
    if (cameraChanged) emit conferenceCameraChanged();
}

void SessionCoordinator::stopConference() { clearConference(true); }

void SessionCoordinator::clearConference(bool notifyPeer)
{
    if (!conferenceActive()) return;
    conferenceDeadline_.stop();
    if (notifyPeer) sendNegotiation("conference", {{"action", "stop"}, {"sessionId", conferenceId_}});
    conferenceId_.clear();
    conferenceMediaPending_ = false;
    conferencePendingSignals_.clear();
    conferenceCleanupPending_ = true;
    conferenceState_ = ConferenceState::Idle;
    conferenceCameraHidden_ = conferenceRemoteCameraHidden_ = false;
    conferenceDetail_ = QStringLiteral("会议已结束");
    conference_.stop(++conferenceEpoch_);
    emit conferenceLocalFrame({}); emit conferenceRemoteFrame({});
    events_.append("conference", conferenceDetail_);
    emit conferenceChanged();
}

void SessionCoordinator::receiveConference(const QJsonObject &payload)
{
    if (roomType_ != RoomType::Meeting)
        return;
    const auto id = payload.value("sessionId").toString();
    const auto action = payload.value("action").toString();
    if (action == "start")
    {
        if (id == conferenceId_) return;
        if (conferenceActive())
        {
            // Resolve crossed calls once, using the same ordering at both peers.
            if (conferenceState_ != ConferenceState::Calling || ownPeerId_ < peerId_)
            {
                sendNegotiation("conference", {{"action", "stop"}, {"sessionId", id}});
                return;
            }
            clearConference(true);
        }
        conferenceId_ = id;
        conferenceState_ = ConferenceState::Incoming;
        conferenceDetail_ = conferenceAutoConnect_ ? QStringLiteral("正在连接会议音视频")
            : QStringLiteral("%1 邀请你加入视频会议").arg(peerName_);
        conferenceDeadline_.start(45000);
        emit conferenceChanged();
        if (conferenceAutoConnect_) acceptConference();
        return;
    }
    if (id.isEmpty() || id != conferenceId_) return;
    if (action == "stop") { clearConference(false); return; }
    if (action == "camera")
    {
        if ((conferenceState_ == ConferenceState::Connecting || conferenceState_ == ConferenceState::Running)
            && payload.value("hidden").isBool())
        {
            conferenceRemoteCameraHidden_ = payload.value("hidden").toBool();
            emit conferenceCameraChanged();
        }
        return;
    }
    if (action == "accept" && conferenceState_ == ConferenceState::Calling)
        beginConferenceMedia(true);
    else if (conferenceState_ == ConferenceState::Connecting || conferenceState_ == ConferenceState::Running)
    {
        if (conferenceMediaPending_)
        {
            if (conferencePendingSignals_.size() >= 256) { stopConference(); return; }
            conferencePendingSignals_.append(payload);
        }
        else conference_.negotiate(action, payload, conferenceEpoch_);
    }
}

void SessionCoordinator::receive(const protocol::Message &message)
{
    if (message.type == "error")
    {
        const auto code = message.payload.value("code").toString();
        if (code == "peer_unavailable" && state_ == State::WaitingForPeer)
            return;
        if (code == "room_full")
            fail(QStringLiteral("房间已满，当前版本最多支持两人"));
        else
            fail(QStringLiteral("信令服务返回错误：") + code);
        return;
    }
    if (message.type == "joined" && state_ == State::Joining)
    {
        if (message.payload.value("roomId").toString() != roomId_)
            return fail(QStringLiteral("服务返回了不匹配的房间"));
        const auto type = message.payload.value("roomType").toString();
        if ((roomType_ == RoomType::Meeting && type != "meeting")
            || (roomType_ == RoomType::Assistance && !type.isEmpty() && type != "assistance"))
            return fail(QStringLiteral("信令服务不支持所选房间类型，请更新服务端"));
        deadline_.stop();
        ownPeerId_ = message.payload.value("peerId").toString();
        const auto peers = message.payload.value("peers").toArray();
        if (peers.isEmpty())
            changeState(State::WaitingForPeer, QStringLiteral("已加入房间，等待另一位成员"));
        else
        {
            peerId_ = peers.first().toObject().value("peerId").toString();
            peerName_ = peers.first().toObject().value("name").toString();
            changeState(State::PeerAvailable, QStringLiteral("双方已入会，音视频尚未开启"));
        }
        emit membersChanged();
        return;
    }
    if (message.type == "peer_joined" && state_ == State::WaitingForPeer)
    {
        peerId_ = message.payload.value("peerId").toString();
        peerName_ = message.payload.value("name").toString();
        changeState(State::PeerAvailable, QStringLiteral("双方已入会，音视频尚未开启"));
        emit membersChanged();
    }
    else if (message.type == "peer_left" && state_ == State::PeerAvailable)
    {
        clearConference(false);
        clearScreenShare(false);
        clearControl();
        peerControllable_ = false;
        peerId_.clear();
        peerName_.clear();
        control_.revoke();
        changeState(State::WaitingForPeer, QStringLiteral("对方已离开，等待另一位成员"));
        emit membersChanged();
    }
    else if (protocol::isRelayType(message.type) && state_ == State::PeerAvailable)
    {
        // Do not accept an event from an old peer after a room membership change.
        if (message.payload.value("fromPeerId").toString() == peerId_)
        {
            if (message.type == "control")
                receiveControlRequest(message.payload);
            else if (message.type == "conference")
                receiveConference(message.payload);
            else
            {
                receiveShare(message);
                emit negotiationReceived(message);
            }
        }
    }
}
} // namespace zjdesk
