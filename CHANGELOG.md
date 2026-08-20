# Changelog

All notable changes will be documented here. The project follows Semantic
Versioning once releases begin.

## Unreleased

### Added

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
- Explicit NFC-unavailable responses for the wiring/RFAL-gated build, guarded
  reboot/factory-reset commands delegated to their owner, and a read-only
  `/api/v1/update` Phase 10 placeholder with no installer or A/B behavior.

### Verified

- The complete Phase 9 native run passes all 163 cases across eighteen suites.
- The pinned WT32-SC01 Plus firmware build succeeds with RAM usage of
  140,880/327,680 bytes (43.0%) and flash usage of 1,848,673/5,242,880 bytes
  (35.3%): +15,208 RAM bytes and +209,972 flash bytes versus Phase 8.
- Portable routing, parsing, patching, and bounded ledgers are host-executed;
  the embedded browser, transport, context, and device controls are compiled.
- Physical Phase 1 hardware verification remains outstanding.
- Physical ST25R3916B, NFC-V, and OpenPrintTag verification remains outstanding.
- Physical NAU7802/load-cell calibration and repeatability verification remains
  outstanding.
- Physical Wi-Fi, DNS, mDNS, NTP, HTTP, and HTTPS verification remains
  outstanding.
- Live Spoolman v0.26.1, FilaBridge v1.2.2, and five-toolhead assignment
  verification remain outstanding.
- Physical-browser validation of the embedded client, `/api/v1`, WebSocket
  behavior, bearer-token flow, controlled reboot/reset, and target-LAN behavior
  remains outstanding.
- OTA upload, installation, A/B switching, pending-image validation, and
  rollback remain Phase 10 work; Phase 9 implements only the GET placeholder.
