# Parts list — build your own

These specifications apply to this repository’s bundled v102 sensors and v2 mounts. For newer Top Mount v19 and Sidecar v7 packages, use the [website build guide](https://launchlab.beywatch.gg/build) and that release’s exact parts and screws. Do not mix mount revisions.

Start with **one M5StickS3 K150 + one analog sensor + three short wires + printed mounts and M2 screws**. The StickS3 already contains the display, processor, IMU and battery. Both **QRE1113** and **TCRT5000 / LM393** use the current shared firmware; select the matching Sensor profile in Settings. The old TCRT 0.2.0 firmware is historical.

**Amazon affiliate disclosure:** As an Amazon Associate I earn from qualifying purchases. Amazon links below use the [Beywatch](https://beywatch.gg/) affiliate tag and help support the project at no extra cost to you.

| Qty per build | Part | Which build / purchase notes | Buy |
| --- | --- | --- | --- |
| 1 | **M5StickS3 K150** | Both sensor builds. ESP32-S3, 8 MB flash/PSRAM, intact factory case. | [Amazon — M5Stack Official, ordered](https://www.amazon.com/dp/B0GWHN8HK3?tag=beywatchgg-20) · [M5Stack K150 specs](https://docs.m5stack.com/en/core/StickS3) |
| 1 | **SparkFun QRE1113 analog breakout, ROB-09453** | Small-sensor build; exact ordered listing. **Analog ROB-09453** is required. Use the SparkFun link if Amazon is unavailable. | [Amazon — SparkFun analog board](https://www.amazon.com/dp/B07YM4551X?tag=beywatchgg-20) · [SparkFun](https://www.sparkfun.com/sparkfun-line-sensor-breakout-qre1113-analog.html) |
| 1, instead of QRE | **TCRT5000 / LM393 module with AO** | Old-sensor build. Choose the four-pin **VCC/GND/D0/A0** module and use A0. The mount uses the measured 31×13×1 mm PCB; check your board/header dimensions. | [Amazon — HiLetgo 10-pack, ordered](https://www.amazon.com/dp/B00LZV1V10?tag=beywatchgg-20) |
| 3 leads | **Short 2.54 mm jumper wires** | VCC, GND and AO/OUT; the ordered SinLoon pack is 8 cm male-to-female. Trim/reroute as needed, checking connector clearance. | [Amazon — SinLoon 8 cm](https://www.amazon.com/dp/B08M3QLL3Q?tag=beywatchgg-20) · [Amazon — ELEGOO mixed jumper kit](https://www.amazon.com/dp/B01EV70C78?tag=beywatchgg-20) |
| 1 | **USB-C data cable** | Flashing and charging; use an existing cable that carries data. | Use an existing data cable, or [Amazon search — USB-C data cable](https://www.amazon.com/s?k=USB-C+data+cable&tag=beywatchgg-20) |
| 1 set | **Printed M5 + sensor mounts** | Bundled public packages: QRE1113 or TCRT5000 sensor v102, paired with Top Mount v2 (QRE only) or Sidecar v2. Download the sensor and matching mount pair separately. | [Print files](HARDWARE_VERSIONS.md#current-downloads) |
| See below | **M2 machine screws** | Head style and length depend on the mount revision. | [Amazon — ordered MEIYYJ countersunk assortment](https://www.amazon.com/dp/B07HC3LQYS?tag=beywatchgg-20); pan-head sizes below |
| Small amount | **1.75 mm PETG filament — recommended** | Prefer PETG for printed mounts, especially flexing clips, snap arms and parts that receive screws. Use the matching PETG 3MF where supplied. For a PLA-only release, open its saved 3MF as a project, select your printer and PETG profile, retain all modifier/blocker objects, and reslice; do not print its saved PLA G-code with PETG. PLA projects remain available. | Use existing PETG, a printing service, or [Amazon search — PETG filament](https://www.amazon.com/s?k=1.75mm+PETG+filament&tag=beywatchgg-20) |

Links marked **ordered**, plus the SparkFun, SinLoon, ELEGOO and uxcell links, match the project-related product listings from purchase history. Cable, filament and pan-head links marked **Amazon search** are general supplies, not exact ordered products. Check the selected variant and pack contents; purchasing a component does not establish printed fit.

### Screws, connectors and tools

The ordered MEIYYJ assortment includes M2×4/5/6/8 **flat/countersunk heads**.
Bundled v102 sensor clips need no clip screws.
Use the specified **pan-head** pieces for the bundled v2 mounts: [Amazon search — M2×4 pan-head](https://www.amazon.com/s?k=M2x4+pan+head+machine+screws&tag=beywatchgg-20) · [Amazon search — M2×6 pan-head](https://www.amazon.com/s?k=M2x6+pan+head+machine+screws&tag=beywatchgg-20).

| Assembly | Screws required |
| --- | --- |
| QRE1113 v102 sensor | 2 × **M2×4 pan-head** for the sensor retainer; captive clips need no screws. |
| Top Mount v2 | 2 × **M2×4 pan-head** from below into M5, plus 4 × **M2×6 pan-head** into the QRE base; M5 snap cover snaps on. |
| Sidecar v2 | 2 × **M2×4 pan-head** into M5 secondary back holes; M5 snap cover snaps on. |
| TCRT5000 v102 sensor | 1 × **M3 board screw** (M3×6 CAD reference); captive clips need no screws. |

If your sensor has bare pads, add a **3-pin 2.54 mm header** or solder the three wires directly. The ordered [uxcell right-angle male header strip](https://www.amazon.com/dp/B01461DQ6S?tag=beywatchgg-20) can be cut to three pins; check header direction and cable clearance against the printed seat. You will also need a small screwdriver, cutters, and a soldering iron/solder if attaching bare wires or headers.

Use a **Beyblade X string launcher with the supplied removable clip/port interface**. The interface derives from Migbello's BP Gear Port Connector; check retention and optical access on your actual launcher before a pull. No universal launcher-fit claim is made by these CAD files.

Wire **AO/OUT → G1 / GPIO1**, **VCC → 3V3 / 3V3_L2**, **GND → GND**. DO is unused. The Grove red lead supplies 5 V; use the Hat 3.3 V pin for these builds. [QRE build/wiring](BUILD.md) · [TCRT build/wiring](TCRT5000.md) · [Sensor and hardware versions](HARDWARE_VERSIONS.md).

