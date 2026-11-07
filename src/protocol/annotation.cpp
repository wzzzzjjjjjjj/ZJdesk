#include "annotation.h"
#include "message.h"
#include <QJsonArray>
#include <cmath>

namespace zjdesk::protocol
{
bool isAnnotationStrokeValid(const QJsonObject &stroke)
{
    if (!isRoomIdValid(stroke.value("strokeId").toString())) return false;
    const auto tool = stroke.value("tool").toString();
    if (tool != "pen" && tool != "circle" && tool != "arrow") return false;
    const auto points = stroke.value("points").toArray();
    if (points.isEmpty() || points.size() > MaximumAnnotationPoints
        || (tool != "pen" && points.size() != 2)) return false;
    for (const auto &value : points)
    {
        if (!value.isArray() || value.toArray().size() != 2) return false;
        for (const auto &coordinate : value.toArray())
            if (!coordinate.isDouble() || !std::isfinite(coordinate.toDouble())
                || coordinate.toDouble() < 0 || coordinate.toDouble() > 1) return false;
    }
    return true;
}
} // namespace zjdesk::protocol
