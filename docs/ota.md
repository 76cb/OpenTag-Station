# OTA, rollback, and recovery

The authenticated local A/B updater documented here is the normal update path
after OpenTag Station is running. The separate HTTPS
[browser flasher](web-flasher.md) installs or recovers a complete serial factory
image over USB; it is not an OTA replacement and may erase configuration or
calibration.

## Phase 11 release status

Phase 11 re-audits OTA ownership, upload bounds, lifecycle exclusion, durable
write order, first-use metadata recovery, and every activation/candidate power
cut boundary. The complete A/B, rollback, interruption, browser-reconnect, and
resource matrix remains UNVERIFIED in
[release-validation.md](release-validation.md).

## Phase 10 boundary and validation levels

Phase 10 implements local browser upload, inactive-slot staging, digest and
image validation, explicit activation/reboot, pending-candidate health
confirmation, and ESP-IDF rollback integration. It does not add a remote URL
updater, a release channel, or firmware signing.

Keep three proof levels distinct:

- **Portable host-tested:** `UpdateManager`, boot-health policy, lifecycle gate,
  record checksum/reconciliation, state transitions, size/digest failures,
  inactive-slot selection through fakes, activation preconditions, rollback
  decisions, API parsing/serialization, authentication, and idempotency.
- **Embedded firmware-compiled:** the fixed-buffer FreeRTOS owner, HTTP upload
  stream, NVS record store, mbedTLS SHA-256, ESP-IDF partition/image/OTA calls,
  boot integration, browser assets, and device lifecycle wiring are compiled
  and linked for `wt32-sc01-plus`.
- **Physical validation pending:** neither host execution nor a successful link
  proves real flash behavior, bootloader state transitions, reset timing, power
  loss recovery, or browser/network behavior. The hardware matrix below remains
  a release gate.

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

## Architecture and ownership

The portable `UpdateManager` owns states, preconditions, progress, validation
decisions, activation ordering, candidate confirmation, rollback decisions, and
boot reconciliation. It reaches hardware only through three interfaces:

- `IOtaPlatform` for partition status, inactive-slot write/abort/finalize,
  staged-image validation, activation, confirmation, and rollback;
- `ISha256` for a rolling digest; and
- `IUpdateRecordStore` for durable generation reservation and records.

The embedded `OtaWorker` is the only caller of that policy during normal
runtime and the only OTA flash/activation owner. Its queue and four command
slots are fixed-depth; each slot reserves a 4096-byte handoff buffer (16 KiB
total). A web request may copy at most one chunk into a reserved slot and waits
for the owner result before reusing it. LVGL, backend workers,
and the normal JSON router never write firmware.

OTA metadata lives in the isolated `opentagOta` NVS namespace. Each fixed-size
record has a schema, checksum, monotonic generation, operation ID, state, target
slot, exact length/progress, both digests, candidate metadata, timestamps, and
activation flags. A generation is durably reserved before a writer opens and
cannot wrap or be reused. Progress is checkpointed every 512 KiB and at the
declared end; transition boundaries are also persisted.

## State model

| State | Meaning |
|---|---|
| `idle` | No upload or candidate lifecycle is active. |
| `upload_receiving` | Metadata and ownership passed; inactive writer and digest are open. |
| `writing` | Bounded chunks are being written and hashed. |
| `validating` | Exact byte count and SHA-256 plus ESP/OpenTag image checks are running. |
| `ready_to_reboot` | Image is valid in the inactive slot. It is unactivated until explicit reboot; `activated` and the boot-partition fields show which side of that boundary the device is on. |
| `reboot_pending` | Activation intent, boot-slot change, and reboot record are durable. |
| `candidate_boot` | The selected image is running as an unconfirmed bootloader candidate. |
| `validating_candidate` | The shared 30-second local-health window is active. |
| `confirmed` | ESP-IDF marked the running candidate valid and cancelled rollback. |
| `rollback_pending` | The candidate was marked invalid and rollback/reboot was requested. |
| `rolled_back` | Boot reconciliation observes the prior image with the candidate recorded invalid. |
| `failed` | A bounded error records the latest rejected or interrupted update. |

