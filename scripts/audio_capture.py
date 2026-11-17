"""Capture one Analysis launch with the Stick's 4 kHz diagnostic mic tape."""

import hashlib
import io
import zlib
import json
import tempfile
import time
import wave
from pathlib import Path

from calibration_export import latest, read_record, write_json

RATE = 4000
TAPE_SAMPLES = 120 * RATE


def command(port, letter):
    time.sleep(0.1)  # Standalone letter after the USB guard's quiet interval.
    port.write(letter + b"\n")


def line_until(port, deadline):
    buf = bytearray()
    while time.monotonic() < deadline:
        buf.extend(port.read_until(b"\n", 4096 - len(buf)))
        if buf.endswith(b"\n"):
            return bytes(buf).strip()
        if len(buf) >= 4096:
            raise ValueError("Oversized USB response")
    raise RuntimeError("Timed out waiting for the Stick")


def fields(line):
    return dict(word.split(b"=", 1) for word in line.split()[1:] if b"=" in word)


def wait_for_spin(port):
    command(port, b"W")
    deadline = time.monotonic() + 8
    while True:
        line = line_until(port, deadline)
        if line.startswith(b"MIC_TAPE armed="):
            if fields(line).get(b"armed") != b"1":
                raise RuntimeError("The Stick could not allocate its mic tape")
            break
    print(
        "Tape armed. Start your video, enter Analysis Mode, wait for the microphone, then launch.",
        flush=True,
    )
    deadline = time.monotonic() + 600
    target = None
    while True:
        line = line_until(port, deadline)
        if line.startswith(b"LAUNCH_RESULT "):
            event = fields(line)
            if event.get(b"valid") == b"1":
                launch = int(event[b"launches"]), float(event[b"peak_rpm"])
                if target is None:
                    target = launch
                    deadline = time.monotonic() + 320
                    print(
                        f"Recording launch {launch[0]}. Press B when it stops and keep USB connected.",
                        flush=True,
                    )
                elif launch != target:
                    raise RuntimeError(
                        "Another launch started before the target recording completed"
                    )
        elif target and line.startswith(b"MIC_SPIN "):
            spin = fields(line)
            if (int(spin[b"launch"]), float(spin[b"optical_rpm"])) == target:
                return target


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def read_pcm(port, meta):
    samples = meta["samples"]
    if (
        meta["rate"] != RATE
        or not isinstance(samples, int)
        or not 0 < samples <= TAPE_SAMPLES
    ):
        raise ValueError("Invalid mic tape sample rate or length")
    line = line_until(port, time.monotonic() + 8)
    if not line.startswith(b"RAW_BEGIN "):
        raise ValueError("Missing audio transfer header")
    header = json.loads(line.split(b" ", 1)[1])
    if (
        header["samples"] != samples
        or header["bytes"] != samples * 2
        or header["complete"] is not True
        or header["adc_error"] != 0
        or header["read_errors"] != 0
    ):
        raise ValueError("Incomplete or inconsistent audio transfer header")

    pcm = bytearray()
    deadline = time.monotonic() + 15
    while True:
        line = line_until(port, deadline)
        if line == b"RAW_END":
            if len(pcm) != samples * 2 or fnv1a(pcm) != header["fnv1a32"]:
                raise ValueError("Whole-recording length or checksum mismatch")
            return pcm
        if line.startswith(b"RAW_ERROR"):
            raise RuntimeError(line.decode(errors="replace"))
        if not line.startswith(b"RAW_CHUNK "):
            continue
        # Firmware retries the same offset if a block or its ACK was lost.
        try:
            _, at, size, checksum, payload = line.split()
            at, size = int(at), int(size)
            data = bytes.fromhex(payload.decode("ascii"))
            valid = (
                0 < size <= 512
                and size == len(data)
                and 0 <= at <= len(pcm)
                and at + size <= samples * 2
                and fnv1a(data) == int(checksum)
            )
        except ValueError:
            continue
        if not valid:
            continue
        if at == len(pcm):
            pcm.extend(data)
        elif pcm[at : at + size] != data:
            raise ValueError("Conflicting retransmitted mic block")
        port.write(f"ACK {at}\n".encode())
        deadline = time.monotonic() + 15


def read_tape(port, launch, rpm):
    command(port, b"V")
    matched = None
    received = 0
    total = 0
    while True:
        line = line_until(port, time.monotonic() + 10)
        if line in (b"MIC_TAPE none", b"MIC_TAPE busy"):
            raise RuntimeError(
                line.decode()
                + "; keep the Stick powered and retry after the spin finishes"
            )
        if line.startswith(b"MIC_TAPE end "):
            if int(fields(line)[b"spins"]) != received or matched is None:
                raise RuntimeError(
                    "No verified matching recording in the Stick's retained tape"
                )
            return matched
        if not line.startswith(b"MIC_TAPE {"):
            continue
        meta = json.loads(line.split(b" ", 1)[1])
        received += 1
        total += meta["samples"]
        if received > 8 or total > TAPE_SAMPLES:
            raise ValueError("Invalid tape directory")
        pcm = read_pcm(port, meta)
        if meta["launch"] == launch and abs(meta["optical_rpm"] - rpm) <= 0.051:
            if matched is not None:
                raise ValueError("Multiple recordings match this launch")
            matched = meta, pcm


