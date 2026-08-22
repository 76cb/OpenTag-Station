# Phase 11 release-candidate validation

This document is the release-hardening record for OpenTag Station. It separates
software evidence produced by deterministic host tests and the pinned firmware
build from physical evidence that does not yet exist.

## Validation status

### SOFTWARE VALIDATED

- Phase 10 baseline commit `ad9005d2083e9936f4eb9d24f4e24a254bae0f7b`
  is the verified Phase 11 source baseline.
- The baseline passed 223/223 native cases in 20 suites and built
  `wt32-sc01-plus` without compiler warnings.
- The baseline used 167,152/327,680 static RAM bytes (51.0%) and
  1,946,637/5,242,880 flash bytes (37.1%).
- Phase 11 passes 225/225 native cases in the same 20 suites. Its warning-free
  build uses 167,152 RAM bytes (51.0%, delta 0) and 1,948,105 flash bytes
  (37.2%, delta +1,468) versus Phase 10.
- Phase 11 deterministic checks cover counter saturation, malformed/truncated
  OpenPrintTag images, embedded JavaScript syntax, compiler stack-frame output,
  and the existing storage/configuration, integration, API, lifecycle,
  boot-health, and OTA failure suites. Source review and request/revision guards
  address stale browser response exclusion; browser execution remains a
  physical/LAN validation item.
- The provisioning stack hotfix passes 240/240 native cases and its warning-free
  WT32 build uses 167,912 RAM bytes and 2,017,881 flash bytes: +72/-288 bytes
  versus the PR #5 base at `e33bf97cceca0d1246317d38c892ecd9c71d5cfe`.
- No statement in this document is evidence of electrical, RF, mechanical,
  browser-on-target, live-backend, or bootloader-on-target behavior.

### PHYSICAL HARDWARE VALIDATION REQUIRED

Every row in the physical matrix below is **UNVERIFIED** until a dated result,
firmware Git SHA, board identity, test setup, observed result, and evidence link
are recorded. A successful firmware build is not physical validation.

## Runtime ownership model

| Area | Sole writer / lifecycle owner | Readers and boundary |
| --- | --- | --- |
| Application lifecycle | Arduino `setup()/loop()` constructs static services; `DeviceLifecycleGate` serializes reboot, reset, OTA, and candidate validation | Owner tasks expose snapshots and bounded receipts |
| Web/API server | Network task starts/stops/publishes; ESP-IDF HTTP task executes handlers | Router is transport-neutral; handlers never own backend or flash state |
| WebSocket publishing | Network task builds events; one fixed asynchronous batch owns payloads until callbacks complete | At most 2 WebSocket clients and 4 total sockets |
| Configuration | `ConfigurationWorker` owns UI/API writes; `ConfigurationService` mutex serializes persistence and snapshots | Scale calibration and confirmed spool mapping use the same service |
| Storage | `StorageService` owns station NVS/LittleFS operations behind one mutex and reset gate | OTA metadata uses a separate `Esp32UpdateRecordStore` namespace |
| Scale | Scale task owns NAU7802 calls and executes the fixed scale command queue | UI/API/diagnostics read coherent snapshots |
| NFC | No runtime NFC task is created while the wiring/RFAL build gate is disabled | Portable NFC-V/OpenPrintTag code is host-tested only |
| Spoolman | Backend task is the only adapter caller | Workflow/UI/API read normalized state |
| FilaBridge | Backend task is the only adapter caller and assignment writer | Exact readback is required before local assignment state advances |
| Toolhead assignment | Backend task calls `StationWorkflow` and `ToolheadAssignmentService` | Spool generation and printer revision reject stale queued work |
| Diagnostics | Subsystem owners publish mutex/atomic snapshots | API, UI, serial health line are readers |
| Logging | `BoundedLog` mutex protects a 32-entry fixed ring | API reads redacted copies |
| Reboot/factory reset | `DeviceControlWorker` holds a generation-checked lifecycle lease | Duplicate identical requests coalesce; conflicts fail closed |
| OTA | `OtaWorker` is the sole post-boot flash/partition writer | HTTP streams one fixed 4 KiB slot and waits for owner acknowledgement |
| Candidate validation | `OtaWorker` owns the candidate-validation lease until confirm/rollback | Application health policy only submits bounded decisions |
| UI/LVGL | UI task is the sole LVGL caller | Callbacks enqueue work; they do not perform HTTP or flash writes |
| Network/Wi-Fi | Network task owns Wi-Fi, scan, mDNS, NTP, and web-server lifecycle | Configuration worker posts a mutex-protected reconfigure request |

