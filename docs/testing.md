# Testing strategy

## Current checks

The validated Phase 9 baseline contains 163 host-only cases across eighteen
suites. The final Phase 10 run contains 223/223 passing host-only cases across
twenty suites. This is the runner result after the final safety fixes:

- zero-based backend to one-based human toolhead translation;
- stable-weight gating and discrepancy confirmation;
- explicit backend capabilities;
- bounded FIFO application events;
- valid and invalid typed application transitions;
- debounced stationary-tag suppression and removal/re-presentation;
- bounded queue/deadline contracts for UI, web, configuration, scale, NFC,
  network, backend, device-control, and OTA owners;
- unresolved NFC wiring remains disabled.
- the required ST25R3916B bring-up order, safe failure shutdown, and complete
  recovery without reboot;
- exact layout and field decoding of an official OpenPrintTag FFF fixture;
- byte preservation for an official unknown-field fixture during auxiliary
  consumed-weight modification;
- Type 5/NDEF/CBOR truncation, bounds, duplicate-key, nesting, and UTF-8-safe
  parsing behavior;
- NFC-V UID byte-order normalization, geometry, lock-map, and full-image reads;
- minimal full-block diff plans, locked-block refusal, per-write tag identity,
  multiple-tag refusal, and exact block readback verification.
- scale configuration validation, ADC/internal-calibration failures, tare and
  reference calibration math, positive/negative load-cell orientation, moving
  average and continuous stability gating, negative/overload states, disconnect
  recovery, persistence failures, and creep warnings;
- empty-spool source priority, invalid net-weight refusal, and distinct normal,
  warning, and confirmation-required reconciliation bands.
- configuration defaults, legacy calibration migration, additive schema
  migration, unknown-field preservation, transactional writes/imports,
  credential redaction, hardware/range validation, and corrupt-primary backup
  recovery;
- incomplete first-run navigation, exponential reconnect backoff, and strict
  HTTP URL parsing including credential, fragment, scheme, and port rejection.
- Spoolman health/version/capability negotiation, bounded normalized parsing,
  URL-encoded identity filters, explicit creation, locations/field definitions,
  concurrent-use protection, verified remaining-weight writes, and merge-safe
  one-key extra updates;
- deterministic instance UUID/cache/NFC UID/GTIN/package/material/metadata
  resolution, duplicate and ambiguous-match handling, UUID/UID normalization,
  plus schema-3 mapping migration, persistence, and conflict rejection.
- FilaBridge health/version guards, stable IDs, strict complete mapping shapes,
  current printer state, T1–T5 normalization, exact assignment/unassignment
  payloads, and non-retried conflict handling;
- occupied-toolhead, active-print, and unverified-printer-state safety policy,
  read-only unknown-version handling, disabled-profile enforcement,
  idempotence, exact post-write comparison, unassignment verification, and
  stale target rejection;
- abrasive/nozzle, temperature, diameter, flexible/support, and disabled-profile
  compatibility advisories;
- the decoded OpenPrintTag → stable scale → Spoolman resolution → reconciliation
  → five-toolhead discovery → exact verified assignment slice, including
  ambiguous identity and independent backend-outage behavior.
- configuration revision/CAS behavior, schema-3 web-token and load-cell-profile
  compatibility, 2 kg/5 kg calibration matching, and secret-preserving patch
  semantics;
- coherent scale diagnostics under concurrent reads, including hardware,
  calibration, overload-threshold, and missing-calibration transitions;
- bounded volatile logging capacity/cursors/drop counts, message truncation,
  and ingestion-time sensitive-token redaction;
- bounded operation correlation, newest-first ordering, expiration, and bounded
  status/error text;
- a 32-entry idempotency ledger with ten-minute retention, same-payload reuse,
  conflict detection, wrap-safe expiry, expire-only capacity reuse, full-ledger
  rejection before side effects, and concurrent access;
- the complete versioned web route table, global/per-route request and response
  bounds, strict JSON shapes/types, conditional bearer authorization, required
  mutation headers, stable structured errors, and destructive confirmations;