def capture(port, folder, recover=False):
    previous, _ = latest(port)
    if recover:
        target = None
        record = previous
    else:
        target = wait_for_spin(port)
        record, _ = latest(port)
    if not record or record.get("schema") != "launchlab-calibration-v1":
        raise RuntimeError(
            "No full Analysis calibration packet. Record in Analysis Mode and stop with B."
        )
    identity = record.get("trace_sha256") or record["packet_sha256"]
    if target and (
        record["launch_id"] != target[0]
        or abs(record["optical_launch_rpm"] - target[1]) > 0.051
        or (
            previous
            and identity == (previous.get("trace_sha256") or previous["packet_sha256"])
        )
    ):
        raise RuntimeError(
            "Latest Analysis JSON does not match the new launch; no pair saved"
        )

    print("Downloading and verifying mic tape…", flush=True)
    meta, pcm = read_tape(port, record["launch_id"], record["optical_launch_rpm"])
    return save_pair(folder, record, meta, pcm)


def save_pair(folder, record, meta, pcm):
    identity = record.get("trace_sha256") or record["packet_sha256"]
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    dest = folder / (
        identity + "-opening" if meta.get("storage") == "flash" else identity
    )
    wav_name = "launch.wav"
    record["audio"] = {
        **meta,
        "file": wav_name,
        "pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "format": "mono signed 16-bit little-endian PCM",
        "time_origin": "optical release",
    }
    # Publish the two files together. Temporary files stay on the same volume.
    with tempfile.TemporaryDirectory(dir=folder) as tmp:
        pair = Path(tmp) / identity
        pair.mkdir()
        with wave.open(str(pair / wav_name), "wb") as wav:
            wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            wav.writeframes(pcm)
        write_json(pair / "launch.json", record)
        if dest.exists():
            old = json.loads((dest / "launch.json").read_text(encoding="utf-8"))
            if (old.get("trace_sha256") or old.get("packet_sha256")) != identity or (
                dest / wav_name
            ).read_bytes() != (pair / wav_name).read_bytes():
                raise ValueError(
                    f"Existing capture conflicts with the download: {dest}"
                )
            if old.get("final_fit") is None and record.get("final_fit") is not None:
                write_json(dest / "launch.json", record)
        else:
            pair.rename(dest)
    print(
        f"Saved {meta['samples'] / RATE:.2f} s of tracker audio and launch JSON in {dest}"
    )
    return dest


def download_saved(port, folder):
    command(port, b"G")
    received = 0
    deadline = time.monotonic() + 10
    while True:
        line = line_until(port, deadline)
        if line.startswith(b"WAV_END "):
            if int(line.split()[1]) != received:
                raise ValueError("Incomplete saved audio batch")
            print(f"Verified {received} saved WAV/JSON pair(s).")
            return received
        if line.startswith((b"CAL_ERROR", b"WAV_ERROR")):
            raise RuntimeError(line.decode(errors="replace"))
        if not line.startswith(b"CAL_DATA "):
            continue
        if received >= 2:
            raise ValueError("Unexpected number of saved recordings")
        record = read_record(port, line)
        header = line_until(port, time.monotonic() + 10).split()
        if len(header) != 3 or header[0] != b"WAV_DATA":
            raise ValueError("Missing saved WAV header")
        size, checksum = map(int, header[1:])
        if not 44 < size <= 44 + 12 * RATE * 2:
            raise ValueError("Invalid saved WAV length")
        data = bytearray()
        deadline = time.monotonic() + 20
        while len(data) < size and time.monotonic() < deadline:
            data.extend(port.read(size - len(data)))
        if len(data) != size or zlib.crc32(data) != checksum:
            raise ValueError(
                "Saved WAV checksum or length mismatch; retry the download"
            )
        with wave.open(io.BytesIO(data), "rb") as wav:
            if (
                wav.getnchannels(),
                wav.getsampwidth(),
                wav.getframerate(),
                wav.getcomptype(),
            ) != (1, 2, RATE, "NONE"):
                raise ValueError("Unexpected saved WAV format")
            samples = wav.getnframes()
            pcm = wav.readframes(samples)
            if not samples or len(pcm) != 2 * samples or size != 44 + len(pcm):
                raise ValueError("Incomplete saved PCM")
        meta = {
            "launch": record["launch_id"],
            "optical_rpm": record["optical_launch_rpm"],
            "rate": RATE,
            "samples": samples,
            "storage": "flash",
        }
        save_pair(folder, record, meta, pcm)
        received += 1
        deadline = time.monotonic() + 10


def collect(port_name, folder, recover=False, saved=False):
    import serial

    with serial.Serial(
        port=None, baudrate=115200, timeout=0.3, write_timeout=3
    ) as port:
        port.dtr = port.rts = False
        port.port = port_name
        port.open()
        port.reset_input_buffer()
        if saved:
            return download_saved(port, folder)
        return capture(port, folder, recover)
