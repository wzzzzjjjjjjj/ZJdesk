#include "session_coordinator.h"
#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>
#include <QBuffer>
#include <QImageReader>
#include <QHostAddress>
#include <cmath>

namespace zjdesk
{
SessionCoordinator::SessionCoordinator(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<State>();
    qRegisterMetaType<PreviewState>();
    qRegisterMetaType<RemoteCursor>();
    qRegisterMetaType<ConferenceState>();
    peerRetry_.setInterval(5000);
    connect(&peerRetry_, &QTimer::timeout, this, &SessionCoordinator::retryPeerSession);
    connect(&peerConnection_, &PeerConnection::routeChanged, this, &SessionCoordinator::changePeerRoute);
    connect(&peerConnection_, &PeerConnection::unavailable, this, [this] {
        changePeerRoute({});
    });
    meshDeadline_.setInterval(1000);
    connect(&meshDeadline_, &QTimer::timeout, this, [this] {
        for (const auto &id : meshDeadlines_.keys())
        {
            if (!meshDeadlines_.value(id).hasExpired()) continue;
            meshDeadlines_.remove(id);
            meshStatus_[id] = "failed";
            meshPeers_.remove(id);
            meetingMedia_.removePeer(id, conferenceEpoch_);
            sendNegotiation("conference", {{"action", "stop"}, {"sessionId", meshSessionId(id)}, {"targetPeerId", id}});
            emit conferencePeerFrame(id, {});
        }
        if (meshDeadlines_.isEmpty()) meshDeadline_.stop();
        if (meshActive_ && meshPeers_.isEmpty())
        {
            clearConference(false);
            conferenceState_ = ConferenceState::Failed;
            conferenceDetail_ = QStringLiteral("会议成员连接超时");
            emit conferenceChanged();
        }
        else if (meshActive_) updateMeshState();
    });
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
    connect(&meetingMedia_, &MeetingMedia::negotiationReady, this,
        [this](quint64 epoch, const QString &id, const QString &type, QJsonObject payload) {
            if (epoch != conferenceEpoch_ || !meshActive_ || !meshPeers_.contains(id)) return;
            payload.insert("action", type); payload.insert("sessionId", meshSessionId(id)); payload.insert("targetPeerId", id);
            sendNegotiation("conference", payload);
        });
    connect(&meetingMedia_, &MeetingMedia::statusChanged, this,
        [this](quint64 epoch, const QString &status, const QString &detail) {
            if (epoch != conferenceEpoch_) return;
            if (status == "stopped") { conferenceCleanupPending_ = false; emit conferenceChanged(); return; }
            if (!meshActive_) return;
            if (status == "ready") { meshReady_ = true; syncConferencePeers(); }
            else if (status == "failed")
            {
                clearConference(true); conferenceState_ = ConferenceState::Failed; conferenceDetail_ = detail;
                emit conferenceChanged();
            }
        });
    connect(&meetingMedia_, &MeetingMedia::peerStatusChanged, this,
        [this](quint64 epoch, const QString &id, const QString &status) {
            if (epoch != conferenceEpoch_ || !meshActive_ || !meshPeers_.contains(id)) return;
            meshStatus_[id] = status;
            if (status == "running" || status == "failed") meshDeadlines_.remove(id);
            if (status == "failed")
            {
                sendNegotiation("conference", {{"action", "stop"}, {"sessionId", meshSessionId(id)}, {"targetPeerId", id}});
                meshPeers_.remove(id); meetingMedia_.removePeer(id, epoch);
                emit conferencePeerFrame(id, {});
                if (meshPeers_.isEmpty()) { clearConference(false); return; }
            }
            updateMeshState();
        });
    connect(&meetingMedia_, &MeetingMedia::localFrame, this, [this](quint64 epoch, const QImage &image) {
        if (epoch == conferenceEpoch_ && meshActive_) emit conferenceLocalFrame(image);
    });
    connect(&meetingMedia_, &MeetingMedia::remoteFrame, this,
        [this](quint64 epoch, const QString &id, const QImage &image) {
            if (epoch != conferenceEpoch_ || !meshPeers_.contains(id)) return;
            emit conferencePeerFrame(id, image);
            if (id == peerId_) emit conferenceRemoteFrame(image);
        });
    connect(&meetingMedia_, &MeetingMedia::receivedAudio, this,
        [this](quint64 epoch, const QString &id, quint64 buffers, double peak) {
            if (epoch == conferenceEpoch_ && meshPeers_.contains(id)) emit conferencePeerAudioReceived(id, buffers, peak);
        });
    connect(&meetingMedia_, &MeetingMedia::mixedAudio, this, [this](quint64 epoch, quint64 buffers, double peak) {
        if (epoch == conferenceEpoch_ && meshActive_) emit conferenceAudioReceived(buffers, peak);
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
        if (message.value("action") != "input" && epoch == shareEpoch_ && screenActive() && controlChannelReady_)
            receiveControl(message);
    });
    connect(&screen_, &ScreenShare::controlReceived, &inputReceiver_, [this](quint64 epoch, const QJsonObject &message) {
        inputReceiver_.receive(epoch, message);
    });
    connect(&screen_, &ScreenShare::controlChannelChanged, &inputReceiver_, [this](quint64 epoch, bool ready) {
        if (!ready) inputReceiver_.revoke(epoch);
    });
    connect(&inputReceiver_, &InputReceiver::processed, &screen_, [this](quint64 epoch, const QJsonObject &result) {
        if (result.value("probe").toBool() && result.value("sourcePeerId").toString().isEmpty()) screen_.sendControl(result, epoch);
    }, Qt::DirectConnection);
    connect(&inputReceiver_, &InputReceiver::processed, this, [this](quint64 epoch, const QJsonObject &result) {
        if (epoch != shareEpoch_ || remoteState_ != RemoteState::Controlled
            || result.value("epoch").toString() != QString::number(control_.epoch()))
            return;
        const auto sequence = result.value("sequence").toString().toULongLong();
        const auto injectUs = result.value("injectUs").toInteger();
        const bool accepted = result.value("accepted").toBool();
        events_.append("control-input", QString("inject seq=%1 kind=%2 button=%3 pressed=%4 accepted=%5 injectUs=%6 error=%7")
            .arg(sequence).arg(result.value("kind").toString()).arg(result.value("button").toInt())
            .arg(result.value("pressed").toBool()).arg(accepted).arg(injectUs).arg(result.value("error").toString()));
        const auto warning = accepted ? QString() : QStringLiteral("输入未执行：") + result.value("error").toString();
        if (inputWarning_ != warning) { inputWarning_ = warning; emit remoteChanged(); }
        if (result.value("probe").toBool()) emit inputProbeInjected(sequence, injectUs, accepted);
    });
    inputProbeDeadline_.setInterval(250);
    connect(&inputProbeDeadline_, &QTimer::timeout, this, [this] {
        bool expired = false;
        for (auto it = inputProbeClocks_.begin(); it != inputProbeClocks_.end();)
        {
            if (it.value().elapsed() < 2000) { ++it; continue; }
            events_.append("control-input", QString("ack timeout seq=%1").arg(it.key()));
            it = inputProbeClocks_.erase(it);
            expired = true;
        }
        if (inputProbeClocks_.isEmpty()) inputProbeDeadline_.stop();
        if (expired)
        {
            inputWarning_ = QStringLiteral("未收到输入回执，请检查对方状态或重新连接");
            emit remoteChanged();
        }
    });
    controlDeadline_.setSingleShot(true);
    connect(&fileClipboard_, &FileClipboard::statusChanged, this, &SessionCoordinator::remoteChanged);
    connect(&reverseFileClipboard_, &FileClipboard::statusChanged, this, &SessionCoordinator::remoteChanged);
    connect(&fileClipboard_, &FileClipboard::sendMessage, this, [this](const QJsonObject &message) {
        if (controlChannelReady_ && screenActive()
            && (remoteState_ == RemoteState::Controlled || remoteState_ == RemoteState::Controlling))
            screen_.sendFile(message, shareEpoch_);
    });
    connect(&reverseFileClipboard_, &FileClipboard::sendMessage, this, [this](QJsonObject message) {
        if (controlChannelReady_ && screenActive()
            && (remoteState_ == RemoteState::Controlled || remoteState_ == RemoteState::Controlling))
        {
            message.insert("reverse", true);
            screen_.sendFile(message, shareEpoch_);
        }
    });
    connect(&screen_, &ScreenShare::fileReceived, this, [this](quint64 epoch, const QJsonObject &message) {
        if (epoch == shareEpoch_ && screenActive() && controlChannelReady_
            && (remoteState_ == RemoteState::Controlled || remoteState_ == RemoteState::Controlling))
        {
            if (message.value("reverse").toBool()) reverseFileClipboard_.receive(message);
            else fileClipboard_.receive(message);
        }
    });
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
            if (remoteAccess_ && state_ == State::PeerAvailable && status == "stopped")
            {
                sendNegotiation("control", {{"action", "capabilities"},
                    {"control", input_.available() && media_.environment().ready},
                    {"maximumFps", ScreenShare::displayMaximumFps()}});
                requestRemoteControlWhenReady();
            }
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
    connect(&deadline_, &QTimer::timeout, this, [this] { fail(QStringLiteral("连接超时，请检查信令服务")); });
    connect(&signaling_, &SignalingClient::connected, this, [this] {
        if (state_ != State::Connecting)
            return;
        if (remoteAccess_)
        {
            changeState(State::Joining, remotePeerAddress_.isEmpty() ? QStringLiteral("正在获取本机校验码") : QStringLiteral("正在注册远程控制服务"));
            QJsonObject registration{{"name", name_}};
            if (!remotePeerAddress_.isEmpty()) registration.insert("peerAddress", remotePeerAddress_);
            registeredRemoteCodePolicy_ = remoteCodePolicy_;
            if (remoteCodePolicy_ != "startup")
            {
                registration.insert("codePolicy", remoteCodePolicy_);
                registration.insert("identity", remoteIdentity_);
            }
            if (!signaling_.send({"register_remote", registration}))
                fail(QStringLiteral("无法注册远程控制服务"));
            return;
        }
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
    if (roomType_ == RoomType::Meeting) clearMeetingShare(true);
    clearConference(true);
    clearControl();
    signaling_.close();
}

int SessionCoordinator::memberCount() const
{
    if (state_ == State::WaitingForPeer)
        return 1;
    return state_ == State::PeerAvailable ? conferencePeers_.size() + 1 : 0;
}

bool SessionCoordinator::join(const QUrl &url, const QString &roomId, const QString &name, RoomType type)
{
    if (state_ != State::Idle && state_ != State::Failed)
        return false;
    remoteAccess_ = false;
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
    if (profileEnabled_ && type == RoomType::Meeting)
    {
        profileOnline_ = true;
        profileMeetingRoom_ = roomId;
        profileName_ = name_;
    }
    sent_ = received_ = 0;
    emit trafficChanged();
    changeState(State::Connecting, QStringLiteral("正在连接信令服务"));
    deadline_.start(8000);
    signaling_.open(url);
    return true;
}

bool SessionCoordinator::setRemoteCodePolicy(const QString &policy, const QString &identity)
{
    if (policy != "startup" && policy != "three_days" && policy != "never") return false;
    if (policy != "startup" && !protocol::isRemoteIdentityValid(identity)) return false;
    remoteCodePolicy_ = policy;
    remoteIdentity_ = identity;
    return true;
}

bool SessionCoordinator::startRemoteAccess(const QUrl &url, const QString &name, const QString &peerAddress)
{
    if (state_ != State::Idle && state_ != State::Failed) return false;
    if (!url.isValid() || (url.scheme() != "ws" && url.scheme() != "wss")
        || url.host().isEmpty() || !url.userInfo().isEmpty() || !protocol::isPeerNameValid(name)
        || (!peerAddress.isEmpty() && QHostAddress(peerAddress).protocol() != QAbstractSocket::IPv4Protocol))
    {
        fail(QStringLiteral("请填写有效的服务地址和昵称"));
        return false;
    }
    remoteAccess_ = true;
    remotePeerAddress_ = peerAddress;
    roomType_ = RoomType::Assistance;
    remoteCode_.clear();
    name_ = name.trimmed();
    if (profileEnabled_)
    {
        profileOnline_ = true;
        profileName_ = name_;
        profileMeetingRoom_.clear();
    }
    changeState(State::Connecting, QStringLiteral("正在连接远程控制服务"));
    deadline_.start(8000);
    signaling_.open(url);
    return true;
}

bool SessionCoordinator::connectRemote(const QString &code)
{
    if (!remoteAccess_ || remoteCode_.isEmpty() || state_ != State::WaitingForPeer
        || remoteConnecting_ || !protocol::isRemoteCodeValid(code) || code == remoteCode_) return false;
    if (!signaling_.send({"connect_remote", {{"code", code}}})) return false;
    remoteConnecting_ = true;
    deadline_.start(8000);
    detail_ = QStringLiteral("正在连接对方电脑");
    emit stateChanged(state_);
    return true;
}

bool SessionCoordinator::connectRemoteIp()
{
    if (!remoteAccess_ || remoteCode_.isEmpty() || state_ != State::WaitingForPeer
        || remoteConnecting_ || remotePeerAddress_.isEmpty()) return false;
    if (!signaling_.send({"connect_remote_ip", {{"address", remotePeerAddress_}}})) return false;
    if (profileEnabled_) profileResumeControl_ = true;
    remoteConnecting_ = true;
    deadline_.start(8000);
    detail_ = QStringLiteral("正在连接对方电脑");
    emit stateChanged(state_);
    return true;
}

void SessionCoordinator::disconnectRemote()
{
    profileResumeControl_ = false;
    if (!remoteAccess_ || remoteCode_.isEmpty()) return;
    deadline_.stop();
    remoteConnecting_ = false;
    remoteDialPending_ = false;
    clearScreenShare(false);
    clearMembership();
    if (!signaling_.send({"disconnect_remote", {}}))
        return fail(QStringLiteral("断开请求发送失败，信令连接已关闭"));
    changeState(State::WaitingForPeer, remotePeerAddress_.isEmpty() ? QStringLiteral("已在线，可输入对方校验码连接") : QStringLiteral("已在线，可连接已验证的对端 IP"));
    emit membersChanged();
}

void SessionCoordinator::clearMembership()
{
    clearConference(false);
    clearScreenShare(true);
    failedStreamId_.clear();
    clearControl();
    peerControllable_ = false;
    peerMaximumFps_ = 0;
    conferencePeers_.clear();
    meshStatus_.clear(); meshHidden_.clear();
    ownPeerId_.clear();
    peerId_.clear();
    peerName_.clear();
    control_.revoke();
}

void SessionCoordinator::leave()
{
    profileOnline_ = false;
    profileResumeControl_ = false;
    profileMeetingRoom_.clear();
    profileShareSource_.clear();
    stopConference();
    stopScreenShare();
    stopPreview();
    stopAudioTest();
    deadline_.stop();
    signaling_.close();
    clearMembership();
    remoteCode_.clear();
    remoteDialPending_ = false;
    remoteConnecting_ = false;
    remoteAccess_ = false;
    changeState(State::Idle, QStringLiteral("连接已断开"));
    emit membersChanged();
}

bool SessionCoordinator::setPeerProfile(const PeerProfile &profile, const QUrl &serverOverride, const QString &name)
{
    if (!profile.error().isEmpty() || !protocol::isPeerNameValid(name)) return false;
    clearPeerProfile();
    profileEnabled_ = profileOnline_ = true;
    profileName_ = name;
    peerRetry_.start();
    peerConnection_.start(profile, serverOverride);
    return true;
}

void SessionCoordinator::clearPeerProfile()
{
    peerConnection_.stop();
    peerRetry_.stop();
    profileEnabled_ = false;
    peerRoute_ = {};
    leave();
}

void SessionCoordinator::changePeerRoute(const NetworkConfiguration &configuration)
{
    if (!profileEnabled_) return;
    rememberPeerShare();
    const bool online = profileOnline_;
    const bool resume = profileResumeControl_;
    const auto meeting = profileMeetingRoom_;
    const auto shareSource = profileShareSource_;
    const auto shareFps = profileShareFps_;
    leave(); // Retire the old authorization, input channel and media before rebinding.
    profileOnline_ = online;
    profileResumeControl_ = resume;
    profileMeetingRoom_ = meeting;
    profileShareSource_ = shareSource;
    profileShareFps_ = shareFps;
    peerRoute_ = configuration;
    if (configuration.peerAddress().isEmpty() || !NetworkSetup::configurationError(configuration).isEmpty())
    {
        peerRoute_ = {};
        emit peerRouteUnavailable();
        return;
    }
    qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", configuration.localAddress().toUtf8());
    qputenv("ZJDESK_WEBRTC_PEER_ADDRESS", configuration.peerAddress().toUtf8());
    emit peerRouteChanged(configuration);
    retryPeerSession();
}

void SessionCoordinator::retryPeerSession()
{
    if (!profileEnabled_ || !profileOnline_ || peerRoute_.peerAddress().isEmpty()) return;
    if (state_ == State::Idle || state_ == State::Failed)
    {
        if (!profileMeetingRoom_.isEmpty())
            join(peerRoute_.serverUrl, profileMeetingRoom_, profileName_, RoomType::Meeting);
        else
            startRemoteAccess(peerRoute_.serverUrl, profileName_, peerRoute_.peerAddress());
    }
    else if (profileResumeControl_ && remoteAccess_ && state_ == State::WaitingForPeer && !remoteConnecting_)
        connectRemoteIp();
    else if (roomType_ == RoomType::Meeting && state_ == State::PeerAvailable
        && !profileShareSource_.isEmpty() && shareState_ == PreviewState::Stopped)
    {
        if (startScreenShare(profileShareSource_, profileShareFps_)) profileShareSource_.clear();
    }
}

void SessionCoordinator::rememberPeerShare()
{
    if (profileEnabled_ && roomType_ == RoomType::Meeting && !receivingScreen_ && screenActive())
    {
        profileShareSource_ = meetingShareSource_;
        profileShareFps_ = meetingShareFps_;
    }
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
            {"control", roomType_ == RoomType::Assistance && input_.available() && media_.environment().ready
                && (!remoteAccess_ || shareState_ != PreviewState::Stopping)},
            {"maximumFps", ScreenShare::displayMaximumFps()}});
        if (conferenceAutoConnect_ && roomType_ == RoomType::Meeting)
            startConference();
    }
}

