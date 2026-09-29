#include "share_quality.h"

namespace zjdesk
{
ShareQuality::ShareQuality(const QSize &nativeSize, int maximumFps)
{
    const auto native = nativeSize.isEmpty() ? QSize(1920, 1080) : nativeSize;
    maximumFps = maximumFps > 0 ? qBound(5, maximumFps, 240) : 60;
    QList<int> frameRates{maximumFps};
    for (int fps : {240, 165, 144, 120, 90, 60, 50, 40, 30, 24, 20, 15, 10, 5})
        if (fps < maximumFps)
            frameRates.append(fps);
    for (int fps : frameRates)
        profiles_.append({native, fps});
    QSize previous = native;
    for (const QSize &limit : {QSize(1920, 1080), QSize(1280, 720), QSize(640, 360)})
    {
        if (native.width() <= limit.width() && native.height() <= limit.height())
            continue;
        auto size = native.scaled(limit, Qt::KeepAspectRatio);
        size = {qMax(2, size.width() / 2 * 2), qMax(2, size.height() / 2 * 2)};
        if (size == previous)
            continue;
        for (int fps : frameRates)
            profiles_.append({size, fps});
        previous = size;
    }
}

bool ShareQuality::observe(double encodedFps, double receivedFps)
{
    const int fps = profile().fps;
    const double actual = receivedFps < 0 ? encodedFps : qMin(encodedFps, receivedFps);
    slowWindows_ = actual < fps * 0.85 ? slowWindows_ + 1 : 0;
    slowFpsTotal_ = slowWindows_ ? slowFpsTotal_ + actual : 0;
    int higherResolution = index_ - 1;
    while (higherResolution >= 0 && profiles_[higherResolution].size == profile().size)
        --higherResolution;
    stableWindows_ = actual >= fps * (higherResolution >= 0 ? 0.85 : 0.95) ? stableWindows_ + 1 : 0;
    if (slowWindows_ >= 2 && index_ + 1 < profiles_.size())
    {
        const auto size = profile().size;
        const double sustainable = slowFpsTotal_ / slowWindows_ / 0.95;
        ++index_;
        // Skip unsustainable frame rates, but exhaust this resolution first.
        while (index_ + 1 < profiles_.size() && profiles_[index_].size == size
            && profiles_[index_].fps > sustainable && profiles_[index_ + 1].size == size)
            ++index_;
    }
    else if (stableWindows_ >= 30 && index_ > 0)
        index_ = higherResolution >= 0 ? higherResolution : index_ - 1;
    else
        return false;
    slowWindows_ = stableWindows_ = 0;
    slowFpsTotal_ = 0;
    return true;
}
} // namespace zjdesk
