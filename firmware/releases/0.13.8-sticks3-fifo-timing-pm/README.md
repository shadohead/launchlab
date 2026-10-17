# Firmware 0.13.8

Runtime: `0.13.8-sticks3-fifo-timing-pm` · M5StickS3 K150 · ESP32-S3.

- Settings, history and diagnostic saves wait for any IMU read in progress before writing flash; if the IMU stays busy the save is retried later instead of risking corrupted motion data.
- Motion samples are timestamped when the sensor buffer is read, so tilt replay stays aligned with the optical launch after a save.
- Startup frees the internal I2C bus if a reset interrupted a sensor read, so the IMU and power chip come back without unplugging the battery.

Everything in 0.13.7 is retained: ±16 g tilt capture, Tilt approx. on gyro full scale, FIFO acquisition, single-click power off, both sensor profiles, history, themes, effects, sound, mounting, Wipe all and USB backup/restore. Ordinary updates write only the application at `0x10000`; the bootloader and partition table are unchanged.

The shipping application is the exact installed and read-back image. Source is frozen in `source/` and the compiled-source ZIP. See [build context](../../../docs/FIRMWARE_0.13.8.md).
