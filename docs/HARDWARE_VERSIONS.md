# Hardware and sensor versions

LaunchLab Mini uses an intact M5StickS3 K150, one analog sensor and printed attachments.
The current shared sensor revisions are QRE1113 v102 and TCRT5000 v102.
The current mount families are Top Mount v2 and Sidecar v2.
Sensor, mount-family and firmware versions are independent.

## Current kits

| Kit | Sensor | M5 mount pair | Firmware |
| --- | --- | --- | --- |
| Compact | Analog SparkFun QRE1113, sensor attachment v102 | Top Mount v2 M5 snap cover and M5 cradle | Current 0.13.5, QRE / Standard profile |
| Compact Sidecar | Analog SparkFun QRE1113, sensor attachment v102 | Sidecar v2 M5 snap cover and M5 cradle | Current 0.13.5, QRE / Standard profile |
| Standard Sidecar | TCRT5000 / LM393 analog AO, sensor attachment v102 | Sidecar v2 M5 snap cover and M5 cradle | Current 0.13.5, TCRT5000 profile |

Sidecar describes a separate sensor attachment and M5 display attachment beside it.
Top Mount holds the M5 above the QRE sensor base; no TCRT Top Mount kit is supplied.
Firmware 0.13.5 supports the current Top and Sidecar layouts.
See [attribution](../THIRD_PARTY_NOTICES.md).
Choose **Settings → Mounting → Top or Sidecar** to match the physical layout; display flip is a separate setting.
Saved CAD, STL readback and print-project audits passed; physical fit, retention, fatigue, optical alignment and absolute RPM calibration remain unverified for these current combinations.

## Current downloads

| Part set | Exact source package | P1S PLA source plate | P1S PETG source plate |
| --- | --- | --- | --- |
| QRE1113 v102 launcher clamp base with captive clips and sensor retainer | [STL/source ZIP](../site/public/hardware/downloads/compact-base-v102.zip) | [PLA](../site/public/hardware/downloads/compact-base-v102.3mf) | Not supplied |
| TCRT5000 v102 mount with captive clips | [STL/source ZIP](../site/public/hardware/downloads/standard-sensor-v102.zip) | [PLA](../site/public/hardware/downloads/standard-sensor-v102.3mf) | Not supplied |
| Top Mount v2 M5 snap cover and M5 cradle | [STL/source ZIP](../site/public/hardware/downloads/top-mount-v2.zip) | [PLA pair](../site/public/hardware/downloads/top-mount-v2-pla.3mf) | [PETG pair](../site/public/hardware/downloads/top-mount-v2-petg.3mf) |
| Sidecar v2 M5 snap cover and M5 cradle | [STL/source ZIP](../site/public/hardware/downloads/sidecar-v2.zip) | [PLA pair](../site/public/hardware/downloads/sidecar-v2-pla.3mf) | [PETG pair](../site/public/hardware/downloads/sidecar-v2-petg.3mf) |

Use one sensor plate plus the matching mount-pair plate.
These are exact saved source projects, not a newly sliced combined full-kit plate.
Mount plates contain both mating pieces; sensors remain separate.
Re-slice for your own printer and loaded filament.
PETG is recommended for printed mounts, especially flexing clips, snap arms and parts that receive screws. Select the matching PETG project where supplied. For the PLA-only QRE1113 v102 and TCRT5000 v102 sensor projects, open the saved PLA 3MF as a project, select the correct printer and PETG profile, retain both HINGE SUPPORT BLOCKER objects and per-object support settings, and reslice. Importing only the STL loses these blocker objects; do not print the saved PLA G-code with PETG. PLA projects remain available. This material recommendation does not change the geometry, filenames, screw specifications or saved profiles, or establish dimensional accuracy or screw-cycle durability.
Sensor source plates use P1S PLA, a 0.4 mm nozzle, 0.2 mm layers and retained hinge support blockers.
Both current mount pairs have audited P1S PLA and PETG source projects.
Both v2 M5 snap covers are byte-identical to their family's v1 M5 snap cover; existing v1 M5 snap cover owners need only the v2 M5 cradle.

## Assembly and fasteners

| Part | Assembly detail | Fasteners |
| --- | --- | --- |
| QRE1113 v102 | Uniform-height hinge blocks with captive print-in-place launcher clips; secure the analog board with its sensor retainer. | 2 × M2×4 pan-head; no clip screws |
| Top Mount v2 | Two flexible M5 cradle arms snap into the removable M5 snap cover; press both arms inward to release. | 2 × M2×4 M5-to-cradle, 4 × M2×6 cradle-to-QRE-base |
| Sidecar v2 | M5 cradle includes the native grip head and two integral snap arms; thread wires through the enclosed left-side exit before fitting the M5 snap cover. | 2 × M2×4 into the M5 secondary back holes |
| TCRT5000 v102 | Measured sensor cradle with captive launcher clips; no top sensor retainer. | One existing M3 board screw (M3×6 CAD reference); no clip screws |

