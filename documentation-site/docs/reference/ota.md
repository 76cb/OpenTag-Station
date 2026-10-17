# Firmware update internals

How the station stores, checks and starts a firmware image uploaded over the network, and what it
does when the new firmware does not come up. Steps for doing an update:
[Update the firmware](../installation/ota.md). API fields: [REST API](api.md).

## Scope

- The update writes one application slot. It never changes the bootloader or the partition
  table.
- Settings, Wi-Fi credentials and scale calibration are stored outside the application slots and
  are kept.
- Images are not signed and the upload uses plain HTTP. The checks below detect a damaged or
  wrong file; they do not prove who built it. Install only files from the project's releases, on
  a network you trust, and set an access token.
- There is no download-from-URL updater, no version-order or downgrade check, and
  `device.update_channel` has no effect.

## Flash layout

16 MiB flash, from
[`partitions.csv`](https://github.com/76cb/OpenTag-Station/blob/main/partitions.csv):

| Partition | Offset | Size | Holds |
|---|---|---:|---|
| `nvs` | `0x9000` | 20 KiB | Scale calibration copy, update record, reset markers |
| `otadata` | `0xe000` | 8 KiB | Which application slot starts |
| `app0` | `0x10000` | 5 MiB | Application slot |
| `app1` | `0x510000` | 5 MiB | Application slot |
| `littlefs` | `0xa10000` | 5.8125 MiB | `/configuration.json`, its backup and staging file; the tag-writer recovery record `/writer-recovery.bin` and its staging file |
| `coredump` | `0xfe0000` | 128 KiB | Crash dump |

One slot runs; an upload always goes to the other one. The client cannot choose the slot. The
browser page and its assets are part of the application image, not of `littlefs`.

## Accepting an image

An upload can start in state `idle`, `failed`, `confirmed` or `rolled_back`, and not while the
running firmware is itself still waiting to be confirmed.

| Check | When | Failure message |
|---|---|---|
| Length is 1 byte to 5 MiB and fits the other slot | Before writing | "Firmware image size is zero or exceeds the inactive OTA slot" |
| `X-OpenTag-Expected-Generation` equals the current `generation` | Before writing | "OTA generation changed before the upload began" |
| Bytes received equal the declared length | End of upload | "Firmware upload ended before the declared byte count" |
| SHA-256 of the received bytes equals `X-OpenTag-Image-SHA256` | End of upload | "Firmware SHA-256 does not match the declared digest" |
| Valid ESP32-S3 application image with its appended hash | After writing | "Staged image size, chip header, or appended hash is invalid" / "Firmware image structure validation failed" |
| Embedded hardware ID is `wt32-sc01-plus-rev-a` | After writing | "Firmware image target is incompatible with this station" |
| Embedded project name is `OpenTag Station` | After writing | "Firmware image project identity is incompatible" |

A failed check leaves the running firmware selected. A check that fails before writing rejects
the request and leaves `state` unchanged; a check that fails later puts the update in state
`failed`.

After a successful upload the image is stored but not selected (`ready_to_reboot`). Nothing
changes until `POST /update/reboot`; `POST /update/cancel` discards the image instead.

## States

`state` in `GET /api/v1/update`:

| State | Meaning |
|---|---|
| `idle` | No update in progress. |
| `upload_receiving` | Upload accepted; the other slot is open for writing. |
| `writing` | Image data is being written and hashed. |
| `validating` | Upload complete; the checks above are running. |
| `ready_to_reboot` | Image is valid and stored. Can be started or cancelled. |
| `reboot_pending` | The new slot is selected and the station is restarting. |
| `candidate_boot` | The new firmware is running and not yet confirmed. |
| `validating_candidate` | The health window is running. |
| `confirmed` | The new firmware passed the health check and is now permanent. |
| `rollback_pending` | The new firmware failed the health check; the station is returning to the previous one. |
| `rolled_back` | The previous firmware is running again; the new image is marked invalid. |
| `failed` | The last upload or update was rejected or interrupted; `last_error` says why. |

The enum also contains `ready_to_activate`. It is not produced by an upload; a stored record in
that state is read as `ready_to_reboot`.

`capabilities` in the same response reports `upload_available`, `cancel_available`,
`reboot_available`, `maximum_image_bytes` and `owner_ready`.

## Health window and rollback

New firmware is never confirmed at start-up. It must run for 30 seconds, and at that point all of
the following must be up:

- storage, and configuration either loaded or in its safe degraded mode
- the application, display and touchscreen task
- the configuration, backend, scale, network, device-control and update tasks
- the scale command queue
- the local web server

Not required: Wi-Fi being connected, Spoolman or FilaBridge being reachable, the NFC reader.

| Result | What happens |
|---|---|
| All requirements met at 30 seconds | The firmware is confirmed (`confirmed`). Rollback is no longer possible. |
| A requirement is missing at 30 seconds, or start-up reports a fatal error | The image is marked invalid and the station restarts into the previous firmware (`rolled_back`). |
| The new firmware crashes or is reset before it is confirmed | Intended: the ESP-IDF bootloader's app rollback starts the previous firmware, and the station then reports `rolled_back`. No automated test covers the bootloader step; step 31 of the hardware acceptance in [docs/releasing.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/releasing.md#hardware-acceptance) checks it. |
| A factory reset is waiting to finish | The reset completes first; the health decision waits for it. |

## Interruptions

| Power lost or connection dropped | State after restart | Next step |
|---|---|---|
| During upload or validation | `failed`: "OTA upload was interrupted before image validation". Previous firmware runs. | Upload again. |
| After validation, before the restart was requested | `ready_to_reboot`; the image is kept. | Start it or cancel it. |
| After the new slot was selected, before the new firmware started | Still selected. | The next restart starts the new firmware. |
| During the 30-second health window | Intended: previous firmware runs (bootloader rollback, checked by the hardware acceptance); `rolled_back`. | Upload again. |
| The stored record does not match what is running | `failed`: "The previous update did not complete; upload it again." or a similar message. | Upload again. |

A browser that loses its connection during the restart should read `GET /update` again once the
station is back; the receipt of the restart request is not proof that the update succeeded.

## Limits

| Item | Value |
|---|---|
| Largest image | 5 MiB (5,242,880 bytes) |
| Upload stall timeout | 5 seconds without data |
| Upload total timeout | 180 seconds |
| Health window | 30 seconds |
| Concurrent updates | One. Reboot, factory reset and update restart are refused while a tag or Spoolman operation is queued or running. |
| Update record | NVS namespace `opentagOta`; survives restarts and updates |

## Recovery

| Situation | Path |
|---|---|
| Station runs and is reachable | Upload again over the network. |
| New firmware failed | Nothing to do; the station returned to the previous firmware by itself. Check `last_error`. |
| Station does not start, or the web page is unreachable | Reinstall over USB: [Install the firmware over USB](../installation/web-flasher.md). |
| Settings are suspect after recovery | [Factory reset and USB recovery](../troubleshooting/recovery.md) |

How release images and the installer are built:
[releasing.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/releasing.md),
[web-flasher.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/web-flasher.md).
