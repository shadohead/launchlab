# LaunchLab Mini for Beyblade X

**Firmware 0.12.2 is the recommended production release.**
It includes independently tested swing-ring recovery and completed startup Single Turn peak corrections.
The owner identified a soldering issue in the reported intermittent behavior.
The exact 0.12.2 application passed software and target-build checks; device evidence is from the related installed diagnostic build, not an exact 0.12.2 flash/readback.
The withdrawn 0.12.1 source and assets remain preserved.
See [release notes](firmware/releases/0.12.2-sticks3-recovery/README.md) and [source build context](docs/FIRMWARE_0.12.2.md).

**A removable launch-practice meter for Beyblade X string launchers.** LaunchLab Mini uses an optical sensor to measure the launcher's shaft speed, then shows your peak RPM, launch history, and a replay of the device's tilt and rotation on an M5StickS3 screen. Use it to compare pulls, track consistency across practice sessions, and try to repeat a chosen launch.

Built around **M5StickS3 K150**, with separate printed mounts for the **small analog QRE1113** or **older TCRT5000 / LM393** sensor. The current firmware supports both sensors through a saved Sensor profile setting; the old TCRT 0.2.0 build is preserved separately. The M5 keeps its factory case, display, controller and battery; you do not need a phone to view your results. The mount is designed to be removable. Check the supplied launcher interface and printed fit before using it on your launcher.

