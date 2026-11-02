#pragma once

#include <QSize>
#include <QString>

namespace zjdesk
{
struct ShareProfile;
struct CameraMode
{
    QSize size;
    int fpsNumerator = 0;
    int fpsDenominator = 1;
    QString caps;
    bool jpeg = false;
    QString description() const;
};

// Select one advertised mode: pixel count first, then frame rate.
CameraMode maximumCameraMode(const QString &advertisedCaps);
// Keep the native fractional rate when the selected profile reaches the device limit.
QString cameraOutputCaps(const CameraMode &native, const ShareProfile &profile, const char *format);
} // namespace zjdesk
