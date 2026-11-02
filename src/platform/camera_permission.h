#pragma once

#include <QString>
#include <functional>

class QObject;

namespace zjdesk
{
enum class CameraPermission { Unknown, Allowed, Denied };
CameraPermission cameraPermission();
using CameraSettingsCompletion = std::function<void(bool, const QString &)>;
void openCameraPrivacySettings(QObject *context, CameraSettingsCompletion completed);
} // namespace zjdesk
