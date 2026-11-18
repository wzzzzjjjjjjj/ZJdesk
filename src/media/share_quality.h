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
// 每次只切换一档；编码端跟不上时连续两次测量即确认，接收端掉帧（链路抖动）则要求
// 连续四个统计窗口，避免一次传输停顿被当成长期能力不足而永久降档。
class ShareQuality
{
public:
    enum class Mode { Local, WideArea, LowBandwidth };
    // 远控手感下限：达到它之后改为降低尺寸，而不是继续降到个位数帧率。
    static constexpr int MinimumFps = 24;
    static int compatibleMaximumFps(int sourceFps, int receiverFps = 0);
    explicit ShareQuality(const QSize &nativeSize = {}, int maximumFps = 60, int minimumFps = MinimumFps,
        Mode mode = Mode::Local);
    ShareProfile profile() const
    {
        auto result = profiles_.at(index_);
        result.bitrateLimit = bitrateLimit_ > 0 ? qMin(bitrateLimit_, maximumBitrate_) : maximumBitrate_;
        return result;
    }
    bool observe(double encodedFps, double receivedFps);
    bool observeNetwork(qint64 receivedPackets, qint64 lostPackets, double excessDelayMs = -1);
    // 接收端排队中的陈旧画面（相对基线的取帧偏移）超过阈值即进入排水：队列只有靠
    // "发送帧率低于接收端消费帧率"才会减少，接收侧丢弃 VP9 参考帧会破坏解码链。
    static constexpr double DrainBacklogMs = 250;
    // 排空判定留迟滞：队列贴着阈值时不要每个窗口来回切档。
    static constexpr double ReleaseBacklogMs = 120;
    bool observeBacklog(double backlogMs, double receiverFps = -1);
    // 排水期间常规阶梯必须冻结：低帧率是主动选择，不是能力不足。
    bool draining() const
    {
        return draining_;
    }
    // 共享开始之后才到达的对端显示上限：只收紧帧率，尺寸档不变。
    bool limitMaximumFps(int fps);
private:
    void clampFpsLimit();
    // 当前尺寸内不大于 fps 的最高档；没有更低档时取本尺寸最小帧率那一档。
    int rungForFps(int fps) const;
    QList<ShareProfile> profiles_;
    int index_ = 0;
    int slowWindows_ = 0;
    int stableWindows_ = 0;
    bool draining_ = false;
    int drainCeilingFps_ = 0;
    bool temporaryDrainCeiling_ = false;
    int maximumFpsLimit_ = 0;
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
