#include "session_coordinator.h"
#include <QJsonArray>

namespace zjdesk
{
SessionCoordinator::SessionCoordinator(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<State>();
    qRegisterMetaType<PreviewState>();
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
    peerId_.clear();
    peerName_.clear();
    control_.revoke();
}

void SessionCoordinator::leave()
{
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

void SessionCoordinator::receive(const protocol::Message &message)
{
    if (message.type == "error")
    {
        const auto code = message.payload.value("code").toString();
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
        clearMembership();
        changeState(State::WaitingForPeer, QStringLiteral("对方已离开，等待另一位成员"));
        emit membersChanged();
    }
    else if (protocol::isRelayType(message.type) && state_ == State::PeerAvailable)
    {
        // Do not accept an event from an old peer after a room membership change.
        if (message.payload.value("fromPeerId").toString() == peerId_)
            emit negotiationReceived(message);
    }
}
} // namespace zjdesk
