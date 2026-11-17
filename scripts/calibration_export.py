"""Read the latest completed Analysis trace through the guarded J command."""

import hashlib
import math
import json
import os
import struct
import tempfile
import time
import zlib
from pathlib import Path

MODELS = ("Coulomb-Viscous", "Coulomb-Quadratic", "Riccati", "Power-Law Damping")
MAX_POINTS = 3000
HEADER = 608
AUDIO_MAX_BYTES = 32 + 420 * 19 + 4
MAX_BYTES = 96 + 26 + 3 * 32 + 5 * MAX_POINTS + 4 + 4 + 512 + (MAX_POINTS + 7) // 8 + 4 + AUDIO_MAX_BYTES


def check_crc(blob):
    if (
        len(blob) < 4
        or zlib.crc32(blob[:-4]) != struct.unpack_from("<I", blob, len(blob) - 4)[0]
    ):
        raise ValueError("Calibration checksum mismatch; retry the download")


def decode_fit(blob):
    if len(blob) != 512:
        raise ValueError("Incorrect fit record length")
    check_crc(blob)
    magic, version, selected = struct.unpack_from("<III", blob)
    if magic != 0x52444C4C or version != 1 or selected >= 4:
        raise ValueError("Unsupported fit record")
    origin, initial, launcher, launch_s, last, stop = struct.unpack_from(
        "<6f", blob, 12
    )
    flags, source, retained, spikes, best, *ranking = struct.unpack_from(
        "<9I", blob, 36
    )
    if (
        best > 4
        or sorted(ranking) != list(range(4))
        or not retained <= source <= MAX_POINTS
    ):
        raise ValueError("Invalid fit metadata")
    model_names = tuple("Fixed Riccati" if flags & 32 and name == "Riccati" else name for name in MODELS)
    result = {
        "acoustic_start_s": origin,
        "initial_acoustic_rpm": initial,
        "optical_rpm": launcher,
        "optical_time_s": launch_s,
        "last_retained_s": last,
        "manual_stop_s": stop if flags & 1 else None,
        "validation_possible": bool(flags & 4),
        "source_count": source,
        "retained_count": retained,
        "rejected_spikes": spikes,
        "winner": model_names[best - 1] if best else None,
        "ranking": [model_names[i] for i in ranking],
        "models": {},
    }
    offset = 72
    for name in model_names:
        status, *params = struct.unpack_from("<I5f", blob, offset)
        offset += 24
        metrics = {}
        for split in ("training", "validation", "all"):
            valid, rmse, mae, maximum = struct.unpack_from("<I3f", blob, offset)
            offset += 16
            metrics[split] = (
                {"rmse": rmse, "mae": mae, "max_error": maximum} if valid else None
            )
        end, signed, absolute, loss, evaluations, count = struct.unpack_from(
            "<4f2I", blob, offset
        )
        offset += 24
        result["models"][name] = {
            "valid": bool(status & 1),
            "converged": bool(status & 2),
            "validation_converged": bool(status & 4),
            "boundary": bool(status & 8),
            "unstable": bool(status & 16),
            "coefficients": dict(zip(("a", "b", "c", "k", "n"), params)),
            "rpm_error": metrics,
            "finish_s": end if status & 32 else None,
            "stop_error_s": signed if status & 64 else None,
            "absolute_stop_error_s": absolute if status & 64 else None,
            "loss": loss,
            "evaluations": evaluations,
            "count": count,
        }
    result["tail_rpm"], result["tail_slope_rpm_s"] = struct.unpack_from(
        "<2f", blob, offset
    )
    estimate, = struct.unpack_from("<I", blob, offset + 8)
    if estimate > 1:
        raise ValueError("Invalid endpoint estimate marker")
    result["endpoint_estimate"] = bool(estimate)
    return result


