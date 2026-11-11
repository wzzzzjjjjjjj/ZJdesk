#pragma once

#include "diagnostics/event_log.h"
#include "media/media_engine.h"
#include "media/conference_media.h"
#include "media/screen_share.h"
#include "platform/camera_permission.h"
#include "remote-control/control_session.h"
#include "remote-control/file_clipboard.h"
#include "remote-control/input_injector.h"
#include "remote-control/input_receiver.h"
#include "signaling/signaling_client.h"
#include <QElapsedTimer>
#include <QDeadlineTimer>
#include <QMap>
#include <QJsonArray>
#include <QTimer>
#include <functional>
#include "peer_connection.h"

class WindowTests;

namespace zjdesk
{
class SessionCoordinator final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Connecting, Joining, WaitingForPeer, PeerAvailable, Failed };
    Q_ENUM(State)
    enum class RoomType { Assistance, Meeting };
    enum class PreviewState { Stopped, Starting, Running, Stopping, Failed };
    Q_ENUM(PreviewState)
    explicit SessionCoordinator(QObject *parent = nullptr);
    ~SessionCoordinator() override;
    bool join(const QUrl &url, const QString &roomId, const QString &name, RoomType type = RoomType::Assistance);
    void leave();
    bool startRemoteAccess(const QUrl &url, const QString &name, const QString &peerAddress = {});
    bool connectRemote(const QString &code);
    bool connectRemoteIp();
    bool setPeerProfile(const PeerProfile &profile, const QUrl &serverOverride, const QString &name);
    void clearPeerProfile();
    void disconnectRemote();
    QString remoteCode() const { return remoteCode_; }
    bool remoteAccess() const { return remoteAccess_; }
    bool remoteConnecting() const { return remoteConnecting_; }
    bool requiresRemoteApproval() const { return requireRemoteApproval_; }
    void setRequiresRemoteApproval(bool enabled) { requireRemoteApproval_ = enabled; }
    bool setRemoteCodePolicy(const QString &policy, const QString &identity);
    QString remoteCodePolicy() const { return remoteCodePolicy_; }
    bool sendNegotiation(const QString &type, const QJsonObject &payload);
    State state() const { return state_; }
    RoomType roomType() const { return roomType_; }
    QString detail() const { return detail_; }
    QString peerName() const { return peerName_; }
    int memberCount() const;
    EventLog *events() { return &events_; }
    quint64 sentMessages() const { return sent_; }
    quint64 receivedMessages() const { return received_; }
    const MediaEnvironment &mediaEnvironment() const { return media_.environment(); }
    const QList<MediaDevice> &devices() const { return devices_; }
    void refreshDevices();
    CameraPermission cameraPermissionStatus() const { return cameraPermission_; }
    void requestCameraPermission();
    void openCameraPrivacySettings();
    bool startPreview(const QString &sourceId);
    void stopPreview();
    bool startAudioTest(bool audible = true);
    void stopAudioTest();
    PreviewState previewState() const { return previewState_; }
    PreviewState audioState() const { return audioState_; }
    QString previewDetail() const { return previewDetail_; }
    QString audioDetail() const { return audioDetail_; }
    PreviewStats previewStats() const { return media_.stats(); }
    bool startScreenShare(const QString &source = "desktop", int targetFps = 0);
    ShareStats shareStats() const { return shareStats_; }
    void reportSharePresentation(quint64 frames, bool active = true, double gapMaxMs = -1,
        const VideoPresentationStats &presentation = {});
    void stopScreenShare();
    PreviewState shareState() const { return shareState_; }
    QString shareDetail() const { return shareDetail_; }
    bool receivingScreen() const { return receivingScreen_; }
    bool screenActive() const { return !streamId_.isEmpty(); }
    QJsonArray annotations() const { return annotations_; }
    QJsonObject annotationAuthor() const { return {{"authorId", ownPeerId_}, {"name", name_}, {"color", annotationColor_}}; }
    bool canAnnotate() const { return roomType_ == RoomType::Meeting && !shareOwnerId_.isEmpty() && screenActive(); }
    bool ownsSharedScreen() const { return canAnnotate() && shareOwnerId_ == ownPeerId_; }
    bool canDrawAnnotation() const { return canAnnotate() && (ownsSharedScreen() || annotationApproved_); }
    bool annotationRequestPending() const { return annotationRequestPending_; }
    bool requestAnnotationPermission();
    bool respondAnnotationPermission(const QString &peerId, const QString &streamId, bool approved);
    bool isDesktopShare() const { return ownsSharedScreen() && desktopSource_; }
    bool annotationsInVideo() const { return annotationsInVideo_; }
    QString annotationDetail() const { return annotationDetail_; }
    bool submitAnnotation(const QJsonObject &stroke);
    bool clearAnnotations(bool all = false);
    enum class RemoteState { Idle, Requesting, Pending, Controlling, Controlled };
    Q_ENUM(RemoteState)
    RemoteState remoteState() const { return remoteState_; }
    QString remoteDetail() const;
    bool canRequestControl() const;
    bool requestControl();
    bool grantControl();
    bool grantMeetingControl(const QString &peerId);
    bool canGrantMeetingControl(const QString &peerId);
    QString meetingControlPeerId() const { return meetingControlPeerId_; }
    void stopControl();
    void sendRemoteInput(const QJsonObject &input);
    enum class ConferenceState { Idle, Calling, Incoming, Connecting, Running, Failed };
    Q_ENUM(ConferenceState)
    ConferenceState conferenceState() const { return conferenceState_; }
    QString conferenceDetail() const { return conferenceDetail_; }
    bool conferenceActive() const { return !conferenceId_.isEmpty(); }
    bool conferenceDevicesBusy() const { return conferenceActive() || conferenceCleanupPending_; }
    bool conferenceCameraHidden() const { return conferenceCameraHidden_; }
    bool conferenceMicrophoneMuted() const { return conferenceMicMuted_; }
    bool conferenceSpeakerMuted() const { return conferenceSpeakerMuted_; }
    bool conferenceRemoteCameraHidden() const { return conferenceRemoteCameraHidden_; }
    QMap<QString, QString> conferencePeers() const { return conferencePeers_; }
    QString peerId() const { return peerId_; }
    QMap<QString, QString> conferencePeerStatus() const { return meshStatus_; }
    bool conferencePeerCameraHidden(const QString &id) const { return meshHidden_.value(id); }
    bool setConferenceDevices(const ConferenceDevices &devices);
    void setConferenceAutoConnect(bool enabled);
    void setConferenceJoinDefaults(bool microphoneEnabled, bool cameraEnabled, bool speakerEnabled);
    bool startConference();
    bool acceptConference();
    void stopConference();
    void setConferenceMuted(bool microphone, bool camera, bool speaker);