`ready_to_activate` remains in the schema-one enum only to reconcile records
written at a former internal cut point. Boot initialization normalizes it to
the public, cancelable, unactivated `ready_to_reboot` state; a fresh upload does
not emit it.

## API, authentication, and upload bounds

`GET /api/v1/update` is a safe read snapshot. It reports revision/generation,
state, operation, running/boot/inactive labels and sizes, current/candidate
build identity, declared and calculated digests, progress, validation and
activation flags, running ESP image state, rollback availability/result,
capabilities, and one bounded last error. It does not expose flash addresses in
the public response.

`POST /api/v1/update/upload` is a dedicated binary route, not a 16 KiB JSON
request. It requires:

- `Authorization: Bearer <current-token>` when a token is configured;
- `X-OpenTag-Request: web`;
- a valid `Idempotency-Key`;
- `Content-Type: application/octet-stream`;
- an explicit nonzero `Content-Length` no larger than 5 MiB or the actual
  inactive partition;
- `X-OpenTag-Image-SHA256` as exactly 64 lowercase hexadecimal characters; and
- `X-OpenTag-Expected-Generation` as canonical nonnegative decimal. Generation
  zero is the initial no-record snapshot; successful uploads reserve a new
  positive monotonic generation.

The HTTP owner reads at most 4096 bytes at a time. Five seconds without receive
progress or 180 seconds total aborts the session. A disconnect, short body,
overrun, owner failure, or expired deadline closes the ESP-IDF handle and leaves
the running slot bootable. Duplicate upload metadata under the same idempotency
key returns the original operation instead of opening a second writer; reuse of
the key with different metadata conflicts.

`POST /api/v1/update/reboot` and `POST /api/v1/update/cancel` use the normal
bounded JSON path. Their body identifies the exact upload operation, expected
generation, expected digest, and confirmation string:

```json
{
  "upload_operation_id": 91,
  "expected_generation": 7,
  "expected_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "confirmation": "REBOOT INTO UPDATE"
}
```

Cancel requires `CANCEL UPDATE`. Reboot and cancel also require source and
idempotency headers, plus bearer authentication when a token is configured. The
device rechecks all three immutable preconditions, so
an old browser tab, operation receipt, or replay cannot control a newer image.
Cancel is accepted only while receiving/writing or while a validated image is
still unactivated.

## Image validation and activation boundary

Preflight rejects zero length, more than 5 MiB, or more than the current
inactive partition. `esp_ota_get_next_update_partition(running)` chooses the
target; the adapter resolves the descriptor back to an application partition
and rejects any target that is not that exact inactive slot or equals the
running slot. Clients never select a partition, address, label, filesystem path,
or erase range.

During streaming, mbedTLS calculates SHA-256 over the exact bytes written.
Finish requires the complete declared count and a constant-time comparison with
the declared digest. `esp_ota_end` must then accept the image. A second staged
verification uses `esp_image_verify` and requires:

- exact verified image length and valid ESP image magic;
- ESP32-S3 chip ID;
- the appended ESP image hash flag; and
- a sane fixed 256-byte OpenTag manifest with project `OpenTag Station` and
  hardware ID `wt32-sc01-plus-rev-a`.

The manifest also supplies version, Git SHA, build date, and platform, while the
ESP application descriptor supplies the IDF version. Version, Git SHA, build
date, and IDF version are reported. Platform identity remains embedded for
offline inspection but is not enforced or exposed by Phase 10. Project and
hardware identity are enforced. Phase 10 does not enforce SemVer order,
downgrade prevention, release channel, minimum bootloader, or
configuration-schema compatibility.

