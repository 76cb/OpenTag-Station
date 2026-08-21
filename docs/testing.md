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
- a 16-entry idempotency ledger with same-payload reuse, conflict detection,
  wrap-safe TTL expiration, deterministic overwrite, and concurrent access;
- the complete versioned web route table, global/per-route request and response
  bounds, strict JSON shapes/types, fail-closed bearer authorization, required
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

Phase 11 contains 225 host cases across the same twenty suites and adds
deterministic counter saturation and OpenPrintTag mutation cases,
embedded-JavaScript syntax validation, and compiler stack-frame reporting. The
WT32 build remains warning-free at 167,152 RAM bytes and 1,948,105 flash bytes.
Run the release checks with:

```bash
git diff --check
python3 tools/check_web_assets.py
.venv/bin/pio test --environment native
.venv/bin/pio run --environment wt32-sc01-plus
python3 tools/analyze_stack_usage.py
```

The final count/build measurements and all-UNVERIFIED hardware/soak matrix are
maintained in [release-validation.md](release-validation.md).

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

1. Before a local API token exists, read-only snapshots load but every mutation
   fails closed. Provision a valid token through the masked touchscreen field,
   then confirm `GET /api/v1/config` reports only
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
   operation history and behavior after the 60-second volatile ledger TTL.
6. Open, close, reload, and reconnect multiple browser/WebSocket clients
   repeatedly. Verify disconnected sockets are reclaimed, updates remain
   coherent, and HTTP handling does not starve the UI, scale, network, or
   backend owners.
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
10. Exercise update upload with absent/wrong/correct token, missing/malformed
    headers, stale generation/digest, repeated and conflicting idempotency keys,
    concurrent uploads, slow receive, disconnect, and short/oversized bodies.
    Separately characterize duplicate headers: pinned ESP-IDF exposes only the
    first matching value to the handler, so Phase 10 cannot truthfully claim a
    duplicate-count rejection at this boundary.
11. Verify the Update panel distinguishes upload, validation, inactive install,
    reboot acceptance, candidate boot, confirmation, rollback, and failure;
    aborting an XHR must clean up the owner and reconnect must restore status.

## V0.1 end-to-end acceptance

The product is not V0.1-complete until the physical station passes boot,
OpenPrintTag read, gross/tare/remaining display, deterministic Spoolman
resolution, live T1–T5 display, FilaBridge assignment plus re-read verification,
reboot persistence, OTA, and deliberate rollback tests from the project brief.
