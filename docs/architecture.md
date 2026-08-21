# Architecture

Phase 11 audited every runtime owner, queue, lifecycle lease, persistent writer,
and project-created task. The authoritative ownership and 96,256-byte configured
dynamic task-stack inventory are in
[release-validation.md](release-validation.md); changes to task creation or
ownership must update that inventory.

## Design objective

OpenTag Station is a recoverable local appliance, not a single-screen sketch.
The architecture isolates hardware protocols, portable tag formats, backend
contracts, domain policy, and presentation so each can evolve independently.

## Layering

```text
Touchscreen UI        Web UI / local API
         \              /
          Application coordinator + state machine
                       |
        Domain services and normalized models
          /             |               \
 Spoolman adapter  FilaBridge adapter   UpdateManager
          |             |                    |
      HTTP/JSON     HTTP/JSON/WS       OTA owner/platform

 NFC manager  -> OpenPrintTag codec
      |
 NFC-V / ISO15693 protocol
      |
 ST RFAL -> ESP32 RFAL port -> ST25R3916B

 Scale service -> NAU7802 driver
```

Dependency arrows point inward toward stable interfaces. In particular:

- OpenPrintTag parsing has no dependency on RFAL or ST25R3916B.
- ST25R3916B/RFAL has no knowledge of Spoolman, FilaBridge, or LVGL.
- backend adapters produce normalized domain objects and structured errors.
- UI controllers dispatch commands and render state; they do not issue backend
  requests or implement reconciliation policy.

## Runtime model

FreeRTOS tasks will own slow or blocking work:

| Task | Owns | Must not own |
|---|---|---|
| UI | LVGL calls and view state | HTTP, long NFC operations, flash writes |
| Configuration | serialized document commits and setup progress | LVGL calls |
| NFC | RFAL worker, presence state, tag I/O | OpenPrintTag business decisions |
| Scale | ADC sampling, filtering, stability | inventory writes |
| Network | Wi-Fi, DNS, HTTP transport | UI transitions |
| Backend | adapters, cache, assignment verification | LVGL calls |
| Update | inactive-slot writes, digest/image validation, activation, candidate health | web sockets, UI, backend availability policy |

Tasks exchange bounded events/commands. Payload ownership is explicit and
queues have overflow diagnostics. No worker may wait forever; every NFC, DNS,
HTTP, and OTA operation gets a deadline.

Phase 1 activates the UI owner as a pinned FreeRTOS task; it is the only context
allowed to call LVGL. Phase 5 activates a separate scale task on the dedicated external I2C
controller; NAU7802 startup/calibration is bounded, missing hardware is retried,
and snapshots cross into UI diagnostics through atomics. Phase 6 activates the
network task and a bounded configuration worker. UI callbacks enqueue settings
commits; only the configuration worker writes the LittleFS document. Network
status crosses into diagnostics through a locked snapshot. Phase 8 activates a
bounded backend worker: it owns Spoolman/FilaBridge probing, decoded-spool
resolution commands, assignment commands, and exact readback verification. UI
callbacks only enqueue commands and render the coordinator's locked snapshot.
Phase 9 routes accepted asynchronous work through bounded configuration, scale,
backend, or device-control owners and records queued/running/terminal state in
a central operation registry. Phase 10 starts a dedicated OTA owner. The web
task reads into one fixed 4 KiB buffer and synchronously hands each chunk to
that owner; only the owner calls flash, SHA-256, partition, activation, and
rollback APIs. A generation-token lifecycle gate excludes OTA and candidate
validation from generic reboot and factory reset without blocking unrelated
scale, configuration, or backend work.

## Local web/API boundary

The embedded browser client and transport-neutral router expose 30
metadata-declared routes under `/api/v1`. Read routes cover station/device
health, scale, NFC status/tag data, spool state, printers/toolheads,
configuration, network provisioning/scan state, diagnostics, logs, operation
status, and update state.
Mutations cover tare/calibration, NFC read, assignment/unassignment, backend
tests, configuration patches, update upload/reboot/cancel, device reboot, and
factory reset. The firmware upload route is metadata-declared but bypasses the
small JSON buffer and streams binary data through the OTA owner. All other
routes return a versioned `{api_version, ok, data|error}` JSON envelope and
apply bounded path, header, body, nesting, content-type, field, type, and
identifier validation before reaching the application context.