signals:
    void annotationsChanged();
    void annotationPermissionRequested(const QString &peerId, const QString &name, const QString &streamId);
    void annotationPermissionResult(bool approved);
    void annotationsCleared(const QString &authorId);
    void stateChanged(zjdesk::SessionCoordinator::State state);
    void membersChanged();
    void peerRouteChanged(const zjdesk::NetworkConfiguration &configuration);
    void peerRouteUnavailable();
    void trafficChanged();
    void negotiationReceived(const zjdesk::protocol::Message &message);
    void devicesChanged();
    void cameraPermissionRequired();
    void cameraPrivacySettingsFinished(bool opened, const QString &error);
    void previewChanged();
    void audioChanged();
    void previewFrame(const QImage &image);
    void shareChanged();
    void shareFrame(const QImage &image);
    void shareStatisticsChanged(const zjdesk::ShareStats &stats);
    void remoteChanged();
    void remotePeerConnected(const QString &code, const QString &name);
    void inputProbeInjected(quint64 sequence, qint64 injectUs, bool accepted);
    void inputProbeAcknowledged(quint64 sequence, qint64 roundTripUs, qint64 injectUs, bool accepted);
    void remoteCursorChanged(const zjdesk::RemoteCursor &cursor);
    void conferenceChanged();
    void conferenceCameraChanged();
    void conferenceLocalFrame(const QImage &image);
    void conferenceRemoteFrame(const QImage &image);
    void conferenceAudioReceived(quint64 buffers, double peak);
    void conferencePeerFrame(const QString &id, const QImage &image);
    void conferencePeerAudioReceived(const QString &id, quint64 buffers, double peak);

