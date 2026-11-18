"""Replay live forecasts or compare historical post-stop fits on calibration exports."""

import argparse
import csv
import hashlib
import itertools
import json
import math
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parents[1]
FEATURE_FLOORS = (300, 200, 25, 5)  # launch RPM, retained RPM, RPM/s, silence seconds


def forecast_audio(data):
    """Reconstruct the dense metadata fields the live model reads."""
    meta = data.get("audio_metadata") or {}
    if not meta.get("collector_available"):
        return []
    packed = []
    for i, r in enumerate(meta.get("records", [])[:150]):
        row = bytearray(19)
        end = 200 * (i + 1)
        at = r.get("available_by_ms")
        if not r["missing"] and at is not None and r["end_ms"] == end:
            lag = at - end
            if lag < 0 or lag % 20 or lag >= 5100:
                raise ValueError("Invalid audio delivery bound")
            row[0] = int(bool(r["candidate_hz"]))
            level = r["peak_level_db"]
            row[2] = 0 if level is None else level + 128
            flat = r["mean_spectral_flatness"]
            row[3] = 255 if flat is None else round(flat * 254)
            margin = r["candidate_margin_db"]
            row[5] = 255 if margin is None else margin + 128
            row[7] = r["candidate_harmonics"] or 0
            row[8:16] = bytes(0 if db is None else db + 128 for db in r["band_peak_db"])
            row[16:19] = bytes((r["frame_count"], r["broadband_frames"], lag // 20))
        packed.append(row.hex())
    return packed if any(bytes.fromhex(row)[16] for row in packed) else []


def load_runs(folder):
    runs = {}
    skipped = {}
    for path in sorted(
        folder.glob("*.json"), key=lambda p: (p.name == "latest.json", p.name)
    ):
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        if (
            data.get("schema") != "launchlab-calibration-v1"
            or data.get("stop_reason") != "manual"
        ):
            reason = data.get("stop_reason") or data.get("schema", "unknown")
            skipped[reason] = skipped.get(reason, 0) + 1
            continue
        samples = [(p["time_ms"], p["raw_rpm"]) for p in data["samples"]]
        rejected = {p["time_ms"] for p in data["samples"] if p.get("live_filter_rejected")}
        recovered = set()
        pitch_confidence = {}
        for point in data["samples"]:
            confidence = point.get("relative_pitch_confidence", 0)
            pitch = point.get("relative_pitch_recovered", False)
            if (
                type(confidence) is not int
                or not 0 <= confidence <= 255
                or bool(confidence) != bool(pitch)
                or (confidence and point.get("confirmed_acquisition_recovered"))
            ):
                raise ValueError(f"Invalid recovery provenance in {path}")
            if confidence:
                pitch_confidence[point["time_ms"]] = confidence
            if pitch or point.get("recovered_acquisition") or point.get("confirmed_acquisition_recovered"):
                recovered.add(point["time_ms"])
        stop = data["observed_stop_ms"]
        launch = data["optical_launch_rpm"]
        if not (
            isinstance(stop, int)
            and 0 < stop <= 301000
            and math.isfinite(launch)
            and launch > 0
            and len(samples) <= 3000
            and all(
                isinstance(t, int) and 0 <= t <= 301000 and 0 < rpm <= 65535
                for t, rpm in samples
            )
            and all(a[0] < b[0] for a, b in itertools.pairwise(samples))
        ):
            raise ValueError(f"Invalid launch data in {path}")
        # Ignore unrelated export metadata; recovery provenance changes the fit.
        provenance = [(t in rejected, t in recovered, pitch_confidence.get(t, 0)) for t, _ in samples]
        key = hashlib.sha256(json.dumps([launch, stop, samples, provenance]).encode()).digest()
        audio = forecast_audio(data)
        if key not in runs:
            runs[key] = {
                "name": path.stem,
                "order": path.stat().st_mtime_ns,
                "launch": launch,
                "stop": stop,
                "samples": samples,
                "recovered": recovered,
                "rejected": rejected,
                "pitch_confidence": pitch_confidence,
                "audio": audio,
            }
        elif audio and not runs[key]["audio"]:
            runs[key]["audio"] = audio
    return sorted(runs.values(), key=lambda r: r["order"]), skipped


def build_helper(folder, historical=False):
    source = Path(__file__).with_name("compare_decay_models.cpp" if historical else "replay_calibration.cpp")
    include = ROOT / "firmware" / "LaunchLabMini"
    exe = folder / ("replay.exe" if os.name == "nt" else "replay")
    if os.name == "nt" and (shutil.which("cl") or not shutil.which("c++")):
        commands = []
        env = dict(os.environ, REPLAY_SRC=str(source), REPLAY_INCLUDE=str(include))
        if not shutil.which("cl"):
            locator = (
                Path(os.environ["ProgramFiles(x86)"])
                / "Microsoft Visual Studio/Installer/vswhere.exe"
            )
            dest = subprocess.check_output(
                [
                    str(locator),
                    "-latest",
                    "-products",
                    "*",
                    "-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property",
                    "installationPath",
                ],
                text=True,
            ).strip()
            if not dest:
                raise ValueError(
                    "Install Visual Studio C++ build tools or put c++ on PATH"
                )
            env["REPLAY_VCVARS"] = str(Path(dest) / "VC/Auxiliary/Build/vcvars64.bat")
            commands.append('call "%REPLAY_VCVARS%" >nul || exit /b 1')
        commands.append(
            'cl /nologo /EHsc /O2 /std:c++17 /I"%REPLAY_INCLUDE%" "%REPLAY_SRC%" /Fe:replay.exe'
        )
        (folder / "compile.cmd").write_text("@echo off\n" + "\n".join(commands))
        subprocess.run(
            ["cmd", "/d", "/c", "compile.cmd"], env=env, cwd=folder, check=True
        )
    else:
        subprocess.run(
            [
                "c++",
                "-O2",
                "-std=c++17",
                "-I",
                str(include),
                str(source),
                "-o",
                str(exe),
            ],
            check=True,
        )
    return exe


def replay(run, step, helper):
    ticks = range(step, run["stop"], step)
    # Replay arrivals, not window centers. Backfilled acquisition windows can
    # arrive after a later original. Keep historical post-stop inputs untouched.
    samples = sorted((t + (600 if t in run["recovered"] and not run.get("pitch_confidence", {}).get(t) else 200),
                      t, rpm) for t, rpm in run["samples"])
    lines = [f"{run['launch']} {len(ticks)}"]
    if run.get("audio"):
        lines.append(f"AUDIO {len(run['audio'])}")
        lines.extend(run["audio"])
    i = 0
    for ms in ticks:
        start = i
        while i < len(samples) and samples[i][0] <= ms:
            i += 1
        lines.append(f"{ms} {i - start}")
        for arrival, t, rpm in samples[start:i]:
            lines.append(f"{arrival} {t} {rpm} {int(t in run['recovered'])} "
                         f"{run.get('pitch_confidence', {}).get(t, 0)} {int(t in run.get('rejected', ()))}")
    output = subprocess.check_output([str(helper)], input="\n".join(lines), text=True)
    states = {}
    for line in output.splitlines():
        ms, count, last, rpm, rate, low, high = map(float, line.split())
        silence = max(0, ms / 1000 - last) if count else math.nan
        states[int(ms)] = (
            (run["launch"], rpm, rate, silence),
            (low, high) if low >= 0 else None,
        )
    return states


def empirical(history, ms, feature):
    # At this tick an ongoing launch has already survived to ms.
    pool = [r for r in history if ms in r["states"]]
    if not pool:
        return None, None
    baseline = median(r["stop"] / 1000 for r in pool)
    pool = [r for r in pool if all(map(math.isfinite, r["states"][ms][0]))]
    if not pool or not all(map(math.isfinite, feature)):
        return baseline, None
    vectors = [r["states"][ms][0] for r in pool]
    scales = []
    for values, floor in zip(zip(*vectors), FEATURE_FLOORS):
        center = median(values)
        scales.append(max(floor, median(abs(x - center) for x in values)))

    def distance(r):
        return sum(
            ((a - b) / s) ** 2 for a, b, s in zip(feature, r["states"][ms][0], scales)
        )

    neighbors = sorted(pool, key=distance)[:5]
    return baseline, ms / 1000 + median((r["stop"] - ms) / 1000 for r in neighbors)


def band(silence):
    if not math.isfinite(silence):
        return "no RPM"
    return "<5s" if silence < 5 else "5-20s" if silence < 20 else "20s+"


def evaluate(runs):
    rows = []
    for run in runs:
        history = [r for r in runs if r["order"] < run["order"]]
        for ms, (feature, firmware) in run["states"].items():
            baseline, similar = empirical(history, ms, feature)
            estimates = [
                ("firmware", firmware),
                ("survivor median", None if baseline is None else (baseline, baseline)),
                ("similar launches", None if similar is None else (similar, similar)),
            ]
            paired = all(value is not None for _, value in estimates)
            for method, estimate in estimates:
                low, high = estimate or (None, None)
                rows.append(
                    {
                        "archive": run["name"],
                        "elapsed_s": ms / 1000,
                        "actual_s": run["stop"] / 1000,
                        "silence": band(feature[-1]),
                        "method": method,
                        "low_s": low,
                        "high_s": high,
                        "paired": paired,
                    }
                )
    return rows


def summarize(rows, label):
    valid = [r for r in rows if r["low_s"] is not None]
    if not valid:
        print(f"{label:29} 0/{len(rows)} forecasts")
        return
    errors = [abs((r["low_s"] + r["high_s"]) / 2 - r["actual_s"]) for r in valid]
    ranges = [r for r in valid if r["low_s"] < r["high_s"]]
    hits = sum(r["low_s"] <= r["actual_s"] <= r["high_s"] for r in ranges)
    print(
        f"{label:29} {len(valid)}/{len(rows)} forecasts | median abs {median(errors):.1f}s | "
        f"worst {max(errors):.1f}s | ranges covering stop {hits}/{len(ranges)}"
    )


def compare_models(runs, helper, csv_path):
    names = ("Coulomb-Viscous", "Coulomb-Quadratic", "Free Riccati", "Power-Law Damping")
    rows = []
    for run in runs:
        samples = run["samples"]
        request = [f"{run['launch']} {run['stop']} {len(samples)}"]
        request.extend(
            f"{at} {rpm} {int(at in run['recovered'])} {run.get('pitch_confidence', {}).get(at, 0)}"
            for at, rpm in samples
        )
        output = subprocess.check_output([str(helper)], input="\n".join(request) + "\n", text=True)
        for line in output.splitlines():
            slot, selected, valid, validation, full, retained = line.split()
            rows.append(dict(archive=run["name"], model=names[int(slot)],
                             selected=bool(int(selected)), valid=bool(int(valid)), retained=int(retained),
                             validation_rmse_rpm=float(validation) if float(validation) >= 0 else None,
                             full_rmse_rpm=float(full) if float(full) >= 0 else None))
    print("Historical post-stop fits at the recorded manual endpoint")
    for name in names:
        models = [r for r in rows if r["model"] == name]
        print(f"{name}: {sum(r['valid'] for r in models)} valid, {sum(r['selected'] for r in models)} selected")
    if csv_path and rows:
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compare-models", action="store_true",
        help="refit historical post-stop models on each complete manual trace",
    )
    parser.add_argument(
        "folder", nargs="?", type=Path, default=ROOT / "firmware/build/calibration"
    )
    parser.add_argument(
        "--step",
        type=int,
        default=10,
        help="checkpoint spacing in seconds (default 10)",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        help="write per-launch, per-checkpoint predictions in seconds",
    )
    args = parser.parse_args()
    if not args.folder.is_dir() or args.step < 1:
        parser.error(
            "provide an existing export directory and a positive checkpoint spacing"
        )
    runs, skipped = load_runs(args.folder)
    print(f"{len(runs)} unique manual launch(es). Skipped files: {skipped or 'none'}")
    if not runs:
        return
    with tempfile.TemporaryDirectory() as temp:
        helper = build_helper(Path(temp), args.compare_models)
        if args.compare_models:
            compare_models(runs, helper, args.csv)
            return
        for run in runs:
            run["states"] = replay(run, args.step * 1000, helper)
    rows = evaluate(runs)
    print(
        "Errors use the midpoint of a range. Counts are checkpoints, not independent launches."
    )
    for method in ("firmware", "survivor median", "similar launches"):
        subset = [r for r in rows if r["method"] == method]
        summarize(subset, method)
        for silence in ("no RPM", "<5s", "5-20s", "20s+"):
            group = [r for r in subset if r["silence"] == silence and r["paired"]]
            if group:
                summarize(group, f"  paired / {silence}")
    if not any(r["paired"] for r in rows):
        print("No shared checkpoints for a three-way accuracy comparison yet.")
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(
                stream,
                [
                    "archive",
                    "elapsed_s",
                    "actual_s",
                    "silence",
                    "method",
                    "low_s",
                    "high_s",
                    "paired",
                ],
            )
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
