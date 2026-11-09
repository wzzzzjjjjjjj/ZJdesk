#include "input_receiver.h"
#include <QElapsedTimer>
#include <QMutexLocker>

namespace zjdesk
{
InputReceiver::InputReceiver()
{
#ifdef ZJDESK_HAS_XTEST
    // Cache Qt screen geometry before the injector starts using its own thread.
    injector_.available();
#endif
    moveToThread(&thread_);
    thread_.start();
}

InputReceiver::~InputReceiver()
{
    revoke();
    QMetaObject::invokeMethod(this, [this] { injector_.releaseAll(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void InputReceiver::authorize(quint64 shareEpoch, quint64 controlEpoch, const QString &peerId)
{
    QMutexLocker lock(&mutex_);
    if (shareEpoch_ == shareEpoch && controlEpoch_ == controlEpoch && peerId_ == peerId)
        return;
    shareEpoch_ = shareEpoch;
    controlEpoch_ = controlEpoch;
    peerId_ = peerId;
    sequence_ = 0;
    ++generation_;
}

void InputReceiver::revoke(quint64 shareEpoch, const QString &peerId, quint64 controlEpoch)
{
    QMutexLocker lock(&mutex_);
    if ((shareEpoch && shareEpoch != shareEpoch_) || (!peerId.isEmpty() && peerId != peerId_)
        || (controlEpoch && controlEpoch != controlEpoch_))
        return;
    shareEpoch_ = controlEpoch_ = sequence_ = 0;
    peerId_.clear();
    ++generation_;
    QMetaObject::invokeMethod(this, [this] { injector_.releaseAll(); }, Qt::QueuedConnection);
}

void InputReceiver::receive(quint64 shareEpoch, const QJsonObject &message, const QString &peerId)
{
    if (message.value("action") == "revoke")
    {
        const auto epoch = message.value("epoch").toString().toULongLong();
        if (!peerId.isEmpty() && !epoch) return;
        revoke(shareEpoch, peerId, epoch);
        return;
    }
    if (message.value("action") != "input")
        return;
    bool epochValid = false, sequenceValid = false;
    const auto epoch = message.value("epoch").toString().toULongLong(&epochValid);
    const auto sequence = message.value("sequence").toString().toULongLong(&sequenceValid);
    quint64 generation;
    {
        QMutexLocker lock(&mutex_);
        if (!epochValid || !sequenceValid || !controlEpoch_ || epoch != controlEpoch_
            || shareEpoch != shareEpoch_ || peerId != peerId_ || !sequence)
            return;
        generation = generation_;
    }
    QMetaObject::invokeMethod(this, [this, shareEpoch, sequence, generation, message, peerId] {
        QMutexLocker lock(&mutex_);
        if (generation != generation_ || sequence <= sequence_)
            return;
        sequence_ = sequence;
        QElapsedTimer clock;
        clock.start();
        const bool accepted = injector_.inject(message);
        const auto injectUs = clock.nsecsElapsed() / 1000;
        const auto error = injector_.lastError();
        lock.unlock();
        if (message.value("probe").toBool() || message.value("kind") == "button")
        {
            auto result = message;
            result.insert("action", "probe_ack");
            result.insert("accepted", accepted);
            result.insert("injectUs", injectUs);
            result.insert("error", error);
            result.insert("sourcePeerId", peerId);
            emit processed(shareEpoch, result);
        }
    });
}
} // namespace zjdesk