private:
    friend class ::WindowTests;
    std::function<void(QObject *, CameraSettingsCompletion)> cameraSettingsOpener_ = zjdesk::openCameraPrivacySettings;
    bool cameraSettingsOpening_ = false;
    void receive(const protocol::Message &message);
    void changeState(State state, const QString &detail);
    void fail(const QString &reason);
    void changePeerRoute(const NetworkConfiguration &configuration);
    void retryPeerSession();
    void rememberPeerShare();
    void clearMembership();
    void receiveShare(const protocol::Message &message);
    void clearScreenShare(bool notifyPeer);
    bool startMeetingShare(const QString &source, int targetFps);
    void clearMeetingShare(bool notifyPeer);
    void addMeetingSharePeer(const QString &id);
    void removeMeetingSharePeer(const QString &id);
    void receiveAnnotation(const QJsonObject &payload);
    void receiveControl(const QJsonObject &message);
    void receiveControlRequest(const QJsonObject &message);
    void receiveMeetingControlRequest(const QJsonObject &message);
    ScreenShare *controlScreen();
    void sendControlMessage(const QJsonObject &message);
    void requestRemoteControlWhenReady();
    void sendControlGrant();
    void startFileClipboard();
    void clearControl();
    void sendCursor();
    void receiveConference(const QJsonObject &payload);
    void clearConference(bool notifyPeer);
    void beginConferenceMedia(bool offerer);
    void startConferenceMediaWhenReady();
    void syncConferencePeers();
    void receiveMeshConference(const QJsonObject &payload);
    QString meshSessionId(const QString &id) const;
    void sendMeshCamera(const QString &id);
    void updateMeshState();

    SignalingClient signaling_;
    PeerConnection peerConnection_;
    QTimer peerRetry_;
    NetworkConfiguration peerRoute_;
    bool profileEnabled_ = false, profileOnline_ = false, profileResumeControl_ = false;
    QString profileName_, profileMeetingRoom_;
    QString profileShareSource_;
    int profileShareFps_ = 0;
    bool remoteAccess_ = false, remoteDialPending_ = false, remoteConnecting_ = false, requireRemoteApproval_ = true;
    QString remoteCode_;
    QString remoteCodePolicy_ = "startup", registeredRemoteCodePolicy_ = "startup", remoteIdentity_;
    EventLog events_;
    ControlSession control_;
    FileClipboard fileClipboard_;
    FileClipboard reverseFileClipboard_;
    InputInjector input_;
    RemoteState remoteState_ = RemoteState::Idle;
    QString remoteDetail_ = QStringLiteral("远程控制未开启");
    bool controlChannelReady_ = false;
    bool peerFileClipboard_ = false, peerFileReady_ = false;
    bool peerReverseFileClipboard_ = false, peerReverseFileReady_ = false;
    bool peerControllable_ = false;
    int peerMaximumFps_ = 0;
    bool desktopSource_ = false;
    bool controlStartedShare_ = false;
    QString controlRequestId_;
    quint64 remoteControlEpoch_ = 0;
    quint64 inputSequence_ = 0;
    QMap<quint64, QElapsedTimer> inputProbeClocks_;
    QElapsedTimer motionProbeClock_;
    QTimer inputProbeDeadline_;
    QString inputWarning_;
    QTimer controlDeadline_;
    QTimer cursorTimer_;
    RemoteCursor sentCursor_, remoteCursor_;
    quint64 cursorSequence_ = 0, remoteCursorSequence_ = 0;
    bool cursorSyncActive_ = false;
    QTimer deadline_;
    MediaEngine media_;
    ScreenShare screen_;
    QMap<QString, ScreenShare *> meetingSharePeers_;
    QSet<QString> meetingShareRunning_;
    QSet<QString> meetingControlReady_;
    QSet<QString> meetingEndedControlRequests_;
    QString meetingControlPeerId_;
    QString shareOwnerId_, pendingShareId_, meetingShareSource_, annotationColor_, annotationDetail_;
    bool annotationApproved_ = false;
    bool annotationRequestPending_ = false;
    int meetingShareFps_ = 0;
    QJsonArray annotations_;
    bool annotationsInVideo_ = false;
    InputReceiver inputReceiver_; // Stops before the media channel is destroyed.
    ConferenceMedia conference_;
    MeetingMedia meetingMedia_;
    QMap<QString, QString> conferencePeers_, meshStatus_;
    QMap<QString, bool> meshHidden_;
    QSet<QString> meshPeers_;
    QMap<QString, QDeadlineTimer> meshDeadlines_;
    QTimer meshDeadline_;
    QList<QJsonObject> meshPending_;
    bool meshActive_ = false, meshReady_ = false;
    ConferenceDevices conferenceDevices_;
    ConferenceState conferenceState_ = ConferenceState::Idle;
    QString conferenceDetail_ = QStringLiteral("视频会议未开启");
    QString conferenceId_;
    quint64 conferenceEpoch_ = 0;
    QTimer conferenceDeadline_;
    bool conferenceMediaPending_ = false;
    bool conferenceCleanupPending_ = false;
    bool conferenceOfferer_ = false;
    bool conferenceAutoConnect_ = false;
    bool conferenceJoinMicrophoneEnabled_ = true, conferenceJoinCameraEnabled_ = true, conferenceJoinSpeakerEnabled_ = true;
    bool conferenceMicMuted_ = false, conferenceCameraHidden_ = false, conferenceSpeakerMuted_ = false;
    bool conferenceRemoteCameraHidden_ = false;
    QList<QJsonObject> conferencePendingSignals_;
    PreviewState shareState_ = PreviewState::Stopped;
    QString shareDetail_ = QStringLiteral("屏幕共享未启动");
    QString streamId_;
    QString failedStreamId_;
    QString ownPeerId_;
    quint64 shareEpoch_ = 0;
    bool receivingScreen_ = false;
    ShareStats shareStats_;
    QList<MediaDevice> devices_;
    CameraPermission cameraPermission_ = CameraPermission::Unknown;
    bool deviceRefreshPending_ = false;
    PreviewState previewState_ = PreviewState::Stopped;
    PreviewState audioState_ = PreviewState::Stopped;
    QString previewDetail_ = QStringLiteral("本地预览未启动");
    QString audioDetail_ = QStringLiteral("音频测试未启动");
    quint64 previewEpoch_ = 0;
    quint64 audioEpoch_ = 0;
    State state_ = State::Idle;
    QString detail_ = QStringLiteral("尚未连接");
    QString roomId_;
    RoomType roomType_ = RoomType::Assistance;
    QString name_;
    QString remotePeerAddress_;
    QString peerId_;
    QString peerName_;
    quint64 sent_ = 0;
    quint64 received_ = 0;
};
} // namespace zjdesk
