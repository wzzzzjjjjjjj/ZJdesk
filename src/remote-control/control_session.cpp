#include "control_session.h"

namespace zjdesk
{
bool ControlSession::request(const QString &peerId)
{
    if (state_ != State::Idle || peerId.isEmpty())
        return false;
    peerId_ = peerId;
    state_ = State::Requested;
    return true;
}

bool ControlSession::grant()
{
    if (state_ != State::Requested)
        return false;
    ++epoch_;
    lastSequence_ = 0;
    state_ = State::Granted;
    return true;
}

void ControlSession::revoke()
{
    state_ = State::Idle;
    peerId_.clear();
    lastSequence_ = 0;
}

bool ControlSession::acceptInput(const QString &peerId, quint64 epoch, quint64 sequence)
{
    if (state_ != State::Granted || peerId != peerId_ || epoch != epoch_
        || sequence == 0 || sequence <= lastSequence_)
        return false;
    lastSequence_ = sequence;
    return true;
}
} // namespace zjdesk
