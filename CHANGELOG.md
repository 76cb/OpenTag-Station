# Changelog

All notable changes will be documented here. The project follows Semantic
Versioning once releases begin.

## Unreleased

### Fixed

- Correct the LVGL-to-LovyanGFX RGB565 byte-order boundary on WT32-SC01 Plus,
  increase on-device text hierarchy and contrast, remove the workflow toolhead
  overlap, and add an opt-in color/geometry/touch display self-test build.
- Explicitly mount and first-use-format the custom `littlefs` partition by label;
  post-provisioning mount failures remain fail-safe and never auto-format data.
- Isolate the NAU7802 on `Wire` from the touchscreen on `Wire1`, and add a
  one-shot bounded GPIO10/11 startup scan with diagnostic-only reversed-wire
  detection that always restores the production pin assignment.

### Added

- An ESP Web Tools HTTPS installer for WT32-SC01 Plus first installation and
  USB recovery, with a PlatformIO-derived merged factory image, pinned GitHub
  Pages automation, source-SHA validation, and no change to normal local A/B
  OTA behavior.
- Phase 11 release-candidate validation record with runtime ownership, task and
  dynamic-stack inventory, persistent write ordering, security limitations,
  and an explicitly UNVERIFIED physical hardware matrix.
- Deterministic embedded-JavaScript syntax and compiler stack-usage CI checks.
- Exhaustive official-fixture truncation and single-byte mutation safety tests.
- Saturating lifetime diagnostic counters, completion-paced backend probes,
  and browser epochs/revisions that exclude stale REST/WebSocket updates.
- Phase 0 upstream compatibility baseline.
- Layered firmware and repository architecture.
- Reproducible PlatformIO dependency pins and deterministic build metadata.
- A/B OTA partition table.
- Central WT32-SC01 Plus board profile with an unresolved ST25R3916B wiring guard.
- Normalized spool, printer, toolhead, weight, error, result, capability, and event types.
- Native unit-test foundation and firmware CI build.
- Phase 1 WT32-SC01 Plus ST7796 display, PWM backlight, and FT6336-compatible
  touch integration through LovyanGFX.
- Dedicated LVGL task with PSRAM-first draw buffers, internal fallback,
  brightness/dim/sleep/wake controls, and hardware diagnostics screen.
- NVS boot/crash-loop tracking, safe first-use LittleFS provisioning, coredump
  partition discovery, reset, uptime, heap, and PSRAM diagnostics.
- Typed application and debounced tag-presence state machines plus runtime task
  ownership/deadline contracts.
- Pinned OpenPrintTag Type 5/NDEF/bounded-CBOR codec with complete current field
  model, validation, official fixtures, and unknown-field-preserving auxiliary
  consumed-weight updates.
- NFC-V single-tag discovery, UID normalization, geometry/security status,
  multi-block image reads, minimal write plans, tag-change protection, and exact
  block readback verification.
- ST25R3916B bring-up/recovery service boundary covering power, reset, identity,
  IRQ, RFAL initialization, and RF field sequencing while RFAL/wiring remain
  gated.
- Configurable ESP32 RFAL platform primitives for SPI transactions, power/reset
  GPIO, latched IRQ acknowledgement, monotonic timing, bounded bus locking, and
  critical sections, without hard-coded production pins or electrical guesses.
- Bounded NAU7802 detection/setup/internal-calibration and raw-read adapter on a
  dedicated I2C controller and FreeRTOS scale task with retrying diagnostics.
- Moving-average scale processing with configurable stability threshold/time,
  tare, signed reference calibration, negative/overload/disconnect detection,
  and creep awareness.
- CRC-protected schema-versioned NVS calibration persistence owned by the
  central storage service.
- Strict gross/empty/net weight semantics, deterministic empty-spool source
  priority, and normal/warning/confirmation reconciliation policy.
- Central schema-2 JSON configuration service with additive schema-1 migration,
  unknown-field preservation, hardware/range validation, credential-redacted
  export, guarded import, atomic LittleFS staging, and validated backup recovery.
- Migration of Phase 5 scale calibration into the central document while
  retaining the CRC-protected legacy mirror for rollback compatibility.