Only after every check succeeds is the record saved as `ready_to_reboot`, with
`validation_passed=true`, `activated=false`, and the boot partition unchanged.
The explicit reboot mutation first saves activation intent, calls
`esp_ota_set_boot_partition` for the same inactive target, saves
`reboot_pending`, and restarts. This ordering leaves enough durable evidence to
reconcile a power cut at every activation boundary.

On the first OTA after a serial flash, ESP-IDF may have no valid otadata entry.
Before selecting the staged image, the adapter selects the already-running
known-good slot and marks that slot valid. If power fails, or mark-valid returns
an error, between those two calls, the durable validated-target and activation-
intent record identifies the narrow recovery cut: boot still equals running,
the staged target is still inactive, and the running image is `NEW` or
`PENDING_VERIFY`. The OTA owner confirms only that pre-existing running slot,
clears the seed intent, and then permits the exact candidate activation to be
retried. It does not confuse that recovery with candidate health confirmation.
If selecting the running slot fails without any observable otadata change, the
running state remains `UNDEFINED`; the same operation/generation/digest may
retry seeding, but cancellation stays forbidden while durable activation intent
is present.

## Installation, candidate health, and rollback

1. Read the current update generation and inactive-slot capability.
2. Hash the selected `.bin` in the browser and start one authenticated upload.
3. Reserve a durable generation and open only the inactive partition.
4. Stream fixed chunks while writing and calculating SHA-256.
5. Finalize only after exact length, digest, ESP image, chip, appended hash,
   project, and hardware validation all succeed.
6. Report the image as validated and installed but unactivated.
7. Require a second explicit authenticated/idempotent reboot confirmation.
8. Persist activation intent, select the candidate boot partition, persist
   `reboot_pending`, and restart.
9. Reconcile the running pending image against its durable record and evaluate
   the unified local-health policy for 30 seconds.
10. Mark the candidate valid and cancel rollback only after health succeeds;
    otherwise mark it invalid and request rollback/reboot.

The pinned Arduino-ESP32/ESP-IDF build enables bootloader application rollback.
The adapter uses `esp_ota_get_state_partition`,
`esp_ota_mark_app_valid_cancel_rollback`,
`esp_ota_check_rollback_is_possible`, and
`esp_ota_mark_app_invalid_rollback_and_reboot`. A candidate is never marked
valid at startup. If it resets without confirmation, the rollback-enabled
bootloader retains the previous valid slot as the recovery image.

The shared 30-second health policy requires:

- storage ready;
- configuration initialized or explicitly safely degraded;
- application state initialized without a fatal startup error;
- display and UI task ready;
- configuration and backend owner tasks running;
- scale commands and scale task running;
- network owner and local web server running; and
- device-control and OTA owners running.

`backend owner running` does not mean Spoolman or FilaBridge must be online.
Backend outages do not trigger rollback. NFC is deliberately wiring-gated and
is not a health prerequisite. A factory-reset recovery marker produces its own
health result; OTA waits for that recovery owner rather than calling it a bad
candidate.

Boot reconciliation treats the ESP bootloader state as authoritative while
requiring matching local intent:

- a receiving/writing/validating record after restart becomes a failed
  interrupted upload;
- a pending running image without a matching validated, activated operation
  record fails closed into the real rollback path;
- an activation-intent record whose target is the boot partition but not yet
  running remains at the safe reboot boundary;
- the exact first-update rollback-seed cut confirms the pre-existing running
  image, preserves the validated inactive target, and returns to the
  unactivated reboot boundary;
- a valid running target from a candidate record becomes `confirmed`; and
- a recorded target reported as the last invalid partition becomes
  `rolled_back`.

## Configuration, reset, and concurrency

Configuration, calibration, LittleFS, and the factory-reset recovery marker are
outside both application slots and are not erased by OTA. Phase 10 adds no
configuration setting. A candidate runs the existing additive configuration
migration path; configuration initialized or safely degraded is required for
health, but an external backend is not.

