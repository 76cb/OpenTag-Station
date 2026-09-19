# Local OTA updates and rollback

**Update Existing Station** stages only the normal application firmware in the inactive app partition. It preserves LittleFS configuration, NVS, Wi-Fi, Spoolman/FilaBridge and printer settings, and scale calibration. It does not flash the partition table, bootloader, LittleFS image or factory image.

## Before you start

Reachable station, correct production application binary and SHA-256 from a trusted build, stable power and a configuration backup. Hardware rollback/interruption acceptance remains pending.

## Steps

1. Open Settings → Advanced and the firmware update controls. Inspect current build, slot and lifecycle status.
2. Choose the production **application** binary (`firmware.bin` or the release’s versioned application file), not the factory image or an archive.
3. Supply/review the expected size and SHA-256 as requested by the updater, then start the staged upload.
4. Wait for image/digest validation in the inactive slot. An uploaded image is not automatically an accepted boot candidate.
5. Use the explicit activation/reboot action, reconnect the browser and inspect candidate health/current version.
6. If the candidate fails health confirmation, inspect rollback status and use [USB recovery](web-flasher.md) if necessary.

## Expected result

The inactive-slot image passes validation and the activated candidate satisfies boot-health policy before acceptance. Compatible configuration remains available outside the two application slots.

## If it fails

Digest/size/board mismatch refuses activation. Do not power-cycle repeatedly through an unknown update state or upload a factory image as an application. SHA-256 detects corruption but firmware is not publisher-signed. Detailed lifecycle and power-loss boundaries are in the [OTA reference](../reference/ota.md).
