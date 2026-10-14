# LaunchLab

A removable practice meter for **Beyblade X string launchers**. An analog optical sensor measures launcher-shaft RPM; an **M5StickS3 K150** shows your peak, launch history, trends and tilt/rotation replay. The screen and battery are built in, so practice does not require a phone.

**[Website & build guide](https://launchlab.beywatch.gg) · [USB updater](https://launchlab.beywatch.gg/update) · [Releases](https://github.com/shadohead/launchlab/releases)**

## Choose a build

| Build | Sensor | Layout |
| --- | --- | --- |
| LaunchLab | SparkFun analog QRE1113, ROB-09453 | Top Mount or Sidecar |
| LaunchLab (TCRT5000) | TCRT5000 / LM393, analog AO | Sidecar |

Sidecar puts the M5 on a separate carrier beside the sensor. Both sensors use the same current firmware; select your Sensor profile in Settings. Use the cradle and screws supplied with your chosen release; a common part name does not establish interchangeability.

## Get started

1. Gather an intact M5StickS3 K150, one analog sensor, three leads, printed mounts, the specified screws and a USB-C **data** cable. See the [parts list](docs/PARTS.md).
2. Download matching parts and follow [QRE assembly and wiring](docs/BUILD.md) or [TCRT assembly and wiring](docs/TCRT5000.md). Wire **AO/OUT → G1**, **VCC → Hat 3V3**, **GND → GND**; leave DO disconnected. Grove red supplies 5 V.
3. Prefer **PETG** for flexing clips, snap arms and screwed parts. Retain project modifiers/support blockers and reslice for your printer and filament.
4. Back up factory firmware before **First install**. Use desktop Chrome or Edge and a data cable; review [updater instructions](docs/UPDATER.md) and [recovery](docs/RECOVERY.md).

The [LaunchLab USB updater](https://launchlab.beywatch.gg/update) is the maintained interface for both sensors. Check its selected version and release notes before installing. Firmware source and published release files remain in this repository.

## Files and limits

This repository bundles **v102 sensor attachments and Top Mount/Sidecar v2 mounts**. The [website downloads](https://launchlab.beywatch.gg/downloads) include newer Top Mount v19 and Sidecar v7 packages. Follow each package’s matching guide and fasteners. [Bundled hardware versions and checksums](docs/HARDWARE_VERSIONS.md).

RPM is measured at the **launcher shaft**, not the Beyblade in the stadium. Tilt replay does not measure distance or compass heading. CAD and slicing checks do not establish physical fit, retention, fatigue or absolute calibration; test-fit your actual launcher and prints.

[Device controls](firmware/LaunchLabMini/README.md) · [Development and validation commands](docs/DEVELOPMENT.md) · [Contributing](CONTRIBUTING.md)

Original software uses **MIT**. Hardware packages retain their own attribution and share-alike notices. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).
