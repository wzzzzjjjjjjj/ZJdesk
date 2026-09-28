#include "session_coordinator.h"
#include <QJsonArray>
#include <QUuid>

namespace zjdesk
{
SessionCoordinator::SessionCoordinator(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<State>();
    qRegisterMetaType<PreviewState>();
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
                stopScreenShare();
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
    });
    connect(&media_, &MediaEngine::audioStatus, this, [this](quint64 epoch, const QString &status, const QString &detail) {
        if (epoch != audioEpoch_)
            return;
        audioState_ = status == "running" ? PreviewState::Running
            : status == "stopped" ? PreviewState::Stopped : PreviewState::Failed;
        audioDetail_ = detail;
        events_.append("media", detail);
        emit audioChanged();
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
        if (!signaling_.send({"join", {{"roomId", roomId_}, {"name", name_}}}))
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
    signaling_.close();
}

int SessionCoordinator::memberCount() const
{
    if (state_ == State::WaitingForPeer)
        return 1;
    return state_ == State::PeerAvailable ? 2 : 0;
}

bool SessionCoordinator::join(const QUrl &url, const QString &roomId, const QString &name)
{
    if (state_ != State::Idle && state_ != State::Failed)
        return false;
    if (!url.isValid() || (url.scheme() != "ws" && url.scheme() != "wss")
        || url.host().isEmpty() || !url.userInfo().isEmpty()
        || !protocol::isRoomIdValid(roomId) || !protocol::isPeerNameValid(name))
    {
        fail(QStringLiteral("请填写有效的服务地址、房间号和昵称"));
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
    stopScreenShare();
    ownPeerId_.clear();
    peerId_.clear();
    peerName_.clear();
    control_.revoke();
}

void SessionCoordinator::leave()
{
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
}

void SessionCoordinator::fail(const QString &reason)
{
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
        return;
    media_.refreshDevices();
}

bool SessionCoordinator::startPreview(const QString &sourceId)
{
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

bool SessionCoordinator::startScreenShare(const QString &source)
{
    if (state_ != State::PeerAvailable || screenActive() || shareState_ == PreviewState::Stopping)
        return false;
    if (!media_.environment().ready)
    {
        shareState_ = PreviewState::Failed;
        shareDetail_ = media_.environment().description;
        emit shareChanged();
        return false;
    }
    streamId_ = QUuid::createUuid().toString(QUuid::Id128);
    receivingScreen_ = false;
    if (!sendNegotiation("share", {{"action", "start"}, {"streamId", streamId_}}))
    {
        streamId_.clear();
        return false;
    }
    shareState_ = PreviewState::Starting;
    shareDetail_ = QStringLiteral("正在连接屏幕共享");
    screen_.start(true, source, ++shareEpoch_);
    emit shareChanged();
    return true;
}

void SessionCoordinator::stopScreenShare()
{
    clearScreenShare(true);
}

void SessionCoordinator::clearScreenShare(bool notifyPeer)
{
    if (!screenActive())
        return;
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
        else if (action == "start" && id != streamId_)
        {
            // Simultaneous clicks choose the same sender at both ends.
            if (screenActive() && !receivingScreen_ && ownPeerId_ < peerId_)
                return;
            stopScreenShare();
            streamId_ = id;
            receivingScreen_ = true;
            shareState_ = PreviewState::Starting;
            shareDetail_ = QStringLiteral("正在接收对方屏幕");
            screen_.start(false, {}, ++shareEpoch_);
            emit shareChanged();
        }
    }
    else if (id == streamId_)
        screen_.negotiate(message.type, message.payload, shareEpoch_);
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
        clearScreenShare(false);
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
            receiveShare(message);
            emit negotiationReceived(message);
        }
    }
}
} // namespace zjdesk
