#pragma once

#include "camera_permission.h"

namespace zjdesk::detail
{
inline CameraPermission desktopCameraPermission(const QString &deviceConsent, const QString &desktopConsent)
{
    if (deviceConsent == "Deny" || desktopConsent == "Deny")
        return CameraPermission::Denied;
    if (deviceConsent == "Allow" && desktopConsent == "Allow")
        return CameraPermission::Allowed;
    return CameraPermission::Unknown;
}
} // namespace zjdesk::detail
