import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from replay_calibration import build_helper, compare_models, empirical, evaluate, forecast_audio, load_runs, replay


class ReplayTests(unittest.TestCase):
    def test_metadata_forecast_uses_delivered_intervals(self):
        records = [dict(missing=True) for _ in range(49)]
        for end in (1000, 2000, 3000, 4000, 9800):
            records[end // 200 - 1] = dict(
                missing=False, end_ms=end, available_by_ms=10020 if end == 9800 else end,
                frame_count=1, peak_level_db=-30, mean_spectral_flatness=.5,
                candidate_hz=90, candidate_margin_db=4, candidate_harmonics=3,
                band_peak_db=[-55, -48, -51, -52, -56, -66, -67, -68], broadband_frames=0,
            )
        audio = forecast_audio({"audio_metadata": {"collector_available": True, "records": records}})
        run = {"launch": 4300, "stop": 31000, "samples": [], "recovered": set(), "audio": audio}
        with tempfile.TemporaryDirectory() as tmp:
            helper = build_helper(Path(tmp))
            dense = replay(run, 1000, helper)
            sparse = replay(run, 10000, helper)
        self.assertAlmostEqual(dense[10000][1][0], 129.80848, places=4)
        self.assertAlmostEqual(dense[11000][1][0], 41.78203, places=4)
        self.assertAlmostEqual(sparse[30000][1][0], 43.61843, places=4)
        self.assertEqual(sparse[30000][1], dense[30000][1])

    def test_compiled_replay_counts_only_original_locks(self):
        run = {
            "launch": 4300, "stop": 11000,
            "samples": [(400, 4000), (600, 3900), (800, 3800)],
            "recovered": {600, 800}, "pitch_confidence": {800: 127},
        }
        with tempfile.TemporaryDirectory() as tmp:
            helper = build_helper(Path(tmp))
            states = replay(run, 10000, helper)
            self.assertAlmostEqual(states[10000][1][0], 136.0522358, places=4)
            for recovered, confidence in ((0, 127), (1, 256)):
                malformed = f"4300 1\n10000 1\n600 400 4000 {recovered} {confidence} 0\n"
                self.assertNotEqual(subprocess.run([str(helper)], input=malformed, text=True,
                                                   capture_output=True).returncode, 0)

    def test_sparse_checkpoints_retain_the_early_reference(self):
        # Double the decay rate after 15 s so the later forecast falls below the opening estimates.
        samples = []
        for ms in range(200, 30001, 200):
            decay_ms = ms + max(0, ms - 15000)
            samples.append((ms, round(6000 / (1 + decay_ms / 30000))))
        run = {"launch": 6500, "stop": 31000, "samples": samples, "recovered": set()}
        with tempfile.TemporaryDirectory() as tmp:
            helper = build_helper(Path(tmp))
            dense = replay(run, 1000, helper)
            sparse = replay(run, 10000, helper)
        for ms in sparse:
            self.assertEqual(sparse[ms][1], dense[ms][1])
        # Reference values from the offline candidate before, at, and after full blend weight.
        self.assertAlmostEqual(dense[10000][1][0], 168.5415, places=3)
        self.assertAlmostEqual(dense[15000][1][0], 170.8457, places=3)
        self.assertAlmostEqual(sparse[30000][1][0], 153.0722, places=3)

    def test_recovery_provenance_reaches_both_helpers(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            record = {
                "schema": "launchlab-calibration-v1", "stop_reason": "manual",
                "observed_stop_ms": 90000, "optical_launch_rpm": 4300,
                "samples": [
                    {"time_ms": 400, "raw_rpm": 4000},
                    {"time_ms": 600, "raw_rpm": 3900, "recovered_acquisition": True},
                    {"time_ms": 800, "raw_rpm": 3800, "relative_pitch_recovered": True,
                     "relative_pitch_confidence": 127},
                ],
            }
            path = folder / "pitch.json"
            path.write_text(json.dumps(record))
            run = load_runs(folder)[0][0]
            self.assertEqual(run["recovered"], {600, 800})
            self.assertEqual(run["pitch_confidence"], {800: 127})
            with patch("replay_calibration.subprocess.check_output", return_value="10000 3 .8 3800 -500 -1 -1") as call:
                replay(run, 10000, Path("helper"))
                self.assertIn("600 400 4000 0 0 0\n1000 800 3800 1 127 0\n1200 600 3900 1 0 0", call.call_args.kwargs["input"])
            with patch("replay_calibration.subprocess.check_output", return_value="0 1 1 1 1 3") as call:
                compare_models([run], Path("helper"), None)
                self.assertIn("400 4000 0 0\n600 3900 1 0\n800 3800 1 127", call.call_args.kwargs["input"])
            # Confidence affects weighting, so differing provenance is not
            # silently deduplicated against a second export of the same RPMs.
            record["samples"][2]["relative_pitch_confidence"] = 255
            (folder / "different.json").write_text(json.dumps(record))
            self.assertEqual(len(load_runs(folder)[0]), 2)
            for invalid in (0, -1, 256, 1.5, True):
                record["samples"][2]["relative_pitch_confidence"] = invalid
                path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):
                    load_runs(folder)

    def test_duplicate_files_and_manual_labels(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            record = {
                "schema": "launchlab-calibration-v1",
                "stop_reason": "manual",
                "samples": [],
                "observed_stop_ms": 90000,
                "optical_launch_rpm": 4300,
            }
            for name in ("archive", "latest"):
                (folder / f"{name}.json").write_text(json.dumps(record))
            record["stop_reason"] = "timeout"
            (folder / "timeout.json").write_text(json.dumps(record))
            record["stop_reason"] = "interrupted"
            record["observed_stop_ms"] = None
            (folder / "interrupted.json").write_text(json.dumps(record))
            runs, skipped = load_runs(folder)
            self.assertEqual([r["name"] for r in runs], ["archive"])
            self.assertEqual(skipped["timeout"], 1)
            self.assertEqual(skipped["interrupted"], 1)

    def test_only_earlier_survivors_supply_labels(self):
        feature = (4000, 2000, -50, 8)

        def run(name, order, stop, ticks):
            return {
                "name": name,
                "order": order,
                "stop": stop,
                "states": {ms: (feature, None) for ms in ticks},
            }

        runs = [
            run("short", 1, 15000, [10000]),
            run("early", 2, 80000, [10000, 20000]),
            run("test", 3, 99000, [10000, 20000]),
            run("tie", 3, 90000, [10000, 20000]),
            run("future", 4, 180000, [10000, 20000]),
        ]
        rows = evaluate(runs)
        for r in rows:
            if r["archive"] == "test" and r["method"] != "firmware":
                self.assertEqual(r["low_s"], 47.5 if r["elapsed_s"] == 10 else 80)
        self.assertEqual(empirical([], 10000, feature), (None, None))


if __name__ == "__main__":
    unittest.main()
