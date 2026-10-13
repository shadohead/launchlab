# Build a LaunchLab Mini - current QRE1113 kits

[Parts list and quantities](../README.md#parts-list--build-your-own) are in the README.
Use an intact M5StickS3 K150 and the analog SparkFun QRE1113 breakout, ROB-09453.
A digital-output or capacitor-timing QRE board is not interchangeable.
M5StickC, C Plus and C Plus2 are different boards and are not supported by these firmware images.

## Choose the current layout

- **Compact:** QRE1113 sensor v102 plus Top Mount v2 cover and dock, with M5 above the sensor.
- **Compact Sidecar:** QRE1113 sensor v102 plus Sidecar v2 cover and base, with separate sensor and display attachments.
- For the larger analog module, use [Standard Sidecar / TCRT5000](TCRT5000.md).

[Current source packages and plates](HARDWARE_VERSIONS.md#current-downloads) supply the exact files for these images.
The sensor plate and mount-pair plate are separate.
Firmware 0.13.5 supports both saved sensor profiles. Choose **Settings → Mounting → Top or Sidecar** to match the physical layout; display flip is a separate setting.

## Parts and wiring

- One intact M5StickS3 K150 and one analog QRE1113 breakout.
- Three 3.3 V signal/power wires, sized for your board/header and layout.
- QRE1113 v102 base with two captive clips, plus its pressure bar.
- The matching Top Mount v2 or Sidecar v2 cover and support pair.
- Two M2×4 pan-head screws for the QRE pressure bar.
- Two M2×4 pan-head screws from underneath into the M5 support.
- Top Mount only: four M2×6 pan-head screws from dock into the sensor base.

| Sensor | M5StickS3 Hat |
| --- | --- |
| OUT / analog AO | G1 / GPIO1 |
| VCC | 3V3 / 3V3_L2 |
| GND | GND |

Check the physical pin labels and solder connections before attaching power.
Use the Hat 3.3 V supply; Grove red supplies 5 V.
Do not use GPIO0 for this analog input.
Keep sensor output within the device's 3.3 V limits.
The firmware does not change the physical supply or automatically identify the sensor.

## Printing and assembly

**PETG is recommended over PLA for the printed mounts**, especially flexing clips, M5 bracket snap arms and parts that receive screws. Use the PETG project matching your release where supplied; otherwise import the STLs and slice with your printer's PETG profile. Keep the supplied orientation, walls, infill, hinge support blockers and captive-clip positions. PLA projects remain available; saved PLA toolpaths and PLA estimates do not become PETG profiles or estimates.

Start screws straight and tighten only enough to seat the parts, using the exact type and length specified for the release. If a screw or snap arm binds, check the print and fit rather than forcing it. The PETG recommendation does not establish dimensional accuracy, screw-cycle durability or physical fit; check the actual print and assembly.

Open the [QRE v102 PLA source plate](../site/public/hardware/downloads/compact-base-v102.3mf), retaining both hinge support blockers.
It contains the base with both clips captive and fully open, plus the pressure bar.
The source profile targets P1S, 0.4 mm nozzle, PLA and 0.2 mm layers.
Select your own loaded filament and re-slice for your printer.
After cooling, remove exterior supports and gently rotate the clips closed.
No clip screws or separate crossbar are required by v102.

For **Top Mount**, use the [PLA pair](../site/public/hardware/downloads/top-mount-v2-pla.3mf) or [PETG pair](../site/public/hardware/downloads/top-mount-v2-petg.3mf).
The pair contains the cover and matching dock.
Secure the M5 to the dock with two M2×4 screws, then the dock to the QRE base with four M2×6 screws.
The cover snaps over both dock arms; press both inward to release.

For **Sidecar**, use the [PLA pair](../site/public/hardware/downloads/sidecar-v2-pla.3mf) or [PETG pair](../site/public/hardware/downloads/sidecar-v2-petg.3mf).
Secure the M5 to the base with two M2×4 screws into its secondary back holes.
Thread the leads through the enclosed left-side wire exit before snapping on the cover.
Use relaxed cable bends between the separate sensor and display attachments.

Both v2 covers are unchanged from their v1; existing v1 cover owners can reprint only the matching v2 support.
Exact CAD/readback and source-project audits passed, but printed fit, snap retention, fatigue and optical alignment remain unverified.
Dry-fit the actual device, launcher, connectors and screw engagement before any pull.
Current files do not inherit physical qualification from older printed prototypes.
The historical [v0.40 plate](../hardware/LaunchLab_v040_ALL_ITEMS_PRINT/) and [v0.68 carrier](../hardware/M5_cable_hook_v068/) remain preserved as older alternatives.

## Firmware and controls

Use the [USB updater](https://shadohead.github.io/launchlab-mini/) in desktop Chrome or Edge. A USB data cable is required. Connect USB, then hold the side Power/Reset button until the internal green LED flashes. Close serial monitors, then connect through the page. Firmware installation begins only after clicking Connect & update/install and choosing a device. See [update instructions](UPDATER.md) for current firmware release gates; this prototype has the limits recorded below.

Update checks for the expected partition table and writes only `0x10000`. First install replaces bootloader `0x0`, partitions `0x8000`, OTA selection `0xe000`, and application `0x10000`, without erase-all. Back up factory firmware before first install if you need to restore it. A full 8 MB backup contains personal settings/data and should stay private.

Short A recalls the latest recap. Short B opens the menu: History, New session, Battery, Settings and Tournament. In menus, A opens or applies, B moves to the next choice and hold B goes back or cancels; see [the firmware guide](../firmware/LaunchLabMini/README.md). After installing, choose your sensor under Settings → Sensor profile: QRE / Standard (default) or TCRT5000. Automatic power-off uses a 3-minute default with saved 1–10 minute choices; measured pulls, buttons and interactive USB commands refresh it. A detected USB computer keeps it awake, including with no serial reader. Battery shake-wake at 80 MHz remains unverified.

The historical 0.11.1 and the preserved 0.10.22/0.10.21/0.10.14/0.10.11/0.10.7/0.10.1 default to the peak of three-consecutive-turn elapsed-time
averages. Settings offers a persistent 1-turn peak alternative. In Settings,
A edits the highlighted row, B cycles its value, A applies and hold B cancels. Changing the
metric or sensor profile starts the next accepted pull in a new session. The default sleep is
3 minutes; saved 1–10 minute choices remain. Hold A on main enters tournament;
B cycles Recording Only/RPM and hold A exits. Auto-sleep restores the selected mode/view.

A fresh K150 needs **First install**, which writes bootloader, partition table,
boot_app0 and application. Update writes the application only and requires a
matching partition table. Both paths verify downloads and device MD5 without
an erase-all. First install replaces the factory app/layout; back up first.

## Troubleshooting

If no USB port appears, try a known data cable, re-enter download mode, and close apps using the serial port. A partition or boot-selection mismatch means stop and back up; do not use First install to bypass it. After an interrupted app-only update, deliberately select Repair / rollback for the intended version. An interrupted First install may also need boot/layout recovery. Follow the [recovery and stock reinstall guide](RECOVERY.md) for the correct route. A successful message requires device MD5 verification. Press Power/Reset if verified firmware does not restart automatically.

## Historical 0.11.1 qualification

**0.11.1** is the user-checked prototype revision. It adds saved themes (Classic, Mint, Amber, Violet), a personal-best effect on the recap, and USB backup and restore of history, personal bests, appearance and settings (`K` export, `R` restore). It fixes 0.11.0, which restarted about once a second after boot (a loop-task stack overflow) and was withdrawn. One firmware still serves both sensors through the saved **Sensor profile** setting (QRE / Standard with an 80-count mark floor, TCRT5000 with 60). It was installed app-only on the owner's M5StickS3 K150: no restarts in a 120 s soak, history kept, a backup and restore round trip identical, and the screen, themes and pulls confirmed. The detector, sampling, thresholds and saved history format are unchanged from 0.10.22, which keeps measuring pulls that start right after an automatic save. Battery endurance, long-session reliability, absolute RPM/angle calibration, physical browser transfer and first install/rollback, and stock recovery remain separate checks.