Confirmed Phase 11 fixes:

- backend probe intervals are now measured from probe completion, preventing a
  slow or unavailable backend from causing an immediate perpetual retry loop;
- persisted boot count, crash streak, and Wi-Fi reconnect-attempt diagnostics
  saturate instead of wrapping to misleading low values;
- browser resource epochs, payload revisions, and socket identity prevent older
  REST/WebSocket work from overwriting newer scale, update, or printer state.

No use-after-free was found in fixed WebSocket batches or OTA slots. HTTP server
shutdown waits for queued callbacks. Timed-out OTA slots remain reserved until
the owner completes them, and a late successful begin is cancelled by the owner.
Queue-send failures release allocations/leases and produce terminal operation
errors. Backend and configuration pointer queues use `new (std::nothrow)` and
delete on every rejected or consumed path.

## Project-created task and stack inventory

ESP-IDF's Arduino port interprets these configured stack depths as bytes.

| Task | Priority | Core | Stack bytes | Expected worst call chain | Responsibility |
| --- | ---: | ---: | ---: | --- | --- |
| `opentag-ui` | 2 | 1 | 12,288 | LVGL refresh, configuration/workflow snapshots, screen rebuild | Sole LVGL/display interaction |
| `opentag-config` | 1 | 0 | 8,192 | JSON configuration copy/validation, LittleFS commit, Wi-Fi handoff | Configuration mutation owner |
| `opentag-scale` | 1 | 0 | 6,144 | NAU7802 I2C poll/filter/calibration/persistence | Scale hardware and command owner |
| `opentag-backend` | 1 | 0 | 12,288 | DNS/HTTP/TLS, bounded JSON parse, resolution, guarded readback | Spoolman/FilaBridge/workflow owner |
| `opentag-network` | 1 | 0 | 16,384 | Wi-Fi scan/reconnect, diagnostics, bounded WebSocket serialization | Network and web lifecycle owner |
| `opentag-control` | 1 | 0 | 4,096 | reset intent, bounded erase, restart | Generic reboot/factory-reset owner |
| `opentag-ota` | 1 | 0 | 24,576 | boot reconciliation, SHA/flash operations, image validation, rollback | OTA/candidate owner |
| ESP-IDF `httpd` | 5 default | unpinned default | 20,480 | request headers/body, JSON router or 4 KiB upload handoff | HTTP/WebSocket handler execution |
| NFC owner | — | — | 0 | Not created while RFAL/wiring gate is disabled | Must be inventoried when enabled |

Configured project-created dynamic task stacks total **104,448 bytes**. The
Arduino loop task is separately configured to **16,384 bytes** through the
framework-supported `SET_LOOP_TASK_STACK_SIZE` mechanism. The total does not
include ESP-IDF system tasks (idle, timer, Wi-Fi,
TCP/IP, event loop, IPC, and driver tasks), whose reservations come from the
pinned framework configuration rather than project calls.

The firmware build enables `-fstack-usage`. Run
`python tools/analyze_stack_usage.py` after the WT32 build to list the largest
individual compiler-reported frames. The Phase 11 build parsed 8,099 frames from
412 files. Largest project frames are OpenPrintTag decode (7,936 bytes), OTA run
(6,512), OTA command processing (6,464), API snapshot serialization (6,032),
OTA pre-task cleanup (5,280), firmware description (5,264), upload setup
(5,024), mutation parsing (4,848), and HTTP upload handling (3,216). Ten project
frames are compiler-marked dynamic; their largest reported estimate is 240
bytes. These values are not cumulative call-chain proof. The gated NFC owner
must be sized above the decode call chain before enablement.

The provisioning hotfix build parses 8,127 frames from 413 files. Moving the
decoded configuration holder to the heap and filling its result in place drops
`decode_document` from 2,416 to 288 bytes, `read_configuration` from 2,688 to
1,216 bytes, and `ConfigurationService::initialize` from 1,424 to 768 bytes.
The configured-boot audited frame sum for `Application::setup` plus those three
frames falls from 8,352 to 4,112 bytes. Network-task WebSocket publication no
longer calls the 2,192-byte router and full 6,192-byte snapshot serializer for
scale/update events; its direct scale/update serializers use 560/1,056 bytes.
The remaining full snapshot frame is 5,056 bytes and stays on the 20 KiB HTTP
task. Compiler frame values remain non-cumulative estimates, so the 16 KiB
network and loop reservations are paired with runtime high-water diagnostics.