void SessionCoordinator::fail(const QString &reason)
{
    rememberPeerShare();
    clearConference(false);
    clearScreenShare(true);
    stopPreview();
    stopAudioTest();
    deadline_.stop();
    signaling_.close();
    clearMembership();
    remoteCode_.clear();
    remoteDialPending_ = false;
    remoteConnecting_ = false;
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
    auto routed = payload;
    if (roomType_ == RoomType::Meeting && !routed.contains("targetPeerId") && !peerId_.isEmpty())
        routed.insert("targetPeerId", peerId_);
    return state_ == State::PeerAvailable && protocol::isRelayType(type)
           && signaling_.send({type, routed});
}

bool SessionCoordinator::startScreenShare(const QString &source, int targetFps)
{
    if (targetFps != 0 && targetFps != 15 && targetFps != 30 && targetFps != 60)
        return false;
    if (roomType_ == RoomType::Meeting) return startMeetingShare(source, targetFps);
    if (state_ != State::PeerAvailable || memberCount() > 2 || screenActive() || shareState_ == PreviewState::Stopping)
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
    screen_.start(true, source, ++shareEpoch_, targetFps, peerMaximumFps_);
    emit shareChanged();
    return true;
}

void SessionCoordinator::stopScreenShare()
{
    profileShareSource_.clear();
    clearScreenShare(true);
}

