"""Exercise the Stick's tape/ACK protocol without opening a serial port."""

import io
import json
import struct
import tempfile
import unittest
import wave
import zlib
from pathlib import Path
from unittest.mock import patch

import audio_capture as audio


def frame(meta, pcm, corrupt=False):
    chunk = f"RAW_CHUNK 0 {len(pcm)} {audio.fnv1a(pcm)} {pcm.hex()}\n".encode()
    header = {
        "samples": len(pcm) // 2,
        "bytes": len(pcm),
        "complete": True,
        "adc_error": 0,
        "read_errors": 0,
        "fnv1a32": audio.fnv1a(pcm) ^ corrupt,
    }
    return (
        b"MIC_TAPE "
        + json.dumps(meta).encode()
        + b"\nRAW_BEGIN "
        + json.dumps(header).encode()
        + b"\n"
        + b"RAW_CHUNK 0 4 0 0000\n"
        + chunk
        + chunk
        + b"RAW_END\n"
    )


class Port:
    def __init__(self, tape, armed=1):
        self.incoming = io.BytesIO()
        self.sent = []
        self.tape = tape
        self.armed = armed

    def write(self, data):
        self.sent.append(data)
        if data == b"W\n":
            self.incoming = io.BytesIO(
                f"MIC_TAPE armed={self.armed} seconds=120 spin_max_s=90\n".encode()
                + b"LAUNCH_RESULT valid=1 peak_rpm=5000.0 launches=2\n"
                + b"MIC_SPIN launch=2 optical_rpm=5000.0\n"
            )
        if data == b"V\n":
            self.incoming = io.BytesIO(self.tape)

    def read_until(self, terminator, size):
        return self.incoming.readline(min(size, 21))  # short serial reads


class CollectorTests(unittest.TestCase):
    def setUp(self):
        self.meta = {
            "launch": 2,
            "optical_rpm": 5000.0,
            "rate": 4000,
            "samples": 3,
            "skipped_ms": 0,
        }
        self.pcm = struct.pack("<3h", -32768, 0, 32767)
        self.rec = {
            "schema": "launchlab-calibration-v1",
            "launch_id": 2,
            "optical_launch_rpm": 5000.04,
            "trace_sha256": "1" * 64,
            "packet_sha256": "2" * 64,
            "final_fit": None,
        }
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        delay = patch.object(audio.time, "sleep")
        delay.start()
        self.addCleanup(delay.stop)

    def test_arm_and_save_signed_pcm_and_corresponding_json(self):
        old = dict(self.meta, launch=1)
        port = Port(
            frame(old, b"123456")
            + frame(self.meta, self.pcm)
            + b"MIC_TAPE end spins=2\n"
        )
        with patch.object(
            audio, "latest", side_effect=[(None, True), (self.rec, True)]
        ):
            dest = audio.capture(port, self.path)
        self.assertEqual(port.sent, [b"W\n", b"V\n"] + [b"ACK 0\n"] * 4)
        self.assertEqual(
            json.loads((dest / "launch.json").read_text())["audio"]["samples"], 3
        )
        with wave.open(str(dest / "launch.wav"), "rb") as wav:
            self.assertEqual(
                (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()),
                (1, 2, 4000),
            )
            self.assertEqual(wav.readframes(3), self.pcm)
        with patch.object(
            audio,
            "latest",
            return_value=(dict(self.rec, final_fit={"winner": "Riccati"}), True),
        ):
            self.assertEqual(audio.capture(port, self.path, True), dest)
        self.assertEqual(
            json.loads((dest / "launch.json").read_text())["final_fit"]["winner"],
            "Riccati",
        )

    def test_failures_do_not_publish_incomplete_pairs(self):
        for tape in (
            frame(self.meta, self.pcm, True),
            frame(dict(self.meta, launch=3), self.pcm) + b"MIC_TAPE end spins=1\n",
            frame(self.meta, self.pcm) + b"MIC_TAPE end spins=2\n",
            frame(self.meta, self.pcm) * 2 + b"MIC_TAPE end spins=2\n",
        ):
            with (
                patch.object(audio, "latest", return_value=(self.rec, True)),
                self.assertRaises((ValueError, RuntimeError)),
            ):
                audio.capture(Port(tape), self.path, True)
            self.assertFalse(list(self.path.iterdir()))

    def test_saved_opening_download_without_arming(self):
        buf = io.BytesIO()
        with wave.open(buf, "wb") as wav:
            wav.setparams((1, 2, 4000, 0, "NONE", "not compressed"))
            wav.writeframes(self.pcm)
        data = buf.getvalue()
        for bad in (False, True):
            port = Port(b"")
            port.read = lambda n: port.incoming.read(min(n, 7))
            port.incoming = io.BytesIO(
                b"CAL_DATA 1 flash\n"
                + f"WAV_DATA {len(data)} {zlib.crc32(data) ^ bad}\n".encode()
                + data
                + b"WAV_END 1\n"
            )
            with patch.object(audio, "read_record", return_value=dict(self.rec)):
                if bad:
                    with self.assertRaises(ValueError):
                        audio.download_saved(port, self.path)
                else:
                    self.assertEqual(audio.download_saved(port, self.path), 1)
                    pair = self.path / (self.rec["trace_sha256"] + "-opening")
                    self.assertEqual((pair / "launch.wav").read_bytes(), data)
                    self.assertEqual(
                        json.loads((pair / "launch.json").read_text())["audio"][
                            "storage"
                        ],
                        "flash",
                    )
                self.assertEqual(port.sent, [b"G\n"])

    def test_chunk_offsets_and_dropped_ack(self):
        self.assertEqual(audio.fnv1a(b"hello"), 0x4F9F2CAB)

        def block(offset, data):
            return f"RAW_CHUNK {offset} {len(data)} {audio.fnv1a(data)} {data.hex()}\n".encode()

        data = bytes(range(256)) * 3
        header = {
            "samples": 384,
            "bytes": 768,
            "complete": True,
            "adc_error": 0,
            "read_errors": 0,
            "fnv1a32": audio.fnv1a(data),
        }
        first, second = block(0, data[:512]), block(512, data[512:])
        port = Port(b"")
        port.incoming = io.BytesIO(
            b"RAW_BEGIN "
            + json.dumps(header).encode()
            + b"\n"
            + second
            + first
            + first
            + second
            + b"RAW_END\n"
        )
        self.assertEqual(audio.read_pcm(port, dict(self.meta, samples=384)), data)
        self.assertEqual(port.sent, [b"ACK 0\n", b"ACK 0\n", b"ACK 512\n"])

    def test_stale_json_and_unarmed_device(self):
        with patch.object(audio, "latest", return_value=(self.rec, True)):
            for armed in (0, 1):
                port = Port(b"", armed)
                with self.assertRaises(RuntimeError):
                    audio.capture(port, self.path)
                self.assertNotIn(b"V\n", port.sent)


if __name__ == "__main__":
    unittest.main()
