# LaunchLab local build and flash

Repository: `C:\Users\Truman\Projects\Launch Lab`

## Current status

Installed locally under `firmware\.arduino`:
- Arduino CLI 1.5.1
- ESP32 Arduino core 3.3.0 and its compiler/tool dependencies
- M5Unified 0.2.23 and M5GFX 0.2.30
- LaunchLab's checksum-verified ESP32-S3 power-management SDK
- Isolated Python environment with esptool 5.1.0 and pyserial 3.5

The Analysis Mode source changes and local build/device scripts are in the repository.
No device has been connected, opened, backed up, or flashed by this setup.

**A firmware binary is NOT ready yet.** The build attempt inside Codex failed before compilation with Windows error 623 (Illegal System DLL Relocation). Both official Windows compiler variants failed to start. A reported Codex Windows issue describes this behavior with GCC while the same compiler runs from a normal Windows terminal: https://github.com/openai/codex/issues/51674

## 1. Finish the build in a normal Windows terminal

Open PowerShell or Windows Terminal outside the Codex command runner and run:

```powershell
Set-Location 'C:\Users\Truman\Projects\Launch Lab'
.\scripts\build-local.cmd
```

No administrator access, global PATH change, or PowerShell execution-policy change is needed. The command verifies the PM SDK, compiles the pinned configuration, checks stack budgets and application size, and writes a checksum manifest only after success. It does not access a device.

Build log: `firmware\build\analysis\build.log`
Application after success: `firmware\build\analysis\LaunchLabMini.ino.bin`
Manifest after success: `firmware\build\analysis\local-build.json`

Then inspect the offline plan:

```powershell
.\scripts\device-local.cmd plan
```

If compilation fails, stop and share `build.log` for diagnosis. Do not use the archived stable application as if it contained Analysis Mode. The development runtime should be `0.20.8-analysis-dev-pm`.

## 2. Later: connect and identify the port

The following steps are for when you choose to flash; they were not run during setup.

Use an M5StickS3 K150 already running LaunchLab, connected by a USB data cable. Close the browser updater and serial monitors. Confirm the physical model; an ESP32-S3 chip alone does not identify a StickS3.

```powershell
.\scripts\device-local.cmd ports
```

Replace COM7 in the commands below with its actual port. If the device will not enter download mode automatically, use the manufacturer's procedure: hold the power button until the green LED blinks, release it, then recheck the port.

## 3. Preview and flash (no automatic backup)

After a successful build, preview without opening the serial port:

```powershell
.\scripts\device-local.cmd flash --port COM7
```

Flash the application:

```powershell
.\scripts\device-local.cmd flash --port COM7 --execute
```

The command validates source/build checksums and artifact sizes, then uses one serial
connection to check ESP32-S3, 8 MB flash, the partition layout, app0 boot selection,
and the LaunchLab marker in the first 64 KiB of app0. This marker is a compatibility
check, not authentication or complete validation of the previous application. Unknown
images are refused. It writes only app0 at 0x10000, verifies the complete new application,
and resets after success. An exact device checksum match skips the write. Bootloader,
partitions, NVS and history are not written. There is no full-device backup or comparison
with a previous flash snapshot in this workflow.

Build and flash only when the build succeeds (Windows PowerShell or PowerShell 7):

```powershell
Set-Location 'C:\Users\Truman\Projects\Launch Lab'
.\scripts\build-local.cmd
if ($LASTEXITCODE -eq 0) {
    .\scripts\device-local.cmd flash --port COM7 --execute
}
```

## 4. Optional separate full backup

```powershell
.\scripts\device-local.cmd backup --port COM7
```

This reads the full 8 MB flash and saves a unique timestamped `.bin` plus SHA-256 receipt
under `firmware\.arduino\device-backups`, then restarts the device. It does not require
a current firmware build and does not write flash. Existing backups are never overwritten.
A backup contains private device data; retain it locally for recovery. Backup completion
does not establish compatibility with the app-only updater; flashing runs its own checks.

## 5. Check the development feature

- Main: hold B for one second to enter Analysis.
- Wait for microphone warm-up, then launch.
- Confirm READY before launching, then optical RPM, large circular MM:SS timer, last valid mic RPM timestamp, and the expanding Mic RPM graph.
- Tap B to freeze the timer/graph; launch again to restart both.
- Tap B once stopped to open Analysis Details. B scrolls additional RPM band rows. A returns to the main Analysis screen.
- Stop manually when the physical spin ends to anchor the fitted curve at zero RPM; automatic timeout does not count as an observed zero.
- Confirm fitting remains responsive to A/B, and hold B or launch again during fitting to check cancellation.
- Tap A to show tilt replay and the initial optical RPM graph; tap A again to return. Recording continues in replay.
- Hold B to return to normal Main; check normal audio returns.
- Check history/settings, sleep/wake, and the side power key.

Analysis automatically stops at five minutes and continues through quiet gaps. The separate Mic RPM mode retains its two-minute/eight-seconds-without-evidence limits. Analysis remains silent while the microphone is active.

## Validation already completed

Targeted firmware host tests were completed in the implementation work. Additional offline tests exercise the local flashing helper's no-write preview, checksum refusal, layout/boot-selection checks, targeted preflight checks, separate full backups, and application-only write sequence using mocked device calls. No physical flashing or runtime validation has occurred.