Digital checks do not replace dry-fit checks of your device, launcher, connectors and screws.
Keep the factory M5 enclosure intact and verify sensor AO, supply voltage and optical alignment using [BUILD.md](BUILD.md) or [TCRT5000.md](TCRT5000.md).
The [hardware provenance](../site/public/provenance/hardware.json) pins source releases and exact download hashes.
The [assembly validation](../site/public/provenance/hardware-assembly-validation.json) pins source meshes, transforms and material/explosion metadata.

## Historical downloads

Earlier v0.19, v0.33, v0.40, v0.41-v0.50 and v0.68 snapshots remain unchanged.
They are historical alternatives, not the parts shown in the current kit images.
The original v0.40 plate contains its original six parts; it has not been relabeled as a v102/v2 full kit.
The old TCRT 0.2.0 firmware remains a separate earlier acquisition build; see its [restore guide](TCRT5000.md#restore-the-saved-old-sensor-application).

## Revision history

Every row links to a frozen local snapshot with source files, manufacturing STLs, reference meshes, original README, attribution and digital reports. Whole M5 carriers are mesh geometry: their STEP files contain only the added pads, cutters or ramp named in that revision, not the entire carrier. TCRT v0.43 includes full per-part STEP files.

| Version | Family | Changes | Saved print state |
| --- | --- | --- | --- |
| [v0.19](../hardware/LaunchLab_v019_tcrt5000_open_stack/) | Old TCRT5000 | Historical open-stack integrated sensor/M5 mount. | Historical slice; physical fit unverified. |
| [v0.33](../hardware/LaunchLab_v033_flush_QRE/) | Small QRE1113 | Flush sensor base and preserved optical seat. | Included in v0.40 plate. |
| [v0.40](../hardware/LaunchLab_v040_ALL_ITEMS_PRINT/) | QRE + M5 | Complete six-piece plate with right-block M5 carrier. | Saved P1S PLA slice. |
| [v0.41](../hardware/minimal_TCRT_stack_v041/) | Old TCRT5000 | Minimal stacked cradle with removable sensor keeper/M5 dock. | Unsliced CAD prototype. |
| [v0.42](../hardware/minimal_TCRT_sensor_only_v042/) | Old TCRT5000 | Separate sensor-only attachment; M5 kept on its own carrier. | Unsliced CAD prototype. |
| [v0.43](../hardware/minimal_TCRT_split_keepers_v043/) | Old TCRT5000 | Independent left/right screw-on clip caps; smooth narrow lower plate. Six printed pieces. | Unsliced CAD prototype. |
| [v0.44](../hardware/M5_level_support_v044/) | M5 carrier | Opposite-end 0.5 mm supports for stepped device underside. | Unsliced STL. |
| [v0.45](../hardware/M5_recessed_screws_v045/) | M5 carrier | Underside screw-head counterbores, 4.2 mm diameter / 1.4 mm depth. | Unsliced 3MF/STL. |
| [v0.46](../hardware/M5_raised_rectangle_v046/) | M5 carrier | Center locating rectangle projection increased to 1.25 mm. | Unsliced 3MF/STL. |
| [v0.47](../hardware/M5_rotated_rectangle_v047/) | M5 carrier | Center rectangle rotated 90°, length extended. | Unsliced 3MF/STL. |
| [v0.48](../hardware/M5_smooth_rectangle_v048/) | M5 carrier | Sloped center rectangle with 0.3 mm rounded transitions. | Unsliced 3MF/STL. |
| [v0.49](../hardware/M5_soft_ramp_v049/) | M5 carrier | Broader center ramp with 0.8 mm fillets. | Unsliced 3MF/STL. |
| [v0.50](../hardware/M5_equal_supports_v050/) | M5 carrier | Opposite-end supports raised to 2.3 mm; v0.49 geometry retained. | Saved P1S PLA slice; seating unverified. |
| [v0.68](../hardware/M5_cable_hook_v068/) | M5 carrier | Historical separate carrier: side-open channel, two older humps and 6 mm underside cable hook. | Saved P1S PLA slice; physical fit and strength unverified. |

[Machine-readable inventory and SHA-256 hashes](../hardware/versions.json) identify all newly published files. Older versions remain available; no published firmware binary or existing v0.40 geometry was replaced. One v0.42 render had a stale local checksum; its original checksum list is preserved under `provenance/`, and the publication list records the final saved render. All original CAD/STL/STEP hashes matched.

The [bundled hardware images](../site/public/hardware/images/) are rendered directly from these frozen STL meshes. Explosion distances and electronics/fastener references are illustrative. Digital CAD, archive and slicing checks do not establish printed fit, absolute RPM accuracy or launch-load retention.
