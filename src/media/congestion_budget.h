#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace zjdesk
{
// Bits per second, including room for RTP/repair traffic in the GCC budget.
struct CongestionBudget
{
    int minimum = 2000000;
    int maximum = 40000000;
    int initial = 40000000;

    static std::optional<CongestionBudget> fromMaximum(const QString &text, int profileBitrate)
    {
        CongestionBudget result;
        result.minimum = qMin(8000000, profileBitrate);
        if (text.isEmpty())
            return result;
        bool ok = false;
        const auto maximum = text.toUInt(&ok);
        if (!ok || maximum < 128000 || maximum > 40000000)
            return std::nullopt;
        result.maximum = result.initial = int(maximum);
        result.minimum = qMin(result.maximum, qMax(128000, qMin(result.minimum, result.maximum / 4)));
        return result;
    }
};
} // namespace zjdesk