Ordinary mutations return an operation ID instead of claiming synchronous
completion. The browser checks only that operation at roughly one-second
intervals for at most 45 seconds and stops at `succeeded`, `failed`, or
`confirmation_required`. Reboot and factory reset instead report the accepted
receipt and enter the reconnect flow because the scheduled restart intentionally
ends the connection.
Targeted operation checks are independent of `/api/v1/events`, whose WebSocket
scale/heartbeat events drive live views and coalesced refreshes without a fast
global polling loop. Outbound events use one fixed shared asynchronous batch;
post-handshake input frames are rejected without payload reads. Embedded HTML,
CSS, and JavaScript are protected by compile-time source-size bounds.

Configuration GET responses expose configured-state flags but no secret
values. PATCH applies typed partial fields only when its `expected_revision`
matches the mutex-protected configuration revision; omitted credentials remain
unchanged and explicit empty credentials clear them. The initial local API
bearer token is set through the physically local setup AP or touchscreen.
Recovery provisioning cannot replace an existing token. The browser prompts for it when
a mutation needs authentication, keeps it only in JavaScript memory for the
current tab, never places it in storage or a URL, and clears it after HTTP 401.

The RFAL/wiring-gated firmware still reports NFC explicitly unavailable. NFC
availability is not an OTA candidate-health requirement.

## OTA ownership and durable lifecycle

`UpdateManager` is portable state policy. It has no ESP-IDF, HTTP, FreeRTOS,
NVS, or reboot dependency; it receives partition, digest, and record-store
interfaces. Its public states are `idle`, `upload_receiving`, `writing`,
`validating`, `ready_to_reboot`, `reboot_pending`, `candidate_boot`,
`validating_candidate`, `confirmed`, `rollback_pending`, `rolled_back`, and
`failed`. The retained `ready_to_activate` enum is only a record-reconciliation
compatibility state and is normalized to the unactivated `ready_to_reboot`
boundary. A fresh upload never selects the boot partition.

`OtaWorker` is the only embedded flash/activation owner. It has a fixed-depth
command pool and copies at most one 4096-byte chunk into an owned slot before
calling `UpdateManager`. The HTTP task therefore applies backpressure without
ever retaining the complete image. Upload input has a five-second no-progress
deadline, a 180-second absolute deadline, and a hard 5 MiB/application-slot
limit. Disconnects, short bodies, invalid chunks, or command failures abort the
ESP-IDF handle; the running slot remains untouched.

Every update has an operation ID and an isolated, checksum-protected NVS record.
The record store reserves a durable monotonic generation before opening flash,
persists bounded progress checkpoints and the validated boundary, then records
activation intent before `esp_ota_set_boot_partition`. Reboot and cancel carry
the exact operation, generation, and SHA-256 so stale clients cannot affect a
newer candidate. Boot reconciliation fails interrupted uploads, recognizes a
known invalid candidate as rolled back, recognizes an already valid running
candidate as confirmed, and requests rollback if a pending running image lacks
a consistent validated activation record.

The ESP32 adapter derives `running`, `boot`, and `inactive` partitions from the
pinned ESP-IDF APIs. The client cannot supply a partition, address, or path.
Final staging requires exact byte count, constant-time rolling SHA-256 equality,
successful `esp_ota_end`, successful `esp_image_verify`, a matching ESP image
length/magic, ESP32-S3 chip ID, appended image hash, and a valid fixed OpenTag
manifest for project `OpenTag Station` and hardware
`wt32-sc01-plus-rev-a`. Version, Git SHA, build date, and IDF version are exposed
as candidate metadata; Phase 10 does not impose SemVer ordering or a downgrade
policy.

For the first update after a serial flash with erased otadata, the adapter seeds
the already-running slot as the known-good rollback image before it selects the
inactive candidate. A durable activation-intent record also identifies the
narrow power-cut window where that running slot is `NEW`/`PENDING_VERIFY`; boot
reconciliation confirms only the pre-existing running image, preserves the
validated inactive candidate, and retries selection under the same operation
and generation. If OTA-owner startup cannot reconcile boot state, network and
device-control mutations remain unavailable.

