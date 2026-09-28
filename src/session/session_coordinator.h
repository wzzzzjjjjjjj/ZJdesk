#pragma once

#include "diagnostics/event_log.h"
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

signals:
    void stateChanged(zjdesk::SessionCoordinator::State state);
    void membersChanged();
    void trafficChanged();
    void negotiationReceived(const zjdesk::protocol::Message &message);

private:
    void receive(const protocol::Message &message);
    void changeState(State state, const QString &detail);
    void fail(const QString &reason);
    void clearMembership();

    SignalingClient signaling_;
    EventLog events_;
    ControlSession control_;
    QTimer deadline_;
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