void SessionCoordinator::reportSharePresentation(quint64 frames, bool active, double gapMaxMs,
    const VideoPresentationStats &presentation)
{
    if (roomType_ == RoomType::Meeting)
    {
        if (receivingScreen_ && meetingSharePeers_.contains(shareOwnerId_))
            meetingSharePeers_[shareOwnerId_]->reportPresentation(frames, shareEpoch_, active, gapMaxMs, presentation);
        return;
    }
    if (screenActive())
        screen_.reportPresentation(frames, shareEpoch_, active, gapMaxMs, presentation);
}

void SessionCoordinator::clearScreenShare(bool notifyPeer)
{
    if (roomType_ == RoomType::Meeting) return clearMeetingShare(notifyPeer);
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

ScreenShare *SessionCoordinator::controlScreen()
{
    return roomType_ == RoomType::Meeting ? meetingSharePeers_.value(meetingControlPeerId_) : &screen_;
}

void SessionCoordinator::sendControlMessage(const QJsonObject &message)
{
    if (auto *lane = controlScreen()) lane->sendControl(message, shareEpoch_);
}

bool SessionCoordinator::canRequestControl() const
{
    if (roomType_ == RoomType::Meeting)
        return receivingScreen_ && screenActive() && peerControllable_ && controlChannelReady_
            && shareState_ == PreviewState::Running && remoteState_ == RemoteState::Idle;
    return roomType_ == RoomType::Assistance && state_ == State::PeerAvailable && media_.environment().ready
        && peerControllable_ && remoteState_ == RemoteState::Idle
        && ((!screenActive() && shareState_ != PreviewState::Stopping)
            || (receivingScreen_ && shareState_ == PreviewState::Running && controlChannelReady_));
}

void SessionCoordinator::requestRemoteControlWhenReady()
{
    if (remoteAccess_ && remoteDialPending_ && canRequestControl() && requestControl())
        remoteDialPending_ = false;
}

bool SessionCoordinator::requestControl()
{
    if (!canRequestControl())
        return false;
    controlRequestId_ = QUuid::createUuid().toString(QUuid::Id128);
    QJsonObject request{{"action", "request"}, {"requestId", controlRequestId_}};
    if (roomType_ == RoomType::Meeting)
    {
        meetingControlPeerId_ = shareOwnerId_;
        request.insert("targetPeerId", shareOwnerId_);
        request.insert("streamId", streamId_);
    }
    if (!sendNegotiation("control", request))
    {
        controlRequestId_.clear();
        return false;
    }
    remoteState_ = RemoteState::Requesting;
    remoteDetail_ = QStringLiteral("等待对方允许控制");
    controlDeadline_.start(60000);
    emit remoteChanged();
    return true;
}

bool SessionCoordinator::grantControl()
{
    if (roomType_ == RoomType::Meeting)
        return remoteState_ == RemoteState::Pending && grantMeetingControl(meetingControlPeerId_);
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
    inputReceiver_.authorize(shareEpoch_, control_.epoch(), roomType_ == RoomType::Meeting ? meetingControlPeerId_ : QString());
    controlDeadline_.stop();
    remoteDetail_ = QStringLiteral("对方正在控制本机，点击结束控制可立即收回");
    sendControlMessage({{"action", "grant"}, {"epoch", QString::number(control_.epoch())},
        {"requestId", controlRequestId_}, {"cursorSync", true},
        {"fileClipboard", roomType_ == RoomType::Assistance}, {"fileClipboardReverse", roomType_ == RoomType::Assistance}});
    emit remoteChanged();
}

void SessionCoordinator::startFileClipboard()
{
    if (roomType_ != RoomType::Assistance || !controlChannelReady_) return;
    if (remoteState_ == RemoteState::Controlling && receivingScreen_ && peerFileClipboard_)
    {
        if (!fileClipboard_.active())
        {
            fileClipboard_.start(false, remoteControlEpoch_);
            screen_.sendControl({{"action", "file_ready"}, {"epoch", QString::number(remoteControlEpoch_)},
                {"reverse", peerReverseFileClipboard_}}, shareEpoch_);
        }
        if (peerReverseFileClipboard_) reverseFileClipboard_.start(true, remoteControlEpoch_);
    }
    else if (remoteState_ == RemoteState::Controlled && !receivingScreen_ && peerFileReady_)
    {
        fileClipboard_.start(true, control_.epoch());
        if (peerReverseFileReady_) reverseFileClipboard_.start(false, control_.epoch());
    }
}

QString SessionCoordinator::remoteDetail() const
{
    auto detail = remoteDetail_;
    if (!inputWarning_.isEmpty()) detail += '\n' + inputWarning_;
    const bool controlled = remoteState_ == RemoteState::Controlled;
    if (!fileClipboard_.status().isEmpty())
        detail += '\n' + (controlled ? QStringLiteral("本机 → 对方：") : QStringLiteral("对方 → 本机：")) + fileClipboard_.status();
    if (!reverseFileClipboard_.status().isEmpty())
        detail += '\n' + (controlled ? QStringLiteral("对方 → 本机：") : QStringLiteral("本机 → 对方：")) + reverseFileClipboard_.status();
    return detail;
}

void SessionCoordinator::clearControl()
{
    inputReceiver_.revoke();
    fileClipboard_.stop();
    reverseFileClipboard_.stop();
    peerFileClipboard_ = peerFileReady_ = false;
    peerReverseFileClipboard_ = peerReverseFileReady_ = false;
    controlDeadline_.stop();
    cursorTimer_.stop();
    if (cursorSyncActive_)
        if (auto *lane = controlScreen()) lane->setCursorEmbedded(true, shareEpoch_);
    cursorSyncActive_ = false;
    sentCursor_ = remoteCursor_ = {};
    cursorSequence_ = remoteCursorSequence_ = 0;
    emit remoteCursorChanged({});
    control_.revoke();
    input_.releaseAll();
    remoteState_ = RemoteState::Idle;
    remoteControlEpoch_ = inputSequence_ = 0;
    inputProbeClocks_.clear();
    inputProbeDeadline_.stop();
    motionProbeClock_.invalidate();
    inputWarning_.clear();
    controlRequestId_.clear();
    meetingControlPeerId_.clear();
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
            controlScreen()->setCursorEmbedded(true, shareEpoch_);
            message.insert("visible", false);
            sendControlMessage(message);
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
            sendControlMessage(message);
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
        controlScreen()->setCursorEmbedded(false, shareEpoch_);
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
    sendControlMessage(message);
    ++cursorSequence_;
    sentCursor_ = cursor;
}

void SessionCoordinator::stopControl()
{
    if (remoteAccess_ && state_ == State::PeerAvailable)
        return disconnectRemote();
    const bool stopDesktop = controlStartedShare_;
    QJsonObject revoke{{"action", "revoke"}, {"requestId", controlRequestId_}};
    if (roomType_ == RoomType::Meeting)
    {
        revoke.insert("targetPeerId", meetingControlPeerId_);
        revoke.insert("streamId", streamId_);
        revoke.insert("epoch", QString::number(remoteState_ == RemoteState::Controlled ? control_.epoch() : remoteControlEpoch_));
        if (receivingScreen_ && !controlRequestId_.isEmpty()) meetingEndedControlRequests_.insert(controlRequestId_);
    }
    if (state_ == State::PeerAvailable && remoteState_ != RemoteState::Idle)
        sendNegotiation("control", revoke);
    if (controlChannelReady_ && remoteState_ != RemoteState::Idle)
        sendControlMessage(revoke);
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
    if (message.value("kind") == "move" && !message.contains("probe")
        && (!motionProbeClock_.isValid() || motionProbeClock_.elapsed() >= 1000))
    {
        message.insert("probe", true);
        motionProbeClock_.start();
        events_.append("control-input", QString("send seq=%1 kind=move x=%2 y=%3")
            .arg(inputSequence_).arg(message.value("x").toDouble(), 0, 'f', 4).arg(message.value("y").toDouble(), 0, 'f', 4));
    }
    if (message.value("kind") == "button")
    {
        if (!message.contains("probe"))
            message.insert("probe", true);
        events_.append("control-input", QString("send seq=%1 button=%2 pressed=%3 x=%4 y=%5")
            .arg(inputSequence_).arg(message.value("button").toInt()).arg(message.value("pressed").toBool())
            .arg(message.value("x").toDouble(), 0, 'f', 4).arg(message.value("y").toDouble(), 0, 'f', 4));
    }
    if (message.value("probe").toBool())
    {
        // Bound memory when a peer stops replying.
        if (inputProbeClocks_.size() >= 64)
        {
            events_.append("control-input", QString("pending acknowledgement limit: dropped seq=%1")
                .arg(inputProbeClocks_.firstKey()));
            inputProbeClocks_.erase(inputProbeClocks_.begin());
        }
        inputProbeClocks_[inputSequence_].start();
        if (!inputProbeDeadline_.isActive()) inputProbeDeadline_.start();
    }
    sendControlMessage(message);
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
        inputProbeClocks_.clear();
        inputProbeDeadline_.stop();
        motionProbeClock_.invalidate();
        inputWarning_.clear();
        remoteState_ = RemoteState::Controlling;
        peerFileClipboard_ = message.value("fileClipboard").toBool();
        peerReverseFileClipboard_ = message.value("fileClipboardReverse").toBool();
        startFileClipboard();
        remoteDetail_ = QStringLiteral("正在控制对方电脑；点击共享画面后操作键鼠");
        if (peerFileClipboard_) remoteDetail_ += peerReverseFileClipboard_
            ? QStringLiteral("；支持双向复制文件，传完后在目标电脑粘贴")
            : QStringLiteral("；复制对方文件后待传输完成可在本机粘贴");
        if (message.value("cursorSync").toBool())
            sendControlMessage({{"action", "cursor_ready"}, {"epoch", QString::number(epoch)}});
        emit remoteChanged();
    }
    else if (action == "revoke")
        stopControl();
    else if (action == "file_ready" && remoteState_ == RemoteState::Controlled && !receivingScreen_
        && message.value("epoch").toString() == QString::number(control_.epoch()))
    {
        peerFileReady_ = true;
        peerReverseFileReady_ = message.value("reverse").toBool();
        startFileClipboard();
    }
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
        auto probe = inputProbeClocks_.find(sequence);
        if (epochValid && sequenceValid && epoch == remoteControlEpoch_ && probe != inputProbeClocks_.end())
        {
            const auto roundTripUs = probe->nsecsElapsed() / 1000;
            inputProbeClocks_.erase(probe);
            if (inputProbeClocks_.isEmpty()) inputProbeDeadline_.stop();
            const auto injectUs = message.value("injectUs").toInteger();
            const bool accepted = message.value("accepted").toBool();
            events_.append("control-input", QString("ack seq=%1 accepted=%2 rttUs=%3 injectUs=%4 error=%5")
                .arg(sequence).arg(accepted).arg(roundTripUs).arg(injectUs).arg(message.value("error").toString()));
            const auto warning = accepted ? QString() : QStringLiteral("输入未执行：") + message.value("error").toString();
            if (inputWarning_ != warning) { inputWarning_ = warning; emit remoteChanged(); }
            emit inputProbeAcknowledged(sequence, roundTripUs, injectUs, accepted);
        }
    }
    else if (action == "input" && remoteState_ == RemoteState::Controlled && !receivingScreen_)
    {
        inputReceiver_.receive(shareEpoch_, message);
    }
}