- Navigable eight-step first-run setup covering Wi-Fi, Spoolman, FilaBridge,
  printer identity, scale, NFC status, and readiness without requiring online
  backends.
- A bounded configuration worker so LVGL callbacks never perform flash writes.
- Wi-Fi scan/connect/reconnect ownership with stored credentials, bounded
  connection attempts, exponential backoff, DHCP/RSSI/IP/DNS/mDNS/NTP status,
  and on-device diagnostics.
- Bounded HTTP transport with strict URL/header/body/response limits,
  redirects disabled, explicit DNS errors, and CA-verified HTTPS only.
- Phase 7 Spoolman adapter pinned to the revalidated v0.26.1/current-master
  contract with health/version and per-operation capability probes, bounded
  normalized parsing, explicit create, location/field discovery, and guarded
  write operations.
- Concurrent-use preconditions and read-after-write verification for explicit
  remaining-weight reconciliation, plus current merge-safe one-key custom-field
  patch semantics.
- Deterministic instance UUID, confirmed-cache, NFC UID, package/material, and
  metadata resolution that returns conflicts/ambiguity instead of selecting a
  plausible spool silently.
- Schema-3 confirmed spool identity mappings with additive schema-1/schema-2
  migrations, bounded persistence, normalization, and duplicate-conflict
  rejection.
- Phase 8 FilaBridge adapter pinned to the revalidated v1.2.2/current-main
  external API contract, with stable printer discovery, normalized printer
  states, complete toolhead mappings, bounded JSON/HTTP, auth-proxy headers,
  guarded capabilities, assignment, and unassignment.
- Exact assignment transactions with live precondition revalidation, occupied
  toolhead confirmation, active/unverified-printer-state advanced override,
  guarded read-only compatibility mode, disabled-profile enforcement, a single
  non-replayed mutation, and mandatory mapping readback verification.
- Advisory material/toolhead checks for abrasive filament on brass, temperature
  and minimum-nozzle limits, flexible/support material, and disabled profiles.
- A thread-safe end-to-end workflow snapshot preserving OpenPrintTag and scale
  data during Spoolman/FilaBridge outages, rejecting ambiguous matches, and
  carrying reconciliation and assignment results to the UI.
- A bounded backend FreeRTOS owner plus a Prusa XL-first LVGL screen displaying
  live T1–T5 assignments and explicit replacement/active-print warnings without
  performing HTTP from touch callbacks.
- Phase 9 transport-neutral routing with 23 metadata-declared `/api/v1` routes,
  a consistent versioned JSON envelope, bounded request parsing, strict typed
  schemas, mutation idempotency keys, and conflict responses.
- Compile-time-bounded embedded HTML/CSS/JavaScript assets with an accessible
  local administration UI and `/api/v1/events` WebSocket live updates. Browser
  refreshes are event-driven and coalesced rather than driven by aggressive
  global polling.
- A central operation registry returning asynchronous operation IDs for
  configuration, scale, backend, toolhead, reboot, and factory-reset
  commands, with bounded owner queues and targeted terminal-status checks.
- Credential-redacted configuration reads and allowlisted imports plus typed
  partial PATCH updates guarded by an expected configuration revision. Omitted
  credentials are preserved and explicit empty values clear them.
- Local API mutation authentication using a 16–128 character bearer token whose
  initial value is provisioned on the physical touchscreen. The browser keeps
  the entered command token only in memory for the current tab and clears it
  after an unauthorized response.
- Persistent YZC-133 hardware profiles corrected to the actual/default 5 kg
  cell while retaining the 2 kg variant and legacy-calibration inference.
- Phase 9 explicit NFC-unavailable responses for the wiring/RFAL-gated build,
  guarded reboot/factory-reset commands delegated to their owner, and a
  read-only `/api/v1/update` boundary.
- Phase 10 portable OTA state/metadata ownership with explicit receiving,
  writing, validation, staged, reboot, candidate, confirmation, rollback, and
  failure states behind mockable partition, digest, and durable-record ports.
- An ESP-IDF 4.4/Arduino-ESP32 2.0.17 OTA adapter that resolves the inactive
  slot internally, rejects the running partition, uses `esp_ota_begin/write/end`,
  verifies the staged ESP32-S3 image, changes the boot slot only after explicit
  confirmation, and calls the pinned candidate confirm/rollback APIs.
