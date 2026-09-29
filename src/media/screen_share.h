#pragma once

#include "media_engine.h"
#include <QElapsedTimer>
#include <QJsonObject>

namespace zjdesk
{
class ScreenShareWorker;

struct ShareStats
{
    QSize size;
    int targetFps = 0;
    quint64 processedFrames = 0;
    quint64 deliveredFrames = 0;
    double processedFps = 0;
    double deliveredFps = 0;
    double peerFps = -1;
    int revision = 0;
    double intervalSeconds = 0;
    double presentedFps = -1;
    double networkMbps = -1;
    qint64 packetsReceived = -1;
    qint64 packetsLost = -1;
    double jitterMs = -1;
    double webRtcRttMs = -1;
};

struct ShareMailbox
{
    QMutex mutex;
    QImage image;
    quint64 epoch = 0;
    ShareStats stats;
};

class ScreenShare final : public QObject
{
    Q_OBJECT
public:
    explicit ScreenShare(QObject *parent = nullptr);
    ~ScreenShare() override;
    // Zero selects automatically; explicit 30/60 is used by performance validation.
    void start(bool sender, const QString &source, quint64 epoch, int targetFps = 0);
    void stop(quint64 epoch);
    void negotiate(const QString &type, const QJsonObject &payload, quint64 epoch);
    void reportPresentation(quint64 frames, quint64 epoch, bool active = true);
    void sendControl(const QJsonObject &message, quint64 epoch);

signals:
    void statusChanged(quint64 epoch, const QString &status, const QString &detail);
    void negotiationReady(quint64 epoch, const QString &type, const QJsonObject &payload);
    void frameReady(quint64 epoch, const QImage &image);
    void statisticsReady(quint64 epoch, const zjdesk::ShareStats &stats);
    void controlChannelChanged(quint64 epoch, bool ready);
    void controlReceived(quint64 epoch, const QJsonObject &message);

private:
    ShareMailbox mailbox_;
    QThread thread_;
    ScreenShareWorker *worker_ = nullptr;
    QTimer frames_;
    QElapsedTimer presentationClock_;
    quint64 previousPresented_ = 0;
};
} // namespace zjdesk

Q_DECLARE_METATYPE(zjdesk::ShareStats)
