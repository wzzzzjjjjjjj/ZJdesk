"""Summarize raw latency-probe JSONL without averaging cumulative percentiles."""

import argparse
import json
import math
import statistics
from pathlib import Path


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)] if ordered else None


def analyze(path, minimum_samples):
    events = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    start = next(event for event in events if event["event"] == "start")
    if start["host"]:
        raise ValueError(f"{path} is a sender report, a receiver report is required")
    result = next((event for event in reversed(events) if event["event"] == "result"), None)
    warmup = start["warmupSeconds"]
    end = result["elapsedMs"] / 1000 if result else events[-1]["elapsedMs"] / 1000
    begin_ns = int(start["monotonicNs"]) - round(start["elapsedMs"] * 1e6)
    key = lambda event: (event["epoch"], event["sequence"])
    all_sent = {key(event): event for event in events if event["event"] == "marker-sent"}
    sent = {key(event): event for event in events if event["event"] == "marker-sent"
            and (int(event["sentNs"]) - begin_ns) / 1e9 >= warmup}
    markers = [event for event in events if event["event"] == "marker" and key(event) in sent]
    latencies = [event["latencyMs"] for event in markers]
    presents = [event for event in events if event["event"] == "present"
                and warmup * 1e9 <= int(event["swappedNs"]) - begin_ns <= end * 1e9]
    # Every complete wall-clock second is retained, including seconds with no swap.
    bins = [0] * max(0, math.floor(end) - warmup)
    for event in presents:
        second = math.floor((int(event["swappedNs"]) - begin_ns) / 1e9) - warmup
        if 0 <= second < len(bins):
            bins[second] += 1
    # Clip the first gap to warmup and include a freeze without a final swap.
    boundaries_ms = [warmup * 1000] + [(int(event["swappedNs"]) - begin_ns) / 1e6
                                     for event in presents] + [end * 1000]
    gaps = [max(0, right - left) for left, right in zip(boundaries_ms, boundaries_ms[1:])]
    stats = [event for event in events if event["event"] == "stats" and event["elapsedMs"] >= warmup * 1000]
    unresolved = set(sent) - {key(event) for event in markers}
    duplicates = len(markers) - len({key(event) for event in markers})
    unmatched = sum(event["event"] == "marker" and key(event) not in all_sent for event in events)
    late_or_duplicate = sum(event["event"] == "marker-late-or-duplicate" for event in events)
    required = max(minimum_samples, 1000 if start["seconds"] >= 1800 else 0)
    weighted_fps = sum(bins) / len(bins) if bins else 0
    median_fps = statistics.median(bins) if bins else 0
    good_seconds = sum(fps >= 27 for fps in bins) / len(bins) if bins else 0
    playout = [event["diagnostics"]["playout"] for event in stats]
    latencies_ms = sorted({item["requestedLatencyMs"] for item in playout})
    readback_ok = bool(playout) and all(item["requestedLatencyMs"] == item["webrtcLatencyReadbackMs"]
        and item["sinkSync"] and item["jitterBufferLatenciesMs"]
        and all(value == item["requestedLatencyMs"] for value in item["jitterBufferLatenciesMs"]) for item in playout)
    resolution_ok = bool(stats) and all((event["width"], event["height"], event["targetFps"]) == (1920, 1080, 30) for event in stats)
    coverage = len(markers) / len(sent) if sent else 0
    gates = {
        "completed": bool(result and result["ok"]),
        "noErrors": not any(event["event"] == "error" or event.get("status") == "failed" for event in events),
        "minimumSamples": len(latencies) >= required,
        "validMarkerAssociation": duplicates == 0 and unmatched == 0 and late_or_duplicate == 0,
        "1080p30": resolution_ok,
        "latencyReadback": readback_ok,
        "weightedFps": weighted_fps >= 28.5,
        "medianFps": median_fps >= 29,
        "secondsAtLeast27Fps": good_seconds >= 0.95,
        "markerTimeoutRate": coverage >= 0.995,
        "gapP99": bool(gaps) and percentile(gaps, 0.99) <= 100,
        "gapMaximum": bool(gaps) and max(gaps) <= 250,
    }
    return {
        "file": str(path), "durationSeconds": end, "warmupSeconds": warmup,
        "latencyMs": {"p50": percentile(latencies, 0.50), "p95": percentile(latencies, 0.95),
                      "p99": percentile(latencies, 0.99), "max": max(latencies) if latencies else None},
        "samples": len(latencies), "sent": len(sent), "unresolved": len(unresolved), "coverage": coverage,
        "duplicateSamples": duplicates, "unmatchedSamples": unmatched, "lateOrDuplicateMarkers": late_or_duplicate,
        "completeSeconds": len(bins), "zeroFpsSeconds": bins.count(0), "weightedFps": weighted_fps,
        "medianFps": median_fps, "secondsAtLeast27Fps": good_seconds,
        "gapMs": {"p99": percentile(gaps, 0.99), "max": max(gaps) if gaps else None},
        "requestedLatencyMs": latencies_ms, "gates": gates, "passed": all(gates.values()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--min-samples", type=int, default=200)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reports = [analyze(path, args.min_samples) for path in args.reports]
    summary = {"reports": reports}
    if len(reports) == 2:
        baseline, candidate = reports
        before, after = baseline["latencyMs"], candidate["latencyMs"]
        enough = all(value is not None for value in (*before.values(), *after.values()))
        comparison = {
            "p50ImprovementMs": before["p50"] - after["p50"] if enough else None,
            "p95ImprovementMs": before["p95"] - after["p95"] if enough else None,
            "p99IncreaseMs": after["p99"] - before["p99"] if enough else None,
        }
        comparison["passed"] = bool(enough and baseline["passed"] and candidate["passed"]
            and comparison["p50ImprovementMs"] >= 100 and comparison["p95ImprovementMs"] >= 80
            and comparison["p99IncreaseMs"] <= 50)
        summary["comparison"] = comparison
    text = json.dumps(summary, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if all(report["passed"] for report in reports) and summary.get("comparison", {"passed": True})["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