- First-update rollback seeding for serial-flashed devices with erased OTA
  metadata: the adapter marks the currently running slot valid before selecting
  a candidate, then re-resolves the inactive target and refuses activation from
  any pending, invalid, or aborted running image.
- A fixed 256-byte OpenTag firmware manifest carrying project, WT32 hardware,
  version, Git SHA, build date, and platform identity inside every image, with
  staged-image project/hardware enforcement.
- A dedicated bounded OTA FreeRTOS owner and streaming binary HTTP path using a
  fixed 4 KiB handoff, an exact nonzero `Content-Length`, a 5 MiB ceiling,
  rolling SHA-256, five-second receive-idle timeout, 180-second absolute
  deadline, and deterministic abort/cleanup on short or disconnected uploads.
- Checksum-protected OTA records in an isolated NVS namespace, durable monotonic
  generation reservation, operation/generation/digest preconditions, progress
  checkpoints, activation-intent ordering, and boot-time reconciliation for
  interrupted upload, power-cut, confirmation, and rollback cut points.
- An intentionally unactivated `ready_to_reboot` boundary: upload completion
  cannot select the boot partition, and only the exact authenticated,
  idempotent reboot mutation can activate and restart into the candidate.
- One 30-second local boot-health policy shared by ordinary boot/crash tracking
  and candidate validation. Required local owners must start; backend service
  outages and deliberately unavailable NFC do not block confirmation.
- A generation-token device lifecycle gate that makes generic reboot, factory
  reset, OTA upload/activation, and candidate validation mutually exclusive.
- A 26-route API inventory, update WebSocket snapshots, and an embedded Update
  panel with browser-side hashing, upload progress, explicit reboot confirmation,
  bounded validation/rollback errors, and reconnect/resume status after restart.

### Verified

- The complete Phase 9 native run passes all 163 cases across eighteen suites.
- The final Phase 10 native run passes all 223 cases across twenty suites.
- The pinned Phase 10 WT32-SC01 Plus firmware build succeeds without compiler
  warnings with RAM usage of 167,152/327,680 bytes (51.0%) and flash usage of
  1,946,637/5,242,880 bytes (37.1%): +26,272 RAM bytes and +97,964 flash bytes
  versus the Phase 9 baseline.
- For comparison, the Phase 9 WT32-SC01 Plus firmware used
  140,880/327,680 bytes (43.0%) and flash usage of 1,848,673/5,242,880 bytes
  (35.3%): +15,208 RAM bytes and +209,972 flash bytes versus Phase 8.
- Portable routing, parsing, patching, bounded ledgers, OTA transitions,
  boot-health decisions, update serialization, and lifecycle exclusion are
  exercised by native suites with fake partition/digest/record providers.
- The embedded browser, streaming transport, production OTA owner/context,
  ESP-IDF image adapter, and rollback calls are covered by the pinned WT32 build.
- Physical Phase 1 hardware verification remains outstanding.
- Physical ST25R3916B, NFC-V, and OpenPrintTag verification remains outstanding.
- Physical NAU7802/load-cell calibration and repeatability verification remains
  outstanding.
- Physical Wi-Fi, DNS, mDNS, NTP, HTTP, and HTTPS verification remains
  outstanding.
- Live Spoolman v0.26.1, FilaBridge v1.2.2, and five-toolhead assignment
  verification remain outstanding.
- Physical-browser validation of the embedded client, `/api/v1`, WebSocket
  behavior, bearer-token flow, streaming upload, controlled reboot/reset, and
  target-LAN behavior remains outstanding.
- Physical A→B and B→A installation, candidate confirmation, deliberate
  candidate failure, bootloader rollback, power loss during upload and after
  activation, reset/crash-loop handling during the health window, and browser
  reconnect after reboot remain outstanding hardware-in-the-loop gates.
- Phase 10 validates image integrity and identity but does not authenticate a
  publisher: images are unsigned. The local upload endpoint is HTTP, not HTTPS,
  and is intended only for a trusted isolated LAN. No remote URL updater or
  globally weakened TLS policy was added.