On hardware, capture `uxTaskGetSystemState` or equivalent diagnostics after
boot, UI navigation, Wi-Fi reconnect, backend timeout, scale calibration,
upload/validation, and candidate confirmation. Record each task's minimum
high-water mark and the system minimum free heap. Any unobserved task, declining
minimum across repeated cycles, or margin below the release threshold blocks
signoff.

Static-resource notes:

- OTA uses four fixed 4 KiB command buffers and never buffers a whole image.
- The HTTP server owns one fixed 4 KiB upload buffer and a fixed 2-client,
  4096-byte WebSocket batch.
- LVGL draw buffers allocate PSRAM first and fall back to a smaller internal
  buffer; allocation failure is explicit.
- Operations (24), idempotency entries (16), and logs (32) are fixed rings.
- Integration responses are capped at 64 KiB and collection counts are bounded.
- Configuration and API JSON documents are bounded at 16/24/32 KiB boundaries.
- Dynamic `String`/vector/JSON allocations remain a fragmentation risk that
  must be observed in the long-run hardware soak.

## Long-run behavior

All millisecond deadline comparisons use unsigned subtraction or signed
deadline comparison and are safe across the 32-bit `millis()` rollover.
Idempotency entries expire after 60 seconds using wrap-safe subtraction.
Destructive backend commands expire after 15 seconds. Operation and log history
are bounded rings. Wi-Fi backoff saturates at the configured maximum and its
attempt counter now saturates. Backend periodic probes are scheduled from
completion to avoid retry storms.

Hardware soak must still demonstrate stable minimum free heap, task high-water
marks, socket count, WebSocket reconnects, Wi-Fi/DNS recovery, backend outage
behavior, and repeated configuration/assignment/OTA cycles. No multi-day soak
has been performed.

## Persistent state and power-cut reconciliation

| State | Durable order and recovery |
| --- | --- |
| Configuration | Write `configuration.new`, flush/readback verify, remove old backup, rename primary to backup, rename staging to primary. Boot accepts only a decoded/validated primary or backup; corrupt/truncated data enters fail-closed persistence-degraded defaults. |
| Configuration migration | Decode and migrate in memory, validate, then persist through the same staged commit. Unknown fields are preserved; supported schema 1/2 migrates deterministically to schema 3. |
| Scale calibration/profile | CRC-protected legacy mirror and central document are validated. Profile/model/capacity change clears incompatible calibration before the central commit; the old central document remains authoritative if the commit fails. |
| Spool identity mappings | Central configuration transaction; conflicts and malformed identities are rejected before persistence. |
| Boot health/crash streak | Persist count/streak and authoritative `bootPending=true`; healthy confirmation clears streak first and pending marker last. Interrupted confirmation remains fail closed. Counters saturate. |
| Factory reset | Persist `resetPending` in separate control NVS, remove backup/staging/primary, clear station application NVS, restore no-format guard, then clear intent. Any cut before the last step replays idempotent early-boot recovery. |
| OTA record | Checksum-protected record with monotonic generation; progress is checkpointed at fixed milestones. Corrupt/unavailable records never authorize a pending candidate. |
| OTA activation | Persist activation intent before selecting the boot slot, then persist activated/reboot state. Boot topology reconciles each cut point; ambiguity retains lifecycle exclusion. |
| Candidate confirmation | Local boot marker is confirmed only after the full health window; ESP-IDF candidate confirmation follows. Failure retains rollback authority. |

Physical power-cut tests remain required because host fake stores cannot prove
LittleFS/NVS/flash behavior, brownout behavior, or bootloader integration.

## Configuration migration findings

Host coverage includes fresh defaults, current schema, schemas 1 and 2, missing
fields, preserved unknown fields, malformed/truncated JSON, invalid enum/range,
secret-preserving import/PATCH, CAS conflicts, profile migration, backup
recovery, failed save, failed legacy calibration clear/save, and reset-oriented
storage behavior. Unsafe types/ranges and mismatched hardware/calibration are
rejected. A corrupt primary with no valid backup starts in an initialized but
persistence-unavailable safe-degraded state: Wi-Fi is unconfigured and the
empty bearer token intentionally permits tokenless local mutations.