- stale spool/printer/configuration preconditions, exact assignment and
  unassignment bodies, operation receipts, payload digests, and duplicate
  request behavior;
- allowlisted configuration GET serialization and PATCH merge semantics,
  including omitted-secret preservation, explicit-empty clearing, profile
  alias conflicts, atomic validation, and calibration invalidation.
- OTA state transitions, inactive-only targeting through fakes, size/chunk/
  truncation/hash/image-identity failure cleanup, unactivated staging, stale
  generation/operation rejection, cancel/activation boundaries, bounded error
  serialization, progress persistence, record corruption, power-cut
  reconciliation, candidate confirmation, rollback, and audit-save cut points;
- one-owner lifecycle exclusion under concurrent acquisition, stale lease
  rejection, wrap-safe 30-second candidate timing, every required local-health
  signal, safe configuration degradation, backend/NFC independence, fatal
  startup handling, and distinct factory-reset recovery; and
- update-route metadata, binary-route rejection by the JSON router, bearer/
  source/content/idempotency policy, exact reboot/cancel schemas, stale digest/
  generation protection, structured update snapshots, operation receipts, and
  replay behavior.

The table below is the stable Phase 9 baseline inventory. Phase 10 adds 42
`test_ota_core` cases and eleven `test_ota_safety` cases, extends
`test_web_api` from 21 to 27, and extends `test_operations` from three to four.
Those additions produce the final 223 cases across twenty suites.

The Phase 9 baseline inventory is:

| Native suite | Cases |
| --- | ---: |
| `test_assignment` | 9 |
| `test_configuration` | 28 |
| `test_diagnostics` | 3 |
| `test_domain` | 11 |
| `test_filabridge` | 8 |
| `test_idempotency` | 4 |
| `test_logging` | 3 |
| `test_material_compatibility` | 5 |
| `test_network` | 3 |
| `test_openprinttag` | 10 |
| `test_operations` | 3 |
| `test_scale` | 16 |
| `test_spool_identity` | 8 |
| `test_spoolman` | 10 |
| `test_st25r3916b` | 3 |
| `test_station_workflow` | 10 |
| `test_web_api` | 21 |
| `test_web_configuration` | 8 |

Phase 10-specific suites:

- `test_ota_core`: manager/record/platform-fake transitions and failure
  reconciliation;
- `test_ota_safety`: lifecycle exclusion and unified boot-health policy;
- existing `test_web_api` and `test_operations`: update authentication,
  preconditions, serialization, idempotency, and operation behavior.

Run:

```bash
.venv/bin/pio test --environment native
.venv/bin/pio run --environment wt32-sc01-plus
```

The firmware build proves compilation and link integration of the display,
touch, LVGL, storage, diagnostics, OpenPrintTag, NFC-V, ST25R3916B service,
NAU7802/scale task, configuration/setup, Wi-Fi owner, HTTP(S) transport, both
backend adapters, identity resolver, backend/configuration/scale/device-control/
OTA workers, safe assignment transaction, workflow coordinator, operation
registry, bounded logging/idempotency, embedded web/update assets, ESP-IDF
HTTP/WebSocket and fixed-buffer upload server, mbedTLS SHA-256, NVS OTA record
store, application manifest, and the pinned ESP-IDF partition/image/activation/
confirmation/rollback APIs.

These are three different validation levels:

- Native tests execute portable logic on the host. OTA fakes prove policy and
  cut-point decisions, not ESP32 flash or boot behavior. Native tests do not
  execute FreeRTOS scheduling, Arduino Preferences, LittleFS, the ESP-IDF HTTP
  daemon, sockets, Wi-Fi, LVGL input, a browser, or the bootloader.
- The firmware build compiles and links those embedded boundaries for the
  WT32-SC01 Plus. It proves the pinned APIs and image fit together, but it does
  not flash or execute the image and cannot prove queue timing, flash
  transactions, boot partition changes, reset/rollback behavior, LAN behavior,
  WebSocket client lifetime, or physical sensor/display correctness.