void SessionCoordinator::receiveControlRequest(const QJsonObject &message)
{
    if (roomType_ == RoomType::Meeting)
        return receiveMeetingControlRequest(message);
    const auto action = message.value("action").toString();
    const auto requestId = message.value("requestId").toString();
    if (action == "capabilities")
    {
        peerControllable_ = message.value("control").toBool();
        const double fps = message.value("maximumFps").toDouble();
        peerMaximumFps_ = std::isfinite(fps) && fps >= 5 && fps <= 240 && fps == int(fps) ? int(fps) : 0;
        emit remoteChanged();
        requestRemoteControlWhenReady();
    }
    else if (action == "request" && protocol::isRoomIdValid(requestId)
        && input_.available() && media_.environment().ready && remoteState_ == RemoteState::Idle
        && (!screenActive() || (!receivingScreen_ && desktopSource_
            && shareState_ == PreviewState::Running && controlChannelReady_))
        && control_.request(peerId_))
    {
        controlRequestId_ = requestId;
        remoteState_ = RemoteState::Pending;
        remoteDetail_ = peerName_ + QStringLiteral(" 请求控制本机（含双方文件复制），请选择允许或拒绝");
        controlDeadline_.start(30000);
        emit remoteChanged();
        if (remoteAccess_ && !requireRemoteApproval_)
            grantControl();
    }
    else if (action == "revoke" && !controlRequestId_.isEmpty() && requestId == controlRequestId_)
        stopControl();
}

