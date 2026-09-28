#pragma once

#include <QSize>
#include <QString>

namespace zjdesk
{
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
} // namespace zjdesk