A single generation-token lifecycle gate serializes generic reboot, factory
reset, OTA upload/activation, and candidate validation. Factory reset and OTA
therefore cannot erase or select boot state concurrently. Only the holder of
the current lease can release it, so stale completion cannot unlock a newer
operation. The OTA owner does not hold LVGL, a backend adapter, the scale
worker, or the configuration writer while flash chunks are written.

## Recovery paths

- Use local browser upload when the running image, Wi-Fi, and web owner start.
- Cancel an incomplete or validated-but-unactivated upload; the running slot
  remains selected and bootable.
- A synchronous begin timeout is fail-closed: if the queued owner later opens
  the writer, it cancels that exact generation before releasing the lifecycle.
- If OTA boot reconciliation cannot complete at startup, device-control and
  network mutation owners remain unavailable rather than accepting a reboot,
  reset, or upload against unknown boot state.
- After activation, allow candidate confirmation or bootloader rollback to
  decide the running slot. Do not attempt another upload during validation.
- Use USB/PlatformIO flashing when the local web service cannot start.
- Use bootloader serial flashing for a fully non-booting unit.
- Export and restore configuration/calibration only through their existing
  validated paths; OTA does not copy or erase them.

During recovery, inspect `GET /api/v1/update` after each reconnect. A browser
receipt is not proof of success: `ready_to_reboot`, `reboot_pending`,
`candidate_boot`, `validating_candidate`, `confirmed`, and `rolled_back` are
distinct outcomes.

## Required hardware-in-the-loop matrix

| Scenario | Expected result | Status |
|---|---|---|
| Valid A → B upload and reboot | B is written only to inactive slot, boots pending, survives 30 seconds, and becomes valid | Physical test pending |
| Valid B → A update | Slot roles reverse without touching configuration/calibration | Physical test pending |
| Wrong project/hardware or malformed ESP image | Upload fails before activation; current image remains bootable | Physical test pending |
| Oversize, short body, bad SHA-256, idle timeout, disconnect | Writer aborts and current boot slot remains unchanged | Physical test pending |
| Power loss during upload | Restart reconciles an interrupted upload as failed; prior image boots | Physical test pending |
| Power loss after validation but before reboot | Validated image remains unactivated/cancelable | Physical test pending |
| Power loss after activation intent/boot selection | Durable reconciliation safely resumes the exact candidate boundary | Physical test pending |
| Candidate fatal startup or intentional crash/reset | Bootloader restores the previous valid slot | Physical test pending |
| Repeated candidate crash/watchdog reset | Candidate never self-confirms; previous slot recovers | Physical test pending |
| Reset during candidate health window | Pending image is not marked valid prematurely | Physical test pending |
| Spoolman and FilaBridge offline | Local owners remain healthy and candidate can confirm | Physical test pending |
| NFC unavailable by design | Candidate can confirm | Physical test pending |
| Factory reset raced with upload/reboot | Exactly one lifecycle lease wins; no mixed reset/OTA state | Physical test pending |
| Browser loses connection during reboot | Client reconnects and resumes candidate/confirmation/rollback status | Physical test pending |

## Security and known limitations

- SHA-256, the appended ESP image hash, and the OpenTag manifest detect damage
  and identity mismatch; they do not authenticate who produced the image.
- Phase 10 has no signed-image or public-key release chain. Only trusted build
  artifacts should be installed.
- The embedded local server uses HTTP, not HTTPS. The bearer token and firmware
  body are visible to an on-path LAN observer. Use a trusted isolated network.
- There is no remote URL updater and no TLS-validation bypass was added.
- Phase 10 enforces project and WT32 hardware identity but not version ordering,
  downgrade prevention, channel policy, minimum bootloader, or configuration
  schema range.
- The current Update UI hashes the selected browser file before upload; this
  browser-side read does not change the station's fixed-memory streaming bound.
- Physical flash, bootloader, power-loss, reset-loop, and browser reconnect
  behavior remain unverified until the matrix above is executed on a WT32-SC01
  Plus.
