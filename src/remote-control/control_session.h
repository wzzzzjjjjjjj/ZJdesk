#pragma once

#include <QString>
#include <QtGlobal>

namespace zjdesk
{
// Authorization gate only. Transport and OS input injection are not implemented yet.
// The controlled device owns this state and grants only after local user consent.
class ControlSession
{
public:
    enum class State { Idle, Requested, Granted };
    bool request(const QString &peerId);
    bool grant();
    void revoke();
    bool acceptInput(const QString &peerId, quint64 epoch, quint64 sequence);
    State state() const { return state_; }
    quint64 epoch() const { return epoch_; }

private:
    State state_ = State::Idle;
    QString peerId_;
    quint64 epoch_ = 0;
    quint64 lastSequence_ = 0;
};
} // namespace zjdesk
