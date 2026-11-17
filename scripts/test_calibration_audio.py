"""Metadata decoding and repeat exports must preserve training evidence."""
import contextlib
import copy
import io
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from calibration_export import decode, decode_audio, save_download


def checksum(payload):
    return bytes(payload) + struct.pack("<I", zlib.crc32(payload))


def packet(duration=1000, features=True):
    raw = bytearray(96)
    raw[:4] = b"LLC2"
    struct.pack_into("<If7I6f", raw, 4, 1, 6000, duration, 1, 0, 0, 0, 0, 0, *([0.] * 6))
    raw = checksum(raw)
    count = (duration+199)//200 if duration <= 30000 else 150+(duration-30000+999)//1000
    audio = bytearray(b"LAF1" + struct.pack("<IHBBIIHHIHH", duration, count, 19, 8, 1200, 3, 400, 200, 30000, 1000, 1))
    audio.extend(bytes(count*19))
    if features and count > 1:
        row = bytearray(19)
        struct.pack_into("<H", row, 0, 203)  # 101.5 Hz, never implicitly RPM.
        row[2:8] = bytes([90, 127, 46, 133, 7 | (1 << 4) | (2 << 6), 6])
        row[8:16] = bytes([77]*8)
        row[16:19] = bytes([2, 1, 13])
        audio[32+19:32+38] = row
    return raw, checksum(audio)


class AudioTests(unittest.TestCase):
    def test_export_with_no_rpm_still_contains_noise_and_pitch(self):
        raw, audio = packet()
        record = decode(raw+audio)
        self.assertEqual(record["samples"], [])
        self.assertEqual(decode(raw)["trace_sha256"], record["trace_sha256"])
        self.assertIsNone(decode(raw)["audio_metadata"])
        rows = record["audio_metadata"]["records"]
        self.assertEqual(len(rows), 5)
        self.assertTrue(rows[0]["missing"])
        self.assertEqual(rows[1]["candidate_hz"], 101.5)
        self.assertEqual(rows[1]["candidate_cycles_per_turn"], 2)
        self.assertEqual(rows[1]["candidate_reason"], "acquiring")
        self.assertEqual(rows[1]["band_peak_db"], [-51]*8)
        self.assertEqual(rows[1]["available_by_ms"], 660)
        self.assertEqual(rows[1]["broadband_frames"], 1)

    def test_full_session_and_partial_last_interval(self):
        for duration, count in ((0, 0), (30000, 150), (30001, 151), (300000, 420)):
            raw, audio = packet(duration, False)
            value = decode(raw+audio)["audio_metadata"]
            self.assertEqual(len(value["records"]), count)
            if count:
                self.assertEqual(value["records"][-1]["end_ms"], duration)
        self.assertEqual(len(audio), 8016)

    def test_missing_fft_and_unknown_delivery_are_explicit(self):
        _, audio = packet()
        body = bytearray(audio[:-4]);at = 32+19
        body[at:at+19] = bytes(19)
        body[at+3:at+6] = bytes([255]*3)
        body[at+6] = 1
        body[at+16] = 1
        body[at+18] = 255
        row = decode_audio(checksum(body), 1000)["records"][1]
        self.assertFalse(row["missing"])
        self.assertIsNone(row["candidate_hz"])
        self.assertIsNone(row["mean_spectral_flatness"])
        self.assertEqual(row["band_peak_db"], [None]*8)
        self.assertIsNone(row["available_by_ms"])
        self.assertTrue(row["delivery_lag_overflow"])

    def test_bad_attachment_is_not_silently_omitted(self):
        raw, audio = packet()
        cases = [audio[:-1], audio+b"x", audio[:50]+bytes([audio[50]^1])+audio[51:]]
        for offset, value in ((4, 0), (8, 8), (10, 20), (20, 0), (30, 2), (32+19+6, 15), (32+19+17, 4)):
            bad = bytearray(audio[:-4]);bad[offset] = value;cases.append(checksum(bad))
        for bad in cases:
            with self.assertRaises(ValueError):
                decode(raw+bad)

    def test_independent_enrichment_keeps_labels_and_timestamps(self):
        raw, audio = packet()
        base = decode(raw);base["packet_sha256"] = "raw"
        acoustic = decode(raw+audio);acoustic["packet_sha256"] = "audio"
        fitted = copy.deepcopy(base);fitted["packet_sha256"] = "fit"
        fitted["final_fit"] = {"winner": "Fixed Riccati"}
        for order in ((acoustic, fitted), (fitted, acoustic)):
            with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()) as log:
                dest = Path(directory)
                save_download([base], dest)
                path = dest/(base["trace_sha256"]+".json")
                labeled = json.loads(path.read_text());labeled["beyblade"] = "Sword Dran 1-60 LF"
                path.write_text(json.dumps(labeled));timestamp = path.stat().st_mtime_ns
                for downloaded in (*order, base):
                    save_download([downloaded], dest)
                result = json.loads(path.read_text())
                self.assertEqual(result["beyblade"], labeled["beyblade"])
                self.assertEqual(result["final_fit"], fitted["final_fit"])
                self.assertEqual(result["audio_metadata"], acoustic["audio_metadata"])
                self.assertEqual(set(result["source_packet_sha256"]), {"raw", "fit", "audio"})
                self.assertEqual(path.stat().st_mtime_ns, timestamp)
                self.assertEqual(json.loads((dest/"latest.json").read_text()), result)
                self.assertIn("Pulls returned by Stick: 1 | Newly saved: 0", log.getvalue())
                self.assertIn("Audio metadata returned: 1/1 pulls | With captured windows: 1", log.getvalue())


if __name__ == "__main__":
    unittest.main()
