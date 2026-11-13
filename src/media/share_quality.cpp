#include "share_quality.h"

namespace zjdesk
{
int ShareQuality::compatibleMaximumFps(int sourceFps, int receiverFps)
{
    const int source = sourceFps > 0 ? qBound(5, sourceFps, 240) : 60;
    return receiverFps >= 5 && receiverFps <= 240 ? qMin(source, receiverFps) : source;
}

ShareQuality::ShareQuality(const QSize &nativeSize, int maximumFps, int minimumFps, Mode mode)
{
    auto native = nativeSize.isEmpty() ? QSize(1920, 1080) : nativeSize;
    maximumFps = maximumFps > 0 ? qBound(5, maximumFps, 240) : 60;
    if (mode != Mode::Local)
    {
        const bool conservative = mode == Mode::LowBandwidth;
        native = native.scaled(conservative ? QSize(960, 540) : QSize(1920, 1080), Qt::KeepAspectRatio).boundedTo(native);
        native = {qMax(2, native.width() / 2 * 2), qMax(2, native.height() / 2 * 2)};
        maximumFps = qMin(maximumFps, conservative ? 15 : 30);
        if (conservative) minimumFps = 5;
        minimumBitrate_ = 128000;
        maximumBitrate_ = conservative ? 800000 : 12000000;
    }
    minimumFps = qBound(5, minimumFps, maximumFps);
    QList<int> frameRates;
    frameRates.append(maximumFps);
    // 原分辨率内保留指定最低帧率；更低帧率只留到最后一级尺寸。
    for (int fps : {240, 165, 144, 120, 90, 60, 50, 45, 40, 30, 24, 20, 15, 10, 5})
        if (fps < maximumFps && fps >= minimumFps && !frameRates.contains(fps))
            frameRates.append(fps);
    if (!frameRates.contains(minimumFps))
        frameRates.append(minimumFps);
    QList<QSize> sizes{native};
    QSize previous = native;
    for (const QSize &limit : {QSize(1920, 1080), QSize(1600, 900), QSize(1280, 720),
             QSize(960, 540), QSize(640, 360)})
    {
        if (native.width() <= limit.width() && native.height() <= limit.height())
            continue;
        auto size = native.scaled(limit, Qt::KeepAspectRatio);
        size = {qMax(2, size.width() / 2 * 2), qMax(2, size.height() / 2 * 2)};
        if (size == previous)
            continue;
        sizes.append(size);
        previous = size;
    }
    for (int i = 0; i < sizes.size(); ++i)
    {
        for (int fps : frameRates)
            profiles_.append({sizes.at(i), fps});
        if (i + 1 != sizes.size())
            continue;
        for (int fps : {20, 15, 10, 5})
            if (fps < frameRates.last())
                profiles_.append({sizes.at(i), fps});
    }
}

bool ShareQuality::observe(double encodedFps, double receivedFps)
{
    const int fps = profile().fps;
    const double actual = receivedFps < 0 ? encodedFps : qMin(encodedFps, receivedFps);
    const bool starved = actual < fps * 0.85;
    slowWindows_ = starved ? slowWindows_ + 1 : 0;
    // 编码端跟不上说明当前档位确实过高，两次测量即可确认；编码正常而接收帧率
    // 掉下去多半是链路或接收端的一次抖动（跨网实测出现过 0.4~0.7 秒的停顿，
    // 最多占掉两个统计窗口），缩短尺寸并不能消除它，所以要求连续四个窗口才降档，
    // 避免一次抖动把整场会话永久降级。
    const bool encoderStarved = starved && encodedFps < fps * 0.85;
    int higherResolution = index_ - 1;
    while (higherResolution >= 0 && profiles_.at(higherResolution).size == profile().size)
        --higherResolution;
    stableWindows_ = excessDelayMs_ < 60 && actual >= fps * (higherResolution >= 0 ? 0.85 : 0.95)
        ? stableWindows_ + 1 : 0;
    const int previous = index_;
    if (slowWindows_ >= (encoderStarved ? 2 : 4) && index_ + 1 < profiles_.size())
        ++index_;
    else if (stableWindows_ >= 15 && index_ > 0)
        index_ = higherResolution >= 0 ? higherResolution : index_ - 1;
    else
        return false;
    clampFpsLimit();
    if (index_ == previous)
        return false;
    slowWindows_ = stableWindows_ = 0;
    return true;
}

bool ShareQuality::observeBacklog(double backlogMs, double receiverFps)
{
    if (!(backlogMs >= (draining_ ? ReleaseBacklogMs : DrainBacklogMs)))
    {
        if (!draining_)
            return false;
        // 队列已排空：立刻回到实测能力对应的档位，不必再等稳定窗口——
        // 上限本来就取自实测帧率，爬回去也不会重新灌满队列。
        draining_ = false;
        const int previous = index_;
        index_ = rungForFps(drainCeilingFps_ > 0 ? drainCeilingFps_ : profile().fps);
        clampFpsLimit();
        slowWindows_ = stableWindows_ = 0;
        return index_ != previous;
    }
    // 接收端上报的帧率就是这条链路现在能消费的帧率；按九成钉成持久上限，
    // 否则稳定窗口会爬回高帧率、把队列重新灌满。排水期间的上报值被自己的低帧率
    // 压住，不能当能力用，否则上限会一路阶梯式塌到 MinimumFps。
    if (!draining_ && receiverFps >= MinimumFps)
    {
        drainCeilingFps_ = qMax(MinimumFps, int(receiverFps * 0.9));
        maximumFpsLimit_ = maximumFpsLimit_ > 0 ? qMin(maximumFpsLimit_, drainCeilingFps_) : drainCeilingFps_;
    }
    const int ceiling = drainCeilingFps_ > 0 ? drainCeilingFps_ : profile().fps;
    const int previous = index_;
    draining_ = true;
    clampFpsLimit();
    // 只降帧率不缩尺寸：排空需要的是帧率差，缩尺寸会同时牺牲清晰度。
    index_ = rungForFps(qBound(MinimumFps, int(ceiling * 0.7), ceiling));
    slowWindows_ = stableWindows_ = 0;
    return index_ != previous;
}

int ShareQuality::rungForFps(int fps) const
{
    const QSize size = profiles_.at(index_).size;
    int first = index_;
    while (first > 0 && profiles_.at(first - 1).size == size)
        --first;
    int lowest = first;
    for (int i = first; i < profiles_.size() && profiles_.at(i).size == size; ++i)
    {
        lowest = i;
        if (profiles_.at(i).fps <= fps)
            return i;
    }
    return lowest;
}

bool ShareQuality::limitMaximumFps(int fps)
{
    if (fps < 5 || fps > 240)
        return false;
    maximumFpsLimit_ = maximumFpsLimit_ > 0 ? qMin(maximumFpsLimit_, fps) : fps;
    const int previous = index_;
    clampFpsLimit();
    if (index_ == previous)
        return false;
    slowWindows_ = stableWindows_ = 0;
    return true;
}

void ShareQuality::clampFpsLimit()
{
    if (maximumFpsLimit_ <= 0)
        return;
    while (profiles_.at(index_).fps > maximumFpsLimit_)
    {
        const int next = index_ + 1;
        // 本尺寸没有更低帧率的档位时不再降，留给常规降档去换尺寸。
        if (next >= profiles_.size() || profiles_.at(next).size != profiles_.at(index_).size)
            return;
        index_ = next;
    }
}

bool ShareQuality::observeNetwork(qint64 receivedPackets, qint64 lostPackets, double excessDelayMs)
{
    excessDelayMs_ = excessDelayMs;
    // 数据通道往返时延的基线抖动本身就有几十到一两百毫秒，单窗口 80 毫秒的
    // 尖峰不足以说明链路拥塞；要求 250 毫秒以上且连续三个窗口才降码率。
    delayedWindows_ = excessDelayMs >= 250 ? delayedWindows_ + 1 : 0;
    double loss = -1;
    if (receivedPackets >= 0 && lostPackets >= 0)
    {
        const auto received = receivedPackets - qMax(qint64(0), previousReceived_);
        const auto lost = lostPackets - qMax(qint64(0), previousLost_);
        // Sparse/static desktop traffic is not a bandwidth estimate.
        if (previousReceived_ >= 0 && received >= 0 && lost >= 0 && double(received) + double(lost) >= 100)
            loss = double(lost) / (double(received) + double(lost));
        previousReceived_ = receivedPackets;
        previousLost_ = lostPackets;
    }
    congestedWindows_ = loss >= 0.02 ? congestedWindows_ + 1 : 0;
    const bool healthy = loss >= 0 ? loss < 0.005 : excessDelayMs >= 0;
    healthyNetworkWindows_ = healthy && excessDelayMs <= 60 ? healthyNetworkWindows_ + 1 : 0;
    const int current = profile().bitrate();
    int next = current;
    if (congestedWindows_ >= 2 || delayedWindows_ >= 3)
        next = qMax(minimumBitrate_, current * 85 / 100);
    else if (healthyNetworkWindows_ >= 15 && bitrateLimit_ > 0)
        next = qMin(qMin(profiles_.at(index_).bitrate(), maximumBitrate_), current + qMax(100000, current / 20));
    else
        return false;
    congestedWindows_ = delayedWindows_ = healthyNetworkWindows_ = 0;
    if (next == current)
        return false;
    bitrateLimit_ = next;
    return true;
}
} // namespace zjdesk
