# Firmware 0.13.7

Runtime: `0.13.7-sticks3-accel-16g-pm` · M5StickS3 K150 · ESP32-S3.

- Tilt replay no longer shows "Tilt out of range" on hard pulls: the accelerometer now uses its ±16 g range (hard pulls peak at 8–13 g).
- A pull that reaches the gyro's 2000 °/s hardware limit keeps its replay, marked **Tilt approx.**, instead of showing nothing.
- IMU samples are read from the BMI270 FIFO. The live level dot and recordings no longer drop out every few seconds at rest or when history and diagnostics are saved.
- Startup checks the IMU axis mapping and scale.
- A single click of the side power key saves pending pulls and powers off instead of resetting the device.
- Serial diagnostics report peak acceleration/rotation per pull and IMU FIFO health.

Everything in 0.13.5 is retained: both saved sensor profiles, the 1,000 RPM launch floor, motion-aware idle, CPU power management, history, themes, effects, optional sound, Top/Sidecar mounting, Wipe all and USB backup/restore. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged from 0.13.5.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.13.7.md).
