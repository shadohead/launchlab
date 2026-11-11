"""Offline regression tests: never open a serial port or call real esptool."""

import hashlib
import json
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import local_firmware as tool


class FakeDevice:
    CHIP_NAME = "ESP32-S3"

    def __init__(self):
        self.digest = "0" * 32
        self.reset = False

    def flash_md5sum(self, address, size):
        assert address == tool.APP_OFFSET
        return self.digest

    def hard_reset(self):
        self.reset = True


class FakeAPI:
    def __init__(self, files):
        self.files = files
        self.size = "8MB"
        self.reads = []
        self.writes = []
        self.verified = []
        self.corrupt = None
        self.fail_verify = False
        self.bad_digest = False

    def attach_flash(self, esp):
        pass

    def detect_flash_size(self, esp):
        return self.size

    def read_flash(self, esp, offset, size, **kwargs):
        self.reads.append((offset, size))
        if offset == 0x8000:
            data = self.files["LaunchLabMini.ino.partitions.bin"]
        elif offset == 0xE000:
            data = self.files["boot_app0.bin"]
        else:
            image = bytearray(tool.APP_PROBE_BYTES)
            image[0] = 0xE9
            image[32:36] = b"\x32\x54\xcd\xab"
            image[100:113] = b"LAUNCH_RESULT"
            data = bytes(image)
        if self.corrupt == offset:
            return b"\xff" * size
        return data

    def write_flash(self, esp, entries, **kwargs):
        self.writes.append((entries, kwargs))
        esp.digest = "0" * 32 if self.bad_digest else hashlib.md5(entries[0][1]).hexdigest()

    def verify_flash(self, esp, entries):
        self.verified.append(entries)
        if self.fail_verify:
            raise RuntimeError("verification failed")