- Hardware/browser verification flashes a real station and exercises the
  actual touchscreen, flash, bootloader, storage, network stack, concurrent
  clients, and supported browsers. It remains required even when host tests and
  both final firmware builds pass.

The current hotfix suite contains 240 host cases across twenty suites.
Provisioning coverage includes first boot, normal connected operation,
three-failure fallback, AP retention during connection attempts, success grace
shutdown, failure retention, millisecond wrap, scan deduplication and
asynchronous start/completion/failure, exact connect-receipt gating, AP-scoped
mutation authorization, tokenless provisioning/local mutation, configured-token
enforcement, secret non-echo, and the existing scale command-safety cases. The
pinned WT32 hotfix build is warning-free at 167,912 RAM bytes and 2,017,881
flash bytes: +72 RAM bytes and -288 flash bytes versus the PR #5 base.

Phase 11 originally contained 225 host cases across the same twenty suites and adds
deterministic counter saturation and OpenPrintTag mutation cases,
embedded-JavaScript syntax validation, and compiler stack-frame reporting. The
WT32 build remains warning-free at 167,152 RAM bytes and 1,948,105 flash bytes.

## Local stabilization gate result — 2026-08-22 pre-commit

The final local stabilization run passes 262/262 native cases across twenty
suites in 00:06:27.085 and 33/33 deterministic browser-transport cases.
Embedded JavaScript syntax validation passes for the 119,132-byte shipped
source. The pinned WT32 build is warning-free at 170,752/327,680 RAM bytes
(52.1%) and 2,090,973/5,242,880 flash bytes (39.9%).

The stack analyzer parses 8,192 frames across 413 files. Its largest project
frames are 7,936 bytes for `Codec::decode` (behind the disabled NFC gate),
6,512 for `OtaWorker::process`, 6,512 for `OtaWorker::run`, 5,312 for OTA
pre-task cleanup, 5,264 for the firmware descriptor, 5,024 to begin streaming
upload, 4,224 for API `snapshot_json`, and 3,232 for the upload handler. Eleven
project frames report dynamic use; the largest estimate is 240
bytes. Frame sizes are individual compiler estimates, not cumulative call-chain
proof.

The final local pre-commit factory bundle passes validation at 2,156,880 bytes,
with embedded version `0.1.0-dev+296d8a47c13d` and SHA-256
`578cb70758d9364bef5684b4e59fd4c0b2644c6df3660e16b641e61498f51b73`.
This bundle predates the final stabilization commit, so it is not the final
release or deployed artifact and no final embedded Git SHA, final artifact
digest, Pages status, or public HTTP result is claimed.

These passing results supersede the temporary pre-run registration estimate but
do not erase the historical Phase 9, Phase 10, Phase 11, or provisioning-hotfix
records above. They also do not execute the physical station: all target
browser/LAN,
scale, Wi-Fi, heap/stack soak, power-loss, backend, OTA/bootloader, and factory
flash checks remain UNVERIFIED.

Run the release checks with:

```bash
git diff --check
python3 tools/check_web_assets.py
node --test tools/test_web_transport.mjs
python3 tools/web_flasher.py validate-source --page web-flasher/index.html --manifest web-flasher/manifest.json
.venv/bin/pio test --environment native
.venv/bin/pio run --environment wt32-sc01-plus
python3 tools/analyze_stack_usage.py
.venv/bin/pio run --environment wt32-sc01-plus --target web-flasher
python3 tools/web_flasher.py validate-bundle --bundle-dir .pio/build/wt32-sc01-plus/web-flasher --maximum-size 16777216
```

The web-flasher checks verify the ESP Web Tools page and manifest paths, the
single merged image at offset zero, ESP32-S3 chip family, source Git SHA,
evaluated PlatformIO upload inputs, and the 16 MiB size bound. They generate no
release or tag and do not prove a physical USB flash; follow
[web-flasher.md](web-flasher.md) for that pending hardware/browser validation.