void SessionCoordinator::receiveShare(const protocol::Message &message)
{
    if (roomType_ == RoomType::Meeting)
    {
        const auto id = message.payload.value("fromPeerId").toString();
        if (message.payload.value("streamId").toString() == streamId_ && meetingSharePeers_.contains(id))
            meetingSharePeers_[id]->negotiate(message.type, message.payload, shareEpoch_);
        return;
    }
    const auto id = message.payload.value("streamId").toString();
    if (!protocol::isRoomIdValid(id))
        return;
    if (message.type == "share")
    {
        const auto action = message.payload.value("action").toString();
        if (action == "start" && memberCount() > 2) return;
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
    if (conferenceAutoConnect_)
    {
        meshActive_ = true; meshReady_ = false;
        conferenceId_ = "mesh";
        conferenceMicMuted_ = !conferenceJoinMicrophoneEnabled_;
        conferenceCameraHidden_ = !conferenceJoinCameraEnabled_;
        conferenceSpeakerMuted_ = !conferenceJoinSpeakerEnabled_;
        conferenceState_ = ConferenceState::Connecting;
        conferenceDetail_ = QStringLiteral("正在连接会议成员音视频");
        conferenceMediaPending_ = true;
        ++conferenceEpoch_;
        stopPreview(); stopAudioTest();
        emit conferenceChanged();
        startConferenceMediaWhenReady();
        return true;
    }
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
    if (meshActive_)
    {
        meetingMedia_.start(conferenceDevices_, conferenceEpoch_, conferenceMicMuted_, conferenceCameraHidden_, conferenceSpeakerMuted_);
        return;
    }
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
        if (meshActive_)
        {
            meetingMedia_.setMuted(microphone, camera, speaker, conferenceEpoch_);
            if (cameraChanged) for (const auto &id : meshPeers_) sendMeshCamera(id);
        }
        else conference_.setMuted(microphone, camera, speaker, conferenceEpoch_);
        if (cameraChanged && !meshActive_)
            sendNegotiation("conference", {{"action", "camera"}, {"sessionId", conferenceId_}, {"hidden", camera}});
    }
    if (cameraChanged) emit conferenceCameraChanged();
}

