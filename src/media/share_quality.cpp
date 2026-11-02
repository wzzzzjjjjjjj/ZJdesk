#include "share_quality.h"

namespace zjdesk
{
ShareQuality::ShareQuality(const QSize &nativeSize, int maximumFps, int minimumFps)
{
    const auto native = nativeSize.isEmpty() ? QSize(1920, 1080) : nativeSize;
    maximumFps = maximumFps > 0 ? qBound(5, maximumFps, 240) : 60;
    minimumFps = qBound(5, minimumFps, maximumFps);
    QList<int> frameRates;
    frameRates.append(maximumFps);
    // 原分辨率内只保留手感可用的帧率；低于 MinimumFps 的档位只留到最后一级尺寸。
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
    slowWindows_ = actual < fps * 0.85 ? slowWindows_ + 1 : 0;
    int higherResolution = index_ - 1;
    while (higherResolution >= 0 && profiles_.at(higherResolution).size == profile().size)
        --higherResolution;
    stableWindows_ = excessDelayMs_ < 30 && actual >= fps * (higherResolution >= 0 ? 0.85 : 0.95)
        ? stableWindows_ + 1 : 0;
    if (slowWindows_ >= 2 && index_ + 1 < profiles_.size())
        ++index_;
    else if (stableWindows_ >= 30 && index_ > 0)
        index_ = higherResolution >= 0 ? higherResolution : index_ - 1;
    else
        return false;
    slowWindows_ = stableWindows_ = 0;
    return true;
}

bool ShareQuality::observeNetwork(qint64 receivedPackets, qint64 lostPackets, double excessDelayMs)
{
    excessDelayMs_ = excessDelayMs;
    delayedWindows_ = excessDelayMs >= 80 ? delayedWindows_ + 1 : 0;
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
    healthyNetworkWindows_ = healthy && excessDelayMs <= 20 ? healthyNetworkWindows_ + 1 : 0;
    const int current = profile().bitrate();
    int next = current;
    if (congestedWindows_ >= 2 || delayedWindows_ >= 2)
        next = qMax(2000000, current * 85 / 100);
    else if (healthyNetworkWindows_ >= 15 && bitrateLimit_ > 0)
        next = qMin(profiles_.at(index_).bitrate(), current + qMax(100000, current / 20));
    else
        return false;
    congestedWindows_ = delayedWindows_ = healthyNetworkWindows_ = 0;
    if (next == current)
        return false;
    bitrateLimit_ = next;
    return true;
}
} // namespace zjdesk