def decode_audio(blob, duration):
    if len(blob) < 36 or blob[:4] != b"LAF1":
        raise ValueError("Invalid audio metadata attachment")
    check_crc(blob)
    elapsed, count, width, bands, skipped, drops, window, dense, transition, sparse, flags = struct.unpack_from("<IHBBIIHHIHH", blob, 4)
    expected = (elapsed + 199) // 200 if elapsed <= 30000 else 150 + (elapsed - 30000 + 999) // 1000
    if (elapsed != duration or elapsed > 300000 or count != expected or width != 19 or bands != 8 or window != 400
            or dense != 200 or transition != 30000 or sparse != 1000 or flags > 1
            or len(blob) != 32 + count * width + 4):
        raise ValueError("Invalid audio metadata layout")
    reasons = ("tracking", "quiet", "clipping", "broadband", "weak", "ambiguous", "harmonic", "acquiring", "faster")
    records = []
    for i in range(count):
        r = blob[32 + width*i:32 + width*(i+1)]
        start = i*200 if i < 150 else 30000 + (i-150)*1000
        end = min(elapsed, start + (200 if i < 150 else 1000))
        frames = r[16]
        record = {"start_ms": start, "end_ms": end, "frame_count": frames, "missing": not frames}
        if frames:
            reason, search, cycles = r[6] & 15, (r[6] >> 4) & 3, r[6] >> 6
            if reason >= len(reasons) or (r[17] & 15) + (r[17] >> 4) > frames:
                raise ValueError("Invalid audio metadata frame counts")
            hz = int.from_bytes(r[:2], "little") / 2
            record.update(
                peak_level_db=r[2]-128 if r[2] else None,
                mean_spectral_flatness=r[3]/254 if r[3] != 255 else None,
                candidate_hz=hz or None,
                candidate_score_db=r[4] if r[4] != 255 else None,
                candidate_margin_db=r[5]-128 if r[5] != 255 else None,
                candidate_harmonics=r[7] if hz else None,
                candidate_cycles_per_turn=cycles or None,
                candidate_reason=reasons[reason], search=("none", "prior", "near", "full")[search],
                band_peak_db=[v-128 if v else None for v in r[8:16]],
                broadband_frames=r[17] & 15, clipped_frames=r[17] >> 4,
                max_delivery_lag_ms=r[18]*20 if r[18] != 255 else None,
                delivery_lag_overflow=r[18] == 255,
                available_by_ms=end+r[18]*20 if r[18] != 255 else None,
            )
        records.append(record)
    return {"schema": "launchlab-audio-features-v1", "collector_available": bool(flags & 1),
            "duration_ms": elapsed, "sample_rate_hz": 4000, "analysis_window_ms": window,
            "dense_interval_ms": dense, "dense_until_ms": transition, "sparse_interval_ms": sparse,
            "band_edges_hz": [40,80,160,320,500,800,1200,1600,2000],
            "band_aggregation": "maximum log band energy across processed windows; relative digital units, not calibrated SPL",
            "candidate_aggregation": "highest harmonic support, then highest score; not necessarily accepted RPM",
            "timing": "intervals group audio window end times; available_by_ms is a conservative delivery bound, not an observation timestamp",
            "mic_skipped_ms": skipped, "mic_queue_drops": drops, "records": records}


