#pragma once

#include "media_engine.h"
#include "frame_timing.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <memory>

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
    double controlRttMs = -1;
    double transportExcessMs = -1; // DataChannel round trip above the recent minimum; not video age.
    quint64 controlBufferedBytes = 0;
    int congestionBitrate = -1;
    int congestionMinimumBitrate = -1;
    int congestionMaximumBitrate = -1;
    qint64 pacedBytes = -1;
    quint64 droppedBeforeEncode = 0;
    double presentedGapMaxMs = -1;
    int encoderBitrate = 0; // Software bitrate target; hardware CQP uses this as a feedback budget, not a wire cap.
    int encoderQuantizer = -1;
    int sendBufferBytes = -1;
    int receiveBufferBytes = -1;
    int rtcpFeedbackBudget = -1;
    quint64 maxEncodedFrameBytes = 0;
    quint64 maxKeyframeBytes = 0;
    quint64 keyframes = 0;
    double codecMeanMs = -1;
    double codecMaxMs = -1;
    double outputGapMaxMs = -1;
    double copyMaxMs = -1;
    quint64 overwrittenFrames = 0;
    QString codec;
    QString codecFallbackReason;
    QString transport;
    QString localAddress;
    QString remoteAddress;
    bool hardwareEncoder = false;
    bool hardwareDecoder = false;
    QString decoder;
    QString codecProfile;
    QString codecOutputFormat;
    QString codecOutputMemory;
    FrameTimingSummary codecTiming;
    FrameTimingSummary outputGapTiming;
    FrameTimingSummary captureToEncoderTiming;
    FrameTimingSummary decodedToSinkTiming;
    FrameTimingSummary sinkToPullTiming;
    FrameTimingSummary frameMapTiming;
    FrameTimingSummary mailboxTiming;
    FrameTimingSummary workerPollTiming;
    FrameTimingSummary workerPollGapTiming;
    VideoPresentationStats presentation;
    quint64 deliveredFrameId = 0; // Local appsink publication sequence within this stream.
    quint64 unmatchedCodecOutputs = 0;

    QJsonObject timingDiagnostics() const;
};

struct ShareMailbox
{
    QMutex mutex;
    QImage image;
    quint64 epoch = 0;
    ShareStats stats;
    qint64 publishedNs = 0;
    quint64 frameId = 0;
    FrameTiming waitTiming;
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
    void reportPresentation(quint64 frames, quint64 epoch, bool active = true, double gapMaxMs = -1,
        const VideoPresentationStats &presentation = {});
    void sendControl(const QJsonObject &message, quint64 epoch);
    void setCursorEmbedded(bool embedded, quint64 epoch);

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
    std::shared_ptr<QJsonObject> pendingPosition_;
    quint64 pendingPositionEpoch_ = 0;
};
} // namespace zjdesk

Q_DECLARE_METATYPE(zjdesk::ShareStats)