void SessionCoordinator::stopConference() { clearConference(true); }

void SessionCoordinator::clearConference(bool notifyPeer)
{
    if (!conferenceActive()) return;
    conferenceDeadline_.stop();
    const bool wasMesh = meshActive_;
    meshDeadline_.stop();
    meshDeadlines_.clear();
    if (notifyPeer)
    {
        if (wasMesh)
            for (const auto &id : conferencePeers_.keys())
                sendNegotiation("conference", {{"action", "stop"}, {"sessionId", meshSessionId(id)}, {"targetPeerId", id}});
        else sendNegotiation("conference", {{"action", "stop"}, {"sessionId", conferenceId_}});
    }
    meshActive_ = meshReady_ = false;
    for (const auto &id : meshPeers_) emit conferencePeerFrame(id, {});
    meshPeers_.clear(); meshStatus_.clear(); meshHidden_.clear(); meshPending_.clear();
    conferenceId_.clear();
    conferenceMediaPending_ = false;
    conferencePendingSignals_.clear();
    conferenceCleanupPending_ = true;
    conferenceState_ = ConferenceState::Idle;
    conferenceCameraHidden_ = conferenceRemoteCameraHidden_ = false;
    conferenceDetail_ = QStringLiteral("会议已结束");
    if (wasMesh) meetingMedia_.stop(++conferenceEpoch_);
    else conference_.stop(++conferenceEpoch_);
    emit conferenceLocalFrame({}); emit conferenceRemoteFrame({});
    events_.append("conference", conferenceDetail_);
    emit conferenceChanged();
}