The final count/build measurements and all-UNVERIFIED hardware/soak matrix are
maintained in [release-validation.md](release-validation.md).

## Embedded browser transport tests

`tools/test_web_transport.mjs` executes the scheduler and live-connection
logic extracted from the shipped embedded JavaScript. Its deterministic fake
transport covers slow responses, dropped GETs, WebSocket reconnects,
configuration GET failure, a mutation during background refresh, duplicate
refreshes, transient operation-poll failure, API 401/409/500, socket exhaustion,
and a stale response arriving after newer state.

The harness must prove:

- ordinary REST concurrency never exceeds two and background work occupies at
  most one slot;
- priority-one mutation/receipt polling is not starved, while identical GETs
  deduplicate and old queued background reads may be superseded;
- one mutation uses one exact body/idempotency key and is never automatically
  replayed after uncertain receipt delivery;
- configuration reaches `READY` or a visible `ERROR`, retries deterministically,
  preserves dirty edits/hidden credentials, and leaves no stuck controls;
- each tab has one WebSocket and one reconnect timer, heartbeat performs no
  refresh, stale/offline/hidden/page-unload transitions clean up correctly, and
  fallback polling ends after recovery;
- tokenless mode reports authentication disabled and local control enabled
  without degraded health or mutation blocking; and
- 100 refresh cycles keep queue/concurrency/resource counters bounded.

This host test validates JavaScript policy and state transitions. It does not
execute a browser TCP stack, ESP-IDF HTTPD, Wi-Fi radio, or heap allocator, so
the one-tab and two-tab hardware procedures below remain release gates.

## Host unit tests

Any logic not requiring physical hardware belongs here: OpenPrintTag
TLV/NDEF/CBOR codec and fixtures, UUID derivation, identity resolution,
reconciliation, JSON parsers, capability detection, migrations, tag-write
planning, toolhead normalization, configuration
patch merging/CAS, operation and idempotency ledgers, bounded log redaction,
coherent diagnostic snapshots, API routing, authorization decisions, and
request/response shape validation. OTA adds platform/digest/record fakes for
state transitions, inactive-slot selection, complete byte/digest checks,
validation/activation separation, durable generation and record integrity,
power-cut reconciliation, confirmation/rollback decisions, unified boot health,
and destructive-operation exclusion.

Fuzz/property tests target bounded parsers with malformed lengths, nesting,
unknown CBOR types/keys, non-canonical ordering, and truncated data.

## Backend contract tests

Host fixtures now exercise
version, list/get, identity filtering, merge-safe extra fields, locations,
explicit creation, remaining-weight update, and verification. Release CI must
also run a pinned live container. FilaBridge tests exercise
health/version, printer IDs, all toolhead entries, zero-based mapping, map,
status verification, unmap, conflicts, and printing state.

Two lanes are maintained:

- release compatibility: exact supported tags/images; failures block release;
- upstream tracking: scheduled current-main/current-release probes; failures
  open an incompatibility signal but never silently change production pins.

## Hardware-in-the-loop gates

### Board/display/touch

First flash `wt32-sc01-plus-display-test`. Verify every labeled color swatch,
the dark-to-light grayscale order, full border, true center marker, and
TOP/BOTTOM/LEFT/RIGHT labels. Touch all four corners and confirm the orange
marker and reported coordinates. Then return to the normal
`wt32-sc01-plus` build and verify the setup, workflow, and diagnostics
screens are readable with no clipped controls. These observations remain
**UNVERIFIED** until performed on physical hardware.

Flash the Phase 1 image and verify:

1. the 480 × 320 diagnostics screen renders with correct color and orientation;
2. touch reaches all four corners and the brightness slider tracks accurately;
3. brightness, automatic dim, Sleep, and a consumed first-touch wake operate;
4. the screen reports PSRAM buffers (or the deliberate internal fallback), NVS,
   LittleFS, and the coredump partition;
5. serial health output remains responsive for at least 30 minutes;
6. reset reason changes appropriately after software reset, watchdog test, and
   power cycle;