The runtime CAS revision is intentionally boot-local and is not a durable
sequence. Clients must reload after reboot or HTTP 409.

## Scale findings and physical procedure

The current/default profile is YZC-133 5 kg (5,000 g); YZC-133 2 kg remains a
supported alternative. Calibration must match model/capacity. Tare requires a
stable filter window. Invalid calibration, timeout, ADC failure, negative load,
creep, and overload are structured states. The configured overload threshold is
capacity times ratio (default 110%); profile changes invalidate incompatible
calibration.

Physical validation must use the exact assembled mechanics:

1. Verify NAU7802 power, SDA/SCL, excitation and A+/A-/E+/E- wiring with power
   removed; record the load-cell datasheet and polarity.
2. Boot unloaded, observe raw polarity, and correct wiring/orientation rather
   than hiding unstable electrical behavior in software.
3. Record zero for at least 10 minutes; quantify peak-to-peak noise and drift.
4. Tare repeatedly with an empty platform and verify rejection while disturbed.
5. Calibrate with a traceable known mass in the useful range and save/reboot.
6. Test at multiple known masses across the range; record indicated value,
   absolute/percent error, and rounding.
7. Repeat load/remove cycles and record repeatability and return-to-zero.
8. Hold a constant load to measure creep/drift and temperature sensitivity.
9. Move the same mass across center/corners/loading positions to expose
   mechanical error.
10. Approach but do not exceed safe mechanical limits; verify software overload
    indication and recovery.
11. Complete all steps on the 5 kg profile. Optionally repeat with an actual
    YZC-133 2 kg cell/profile and confirm incompatible calibration invalidation.

Do not infer accuracy from host tests.

## NFC/OpenPrintTag findings and physical checklist

The codec bounds tag image/region size, NDEF/TLV offsets, CBOR depth/entries and
array items, UTF-8, duplicate keys, integer conversions, geometry, block locks,
single-tag presence, minimal full-block writes, and exact readback. Phase 11
adds deterministic coverage for every truncated prefix and every single-byte
mutation of the official 312-byte fixture. Unsupported/malformed tags fail with
structured errors.

Physical NFC remains wiring-gated. Before enabling it:

1. Identify the exact ST25R3916B module, voltage, oscillator, antenna/matching
   network, bus-selection straps, and RFAL distribution/license.
2. Record WT32 SPI CS/SCLK/MOSI/MISO, IRQ, reset, power, ground, logic levels,
   and shared-bus constraints; continuity-check with power removed.
3. Verify safe power/reset sequence and chip identity.
4. Verify IRQ polarity/clearing, SPI locking, RF field on/off, and recovery.
5. Inventory one NFC-V tag, reject multiple tags, read geometry/security status,
   and read the full image.
6. Decode official and real OpenPrintTag records; reject malformed, unsupported,
   oversized, locked, removed, and changed tags.
7. Write only a sacrificial compatible tag, verify changed blocks and exact
   readback, then confirm unknown fields/regions remain unchanged.
8. Repeat at position/orientation/range limits and after RF reset/reboot.

All steps are UNVERIFIED.

## Spoolman, FilaBridge, and Prusa XL findings

Adapters use fixed connect/read timeouts, bounded bodies/collections, no
redirects, explicit DNS errors, and CA-required HTTPS. Malformed/missing fields,
HTTP errors, unknown versions, and backend outages degrade to structured
read-only/offline states. FilaBridge map/unmap is single-shot and independently
read back. Stale spool generation, expected spool, printer revision/state,
current mapping, and 15-second queue expiry prevent an old operation assigning
the wrong spool. UI T1-T5 labels translate once to backend IDs 0-4.

Live Spoolman v0.26.1, FilaBridge v1.2.2, and Prusa XL execution remain
UNVERIFIED.

## Definitive API contract

All responses use the versioned structured envelope documented in
[web.md](web.md). All GET routes are public. Every mutation requires
`X-OpenTag-Request: web` and a valid idempotency key; the exact bearer token is
also required when a nonempty token is configured. Buffered mutations require JSON. Upload requires
`application/octet-stream`, exact length/digest/generation headers, a
five-second no-progress deadline, and a 180-second absolute deadline.