def decode(blob):
    stamina = blob[:4] == b"LLCA"
    endpoint_reason = stamina or blob[:4] == b"LLC9"
    relative_pitch = endpoint_reason or blob[:4] == b"LLC8"
    history_shadow = blob[:4] == b"LLC7"
    candidate_shadow = history_shadow or blob[:4] == b"LLC6"
    riccati_shadow = (relative_pitch and not stamina) or candidate_shadow or blob[:4] == b"LLC5"
    shadow = stamina or riccati_shadow or blob[:4] == b"LLC4"
    diagnostic = shadow or blob[:4] == b"LLC3"
    immediate = diagnostic or blob[:4] == b"LLC2"
    header = 96 if immediate else HEADER
    if not header + 4 <= len(blob) <= MAX_BYTES or blob[:4] not in (
        b"LLC1",
        b"LLC2",
        b"LLC3",
        b"LLC4",
        b"LLC5",
        b"LLC6",
        b"LLC7",
        b"LLC8",
        b"LLC9",
        b"LLCA",
    ):
        raise ValueError("Unsupported calibration packet")
    if not immediate:
        check_crc(blob)
    launch, rpm, duration, manual, count, at, used, best, kind, lo, hi, *stops = (
        struct.unpack_from("<If7I6f", blob, 4)
    )
    if (
        count > MAX_POINTS
        or duration > 300000
        or manual > (2 if endpoint_reason else 1)
        or not math.isfinite(rpm)
        or rpm <= 0
        or (best not in (0, 8) if stamina else best > (7 if diagnostic else 5))
        or kind > 2
        or used > count
    ):
        raise ValueError("Invalid calibration header")
    fit = None if immediate else decode_fit(blob[96:HEADER])
    samples = []
    ms = 0
    offset = header
    for _ in range(count):
        code = 0
        for shift in range(0, 28 if diagnostic else 21, 7):
            if offset >= len(blob) - 4:
                raise ValueError("Truncated observation")
            byte = blob[offset]
            offset += 1
            code |= (byte & 127) << shift
            if byte < 128:
                break
        else:
            raise ValueError("Invalid sample delta")
        if offset + 2 > len(blob) - 4:
            raise ValueError("Truncated RPM")
        raw = struct.unpack_from("<H", blob, offset)[0]
        offset += 2
        confidence = 0
        if relative_pitch and code & 8:
            if offset >= len(blob) - 4:
                raise ValueError("Truncated pitch confidence")
            confidence = blob[offset]
            offset += 1
            if not confidence or not code & 4:
                raise ValueError("Invalid pitch recovery provenance")
        ms += code >> (4 if relative_pitch else 3 if diagnostic else 2)
        if not raw or ms > 301000 or (samples and ms <= samples[-1]["time_ms"]):
            raise ValueError("Invalid observation")
        samples.append(
            {
                "time_ms": ms,
                "raw_rpm": raw,
                "live_filter_rejected": bool(code & 1),
                "final_fit_excluded": bool(code & 2),
            }
        )
        if diagnostic:
            # Historical name retained as a generic recovery flag: old tools
            # must not count relative-pitch recovery as an original mic lock.
            samples[-1]["recovered_acquisition"] = bool(code & 4)
        if relative_pitch:
            samples[-1].update(
                confirmed_acquisition_recovered=bool(code & 4) and not confidence,
                relative_pitch_recovered=bool(confidence),
                relative_pitch_confidence=confidence,
            )
    identity = {}
    if diagnostic:
        if offset + 30 > len(blob):
            raise ValueError("Truncated mic diagnostics")
        counts = struct.unpack_from("<9H", blob, offset)
        names = (
            "tracking",
            "quiet",
            "clipping",
            "broadband",
            "weak",
            "ambiguous",
            "harmonic",
            "acquiring",
            "faster",
        )
        identity["mic_frame_counts"] = dict(zip(names, counts))
        identity["mic_skipped_ms"], identity["mic_queue_drops"] = struct.unpack_from(
            "<II", blob, offset + 18
        )
        offset += 26
    if shadow:
        sources = {8: "Stamina predictor"} if stamina else {
            5: "Fixed Riccati",
            6: "Early mic estimate",
            7: "Population estimate",
        }
        checkpoints = []
        for target in (10, 15, 30):
            if offset + (12 if stamina else 32 if history_shadow else 28 if candidate_shadow else 24 if riccati_shadow else 16) + 4 > len(blob):
                raise ValueError("Truncated forecast checkpoint")
            observed, source, current = struct.unpack_from("<IIf", blob, offset)
            offset += 12
            if not stamina:
                corrected = struct.unpack_from("<f", blob, offset)[0]
                offset += 4
            if riccati_shadow:
                limited, accepted, original = struct.unpack_from("<fHH", blob, offset)
                offset += 8
                if not accepted <= original <= count or (
                    math.isfinite(limited) and (source != 5 or limited > current)
                ):
                    raise ValueError("Invalid Riccati shadow checkpoint")
            if candidate_shadow:
                candidate = struct.unpack_from("<f", blob, offset)[0]
                offset += 4
                if observed and not (math.isfinite(candidate) and candidate > 0):
                    raise ValueError("Invalid experimental forecast checkpoint")
            if history_shadow:
                history_finish = struct.unpack_from("<f", blob, offset)[0]
                offset += 4
                if observed and not (math.isfinite(history_finish) and history_finish > 0):
                    raise ValueError("Invalid v1.1 forecast checkpoint")
            if not observed and not source:
                continue
            if not (
                target * 1000 <= observed <= duration
                and source in sources
                and math.isfinite(current)
            ):
                raise ValueError("Invalid forecast checkpoint")
            if source == 6 and not math.isfinite(corrected):
                raise ValueError("Missing shadow correction")
            checkpoints.append(
                {
                    "checkpoint_s": target,
                    "observed_at_ms": observed,
                    "source": sources[source],
                    "displayed_finish_s": current,
                    "corrected_finish_s": corrected if source == 6 else None,
                }
            )
            if riccati_shadow:
                checkpoints[-1].update(
                    riccati_limited_finish_s=limited if math.isfinite(limited) else None,
                    riccati_accepted_inliers=accepted if source == 5 else None,
                    riccati_original_inliers=original if source == 5 else None,
                )
            if candidate_shadow:
                checkpoints[-1]["ml_candidate_finish_s"] = candidate
            if history_shadow:
                checkpoints[-1]["ml_v1_1_finish_s"] = history_finish
        identity["forecast_checkpoints"] = checkpoints
    if immediate:
        raw = blob[: offset + 4]
        check_crc(raw)
        identity["trace_sha256"] = hashlib.sha256(raw).hexdigest()
        remaining = blob[offset + 4 :]
        attachment = b""
        if remaining.startswith(b"LLF2"):
            size = 520 + (count + 7) // 8
            attachment, remaining = remaining[:size], remaining[size:]
            if len(attachment) != size:
                raise ValueError("Invalid final-fit attachment")
            check_crc(attachment)
            fit = decode_fit(attachment[4:516])
        for i, sample in enumerate(samples):
            sample["final_fit_excluded"] = (
                bool(attachment[516 + i // 8] & (1 << (i % 8))) if attachment else None
            )
        identity["audio_metadata"] = decode_audio(remaining, duration) if remaining else None
    elif offset != len(blob) - 4:
        raise ValueError("Unexpected trailing calibration data")
    if fit is not None and fit["source_count"] != count:
        raise ValueError("Fit and raw trace counts disagree")
    return {
        **identity,
        "schema": "launchlab-calibration-v1",
        "firmware_elf_sha256": blob[64:96].hex(),
        "launch_id": launch,
        "optical_launch_rpm": rpm,
        "duration_ms": duration,
        "stop_reason": ("timeout", "manual", "interrupted")[manual],
        "observed_stop_ms": duration if manual == 1 else None,
        "latest_preview": {
            "at_ms": at,
            "raw_count": used,
            "winner": "Stamina predictor"
            if best == 8
            else "Population estimate"
            if best == 7
            else "Early mic estimate"
            if best == 6
            else "Fixed Riccati"
            if best == 5
            else MODELS[best - 1]
            if best
            else None,
            "forecast": ("unavailable", "single", "range")[kind],
            "finish_s": [lo, hi] if kind else None,
            "candidate_finish_s": {}
            if best in (6, 7, 8)
            else {"Fixed Riccati": stops[2]}
            if best == 5
            else {name: (value or None) for name, value in zip(MODELS, stops)},
        }
        if at
        else None,
        "final_fit": fit,
        "samples": samples,
    }


def read_record(port, line):
    deadline = time.monotonic() + 12
    tag, size, source = line.split()
    size = int(size)
    if source not in (b"ram", b"flash") or not 0 < size <= MAX_BYTES:
        raise ValueError("Invalid export header")
    data = bytearray()
    while len(data) < size and time.monotonic() < deadline:
        data.extend(port.read(size - len(data)))
    if len(data) != size:
        raise ValueError("Incomplete USB download; retry")
    blob = bytes(data)
    record = (
        decode(blob)
        if tag == b"CAL_DATA"
        else {"schema": "launchlab-fit-only-v1", "final_fit": decode_fit(blob)}
    )
    record["device_storage"] = source.decode()
    record["packet_sha256"] = hashlib.sha256(blob).hexdigest()
    return record


def latest(port):
    time.sleep(0.1)
    port.write(b"J\n")
    deadline = time.monotonic() + 12
    history = False
    while time.monotonic() < deadline:
        line = port.readline().strip()
        if history and line == b"CAL_ERROR no_saved_trace":
            return None, True
        if line.startswith(b"CAL_ERROR "):
            raise RuntimeError(line.decode(errors="replace"))
        if line == b"CAL_HISTORY":
            history = True
        if line.startswith((b"CAL_DATA ", b"CAL_FIT ")):
            return read_record(port, line), history
    raise RuntimeError(
        "No calibration reply. Check the port, leave the wipe screen, and install firmware with the J export command."
    )


def fetch(port):
    return latest(port)[0]


def control_line(port):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        line = port.readline().strip()
        if line.startswith(b"CAL_ERROR "):
            raise RuntimeError(line.decode(errors="replace"))
        if line.startswith(b"CAL_"):
            return line
    raise RuntimeError("Incomplete calibration batch; reconnect and retry")


def download(port):
    newest, history = latest(port)
    if history:
        port.write(b"Z\n")
        header = control_line(port).split()
        if header == [b"CAL_RAM_ONLY"]:
            print("Flash archive unavailable. Downloading the pending RAM traces.")
            header = control_line(port).split()
        if len(header) != 2 or header[0] != b"CAL_COUNT" or int(header[1]) < 0:
            raise ValueError("Invalid calibration batch header")
        last = None
        for _ in range(int(header[1])):
            line = control_line(port)
            if not line.startswith(b"CAL_DATA "):
                raise ValueError("Incomplete calibration batch")
            last = read_record(port, line)
            yield last
        if control_line(port) != b"CAL_END":
            raise ValueError("Missing calibration batch trailer")
        if last is not None:
            return
    if newest is not None:
        yield newest


def write_json(path, record):
    payload = json.dumps(record, indent=2, allow_nan=False) + "\n"
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as out:
        temp = Path(out.name)
        try:
            out.write(payload)
        except BaseException:
            out.close()
            temp.unlink(missing_ok=True)
            raise
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def export(port_name, folder):
    import serial

    with serial.Serial(
        port=None, baudrate=115200, timeout=0.3, write_timeout=3
    ) as port:
        port.dtr = False
        port.rts = False
        port.port = port_name
        port.open()
        save_download(download(port), folder)


def save_download(records, folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    total = added = with_audio = with_windows = 0
    latest_record = None
    for record in records:
        latest_record = record
        if record["schema"] == "launchlab-fit-only-v1":
            print("Legacy fit summary only; no raw pull trace in this packet.")
        else:
            total += 1
            audio = record.get("audio_metadata")
            if audio is not None:
                with_audio += 1
                with_windows += any(row["frame_count"] for row in audio["records"])
        key = record.get("trace_sha256", record["packet_sha256"])
        path = folder / (key + ".json")
        if not path.exists():
            write_json(path, record)
            added += record["schema"] != "launchlab-fit-only-v1"
        elif "trace_sha256" in record:
            # Attachments can finish independently. Keep labels and richer
            # prior downloads when the same raw trace is exported again.
            old = json.loads(path.read_text(encoding="utf-8"))
            merged = dict(old)
            if old.get("final_fit") is None and record.get("final_fit") is not None:
                merged["final_fit"] = record["final_fit"]
                merged["samples"] = record["samples"]
            if old.get("audio_metadata") is None and record.get("audio_metadata") is not None:
                merged["audio_metadata"] = record["audio_metadata"]
            if merged != old:
                # The on-disk JSON may combine attachments from separate USB
                # packets. Preserve hashes of all packets supplying evidence.
                merged["source_packet_sha256"] = list(dict.fromkeys(
                    old.get("source_packet_sha256", [old["packet_sha256"]])
                    + [record["packet_sha256"]]
                ))
                timestamp = path.stat()
                write_json(path, merged)
                os.utime(path, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
            latest_record = merged
    if latest_record is not None:
        write_json(folder / "latest.json", latest_record)
    print(
        f"Pulls returned by Stick: {total} | Newly saved: {added} | Already on PC: {total - added}"
    )
    if total:
        print(f"Audio metadata returned: {with_audio}/{total} pulls | With captured windows: {with_windows}")