7. an interrupted boot increments crash streak, while 30 seconds of healthy
   uptime clears the persisted boot-pending marker;
8. a post-provisioning LittleFS mount failure does not format stored data.

### Scale

Verify raw ADC, internal calibration, zero/tare, multiple reference weights,
repeatability, creep/drift, stable detection, overload/negative handling,
power-cycle persistence, and export/import. Test both empty and representative
full spools without NFC field interference.

### ST25R3916B / NFC-V

1. Verify supply and clock electrically before RF.
2. Read the IC identity over SPI and exercise IRQ/reset recovery.
3. Initialize RFAL and field-on guard time.
4. Inventory a known NFC-V tag and normalize UID.
5. Read geometry, single/multiple blocks, and a full official tag.
6. Write an allowed auxiliary field, read affected blocks, decode, and verify.
7. Repeat after tag removal/replacement.

Fault cases: removal during read/write, two tags, unsupported tag, malformed
NDEF/CBOR, protected block, CRC/protocol error, timeout, field cycling, and
recovery without reboot.

### OTA

Use two visibly distinct, correctly manifested WT32 images and record serial,
`GET /api/v1/update`, boot partition, reset reason, boot/crash counters, and
configuration/calibration state at every cut point:

1. Start with a serial-flashed known-good A and erased/uninitialized OTA data.
   Confirm the first activation seeds A as `VALID` before selecting B, leaves
   rollback available, and reports B as the inactive slot. Repeat from an
   already initialized A to cover the normal `VALID` path.
2. Upload A → B and verify fixed-size progress, exact digest, staged metadata,
   `validation_passed=true`, `activated=false`, and unchanged boot partition at
   `ready_to_reboot`.
3. Confirm reboot explicitly, observe B as `PENDING_VERIFY`, wait the full
   30-second local-health window, and observe B become `VALID`/`confirmed`.
4. Repeat B → A to prove both slots can be inactive and that configuration,
   scale calibration, LittleFS data, and reset markers remain intact.
5. Reject zero/oversize/short bodies, bad digest, malformed ESP image, missing
   appended hash, wrong chip, wrong OpenTag project, and wrong hardware without
   changing the selected boot partition.
6. Remove power during upload at multiple offsets. The previous image must boot
   and reconciliation must report a failed interrupted upload.
7. Remove power after validation but before activation. The image must remain
   unactivated and cancelable.
8. Remove power after activation intent and after boot-slot selection. Restart
   must reconcile only the recorded candidate; it must not activate stale data.
9. Reset during candidate validation and intentionally crash/watchdog the
   candidate before confirmation. Verify automatic bootloader B → A rollback
   and a `rolled_back`/last-invalid result.
10. Repeat candidate failure/reset to prove no crash loop can silently confirm.
11. Keep Spoolman and FilaBridge offline and leave NFC unavailable; an otherwise
    healthy candidate must still confirm.
12. Race factory reset, generic reboot, upload, cancel, and update reboot. Only
    one lifecycle owner may proceed, with no mixed reset/OTA record.
13. Keep the browser open through activation/reboot and verify reconnect resumes
    candidate, confirmation, or rollback status rather than declaring upload
    completion as final success.
14. Inspect logs and wire traffic to confirm Authorization, bearer token, and
    firmware bytes are not logged. Perform this only on an isolated LAN because
    the local HTTP upload itself is plaintext.
15. During a maximum-size upload and manifest scan, record the OTA and HTTP
    task stack high-water marks from an instrumented build. Confirm the bounded
    24 KiB OTA-owner and 20 KiB HTTP-server stacks retain documented safety
    margins and no FreeRTOS stack overflow hook or watchdog fires.

Do not mark rollback hardware-validated until an intentionally failed candidate
has visibly returned to the previous known-good partition.

### Local web/API

Flash a real board on an isolated test LAN and verify:

1. With a blank local API token, verify health is not degraded, setup can
   complete, the UI says authentication DISABLED and browser control ENABLED,
   and scale/config/backend/device/update mutations work without
   `Authorization`. Provision a valid token through the masked touchscreen
   field, verify the UI changes to ENABLED and missing/wrong credentials fail, then
   confirm `GET /api/v1/config` reports only
   `web.access_token_configured: true` and never the token.
2. Inspect the configuration response and logs over the wire for Wi-Fi
   passwords, backend credentials, CA material, authorization values, tokens,
   and PEM leakage. Confirm only the documented configured-state flags appear.
3. Exercise missing, malformed, and correct bearer credentials; required
   content-type/source/idempotency headers; authenticated token rotation; token
   clearing; and physical recovery after clearing.
4. Submit two editors from the same revision and verify one succeeds while the
   stale proposal conflicts without overwriting the winner. Repeat assignment
   confirmation after changing spool generation, printer revision/state, and
   current toolhead spool.
5. Retry an identical idempotency key/body and receive the same operation;
   reuse the key with a different body and receive a conflict. Verify bounded
   operation history and behavior after the ten-minute volatile ledger TTL.
6. Open, close, F5-reload, and reconnect one tab repeatedly; verify it owns
   exactly one WebSocket and one timer. Then operate two tabs and attempt an
   excess live client. Verify graceful fallback/rejection, disconnected socket
   reclamation, coherent state, and no starvation of UI, scale, network,
   configuration, mutation receipts, or backend owners.
7. Render backend/printer/spool/log strings containing HTML metacharacters and
   confirm they remain text, not markup or executable script. Exercise desktop
   and narrow/mobile layouts in each supported browser.
8. Run tare/calibration, backend probe, assignment/unassignment, reboot, and
   factory-reset confirmations through the browser. Verify operation status,
   actual restart, exact-scope data removal, interrupted-reset recovery, and
   post-reset return to fail-closed first-run setup.
9. Generate enough logs and operations to force bounded rollover, then confirm
   cursor/drop/history-gap reporting and ingestion-time redaction remain
   correct on the real endpoint.
10. Exercise tokenless update upload without Authorization; then configure a
    token and exercise missing/wrong/correct values, plus missing/malformed
    headers, stale generation/digest, repeated and conflicting idempotency keys,
    concurrent uploads, slow receive, disconnect, and short/oversized bodies.
    Separately characterize duplicate headers: pinned ESP-IDF exposes only the
    first matching value to the handler, so Phase 10 cannot truthfully claim a
    duplicate-count rejection at this boundary.
11. Verify the Update panel distinguishes upload, validation, inactive install,
    reboot acceptance, candidate boot, confirmation, rollback, and failure;
    aborting an XHR must clean up the owner and reconnect must restore status.
12. Run **Local Interface Self-Test** and capture every REST endpoint's HTTP
    result, latency, API envelope result/error, and the existing WebSocket
    state. Confirm it performs no mutation, opens no second WebSocket, displays
    no body, and exposes no secret.
13. Exercise navigation away/back, setup-AP to LAN transition, LAN loss/rejoin,
    WebSocket loss, and station reboot. Confirm the UI moves through Connecting,
    Connected, Disconnected/retrying, and polling fallback as appropriate,
    resumes live updates, and never carries stale JavaScript state forward.
14. Record free/minimum/largest internal heap block, free/minimum/largest PSRAM
    block, active/maximum HTTP sockets, WebSocket clients, operation/REST queue
    depths, and all task stack margins at boot, HTTP start, first WebSocket,
    initial load, 10 and 100 refreshes, config save, tare, calibration, Wi-Fi
    scan, backend test, and after a 30-minute connected-browser soak. Repeated
    equivalent cycles must not show an unexplained monotonic memory decline or
    stack-margin collapse.

## V0.1 end-to-end acceptance

The product is not V0.1-complete until the physical station passes boot,
OpenPrintTag read, gross/tare/remaining display, deterministic Spoolman
resolution, live T1–T5 display, FilaBridge assignment plus re-read verification,
reboot persistence, OTA, and deliberate rollback tests from the project brief.