| Route(s) | Body bound/type | Idempotency / revision behavior |
| --- | --- | --- |
| `GET status,device,health,scale,nfc,nfc/tag,spool,printers,toolheads,config,diagnostics,logs,update` | 0 bytes | Snapshot; config is allowlisted/redacted |
| `GET operations/{id}` | 0 bytes | Canonical positive ID; 404 after bounded eviction |
| `POST scale/tare,nfc/read,backends/test` | 256-byte exact `{}` | Volatile 60-second/16-entry idempotency |
| `POST scale/calibrate` | 512-byte strict JSON | Reference within selected capacity |
| `POST toolheads/{id}/assign` | 2,048-byte strict JSON | Spool generation, printer revision/state, current spool, confirmations |
| `POST toolheads/{id}/unassign` | 2,048-byte strict JSON | Same stale guards and exact current spool |
| `PATCH config` | 16 KiB strict JSON | Boot-local `expected_revision` CAS; omitted secrets preserved |
| `POST update/upload` | 5 MiB streaming binary | Exact generation/digest; inactive slot chosen internally |
| `POST update/reboot,update/cancel` | 512-byte strict JSON | Exact operation/generation/digest/confirmation |
| `POST device/reboot,device/factory-reset` | 256-byte strict JSON | Exact confirmation; lifecycle exclusion |

Normal success is 200; accepted asynchronous work is 202. Stable errors use
400/401/404/405/408/409/413/415/422/429/500/502/503/507 as applicable. Bodies,
error messages, paths, header counts/bytes, response serialization, operation
messages, and logs are bounded. Public serialization rejects secret-named keys
and does not expose passwords, bearer/backend tokens, CA text, raw flash
addresses, arbitrary filesystem paths, or pointers.

## Build and CI reproducibility findings

Release builds require Python 3.12, Git, and `platformio==6.1.19` from
`requirements-dev.txt`. PlatformIO pins `native@1.2.1`, `espressif32@6.13.0`,
Arduino-ESP32 `3.20017.241212+sha.dcc1105b`, the ESP32-S3 GCC 8.4.0 toolchain,
and every external firmware library version. The custom board JSON and
partition CSV remain repository-owned inputs. `SOURCE_DATE_EPOCH` should be set
by release builders; otherwise metadata deterministically uses the Git commit
date.

CI uses Ubuntu 24.04 and Python 3.12, pins the official checkout/setup-python
actions to immutable v4.4.0/v5.6.0 commits, installs only the pinned development
requirement, checks the complete tree for whitespace errors, syntax-checks the
embedded dependency-free JavaScript, runs all native suites, builds the WT32
target, and emits the stack report under a 30-minute timeout. No third-party CI
service was added.

The runner image label and host operating-system packages are managed external
inputs, so bit-for-bit artifact identity across time is not claimed. Source,
declared PlatformIO packages, libraries, scripts, board definition, partition
table, and build metadata behavior are pinned and reviewable.

## Security review and remaining limitations

Implemented controls include fail-closed bearer matching, authentication before
body parsing, strict body/header/path bounds, no HTTP redirects, credential-free
URLs, CA-required HTTPS backends, same-origin browser requests, restrictive
static response headers, no external frontend dependency, bounded WebSocket
clients, fixed operation/idempotency/log registries, exact destructive
confirmations, lifecycle exclusion, inactive-slot-only OTA, digest/image/target
validation, and log/config redaction.

Remaining limitations:

- local HTTP and `ws://` are plaintext; bearer tokens and data can be observed
  or modified by an attacker on the LAN;
- read-only API/WebSocket data is unauthenticated and includes operational,
  network, backend URL, spool, printer, and diagnostic metadata;
- OTA images are SHA-256 integrity checked but not publisher signed;
- no SemVer downgrade, release-channel, or anti-rollback policy is enforced;
- no per-source rate limiter exists beyond fixed sockets, queues, body
  deadlines, rings, and lifecycle/idempotency controls;
- physical access, serial bootloader recovery, NVS extraction, Wi-Fi security,
  DNS trust, browser security, and backend compromise are outside the firmware
  trust boundary;
- TLS infrastructure and firmware signing are deferred release-security work,
  not silently claimed by Phase 11.