class LocalFirmwareTests(unittest.TestCase):
    def files(self):
        return {
            "LaunchLabMini.ino.bin": b"\xe9" + b"new application" * 100,
            **{
                n: (tool.RELEASE / n).read_bytes()
                for n in ("LaunchLabMini.ino.partitions.bin", "boot_app0.bin")
            },
        }

    def test_targeted_reads_app_only_write_and_verify(self):
        files = self.files()
        api = FakeAPI(files)
        esp = FakeDevice()
        tool.flash_connected(api, esp, files)
        self.assertEqual(len(api.reads), 3)
        self.assertLess(sum(n for _, n in api.reads), 80000)
        self.assertEqual(
            api.writes,
            [([(tool.APP_OFFSET, files["LaunchLabMini.ino.bin"])], {"compress": True, "erase_all": False})],
        )
        self.assertEqual(api.verified, [[(tool.APP_OFFSET, files["LaunchLabMini.ino.bin"])]])
        self.assertTrue(esp.reset)

    def test_preflight_refusals_never_write(self):
        for problem in ("chip", "size", 0x8000, 0xE000, tool.APP_OFFSET):
            with self.subTest(problem=problem):
                files = self.files()
                api = FakeAPI(files)
                esp = FakeDevice()
                if problem == "chip":
                    esp.CHIP_NAME = "ESP32"
                elif problem == "size":
                    api.size = "4MB"
                else:
                    api.corrupt = problem
                with self.assertRaises(RuntimeError):
                    tool.flash_connected(api, esp, files)
                self.assertFalse(api.writes)
                self.assertFalse(esp.reset)

    def test_same_binary_is_noop_and_postwrite_failure_does_not_reset(self):
        files = self.files()
        api = FakeAPI(files)
        esp = FakeDevice()
        esp.digest = hashlib.md5(files["LaunchLabMini.ino.bin"]).hexdigest()
        tool.flash_connected(api, esp, files)
        self.assertFalse(api.writes)
        self.assertTrue(esp.reset)
        for kind in ("fail_verify", "bad_digest"):
            api = FakeAPI(files)
            esp = FakeDevice()
            setattr(api, kind, True)
            with self.assertRaises(RuntimeError):
                tool.flash_connected(api, esp, files)
            self.assertFalse(esp.reset)

    def test_one_connection_closes_on_success_and_failure(self):
        for fail in (False, True):
            files = self.files()
            api = FakeAPI(files)
            api.fail_verify = fail
            esp = FakeDevice()
            esp._port = types.SimpleNamespace(close=Mock())
            esp.change_baud = Mock()
            api.run_stub = Mock(return_value=esp)
            detect = Mock(return_value=esp)
            module = types.SimpleNamespace(detect_chip=detect, cmds=api)
            with (
                patch.dict("sys.modules", {"esptool": module}),
                patch.object(tool, "flash_files", return_value=files),
            ):
                if fail:
                    with self.assertRaises(RuntimeError):
                        tool.flash("COM99", True)
                else:
                    tool.flash("COM99", True)
            detect.assert_called_once_with(port="COM99", baud=115200)
            esp.change_baud.assert_called_once_with(460800)
            esp._port.close.assert_called_once_with()

    def test_preview_has_no_device_access(self):
        with (
            patch.object(tool, "flash_files", return_value=self.files()),
            patch.object(tool, "flash_connected") as connected,
        ):
            tool.flash("COM99")
            connected.assert_not_called()

    def test_flash_files_validate_required_bytes_and_capacity(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            files = self.files()
            for name, data in files.items():
                (base / name).write_bytes(data)
            manifest = {
                "files": {
                    name: {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
                    for name, data in files.items()
                }
            }
            with (
                patch.object(tool, "BUILD", base),
                patch.object(tool, "verified_build", return_value=manifest),
            ):
                self.assertEqual(tool.flash_files(), files)
                (base / "LaunchLabMini.ino.bin").write_bytes(b"changed")
                with self.assertRaisesRegex(RuntimeError, "checksum/size"):
                    tool.flash_files()
                data = b"\xe9" * (tool.APP_SIZE + 1)
                (base / "LaunchLabMini.ino.bin").write_bytes(data)
                manifest["files"]["LaunchLabMini.ino.bin"] = {
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "bytes": len(data),
                }
                with self.assertRaisesRegex(RuntimeError, "capacity"):
                    tool.flash_files()

    def test_backup_is_independent_full_read_with_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            calls = []

            def fake(port, *args, **kwargs):
                calls.append((port, args, kwargs))
                Path(args[-1]).write_bytes(b"\xff" * tool.FLASH_SIZE)

            with (
                patch.object(tool, "CACHE", Path(directory)),
                patch.object(tool, "esptool", side_effect=fake),
                patch.object(
                    tool, "verified_build", side_effect=AssertionError("Backup must not need a build")
                ),
            ):
                path = tool.backup("COM99")
                receipt = json.loads(path.with_suffix(".json").read_text())
                self.assertEqual(receipt["sha256"], tool.sha(path))
                self.assertEqual(path.stat().st_size, tool.FLASH_SIZE)
                self.assertEqual(calls[0][1][:3], ("read-flash", "0", str(tool.FLASH_SIZE)))
                self.assertEqual(calls[0][2], {"after": "hard-reset"})

    def test_changed_source_and_artifacts_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            files = self.files()
            for name, content in files.items():
                (base / name).write_bytes(content)
            manifest = base / "local-build.json"
            entries = {
                name: {"sha256": tool.sha(base / name), "bytes": len(content)}
                for name, content in files.items()
            }
            data = {"source_sha256": "source", "files": entries}
            manifest.write_text(json.dumps(data))
            with (
                patch.object(tool, "BUILD", base),
                patch.object(tool, "MANIFEST", manifest),
                patch.object(tool, "source_digest", return_value="source"),
            ):
                tool.verified_build()
                (base / tool.BUILD_FILES[0]).write_bytes(b"changed")
                with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                    tool.verified_build()
                entries.pop(tool.BUILD_FILES[0])
                manifest.write_text(json.dumps(data))
                with self.assertRaisesRegex(RuntimeError, "Incomplete firmware manifest"):
                    tool.verified_build()
            with (
                patch.object(tool, "MANIFEST", manifest),
                patch.object(tool, "source_digest", return_value="changed"),
                self.assertRaisesRegex(RuntimeError, "Source changed"),
            ):
                tool.verified_build()


if __name__ == "__main__":
    unittest.main()
