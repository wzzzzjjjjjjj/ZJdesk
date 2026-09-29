#pragma once

#include <QList>
#include <QSize>

namespace zjdesk
{
struct ShareProfile
{
    QSize size;
    int fps = 60;
    int bitrate() const
    {
        return int(qBound(qint64(2000000), qint64(size.width()) * size.height() * qMax(30, fps) / 15,
            qint64(40000000)));
    }
};

// Preserve resolution before reducing it; require sustained evidence to switch.
class ShareQuality
{
public:
    explicit ShareQuality(const QSize &nativeSize = {}, int maximumFps = 60);
    ShareProfile profile() const { return profiles_.at(index_); }
    bool observe(double encodedFps, double receivedFps);
private:
    QList<ShareProfile> profiles_;
    int index_ = 0;
    int slowWindows_ = 0;
    double slowFpsTotal_ = 0;
    int stableWindows_ = 0;
};
} // namespace zjdesk
