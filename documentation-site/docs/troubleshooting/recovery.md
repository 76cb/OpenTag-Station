# Factory recovery

Return an unbootable station to normal production firmware.

## Before you start

Correct board, stable USB power/data and a trusted production factory bundle. Recoverable settings should be backed up before erasing.

## Steps

1. First record boot output/version and check supply/cable faults. A brownout is not repaired by firmware replacement.
2. Close applications holding the serial port and follow the manufacturer’s download-mode procedure if automatic connection fails.
3. Use the production-only [USB installer](../installation/web-flasher.md), review erase behavior and wait for complete verification.
4. Reboot, confirm normal firmware version, restore compatible nonsecret configuration and re-enter missing credentials locally.
5. Validate touch, reader and scale; recalibrate if configuration/calibration was erased.

## Expected result

The station boots normal OpenTag Station and completes setup using the supported hardware profile.

## If it fails

Do not flash arbitrary test targets or feed a factory image into OTA. If an outstanding tag journal was erased, do not assume the partially modified tag is safe; preserve it for investigation.
