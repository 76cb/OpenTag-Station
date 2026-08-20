# OTA, rollback, and recovery

## Current Phase 9 boundary

Phase 9 implements only the read-only `GET /api/v1/update` status placeholder.
It reports that OTA is unavailable in this phase so the browser can render the
boundary honestly. There is no upload or install mutation, manifest fetch,
image hashing, OTA-partition write, boot-slot selection, pending-image health
window, A/B transition, or rollback implementation.

Authenticated reboot and factory-reset commands are separate device-control
operations. They require explicit confirmation and execute through a bounded
owner queue; they do not install firmware. Portable route, authorization, and
control policy is host-tested; the embedded HTTP/API context and device-control
worker are firmware-compiled. Browser, restart, reset-recovery, and target-LAN
behavior still require physical validation.

## Partition layout

The initial 16 MiB layout reserves:

| Partition | Size | Purpose |
|---|---:|---|
| NVS | 20 KiB | Persistent settings/calibration metadata |
| OTA data | 8 KiB | Boot selection and pending state |
| app0 | 5 MiB | Application slot A |
| app1 | 5 MiB | Application slot B |
| data | 5.8125 MiB | LittleFS-compatible web assets, backups, logs |
| coredump | 128 KiB | Crash diagnostics |

Offsets and exact sizes are in `partitions.csv` and must be frozen before the
first field release; changing a partition table is a special recovery update,
not a routine OTA.

## Planned Phase 10 provider-neutral manifest

GitHub Releases and browser upload will feed the same validator. A versioned
manifest will contain version/channel, hardware allow-list, configuration schema
range, minimum bootloader, image size and SHA-256, release notes URL, and tested
upstream revisions. Unknown fields will be tolerated. Signing metadata is
reserved; unsigned development images must never be silently accepted on a
stable channel.

## Planned Phase 10 installation flow

1. Fetch/upload manifest under bounded size and timeout limits.
2. Validate schema, channel, semantic version, hardware ID, and app-slot size.
3. Stream the image to the inactive OTA partition while hashing.
4. Reject incomplete length or SHA-256 mismatch.
5. Mark the inactive image pending and reboot.
6. Initialize storage, config, event system, display/UI, scale, and NFC platform.
7. Remain watchdog-stable for the health window.
8. Mark the image valid. Backend connectivity is reported separately and is not
   required for validity.

A crash loop, watchdog reset, invalid image, or failure of fundamental local
services before validation leaves the prior image eligible for automatic
rollback. A failed ST25R3916B tag scan is not by itself a reason to roll back;
an uninitializable enabled driver may be, based on the release's declared core
hardware policy.

## Planned configuration interaction

Configuration is not stored in an app partition. The planned updater will stage
additive migrations after boot and commit them only when safe. Pre-migration
snapshots will be retained until the new image is valid. Phase 10 diagnostics
will expose both running/pending image and configuration schema.

## Recovery paths

- Current: USB/PlatformIO flashing for first install and firmware recovery.
- Current: bootloader serial flashing for a fully non-booting unit.
- Current: configuration/calibration export and restore after explicit
  validation.
- Planned Phase 10: local browser upload for normal recovery when Wi-Fi and web
  services start.
- Planned release artifact: USB/WebSerial flashing for damaged data/network.

Recovery documentation and deliberately broken-image rollback tests are release
gates, not post-release tasks. Until Phase 10 implements and validates the
installer, USB/serial flashing remains the available firmware recovery path.
