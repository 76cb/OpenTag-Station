# Architecture

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
 Spoolman adapter  FilaBridge adapter  update provider
          |             |               |
      HTTP/JSON     HTTP/JSON/WS       OTA platform

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
| Update | Phase 10 manifest/download/OTA validation | backend availability policy |

Tasks exchange bounded events/commands. Payload ownership is explicit and
queues have overflow diagnostics. No worker may wait forever; every NFC, DNS,
HTTP, and future OTA operation gets a deadline.

Phase 1 activates the UI owner as a pinned FreeRTOS task; it is the only context
allowed to call LVGL. Phase 5 activates a separate scale task on the second I2C
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
a central operation registry. NFC and OTA ownership contracts remain defined
but their workers are not started until the corresponding subsystem exists.

## Phase 9 local web boundary

The embedded browser client and transport-neutral router expose 23
metadata-declared routes under `/api/v1`. Read routes cover station/device
health, scale, NFC status/tag data, spool state, printers/toolheads,
configuration, diagnostics, logs, operation status, and the update boundary.
Mutations cover tare/calibration, NFC read, assignment/unassignment, backend
tests, configuration patches, reboot, and factory reset. The router always
returns a versioned `{api_version, ok, data|error}` JSON envelope and applies
bounded path, header, body, nesting, content-type, field, type, and identifier
validation before reaching the application context.

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
bearer token is set on the physical touchscreen. The browser prompts for it when
a mutation needs authentication, keeps it only in JavaScript memory for the
current tab, never places it in storage or a URL, and clears it after HTTP 401.

The RFAL/wiring-gated firmware reports NFC explicitly unavailable. Reboot and
factory reset require authenticated, idempotent, explicitly confirmed commands
and execute through the device-control owner. `/api/v1/update` is a read-only
Phase 10 placeholder; Phase 9 contains no OTA upload, inactive-slot write, A/B
selection, or rollback implementation.

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
- `UpdateManager`: validates a provider-neutral manifest and delegates safe OTA.

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
│   ├── platform/rfal/
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
buffers will be sized from verified memory geometry and capped. Long-lived
services avoid repeated dynamic allocation. PSRAM is useful for display buffers
and bounded diagnostic export, not for hiding unbounded growth.

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
| 10 | OTA | Not started: valid A→B and B→A updates must preserve config/calibration; manifest hardware/size/SHA checks must reject bad images; an early crash must roll back. |
| 11 | Release hardening | Pinned compatibility, parser, migration, fault, performance, memory, HIL, and rollback suites pass; recovery artifacts and release documentation are published. |

Every milestone must leave both the firmware environment and native tests
buildable. Hardware-dependent milestones remain incomplete until exercised on
the actual assembly.
