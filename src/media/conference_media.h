#pragma once

#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QThread>

namespace zjdesk
{
struct ConferenceDevices
{
    // Empty selects the first supported device; "test" uses synthetic media / a silent sink.
    QString camera, microphone, speaker;
};

struct ConferenceMailbox
{
    QMutex mutex;
    quint64 epoch = 0;
    QImage local, remote;
    bool framePending = false;
    quint64 localDelivered = 0;
};

class ConferenceWorker;
class ConferenceMedia final : public QObject
{
    Q_OBJECT
public:
    explicit ConferenceMedia(QObject *parent = nullptr);
    ~ConferenceMedia() override;
    void start(bool offerer, const ConferenceDevices &devices, quint64 epoch);
    void stop(quint64 epoch);
    void negotiate(const QString &type, const QJsonObject &payload, quint64 epoch);
    void setMuted(bool microphone, bool camera, bool speaker, quint64 epoch);
signals:
    void negotiationReady(quint64 epoch, const QString &type, const QJsonObject &payload);
    void statusChanged(quint64 epoch, const QString &status, const QString &detail);
    void localFrame(quint64 epoch, const QImage &image);
    void remoteFrame(quint64 epoch, const QImage &image);
    void receivedAudio(quint64 epoch, quint64 buffers, double peak);
private:
    ConferenceMailbox mailbox_;
    QThread thread_;
    ConferenceWorker *worker_;
};
} // namespace zjdesk