After explicit reboot activation, the rollback-enabled pinned bootloader starts
the candidate as pending. One `BootHealthPolicy` supplies both ordinary boot
tracking and OTA confirmation. It waits 30 seconds and requires storage,
configuration initialized or safely degraded, application/display readiness,
and the local UI, configuration, backend-owner, scale, network, device-control,
web, and OTA tasks. It does not require Spoolman or FilaBridge to be reachable,
and it does not require NFC hardware. A healthy candidate clears the existing
boot/crash marker and calls `esp_ota_mark_app_valid_cancel_rollback`; fatal or
missing local prerequisites request `esp_ota_mark_app_invalid_rollback_and_reboot`.
Factory-reset recovery is a distinct health result and is never reclassified as
an OTA failure.

Generic reboot, factory reset, OTA update, and candidate validation acquire one
generation-token `DeviceLifecycleGate`. Only the current lease can release it,
so a stale completion cannot unlock a newer destructive operation. OTA does not
hold the LVGL or backend task, change configuration/calibration data, or touch
LittleFS. The local API is authenticated and idempotent, but the image is not
cryptographically signed and the local HTTP transport is not TLS-protected;
deployments must use a trusted isolated LAN.

The NFC protocol, configurable ESP32 RFAL primitives, and frontend orchestration
are now implemented behind bounded interfaces. The vendor RFAL binding is
intentionally absent until the exact ST distribution and module wiring are
available. Unit tests use deterministic transports/backends; production code
cannot bypass the wiring guard.

## Application states

The application state machine and Phase 8 workflow coordinator use typed state:

```text
BOOTING -> IDLE -> TAG_DETECTED -> READING_TAG -> TAG_PARSED
                                             -> ERROR
TAG_PARSED -> RESOLVING_SPOOL -> SPOOL_READY
SPOOL_READY -> ASSIGNING -> ASSIGNMENT_COMPLETE -> SPOOL_READY
SPOOL_READY -> RECONCILING -> WRITING_TAG -> SPOOL_READY
```

Tag presence is independently debounced as `NO_TAG`, `TAG_PRESENT`,
`TAG_PROCESSED`, `TAG_REMOVED` so one stationary spool does not retrigger reads.
The workflow snapshot separately models waiting for stable weight, offline
resolution, no match, explicit multi-match selection, spool readiness, and
verified assignment so backend failure never erases local tag/scale facts.

## Domain services

- `SpoolResolver`: deterministic instance UUID → configured Spoolman field →
  cache → NFC UID → metadata candidates → explicit user choice.
- `ToolheadManager`: obtains live mappings, applies reassignment/print safety,
  commands the adapter, then re-reads state and compares the exact spool ID.
- `WeightReconciler`: distinguishes gross/empty/net values, resolves the
  documented empty-weight source priority, operates only on stable valid
  measurements, and uses normal/warning/confirmation bands.
- `ScaleService`: uses the persistent YZC-133 5 kg actual/default hardware
  profile while retaining the YZC-133 2 kg profile and legacy calibration
  inference; a model/capacity change requires new calibration.
- `TagProvisioner`: plans writes, preserves unknown CBOR, writes only affected
  blocks, re-reads, decodes, and verifies.
- `MaterialCompatibilityService`: advisory rules separate from mapping policy.
- `UpdateManager`: owns portable OTA transitions, immutable operation/generation
  preconditions, rolling-digest/byte-count decisions, durable reconciliation,
  inactive-slot safety, explicit activation, candidate confirmation, and
  rollback policy behind mockable platform ports.

## Source-of-truth and cache policy

Caches always carry a confirmation timestamp and backend identity. Cached data
may keep the UI useful offline but is never presented as newly verified. Offline
destructive commands are not automatically replayed later. A stale assignment
must be revalidated and confirmed.

## Backend capability negotiation

Connection success is distinct from feature availability. Adapters probe
version and concrete operations, returning a capability bitset. Unexpected JSON
is a structured `api_changed` error scoped to that feature. Other functions stay
available.

Version selection may later choose an internal parsing strategy or versioned
adapter. No backend-specific path or JSON key is allowed outside its adapter.

## Proposed final repository tree