Use only on a trusted isolated WPA2/WPA3 LAN; do not port-forward the station.

## Physical hardware validation matrix

Every item below starts and remains **UNVERIFIED** until real evidence is added.

### WT32-SC01 Plus

- [ ] **UNVERIFIED** — cold/warm boot and serial diagnostics.
- [ ] **UNVERIFIED** — ST7796 display orientation, color, refresh, sleep/wake.
- [ ] **UNVERIFIED** — touchscreen mapping, edges, keyboard/focus actions.
- [ ] **UNVERIFIED** — PSRAM allocation/fallback behavior.
- [ ] **UNVERIFIED** — Wi-Fi association, DHCP, DNS, reconnect/backoff.
- [ ] **UNVERIFIED** — mDNS and NTP behavior.
- [ ] **UNVERIFIED** — Web UI/API, bearer failures, malformed responses.
- [ ] **UNVERIFIED** — WebSocket loss/reconnect and stale-response exclusion.

### Scale

- [ ] **UNVERIFIED** — NAU7802 wiring, detection, internal calibration.
- [ ] **UNVERIFIED** — YZC-133 5 kg polarity and full procedure above.
- [ ] **UNVERIFIED** — tare, known-mass calibration, reboot persistence.
- [ ] **UNVERIFIED** — repeatability, zero return, noise, drift, creep.
- [ ] **UNVERIFIED** — center/corner loading positions.
- [ ] **UNVERIFIED** — negative/overload indication and recovery.
- [ ] **UNVERIFIED** — optional YZC-133 2 kg compatibility/profile invalidation.

### NFC

- [ ] **UNVERIFIED** — exact ST25R3916B module, antenna, wiring, RFAL binding.
- [ ] **UNVERIFIED** — bring-up, identity, IRQ, RF field and recovery.
- [ ] **UNVERIFIED** — NFC-V inventory/geometry/security/multi-tag behavior.
- [ ] **UNVERIFIED** — OpenPrintTag real-tag read/decode.
- [ ] **UNVERIFIED** — sacrificial-tag write/readback/preservation.

### Integrations

- [ ] **UNVERIFIED** — live pinned Spoolman probe/read/write/readback/outage.
- [ ] **UNVERIFIED** — live pinned FilaBridge probe/read-only/outage behavior.
- [ ] **UNVERIFIED** — Prusa XL T1-T5 assignment/unassignment and readback.
- [ ] **UNVERIFIED** — stale browser/backend response cannot map wrong spool.
- [ ] **UNVERIFIED** — active print and unknown-state override warnings.

### Recovery and storage

- [ ] **UNVERIFIED** — ordinary reboot with data retained.
- [ ] **UNVERIFIED** — factory reset exact erase scope and first-run return.
- [ ] **UNVERIFIED** — power interruption at every reset write-order cut point.
- [ ] **UNVERIFIED** — primary/backup configuration corruption and recovery.
- [ ] **UNVERIFIED** — interrupted configuration/profile/calibration migration.
- [ ] **UNVERIFIED** — brownout/mount failure never triggers unsafe reformat.

### OTA

- [ ] **UNVERIFIED** — A to B upload, activation, boot and confirmation.
- [ ] **UNVERIFIED** — confirmed B to A update.
- [ ] **UNVERIFIED** — intentional B health failure and B to A rollback.
- [ ] **UNVERIFIED** — first-use OTA metadata seeding/retry.
- [ ] **UNVERIFIED** — interrupted/short/disconnected upload.
- [ ] **UNVERIFIED** — power cut before activation.
- [ ] **UNVERIFIED** — power cut after activation intent/slot selection.
- [ ] **UNVERIFIED** — crash/reset during candidate validation.
- [ ] **UNVERIFIED** — corrupt/unavailable OTA record with pending candidate.
- [ ] **UNVERIFIED** — browser disconnect/reconnect/resume through all states.

### Resource and soak validation

- [ ] **UNVERIFIED** — all project and framework task stack high-water marks.
- [ ] **UNVERIFIED** — minimum free internal heap and PSRAM under peak workflows.
- [ ] **UNVERIFIED** — repeated UI/config/backend/assignment/update cycles.
- [ ] **UNVERIFIED** — socket/client cleanup and Wi-Fi/backend reconnect cycles.
- [ ] **UNVERIFIED** — multi-day stability with no declining memory margin.
