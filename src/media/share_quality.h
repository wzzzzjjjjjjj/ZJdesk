#pragma once

#include <QList>
#include <QSize>

namespace zjdesk
{
struct ShareProfile
{
    QSize size;
    int fps = 60;
    int bitrateLimit = 0;
    int bitrate() const
    {
        const int target = int(qBound(qint64(2000000), qint64(size.width()) * size.height() * qMax(30, fps) / 15,
            qint64(40000000)));
        return bitrateLimit > 0 ? qMin(target, bitrateLimit) : target;
    }
};

// 尺寸优先：原分辨率内先降帧率，但帧率不低于 MinimumFps；只有到下限之后才缩小尺寸。
// 每次只切换一档，并要求连续两次测量确认，避免一次跳到底或来回抖动。
class ShareQuality
{
public:
    // 远控手感下限：达到它之后改为降低尺寸，而不是继续降到个位数帧率。
    static constexpr int MinimumFps = 24;
    static int compatibleMaximumFps(int sourceFps, int receiverFps = 0);
    explicit ShareQuality(const QSize &nativeSize = {}, int maximumFps = 60, int minimumFps = MinimumFps,
        bool lowBandwidth = false);
    ShareProfile profile() const
    {
        auto result = profiles_.at(index_);
        result.bitrateLimit = bitrateLimit_ > 0 ? qMin(bitrateLimit_, maximumBitrate_) : maximumBitrate_;
        return result;
    }
    bool observe(double encodedFps, double receivedFps);
    bool observeNetwork(qint64 receivedPackets, qint64 lostPackets, double excessDelayMs = -1);
private:
    QList<ShareProfile> profiles_;
    int index_ = 0;
    int slowWindows_ = 0;
    int stableWindows_ = 0;
    int bitrateLimit_ = 0;
    int minimumBitrate_ = 2000000;
    int maximumBitrate_ = 40000000;
    int congestedWindows_ = 0;
    int healthyNetworkWindows_ = 0;
    int delayedWindows_ = 0;
    double excessDelayMs_ = -1;
    qint64 previousReceived_ = -1;
    qint64 previousLost_ = -1;
};
} // namespace zjdesk