```text
.
├── platformio.ini
├── partitions.csv
├── VERSION
├── docs/
├── third_party/ST_RFAL/
├── src/
│   ├── application/
│   ├── boards/
│   ├── config/
│   ├── core/
│   ├── diagnostics/
│   ├── domain/
│   ├── events/
│   ├── hardware/
│   │   ├── display/
│   │   ├── touch/
│   │   ├── scale/
│   │   └── nfc/st25r3916b/
│   ├── integrations/
│   │   ├── spoolman/
│   │   └── filabridge/
│   ├── logging/
│   ├── nfc/
│   │   ├── formats/openprinttag/
│   │   └── protocols/nfcv/
│   ├── ota/
│   ├── platform/
│   │   ├── ota/
│   │   └── rfal/
│   ├── services/
│   ├── ui/
│   ├── web/
│   └── main.cpp
├── test/
├── tools/
└── .github/workflows/
```

Only directories needed by the current milestone are instantiated. Empty
architecture theatre is avoided, but new code must land in the boundary above.

## Memory policy

Network bodies, JSON documents, browser assets, tag dumps, caches, and log rings
receive fixed maximum sizes. The OpenPrintTag section maximum is 512 bytes; tag
buffers are sized from verified memory geometry and capped. Firmware bodies are
never placed in the 16 KiB JSON request buffer: the web transport owns one
fixed 4096-byte receive buffer and each of the OTA owner's four command slots
owns one fixed 4096-byte handoff buffer (16 KiB across that pool). The queue and
slot count are compile-time bounded and the caller waits for ownership before
reusing a chunk. Long-lived services
avoid repeated dynamic allocation. PSRAM is useful for display buffers and
bounded diagnostic export, not for hiding unbounded growth.

## Development milestones and acceptance criteria

| Phase | Scope | Acceptance criterion |
|---:|---|---|
| 0 | Research and foundation | Current upstream revisions are recorded; both native tests and pinned WT32 firmware build pass; unverified hardware is labeled. |
| 1 | Board bring-up | Implemented and compiled; serial, display, full-screen touch, storage, PSRAM, reset diagnostics, and responsive LVGL loop must still pass on the actual board. |
| 2 | ST25R3916B bring-up | Sequence/recovery service unit-tested; exact RFAL distribution, module/wiring, concrete backend, and all physical checks remain gated. |
| 3 | NFC-V | Protocol contracts, single-tag/geometry/locks/read/write verification are unit-tested; RFAL binding and real-tag verification remain gated. |
| 4 | OpenPrintTag | Official host fixtures decode and safely modify with semantic verification; real-tag transaction through ST25R3916B remains gated. |
| 5 | Scale | NAU7802 raw/tare/calibration/filter/stability behavior passes with reference weights; calibration survives power cycles and export/import. |
| 6 | Configuration + networking | One migrated settings service, resilient first-run setup, Wi-Fi/backoff/status, and bounded CA-verified HTTP(S) pass host/build gates; physical LAN behavior remains gated. |
| 7 | Spoolman | Version/capability probes and pinned contract tests pass; identity resolution is deterministic; remaining-weight writes are stable, explicit, merge-safe, and verified. |
| 8 | FilaBridge + main workflow | Pinned contract tests pass for printer/toolheads/mappings/map/unmap; numbering normalizes once; place → identify → weigh → resolve → T1–T5 → assign → verify degrades safely. |
| 9 | Web UI/API | Portable 23-route router, parser, patch, and bounded-ledger logic is host-tested; embedded assets, production context, HTTP/WebSocket transport, owner queues, bearer authentication, reset control, and the update placeholder are firmware-compiled. Physical-browser, target-LAN, and hardware validation remain outstanding. |
| 10 | OTA | Portable state/record/health/exclusion policy is host-tested and the fixed-buffer owner, streaming API/UI, ESP-IDF inactive-slot adapter, image/manifest validation, activation, confirmation, and rollback integration are firmware-compiled. Physical A→B/B→A, failure rollback, power cuts, restart, and browser recovery remain gated. |
| 11 | Release hardening | Pinned compatibility, parser, migration, fault, performance, memory, HIL, and rollback suites pass; recovery artifacts and release documentation are published. |

Every milestone must leave both the firmware environment and native tests
buildable. Hardware-dependent milestones remain incomplete until exercised on
the actual assembly.
