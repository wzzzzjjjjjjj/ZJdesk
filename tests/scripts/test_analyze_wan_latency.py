"""Deterministic checks for missing frames and invalid latency associations."""

import json
import tempfile
import unittest
from pathlib import Path

from analyze_wan_latency import analyze, percentile


class LatencyAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "events.jsonl"
        self.events = [{"event": "start", "host": False, "seconds": 40,
                        "warmupSeconds": 20, "monotonicNs": "0", "elapsedMs": 0}]
        for second in range(20, 40):
            for frame in range(30):
                now = second + (frame + 0.5) / 30
                self.events.append({"event": "present", "elapsedMs": now * 1000,
                                    "swappedNs": str(round(now * 1e9)), "gapMs": 1000 / 30})
        for second in range(20, 34):
            marker = {"epoch": "123", "sequence": str(second)}
            self.events.append({"event": "marker-sent", **marker, "sentNs": str(second * 10**9)})
            self.events.append({"event": "marker", **marker, "latencyMs": 400})
        self.events.append({"event": "stats", "elapsedMs": 25000, "width": 1920,
                            "height": 1080, "targetFps": 30, "diagnostics": {"playout": {
                                "requestedLatencyMs": 400, "webrtcLatencyReadbackMs": 400,
                                "sinkSync": True, "jitterBufferLatenciesMs": [400]}}})
        self.events.append({"event": "result", "ok": True, "elapsedMs": 40000})

    def summary(self):
        self.path.write_text("\n".join(json.dumps(event) for event in self.events), encoding="utf-8")
        return analyze(self.path, 1)

    def test_raw_nearest_rank_percentiles(self):
        values = [10] * 99 + [900]
        self.assertEqual(percentile(values, 0.99), 10)
        self.assertEqual(percentile(values, 1), 900)
        self.assertIsNone(percentile([], 0.99))
        self.assertTrue(self.summary()["passed"])

    def test_zero_frame_seconds_remain_in_denominator(self):
        self.events = [event for event in self.events if not (event["event"] == "present"
                       and 29000 <= event["elapsedMs"] < 31000)]
        result = self.summary()
        self.assertEqual(result["zeroFpsSeconds"], 2)
        self.assertEqual(result["weightedFps"], 27)
        self.assertEqual(result["medianFps"], 30)
        self.assertFalse(result["passed"])

    def test_missing_response_reduces_coverage(self):
        self.events = [event for event in self.events
                       if not (event["event"] == "marker" and event["sequence"] == "25")]
        result = self.summary()
        self.assertEqual(result["unresolved"], 1)
        self.assertAlmostEqual(result["coverage"], 13 / 14)
        self.assertFalse(result["gates"]["markerTimeoutRate"])

    def test_final_freeze_is_included_without_a_subsequent_swap(self):
        self.events = [event for event in self.events if not (event["event"] == "present"
                       and event["elapsedMs"] >= 39000)]
        result = self.summary()
        self.assertEqual(result["weightedFps"], 28.5)
        self.assertTrue(result["gates"]["secondsAtLeast27Fps"])
        self.assertGreater(result["gapMs"]["max"], 1000)
        self.assertFalse(result["gates"]["gapMaximum"])

    def test_first_gap_excludes_the_warmup_portion(self):
        self.events = [event for event in self.events if not (event["event"] == "present"
                       and event["elapsedMs"] < 20100)]
        first = next(event for event in self.events if event["event"] == "present")
        first["gapMs"] = first["elapsedMs"] - 19000
        result = self.summary()
        self.assertLess(result["gapMs"]["max"], 250)
        self.assertTrue(result["passed"])

    def test_start_record_delay_preserves_measurement_origin(self):
        self.events[0].update(monotonicNs="300000000", elapsedMs=300)
        result = self.summary()
        self.assertEqual(result["weightedFps"], 30)
        self.assertTrue(result["passed"])

    def test_old_epoch_cannot_match_new_request(self):
        for event in self.events:
            if event["event"] == "marker" and event["sequence"] == "25":
                event["epoch"] = "122"
        result = self.summary()
        self.assertEqual(result["unresolved"], 1)
        self.assertEqual(result["unmatchedSamples"], 1)
        self.assertFalse(result["gates"]["validMarkerAssociation"])

    def test_duplicate_response_fails_association(self):
        self.events.append(next(event for event in self.events if event["event"] == "marker").copy())
        self.assertFalse(self.summary()["gates"]["validMarkerAssociation"])

    def test_missing_result_and_insufficient_samples_fail(self):
        self.events.pop()
        result = self.summary()
        self.assertFalse(result["gates"]["completed"])
        self.path.write_text("\n".join(json.dumps(event) for event in self.events), encoding="utf-8")
        self.assertFalse(analyze(self.path, 200)["gates"]["minimumSamples"])


if __name__ == "__main__":
    unittest.main()