**[USB firmware updater](https://shadohead.github.io/launchlab-mini/) · [Recovery & M5Stack reinstall](docs/RECOVERY.md) · [Firmware & print releases](https://github.com/shadohead/launchlab-mini/releases) · [Parts list & Amazon links](#parts-list--build-your-own) · [Build & wiring](docs/BUILD.md)**

**Prototype validation:** **0.11.1** is the user-checked prototype revision. It adds saved themes (Classic, Mint, Amber, Violet), a personal-best effect on the recap, and USB backup and restore of history, personal bests, appearance and settings (`K` export, `R` restore). It fixes 0.11.0, which restarted about once a second after boot (a loop-task stack overflow) and was withdrawn. One firmware still serves both sensors through the saved **Sensor profile** setting (QRE / Standard with an 80-count mark floor, TCRT5000 with 60). It was installed app-only on the owner's M5StickS3 K150: no restarts in a 120 s soak, history kept, a backup and restore round trip identical, and the screen, themes and pulls confirmed. The detector, sampling, thresholds and saved history format are unchanged from 0.10.22, which keeps measuring pulls that start right after an automatic save. Battery endurance, long-session reliability, absolute RPM/angle calibration, physical browser transfer and first install/rollback, and stock recovery remain separate checks.

## Sidecar form factor

**Sidecar** is the LaunchLab form factor with the M5StickS3 mounted on its own carrier beside a separate optical sensor attachment. Sidecar supports either QRE1113 or TCRT5000; the name describes the mounting layout, not the sensor, firmware or hardware revision.

See the [Sidecar build guide](docs/BUILD.md) and [hardware downloads](docs/HARDWARE_VERSIONS.md#current-downloads).

## Parts list — build your own

Start with **one M5StickS3 K150 + one analog sensor + three short wires + printed mounts and M2 screws**. The StickS3 already contains the display, processor, IMU and battery. Choose **QRE1113** for the current firmware/UI, or **TCRT5000 / LM393** for the preserved old-sensor firmware.

**Amazon affiliate disclosure:** As an Amazon Associate I earn from qualifying purchases. Amazon links below use the [Beywatch](https://beywatch.gg/) affiliate tag and help support the project at no extra cost to you.

| Qty per build | Part | Which build / purchase notes | Buy |
| --- | --- | --- | --- |
| 1 | **M5StickS3 K150** | Both sensor builds. ESP32-S3, 8 MB flash/PSRAM, intact factory case. | [Amazon — M5Stack Official, ordered](https://www.amazon.com/dp/B0GWHN8HK3?tag=beywatchgg-20) · [M5Stack K150 specs](https://docs.m5stack.com/en/core/StickS3) |
| 1 | **SparkFun QRE1113 analog breakout, ROB-09453** | Small-sensor build; exact ordered listing. **Analog ROB-09453** is required. Use the SparkFun link if Amazon is unavailable. | [Amazon — SparkFun analog board](https://www.amazon.com/dp/B07YM4551X?tag=beywatchgg-20) · [SparkFun](https://www.sparkfun.com/sparkfun-line-sensor-breakout-qre1113-analog.html) |
| 1, instead of QRE | **TCRT5000 / LM393 module with AO** | Old-sensor build. Choose the four-pin **VCC/GND/D0/A0** module and use A0. The mount uses the measured 31×13×1 mm PCB; check your board/header dimensions. | [Amazon — HiLetgo 10-pack, ordered](https://www.amazon.com/dp/B00LZV1V10?tag=beywatchgg-20) |
| 3 leads | **Short 2.54 mm jumper wires** | VCC, GND and AO/OUT; the ordered SinLoon pack is 8 cm male-to-female. Trim/reroute as needed, checking connector clearance. | [Amazon — SinLoon 8 cm](https://www.amazon.com/dp/B08M3QLL3Q?tag=beywatchgg-20) · [Amazon — ELEGOO mixed jumper kit](https://www.amazon.com/dp/B01EV70C78?tag=beywatchgg-20) |
| 1 | **USB-C data cable** | Flashing and charging; use an existing cable that carries data. | Use an existing data cable, or [Amazon search — USB-C data cable](https://www.amazon.com/s?k=USB-C+data+cable&tag=beywatchgg-20) |
| 1 set | **Printed M5 + sensor mounts** | QRE1113 or TCRT5000 sensor v102, paired with Top Mount v2 (QRE only) or Sidecar v2. Download the sensor and matching mount pair separately. | [Print files](docs/HARDWARE_VERSIONS.md#current-downloads) |
| See below | **M2 machine screws** | Head style and length depend on the mount revision. | [Amazon — ordered MEIYYJ countersunk assortment](https://www.amazon.com/dp/B07HC3LQYS?tag=beywatchgg-20); pan-head sizes below |
| Small amount | **1.75 mm PLA filament** | Print yourself or use a printing service; saved profiles target P1S / 0.4 mm nozzle. | Use existing PLA, a printing service, or [Amazon search — PLA filament](https://www.amazon.com/s?k=1.75mm+PLA+filament&tag=beywatchgg-20) |

Links marked **ordered**, plus the SparkFun, SinLoon, ELEGOO and uxcell links, match the project-related product listings from purchase history. Cable, filament and pan-head links marked **Amazon search** are general supplies, not exact ordered products. Check the selected variant and pack contents; purchasing a component does not establish printed fit.

### Screws, connectors and tools

The ordered MEIYYJ assortment includes M2×4/5/6/8 **flat/countersunk heads**.
Current v102 sensor clips need no clip screws.
Use the specified **pan-head** pieces for current mounts: [Amazon search — M2×4 pan-head](https://www.amazon.com/s?k=M2x4+pan+head+machine+screws&tag=beywatchgg-20) · [Amazon search — M2×6 pan-head](https://www.amazon.com/s?k=M2x6+pan+head+machine+screws&tag=beywatchgg-20).

| Assembly | Screws required |
| --- | --- |
| QRE1113 v102 sensor | 2 × **M2×4 pan-head** for the pressure bar; captive clips need no screws. |
| Top Mount v2 | 2 × **M2×4 pan-head** from below into M5, plus 4 × **M2×6 pan-head** into the QRE base; cover snaps on. |
| Sidecar v2 | 2 × **M2×4 pan-head** into M5 secondary back holes; cover snaps on. |
| TCRT5000 v102 sensor | 1 × **M3 board screw** (M3×6 CAD reference); captive clips need no screws. |

If your sensor has bare pads, add a **3-pin 2.54 mm header** or solder the three wires directly. The ordered [uxcell right-angle male header strip](https://www.amazon.com/dp/B01461DQ6S?tag=beywatchgg-20) can be cut to three pins; check header direction and cable clearance against the printed seat. You will also need a small screwdriver, cutters, and a soldering iron/solder if attaching bare wires or headers.

Use a **Beyblade X string launcher with the supplied removable clip/port interface**. The interface derives from Migbello's BP Gear Port Connector; check retention and optical access on your actual launcher before a pull. No universal launcher-fit claim is made by these CAD files.

Wire **AO/OUT → G1 / GPIO1**, **VCC → 3V3 / 3V3_L2**, **GND → GND**. DO is unused. The Grove red lead supplies 5 V; use the Hat 3.3 V pin for these builds. [QRE build/wiring](docs/BUILD.md) · [TCRT build/wiring](docs/TCRT5000.md) · [Sensor and hardware versions](docs/HARDWARE_VERSIONS.md).

![Compact QRE1113 v102 with Top Mount v2, rendered from the exact source meshes](site/public/hardware/images/top-qre.webp)


The updater defaults to **0.12.2** for both saved sensor profiles; the withdrawn 0.12.1 is not offered in its version list.
It checks the complete known installed application before an app-only update, blocks downgrades and unknown/incomplete images, and skips writing an identical version.
**Repair / rollback** requires an explicit target/backup confirmation; both app-only paths require the matching partition layout and boot selection.
All modes check the detected ESP32-S3 chip and 8 MB flash capacity.
The previous 0.11.2, 0.11.1, 0.10.22, 0.10.21, 0.10.14, 0.10.11, 0.10.7 and 0.10.1 binaries are preserved.
Physical browser transfer, four-image installation of this revision, stock recovery and rollback remain unverified.
[Update details](docs/UPDATER.md) · [Recovery and vendor reinstall](docs/RECOVERY.md).

Current source: **0.12.2 production** (QRE1113 or TCRT5000 via Sensor profile).
Updater default: **0.12.2**.
Old TCRT firmware: **0.2.0**.
Current printed hardware: **QRE1113 v102**, **TCRT5000 v102**, **Top Mount v2** and **Sidecar v2**.
The historical v0.40 plate and earlier carriers remain available.
Sidecar motion-axis normalization is planned for firmware **0.13.1**, not included in the current 0.12.2.
Firmware, M5 carrier and sensor-mount versions are independent.

**[Choose your current kit and see every revision](docs/HARDWARE_VERSIONS.md)**.
Hardware families and shared sensor revisions have separate version histories.

## What it does

The current source revision is **0.12.2**, running at 80 MHz with unchanged 50 kS/s optical sampling.
It offers ten saved accent themes, five personal-best recap effects with Random/Off, optional Off/Low/High sound and USB backup/restore (`K`/`R`), and its build fails if any stack frame exceeds its budget.
A saved Sensor profile selects QRE / Standard (80-count mark floor) or TCRT5000 (60-count floor).
Controls are simpler: A opens or applies, B advances, hold B goes back or cancels.
A saved 180-degree display flip and previewed brightness live in Settings.
IMU startup recovery, prompt recaps, power diagnostics, tournament mode and USB keep-awake are retained; battery savings remain unmeasured.
The old TCRT 0.2.0 release is an earlier RPM acquisition build with its own detector thresholds.

| Feature | What you see or do |
| --- | --- |
| Optical peak RPM | Three-turn elapsed-time average peak by default; validated single-turn peak is selectable in Settings. Three consistent turns confirm the pull before a peak is recorded. |
| Live level | Use the bubble and tilt angle while holding still to check the device's orientation before launching. |
| Launch recap | After a pull, see peak RPM, a clearer Start-to-End rotation replay, and frozen Start/End tilt circles. Press A to recall the latest recap. |
| Launch history | Review recent pulls and the current session's pull count, average, and best RPM. Retains up to 128 launches and 24 sessions on the device. |
| Pull and session trends | Compare individual pulls, a trailing five-pull average, and session averages to follow your practice consistency. |
| Tournament mode | Hold A on main to enter or exit; B cycles Recording Only/RPM. |
| Battery and auto-off | Check percentage, voltage and charging status. Shake-to-wake sleep after a configurable 1–10 minutes (3-minute default) without activity; a detected USB computer keeps it awake. |
| Browser firmware updates | Install over USB in desktop Chrome or Edge. Updates write only the application after checking the partition layout. |

RPM is **launcher-shaft speed**, not a direct measurement of the Beyblade's release RPM or its spin in the stadium. Motion replay shows rotation and gravity-relative tilt; it does not measure travel distance or absolute compass heading. The live level references the screen plane until the mounting orientation is calibrated.

## Device UI

These are saved device-framebuffer screenshots, not concept UI. Screens marked **DEMO** use synthetic sample data. The live/recap images are from firmware 0.6.2, and are historical examples; 0.10.1 has a revised tilt recap; history/trend images are from the 0.3.0 baseline and battery from 0.3.1. [Image provenance](docs/assets/ui/provenance.json) records their sources.

<table>
<tr><th>Live RPM & level</th><th>Launch recap</th><th>Launch history</th></tr>
<tr>
<td align="center"><img src="docs/assets/ui/live-rpm-level.png" width="200" alt="Idle live screen showing READY, RPM placeholder and bubble level"></td>
<td align="center"><img src="docs/assets/ui/launch-recap.png" width="200" alt="Demo launch recap showing 6500 RPM, a rotation replay and frozen start/end tilt circles"></td>
<td align="center"><img src="docs/assets/ui/launch-history.png" width="200" alt="Demo launch history listing pull RPM with session count, mean and best"></td>
</tr>
<tr><td>Ready state, held RPM result and level.</td><td>Recall the latest pull with A.</td><td>Browse pulls and session summaries.</td></tr>
</table>

<table>
<tr><th>Pull trend</th><th>Session trend</th><th>Battery</th></tr>
<tr>
<td align="center"><img src="docs/assets/ui/pull-trend.png" width="200" alt="Demo pull trend with individual measurements and a five-pull average"></td>
<td align="center"><img src="docs/assets/ui/session-trend.png" width="200" alt="Demo session trend plotting session-average RPM"></td>
<td align="center"><img src="docs/assets/ui/battery.png" width="200" alt="Battery page showing approximate charge, voltage, charging state and auto-off countdown"></td>
</tr>
<tr><td>See variation across recent pulls.</td><td>Compare averages across sessions.</td><td>Check power before practice.</td></tr>
</table>

From the live screen, **A shows the last recap** and **B opens the menu** (History, New session, Battery, Settings, Tournament). In menus, A opens or applies, B moves to the next choice and hold B goes back one level or cancels an edit. Settings holds Sleep after, RPM method, Brightness, Flip display and Sensor profile; brightness and flip preview before you apply them. Hold A on main for one second to enter tournament mode (B cycles Recording Only/RPM; hold A exits). See the [firmware guide](firmware/LaunchLabMini/README.md).

## Current hardware assemblies

The current kits combine shared sensor v102 with a matching mount-family v2 pair.
Sidecar has separate sensor and M5 attachments; Top Mount holds the M5 above the QRE sensor.
Images use the exact reviewed assembly meshes.
Electronics and screws are reference envelopes, not printed parts.
Explosion offsets illustrate assembly order, not a measured position on a complete launcher.

| Kit | Shared sensor | M5 mount | Current assembly |
| --- | --- | --- | --- |
| Compact | QRE1113 v102 | Top Mount v2 cover and dock | ![Compact Top Mount assembly](site/public/hardware/images/top-qre-exploded-960.webp) |
| Compact Sidecar | QRE1113 v102 | Sidecar v2 cover and base | ![Compact Sidecar assembly](site/public/hardware/images/side-qre-exploded-960.webp) |
| Standard Sidecar | TCRT5000 v102 | Sidecar v2 cover and base | ![Standard Sidecar assembly](site/public/hardware/images/side-tcrt-exploded-960.webp) |

### Exact print downloads

| Part set | STL/source package | P1S PLA plate | P1S PETG plate |
| --- | --- | --- | --- |
| QRE1113 v102 base, captive clips and pressure bar | [Package](site/public/hardware/downloads/compact-base-v102.zip) | [PLA](site/public/hardware/downloads/compact-base-v102.3mf) | No sensor PETG plate supplied |
| TCRT5000 v102 mount and captive clips | [Package](site/public/hardware/downloads/standard-sensor-v102.zip) | [PLA](site/public/hardware/downloads/standard-sensor-v102.3mf) | No sensor PETG plate supplied |
| Top Mount v2 cover and dock | [Package](site/public/hardware/downloads/top-mount-v2.zip) | [PLA pair](site/public/hardware/downloads/top-mount-v2-pla.3mf) | [PETG pair](site/public/hardware/downloads/top-mount-v2-petg.3mf) |
| Sidecar v2 cover and base | [Package](site/public/hardware/downloads/sidecar-v2.zip) | [PLA pair](site/public/hardware/downloads/sidecar-v2-pla.3mf) | [PETG pair](site/public/hardware/downloads/sidecar-v2-petg.3mf) |

Choose one sensor plate and the matching mount-pair plate.
These source plates are separate, not a newly sliced combined full kit.
The pair plate contains both mating mount pieces.
Both v2 shells are unchanged from their family's v1; existing v1 shell owners need only the matching v2 base or dock.
Saved CAD, STL readback and print-project audits passed.
Physical fit, snap retention, fatigue, optical alignment and absolute RPM calibration remain unverified for these current hardware combinations.
Sidecar motion-axis normalization is pending firmware 0.13.1; do not infer it from the hardware preview.
See [attribution](THIRD_PARTY_NOTICES.md).

[Hardware versions and assembly notes](docs/HARDWARE_VERSIONS.md) identify current and historical parts.
[Render/download provenance](site/public/provenance/hardware.json) and [assembly validation](site/public/provenance/hardware-assembly-validation.json) pin the displayed geometry and source plates.
All historical hardware folders and downloads remain unchanged.

## Get started

1. Choose Compact, Compact Sidecar or Standard Sidecar from the current table above.
2. Download the sensor plate and its matching mount-pair plate, then review them for your printer and filament.
3. Assemble an intact M5StickS3 K150 and matching analog sensor using [BUILD.md](docs/BUILD.md) or [TCRT5000.md](docs/TCRT5000.md).
4. Use the [updater](https://shadohead.github.io/launchlab-mini/) in desktop Chrome or Edge for firmware 0.12.2, then select the matching Sensor profile in Settings.

Update writes only the application at `0x10000` after checking the installed partition table.
First install replaces the factory firmware and partition table and requires an explicit checkbox.
Neither flow erases all flash.
Every downloaded image is SHA-256 checked and writes use device MD5 verification.
Confirm your device is specifically an M5StickS3 K150.

## Hardware files

- [Current hardware kits and historical revisions](docs/HARDWARE_VERSIONS.md), with the [checksum inventory](hardware/versions.json).
- [Latest sensor and mount downloads](site/public/hardware/downloads/), with exact source plates and STL/source packages.
- Historical [v0.68 carrier](hardware/M5_cable_hook_v068/), [v0.50 carrier](hardware/M5_equal_supports_v050/), [v0.43 TCRT](hardware/minimal_TCRT_split_keepers_v043/), [v0.40 plate](hardware/LaunchLab_v040_ALL_ITEMS_PRINT/) and [v0.33 QRE](hardware/LaunchLab_v033_flush_QRE/) remain preserved.

The launcher attachment derives from Migbello's BP Gear Port Connector.
Preserve its attribution and share-alike notice when distributing changes.

## Develop

Install Arduino CLI, then run `bash scripts/build-firmware.sh`. It installs the pinned ESP32 3.3.0, M5Unified 0.2.23 and M5GFX 0.2.30 dependencies into a project-local cache and compiles the 8 MB flash / OPI PSRAM target. Source builds may differ byte-for-byte from the archived binary across hosts/toolchains. The published binary is the exact saved release, with recorded checksum.

Run `bash scripts/test-firmware.sh` for 20 native C++ suites with address/undefined sanitizers and six production harnesses. Website development requires Node 24+:

```sh
cd site
npm ci
npm test
npm run dev
npm run build
```

`python3 scripts/verify-artifacts.py` verifies published checksums and 3MF ZIP integrity. GitHub Actions runs the native tests, updater tests and artifact checks before deploying Pages from `main`.

Frozen M5 firmware versions are in [firmware/releases](firmware/releases/catalog.json), including the working 0.3.0 QRE baseline and the separate 0.2.0 old TCRT5000 variant.
The web updater recommends the 0.12.2 shared-profile application and preserves earlier selections; withdrawn 0.12.1 remains archived.
Historical source snapshots retain their original layouts and notes; use each release's source rather than mixing headers across versions.
Device backups, personal practice exports and factory flash dumps are excluded.

## Validation scope

The current firmware passed 31 sanitizer-enabled native suites, nine public production harnesses, pinned target compilation and stack checks.
The same-source private optical checks retain 18 confirmed configurations and 12 diagnostic-only configurations.
Both corrections were exercised in the related installed diagnostic build, with complete application readback, backup retention and matching metric/history/UI registrations.
The exact production 0.12.2 application has not been flashed or read back.
The owner identified a soldering issue in the intermittent field behavior; that physical report is separate from the generated evidence for the software corrections.
Independent absolute RPM calibration, printed fit/strength, first install, physical browser flashing, physical personal-best/Random sequencing and endurance remain outside these checks.

RPM measures launcher-shaft revolutions, not direct Beyblade release RPM. Tilt replay has no measured travel distance or compass heading. Digital CAD/slicing checks establish file consistency, not printed fit or safe launch-load retention.

Software: MIT with upstream component licenses. Hardware: preserved Creative Commons Attribution Share Alike notices. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Firmware 0.12.2 retains the extra-mark guard, ten accent themes, mounted Flip mapping, expanded main screen, five personal-best effects and optional sound.
It resets the learned swing ring consistently and retains a qualified completed startup peak while the latest fastest arrival is checked.
Profile thresholds, paired launch boundaries, acquisition and persistent formats remain unchanged.
[Release evidence](firmware/releases/0.12.2-sticks3-recovery/README.md).
