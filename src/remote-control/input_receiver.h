#pragma once

#include "input_injector.h"
#include <QMutex>
#include <QObject>
#include <QThread>

namespace zjdesk
{
// Authorization is set by the coordinator; native input never waits for its UI loop.
class InputReceiver final : public QObject
{
    Q_OBJECT
public:
    InputReceiver();
    ~InputReceiver() override;
    void authorize(quint64 shareEpoch, quint64 controlEpoch, const QString &peerId = {});
    void revoke(quint64 shareEpoch = 0, const QString &peerId = {}, quint64 controlEpoch = 0);
    void receive(quint64 shareEpoch, const QJsonObject &message, const QString &peerId = {});
signals:
    void processed(quint64 shareEpoch, const QJsonObject &result);
private:
    QThread thread_;
    InputInjector injector_;
    QMutex mutex_;
    QString peerId_;
    quint64 shareEpoch_ = 0, controlEpoch_ = 0, sequence_ = 0, generation_ = 0;
};
} // namespace zjdesk
