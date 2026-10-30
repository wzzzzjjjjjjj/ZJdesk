#pragma once

#include <QJsonObject>
#include <QtGlobal>
#include <array>
#include <chrono>
#include <cmath>

namespace zjdesk
{
struct FrameTimingSummary
{
    quint64 samples = 0;
    double meanMs = -1;
    double p50Ms = -1;
    double p95Ms = -1;
    double p99Ms = -1;
    double maxMs = -1;
    quint64 over50Ms = 0;
    quint64 over100Ms = 0;
    quint64 over200Ms = 0;

    QJsonObject toJson() const
    {
        return {{"samples", qint64(samples)}, {"meanMs", meanMs}, {"p50Ms", p50Ms}, {"p95Ms", p95Ms},
            {"p99Ms", p99Ms}, {"maxMs", maxMs}, {"over50Ms", qint64(over50Ms)},
            {"over100Ms", qint64(over100Ms)}, {"over200Ms", qint64(over200Ms)}};
    }
};

// Stream-lifetime histogram: 0.1 ms upper bounds through 100 ms; overflow uses the
// observed maximum as a conservative percentile bound. Memory never grows with duration.
// Callers provide synchronization when recording from more than one thread.
class FrameTiming
{
public:
    static qint64 nowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void record(double ms)
    {
        if (!std::isfinite(ms) || ms < 0)
            return;
        const auto bucket = ms > 100 ? size_t(1001) : size_t(std::ceil(ms * 10));
        ++buckets_[bucket];
        ++summary_.samples;
        totalMs_ += ms;
        summary_.maxMs = qMax(summary_.maxMs, ms);
        summary_.over50Ms += ms > 50;
        summary_.over100Ms += ms > 100;
        summary_.over200Ms += ms > 200;
    }

    FrameTimingSummary summary() const
    {
        auto result = summary_;
        if (result.samples)
        {
            result.meanMs = totalMs_ / result.samples;
            result.p50Ms = percentile(0.50);
            result.p95Ms = percentile(0.95);
            result.p99Ms = percentile(0.99);
        }
        return result;
    }

private:
    double percentile(double fraction) const
    {
        const auto rank = quint64(std::ceil(summary_.samples * fraction));
        quint64 count = 0;
        for (size_t i = 0; i < buckets_.size(); ++i)
        {
            count += buckets_[i];
            if (count >= rank)
                return i == 1001 ? summary_.maxMs : double(i) / 10;
        }
        return summary_.maxMs;
    }

    std::array<quint64, 1002> buckets_{};
    FrameTimingSummary summary_;
    double totalMs_ = 0;
};

struct VideoPresentationStats
{
    FrameTimingSummary gaps;
    FrameTimingSummary receiveToPaint;
    FrameTimingSummary paint;
    FrameTimingSummary receiveToSwap;
    quint64 supersededFrames = 0;
    quint64 lastFrameId = 0; // Local VideoSurface sequence, not a cross-machine frame ID.

    QJsonObject toJson() const
    {
        return {{"gaps", gaps.toJson()}, {"receiveToPaint", receiveToPaint.toJson()},
            {"paint", paint.toJson()}, {"receiveToSwap", receiveToSwap.toJson()},
            {"supersededFrames", qint64(supersededFrames)}, {"lastFrameId", qint64(lastFrameId)}};
    }
};
} // namespace zjdesk
