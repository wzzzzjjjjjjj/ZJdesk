#pragma once

#include "diagnostics/event_log.h"
#include "media/media_engine.h"
#include "media/screen_share.h"
#include "remote-control/control_session.h"
#include "signaling/signaling_client.h"
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
    bool startScreenShare(const QString &source = "desktop");
    void stopScreenShare();
    PreviewState shareState() const { return shareState_; }
    QString shareDetail() const { return shareDetail_; }
    bool receivingScreen() const { return receivingScreen_; }
    bool screenActive() const { return !streamId_.isEmpty(); }

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

private:
    void receive(const protocol::Message &message);
    void changeState(State state, const QString &detail);
    void fail(const QString &reason);
    void clearMembership();
    void receiveShare(const protocol::Message &message);
    void clearScreenShare(bool notifyPeer);

    SignalingClient signaling_;
    EventLog events_;
    ControlSession control_;
    QTimer deadline_;
    MediaEngine media_;
    ScreenShare screen_;
    PreviewState shareState_ = PreviewState::Stopped;
    QString shareDetail_ = QStringLiteral("屏幕共享未启动");
    QString streamId_;
    QString ownPeerId_;
    quint64 shareEpoch_ = 0;
    bool receivingScreen_ = false;
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
