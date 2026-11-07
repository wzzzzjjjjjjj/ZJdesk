#pragma once

#include <QJsonObject>

namespace zjdesk::protocol
{
inline constexpr int MaximumAnnotationPoints = 256;
inline constexpr int MaximumAnnotationStrokes = 128;
bool isAnnotationStrokeValid(const QJsonObject &stroke);
} // namespace zjdesk::protocol