void SessionCoordinator::receiveConference(const QJsonObject &payload)
{
    if (roomType_ != RoomType::Meeting)
        return;
    if (conferenceAutoConnect_) { receiveMeshConference(payload); return; }
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

QString SessionCoordinator::meshSessionId(const QString &id) const
{
    const auto pair = ownPeerId_ < id ? ownPeerId_ + ":" + id : id + ":" + ownPeerId_;
    return QString::fromLatin1(QCryptographicHash::hash(pair.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

void SessionCoordinator::sendMeshCamera(const QString &id)
{
    sendNegotiation("conference", {{"action", "camera"}, {"sessionId", meshSessionId(id)},
        {"targetPeerId", id}, {"hidden", conferenceCameraHidden_}});
}

void SessionCoordinator::syncConferencePeers()
{
    if (!meshActive_ || !meshReady_) return;
    for (auto it = conferencePeers_.cbegin(); it != conferencePeers_.cend(); ++it)
    {
        const auto &id = it.key();
        if (meshPeers_.contains(id) || meshStatus_.contains(id)) continue;
        meshPeers_.insert(id); meshStatus_[id] = "connecting";
        meshDeadlines_.insert(id, QDeadlineTimer(45000));
        meshDeadline_.start();
        meetingMedia_.addPeer(id, ownPeerId_ < id, conferenceEpoch_);
        sendMeshCamera(id);
    }
    const auto pending = meshPending_; meshPending_.clear();
    for (const auto &payload : pending) receiveMeshConference(payload);
    if (meshActive_ && meshPeers_.isEmpty()) { clearConference(false); return; }
    updateMeshState();
}

void SessionCoordinator::updateMeshState()
{
    if (!meshActive_) return;
    int running = 0;
    for (const auto &status : meshStatus_) if (status == "running") ++running;
    conferenceState_ = running ? ConferenceState::Running : ConferenceState::Connecting;
    conferenceDetail_ = QStringLiteral("已连接 %1 / %2 位远端成员").arg(running).arg(conferencePeers_.size());
    emit conferenceChanged();
}

void SessionCoordinator::receiveMeshConference(const QJsonObject &payload)
{
    const auto id = payload.value("fromPeerId").toString();
    if (!conferencePeers_.contains(id) || payload.value("sessionId").toString() != meshSessionId(id)) return;
    const auto action = payload.value("action").toString();
    if (action == "camera")
    {
        meshHidden_[id] = payload.value("hidden").toBool();
        if (id == peerId_) conferenceRemoteCameraHidden_ = meshHidden_.value(id);
        emit conferenceCameraChanged(); return;
    }
    if (action == "stop")
    {
        if (!meshActive_) return;
        meshStatus_[id] = "stopped"; meshPeers_.remove(id);
        meshDeadlines_.remove(id);
        meetingMedia_.removePeer(id, conferenceEpoch_); emit conferencePeerFrame(id, {});
        // Before ready, the other members have not been added to meshPeers_ yet.
        if (meshReady_ && meshPeers_.isEmpty()) clearConference(false);
        else updateMeshState();
        return;
    }
    if (!meshActive_) return;
    if (!meshReady_)
    {
        if (meshPending_.size() >= 768) { clearConference(true); return; }
        meshPending_.append(payload); return;
    }
    if (meshPeers_.contains(id)) meetingMedia_.negotiate(id, action, payload, conferenceEpoch_);
}

void SessionCoordinator::receive(const protocol::Message &message)
{
    if (message.type == "annotation")
    {
        if (roomType_ == RoomType::Meeting && (state_ == State::PeerAvailable || state_ == State::WaitingForPeer))
            receiveAnnotation(message.payload);
        return;
    }
    if (remoteAccess_ && message.type == "remote_registered"
        && (state_ == State::Joining || state_ == State::WaitingForPeer))
    {
        const auto code = message.payload.value("code").toString();
        if (!protocol::isRemoteCodeValid(code)) return fail(QStringLiteral("服务返回了无效校验码"));
        if (registeredRemoteCodePolicy_ != "startup"
            && message.payload.value("codePolicy").toString() != registeredRemoteCodePolicy_)
            return fail(QStringLiteral("信令服务不支持所选校验码策略，请更新服务端"));
        remoteCode_ = code;
        if (state_ == State::WaitingForPeer) { emit stateChanged(state_); return; }
        deadline_.stop();
        changeState(State::WaitingForPeer, remotePeerAddress_.isEmpty() ? QStringLiteral("已在线，可输入对方校验码连接") : QStringLiteral("已在线，可连接已验证的对端 IP"));
        return;
    }
    if (remoteAccess_ && message.type == "remote_paired" && state_ == State::WaitingForPeer)
    {
        remoteConnecting_ = false;
        roomId_ = message.payload.value("roomId").toString();
        remoteDialPending_ = message.payload.value("controller").toBool();
        state_ = State::Joining;
        receive({"joined", message.payload});
        if (state_ == State::PeerAvailable)
        {
            const auto peers = message.payload.value("peers").toArray();
            const auto code = peers.isEmpty() ? QString() : peers.first().toObject().value("code").toString();
            if (protocol::isRemoteCodeValid(code)) emit remotePeerConnected(code, peerName_);
        }
        return;
    }
    if (remoteAccess_ && message.type == "remote_ready")
    {
        // Legacy servers have no reason field, so require a fresh user request there.
        if (message.payload.value("reason").toString() != "disconnected") profileResumeControl_ = false;
        deadline_.stop();
        remoteConnecting_ = false;
        clearScreenShare(false);
        clearMembership();
        remoteDialPending_ = false;
        changeState(State::WaitingForPeer, remotePeerAddress_.isEmpty() ? QStringLiteral("已在线，可输入对方校验码连接") : QStringLiteral("已在线，可连接已验证的对端 IP"));
        emit membersChanged();
        return;
    }
    if (message.type == "error")
    {
        const auto code = message.payload.value("code").toString();
        if (remoteAccess_ && code == "remote_identity_in_use")
            return fail(QStringLiteral("本机固定校验码已被另一个实例使用，请关闭重复实例后重试"));
        if (remoteAccess_ && code == "remote_registry_unavailable")
            return fail(QStringLiteral("信令服务无法保存校验码，请检查服务端配置目录的读写权限"));
        if (remoteAccess_ && state_ == State::WaitingForPeer && code == "not_joined") return;
        if (remoteAccess_ && (code == "remote_unavailable" || code == "remote_busy" || code == "invalid_remote_code" || code == "invalid_remote_address" || code == "remote_ambiguous"))
        {
            deadline_.stop();
            remoteConnecting_ = false;
            detail_ = code == "remote_busy" ? QStringLiteral("对方正在连接其他电脑，请稍后重试")
                : !remotePeerAddress_.isEmpty() ? QStringLiteral("对端未在线、未验证本机 IP 或地址不唯一，请检查后重试")
                : QStringLiteral("校验码无效或对方不在线，请检查后重试");
            emit stateChanged(state_);
            return;
        }
        if (code == "peer_unavailable" && (state_ == State::WaitingForPeer || state_ == State::PeerAvailable))
            return;
        if (code == "room_full")
            fail(roomType_ == RoomType::Meeting ? QStringLiteral("会议房间已满，最多支持四人") : QStringLiteral("协助房间已满，最多支持两人"));
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
        annotationColor_ = message.payload.value("annotationColor").toString();
        const auto peers = message.payload.value("peers").toArray();
        for (const auto &value : peers)
        {
            const auto peer = value.toObject();
            conferencePeers_.insert(peer.value("peerId").toString(), peer.value("name").toString());
        }
        if (peers.isEmpty())
            changeState(State::WaitingForPeer, QStringLiteral("已加入房间，等待另一位成员"));
        else
        {
            peerId_ = peers.first().toObject().value("peerId").toString();
            peerName_ = peers.first().toObject().value("name").toString();
            changeState(State::PeerAvailable, roomType_ == RoomType::Meeting
                ? QStringLiteral("会议成员已入会，正在连接音视频") : remoteAccess_
                ? QStringLiteral("已连接对方，等待远控授权") : QStringLiteral("双方已入会，音视频尚未开启"));
        }
        emit membersChanged();
        return;
    }
    if (message.type == "peer_joined" && (state_ == State::WaitingForPeer || state_ == State::PeerAvailable))
    {
        const auto id = message.payload.value("peerId").toString();
        conferencePeers_.insert(id, message.payload.value("name").toString());
        if (roomType_ == RoomType::Meeting && ownsSharedScreen()) addMeetingSharePeer(id);
        if (peerId_.isEmpty()) { peerId_ = id; peerName_ = conferencePeers_.value(id); }
        if (state_ == State::WaitingForPeer)
            changeState(State::PeerAvailable, QStringLiteral("成员已入会"));
        else if (meshActive_) syncConferencePeers();
        emit membersChanged();
    }
    else if (message.type == "peer_left" && state_ == State::PeerAvailable)
    {
        const auto id = message.payload.value("peerId").toString();
        if (!conferencePeers_.remove(id)) return;
        if (meshActive_)
        {
            meshPeers_.remove(id); meshStatus_.remove(id); meshHidden_.remove(id);
            meshDeadlines_.remove(id);
            meetingMedia_.removePeer(id, conferenceEpoch_); emit conferencePeerFrame(id, {});
            for (qsizetype i = meshPending_.size(); i-- > 0;)
                if (meshPending_.at(i).value("fromPeerId").toString() == id) meshPending_.removeAt(i);
        }
        else if (id == peerId_) clearConference(false);
        if (roomType_ == RoomType::Meeting) removeMeetingSharePeer(id);
        else if (id == peerId_) { clearScreenShare(false); clearControl(); }
        if (!conferencePeers_.isEmpty())
        {
            if (id == peerId_)
            {
                peerId_ = conferencePeers_.firstKey(); peerName_ = conferencePeers_.first();
                emit conferenceRemoteFrame({});
                conferenceRemoteCameraHidden_ = meshHidden_.value(peerId_);
            }
            updateMeshState(); emit membersChanged(); return;
        }
        clearConference(false);
        clearScreenShare(false);
        clearControl();
        peerControllable_ = false;
        peerMaximumFps_ = 0;
        peerId_.clear();
        peerName_.clear();
        control_.revoke();
        changeState(State::WaitingForPeer, QStringLiteral("对方已离开，等待另一位成员"));
        emit membersChanged();
    }
    else if (protocol::isRelayType(message.type) && state_ == State::PeerAvailable)
    {
        // Do not accept an event from an old peer after a room membership change.
        if (conferencePeers_.contains(message.payload.value("fromPeerId").toString()))
        {
            if (message.type == "control")
                receiveControlRequest(message.payload);
            else if (message.type == "conference")
                receiveConference(message.payload);
            else
            {
                if (roomType_ == RoomType::Meeting || message.payload.value("fromPeerId").toString() == peerId_) receiveShare(message);
                emit negotiationReceived(message);
            }
        }
    }
}
} // namespace zjdesk
