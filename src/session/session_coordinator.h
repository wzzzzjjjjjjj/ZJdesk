#pragma once

#include "diagnostics/event_log.h"
#include "media/media_engine.h"
#include "media/conference_media.h"
#include "media/screen_share.h"
#include "remote-control/control_session.h"
#include "remote-control/input_injector.h"
#include "signaling/signaling_client.h"
#include <QElapsedTimer>
#include <QTimer>

namespace zjdesk
{
class SessionCoordinator final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Connecting, Joining, WaitingForPeer, PeerAvailable, Failed };
    Q_ENUM(State)
    enum class PreviewState { Stopped, Starting, Running, Stopping, Failed };
    Q_ENUM(PreviewState)
    explicit SessionCoordinator(QObject *parent = nullptr);
    ~SessionCoordinator() override;
    bool join(const QUrl &url, const QString &roomId, const QString &name);
    void leave();
    bool sendNegotiation(const QString &type, const QJsonObject &payload);
    State state() const { return state_; }
    QString detail() const { return detail_; }
    QString peerName() const { return peerName_; }
    int memberCount() const;
    EventLog *events() { return &events_; }
    quint64 sentMessages() const { return sent_; }
    quint64 receivedMessages() const { return received_; }
    const MediaEnvironment &mediaEnvironment() const { return media_.environment(); }
    const QList<MediaDevice> &devices() const { return devices_; }
    void refreshDevices();
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
    enum class RemoteState { Idle, Requesting, Pending, Controlling, Controlled };
    Q_ENUM(RemoteState)
    RemoteState remoteState() const { return remoteState_; }
    QString remoteDetail() const { return remoteDetail_; }
    bool canRequestControl() const;
    bool requestControl();
    bool grantControl();
    void stopControl();
    void sendRemoteInput(const QJsonObject &input);
    enum class ConferenceState { Idle, Calling, Incoming, Connecting, Running, Failed };
    Q_ENUM(ConferenceState)
    ConferenceState conferenceState() const { return conferenceState_; }
    QString conferenceDetail() const { return conferenceDetail_; }
    bool conferenceActive() const { return !conferenceId_.isEmpty(); }
    bool conferenceDevicesBusy() const { return conferenceActive() || conferenceCleanupPending_; }
    bool setConferenceDevices(const ConferenceDevices &devices);
    bool startConference();
    bool acceptConference();
    void stopConference();
    void setConferenceMuted(bool microphone, bool camera, bool speaker);

signals:
    void stateChanged(zjdesk::SessionCoordinator::State state);
    void membersChanged();
    void trafficChanged();
    void negotiationReceived(const zjdesk::protocol::Message &message);
    void devicesChanged();
    void previewChanged();
    void audioChanged();
    void previewFrame(const QImage &image);
    void shareChanged();
    void shareFrame(const QImage &image);
    void shareStatisticsChanged(const zjdesk::ShareStats &stats);
    void remoteChanged();
    void inputProbeInjected(quint64 sequence, qint64 injectUs, bool accepted);
    void inputProbeAcknowledged(quint64 sequence, qint64 roundTripUs, qint64 injectUs, bool accepted);
    void remoteCursorChanged(const zjdesk::RemoteCursor &cursor);
    void conferenceChanged();
    void conferenceLocalFrame(const QImage &image);
    void conferenceRemoteFrame(const QImage &image);
    void conferenceAudioReceived(quint64 buffers, double peak);

private:
    void receive(const protocol::Message &message);
    void changeState(State state, const QString &detail);
    void fail(const QString &reason);
    void clearMembership();
    void receiveShare(const protocol::Message &message);
    void clearScreenShare(bool notifyPeer);
    void receiveControl(const QJsonObject &message);
    void receiveControlRequest(const QJsonObject &message);
    void sendControlGrant();
    void clearControl();
    void sendCursor();
    void receiveConference(const QJsonObject &payload);
    void clearConference(bool notifyPeer);
    void beginConferenceMedia(bool offerer);
    void startConferenceMediaWhenReady();

    SignalingClient signaling_;
    EventLog events_;
    ControlSession control_;
    InputInjector input_;
    RemoteState remoteState_ = RemoteState::Idle;
    QString remoteDetail_ = QStringLiteral("远程控制未开启");
    bool controlChannelReady_ = false;
    bool peerControllable_ = false;
    bool desktopSource_ = false;
    bool controlStartedShare_ = false;
    QString controlRequestId_;
    quint64 remoteControlEpoch_ = 0;
    quint64 inputSequence_ = 0;
    quint64 pendingProbeSequence_ = 0;
    QElapsedTimer inputProbeClock_;
    QTimer controlDeadline_;
    QTimer cursorTimer_;
    RemoteCursor sentCursor_, remoteCursor_;
    quint64 cursorSequence_ = 0, remoteCursorSequence_ = 0;
    bool cursorSyncActive_ = false;
    QTimer deadline_;
    MediaEngine media_;
    ScreenShare screen_;
    ConferenceMedia conference_;
    ConferenceDevices conferenceDevices_;
    ConferenceState conferenceState_ = ConferenceState::Idle;
    QString conferenceDetail_ = QStringLiteral("视频会议未开启");
    QString conferenceId_;
    quint64 conferenceEpoch_ = 0;
    QTimer conferenceDeadline_;
    bool conferenceMediaPending_ = false;
    bool conferenceCleanupPending_ = false;
    bool conferenceOfferer_ = false;
    bool conferenceMicMuted_ = false, conferenceCameraHidden_ = false, conferenceSpeakerMuted_ = false;
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
    PreviewState previewState_ = PreviewState::Stopped;
    PreviewState audioState_ = PreviewState::Stopped;
    QString previewDetail_ = QStringLiteral("本地预览未启动");
    QString audioDetail_ = QStringLiteral("音频测试未启动");
    quint64 previewEpoch_ = 0;
    quint64 audioEpoch_ = 0;
    State state_ = State::Idle;
    QString detail_ = QStringLiteral("尚未加入房间");
    QString roomId_;
    QString name_;
    QString peerId_;
    QString peerName_;
    quint64 sent_ = 0;
    quint64 received_ = 0;
};
} // namespace zjdesk
