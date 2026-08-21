# OpenTag Station

OpenTag Station is a source-available, dedicated filament-management terminal for a
WT32-SC01 Plus, NAU7802 load-cell ADC, and **ST25R3916B** NFC reader. Its target
workflow is:

> Place spool → recognize OpenPrintTag → weigh → resolve in Spoolman → tap T1–T5
> → assign through FilaBridge → verify.

This is a clean implementation, not a SpoolmanScale fork. SpoolmanScale is used
only as a hardware-reference source for the shared physical platform. There is
no PN532 implementation or fallback.

## Project status

The repository contains the Phase 0 research baseline and implemented,
host-tested, firmware-compiled software through Phase 11 release hardening:

- pinned PlatformIO, ESP32, LVGL, display, scale, and JSON dependencies;
- A/B OTA partition layout;
- centralized WT32-SC01 Plus board profile;
- deliberately unassigned ST25R3916B pins and a compile-time wiring guard;
- normalized domain models and backend interfaces;
- a bounded application-event primitive;
- deterministic build metadata;
- a LovyanGFX ST7796 parallel display and FT6336-compatible touch driver using
  the centralized WT32-SC01 Plus profile;
- a dedicated LVGL owner task with PSRAM draw buffers and an internal-memory
  fallback, touch debounce/range checks, brightness, dim, sleep, and wake;
- NVS boot/crash tracking, one-time LittleFS provisioning, coredump partition
  detection, reset reason, uptime, heap, and PSRAM diagnostics;
- a hardware diagnostics screen and typed application/tag-presence state
  machines;
- a bounded Type 5 TLV, NDEF, and CBOR OpenPrintTag codec pinned to the current
  official fixture revision, including all defined main/auxiliary fields,
  validation, and byte-preserving consumed-weight updates;
- an NFC-V protocol boundary for UID normalization, single-tag enforcement,
  geometry, lock status, full-image reads, minimal block-diff plans, per-block
  presence checks, and exact readback verification;
- configurable ESP32 RFAL SPI/GPIO/IRQ/timer/mutex primitives plus a testable
  ST25R3916B bring-up/recovery sequence that keeps the RF field and frontend
  powered down after a failed step;
- a bounded NAU7802 driver on the second I2C controller, a dedicated retrying
  scale task, moving-average/stability/creep/overload processing, tare and
  signed reference calibration, and CRC-protected schema-versioned NVS
  persistence, with the actual YZC-133 5 kg variant as the default persistent
  hardware profile and the YZC-133 2 kg variant retained as a supported profile;
- strict gross/empty/net weight semantics, documented empty-weight source
  priority, and normal/warning/confirmation reconciliation bands;
- one schema-versioned configuration service with additive migration,
  unknown-field preservation, validated/redacted export and import, atomic
  LittleFS staging, backup recovery, legacy 2 kg calibration inference, and a
  mutex-protected revision used for compare-and-swap updates;
- an eight-step on-device first-run flow that remains navigable while Wi-Fi or
  either backend is unavailable, with flash writes delegated to a bounded
  configuration worker rather than the LVGL owner;
- a dedicated Wi-Fi owner with asynchronous scan, stored credentials,
  bounded connection attempts, exponential reconnect, DHCP/RSSI/IP/DNS/mDNS/NTP
  diagnostics, plus bounded HTTP and CA-verified HTTPS transport;
- a bounded Spoolman adapter with health/runtime probes, explicit capability
  reporting, strict normalized responses, filter/list/get/create/location/field
  operations, guarded verified remaining-weight writes, and one-key custom-field
  updates that preserve unrelated metadata;
- deterministic spool identity resolution through configured instance UUID,
  confirmed cache, NFC UID, GTIN/package/material identity, and metadata, with
  explicit ambiguous/conflict results and persistent conflict-checked mappings;
- a bounded FilaBridge adapter against the revalidated `sargonas/filabridge`
  v1.2.2/current-main contract, with stable printer IDs, normalized live state,
  complete zero-based toolhead maps, version/capability guards, and map/unmap;
- a transaction service that revalidates live printer/toolhead state, blocks
  casual active-print changes, requires occupied-toolhead confirmation, performs
  one mutation, and only reports success after an exact mapping readback;
- a host-testable decoded-tag → stable-weight → deterministic Spoolman match →
  reconciliation → T1–T5 → verified assignment coordinator that retains local
  tag/scale data through backend outages and never queues offline assignments;
- a bounded backend owner task and a Prusa XL-oriented touchscreen workflow with
  live T1–T5 mappings, explicit replacement/advanced-print warnings, backend
  degradation, and local material/toolhead advisories;
- a bounded embedded browser client, WebSocket live-update channel, and
  transport-neutral 26-route `/api/v1` surface for status, configuration,
  diagnostics, logs, scale, NFC, spool, printers/toolheads, controlled device
  actions, operation status, and validated firmware updates;
- asynchronous mutation receipts backed by a central operation registry and
  bounded configuration, scale, backend, and device-control owner queues;
- typed, credential-redacted configuration reads and compare-and-swap partial
  writes, with an initial local API bearer token provisioned from the physical
  touchscreen and browser credentials retained only in memory for the current
  tab;
- a dedicated OTA owner that streams fixed 4 KiB chunks only to the inactive
  5 MiB application slot, checks complete length and rolling SHA-256, verifies
  the ESP32-S3 image and OpenTag manifest, and leaves a validated image
  unactivated until an explicit authenticated reboot;
- durable OTA generation/operation metadata, one shared 30-second local boot
  health policy, ESP-IDF pending-image confirmation, and bootloader rollback
  integration that is mutually exclusive with generic reboot and factory reset;
- a release-hardening audit covering runtime ownership, task stacks, long-run
  behavior, persistent write ordering, configuration migration, API/UI
  consistency, failure injection, security, CI, and reproducibility, with one
  explicitly unverified physical validation matrix;
- native host suites covering the domain foundation, official OpenPrintTag
  fixtures and malformed inputs, NFC-V safety, frontend recovery, and scale
  processing/fault behavior, configuration transactions/migrations/recovery,
  setup navigation, URL policy, reconnect backoff, Spoolman contracts, and
  deterministic identity resolution, FilaBridge contracts, assignment safety,
  material advisories, the full host workflow, and the local web/API boundary.

The verified Phase 10 source baseline passes 223/223 cases across twenty suites.
Its pinned WT32-SC01 Plus firmware builds without compiler warnings at 167,152 of
327,680 RAM bytes (51.0%) and 1,946,637 of 5,242,880 flash bytes (37.1%):
+26,272 RAM bytes and +97,964 flash bytes versus Phase 9.
The Phase 11 suite contains 225 host cases across the same twenty suites. Its
hardened build uses 167,152 RAM bytes (51.0%, no Phase 10 change) and 1,948,105
flash bytes (37.2%, +1,468 bytes versus Phase 10), with no compiler warnings.
Display, touch, backlight, storage, and PSRAM behavior still require execution
on the actual board, so Phase 1 is not hardware-verified. NFC cannot be connected
to a real frontend until the exact module/wiring and RFAL distribution gates are
resolved; the current firmware and API report NFC explicitly unavailable rather
than pretending a reader exists. Scale hardware behavior and physical
calibration are not verified.
Wi-Fi and transport behavior still require physical/on-network validation.
Portable API routing, parsing, patching, bounded ledgers, OTA state transitions,
boot-health decisions, lifecycle exclusion, and failure cleanup are host-tested.
The embedded browser, production API context, HTTP/WebSocket upload transport,
ESP-IDF OTA adapter, and device-control integration are firmware-compiled but
not browser/LAN/flash-tested.
Spoolman and FilaBridge behavior still require validation against live pinned
instances, and the five-toolhead flow requires physical execution before
release signoff. A/B installation, candidate confirmation, deliberate failure,
and rollback still require the hardware-in-the-loop matrix before release
signoff.

See [Phase 11 release-candidate validation](docs/release-validation.md) for the
ownership/resource audit, remaining limitations, and physical test matrix.

## Responsibilities

| Component | Authority |
|---|---|
| Spoolman | Inventory, spool records, remaining database weight, locations |
| FilaBridge | Printer state, toolhead mappings, print monitoring, consumption |
| Scale | Gross physical weight at the time of weighing |
| OpenPrintTag | Portable identity, material/container metadata, portable usage data |
| OpenTag Station | Orchestration, safe reconciliation, interaction, diagnostics, OTA |

The touchscreen UI and local `/api/v1` surface call the same application
services and bounded owner queues. Neither calls Spoolman or FilaBridge HTTP
endpoints directly.

## Hardware baseline

- WT32-SC01 Plus: ESP32-S3, 480 × 320 ST7796 display, FT6336U-compatible touch,
  16 MB flash, PSRAM.
- NAU7802 and YZC-133 5 kg beam load cell as the actual/default profile; the
  YZC-133 2 kg profile remains supported.
- ST25R3916B over SPI, using ST RFAL and NFC-V/ISO15693 first.

The ST25R3916B is the product's only reader architecture because it provides the
NFC-V capability, field diagnostics, interrupt-driven operation, and RFAL path
needed for OpenPrintTag and later protocol growth. PN532 hardware and software
are intentionally unsupported.

The exact ST25R3916B breakout/module is not yet identified. Its CS, IRQ, reset,
SPI, power, bus-selection, and antenna connections therefore remain a documented
[wiring checkpoint](docs/wiring.md). They must be resolved in the one board
profile before NFC can be enabled.

## Build

Prerequisites: Python 3.12 and Git.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install --requirement requirements-dev.txt
.venv/bin/pio test --environment native
.venv/bin/pio run --environment wt32-sc01-plus
```

PlatformIO Core is pinned in `requirements-dev.txt`; all firmware libraries and
the ESP32 platform are pinned in `platformio.ini`. Release builders should set
`SOURCE_DATE_EPOCH`; otherwise the build date is derived from the Git commit
date, not the wall clock.

## First install

For first installation or USB recovery, use the HTTPS
[browser firmware installer](docs/web-flasher.md) from desktop Chrome or Edge.
It flashes a generated ESP Web Tools factory image without requiring
PlatformIO on the user's computer. Normal future updates should use the
station's authenticated local A/B OTA Update panel.

For development or direct serial installation, connect the WT32-SC01 Plus over
USB, identify its serial port, and run:

```bash
.venv/bin/pio run --environment wt32-sc01-plus --target upload --upload-port PORT
.venv/bin/pio device monitor --baud 115200 --port PORT
```

The firmware boots to serial and the on-device first-run flow; diagnostics
remain reachable even when setup is incomplete. Use
the full display, brightness slider, and Sleep button to verify touch geometry,
backlight control, and wake behavior. Confirm NVS, LittleFS, coredump, heap,
PSRAM, and live scale state on screen and in the serial health line. Follow the
[scale validation procedure](docs/scale.md) before trusting weight values. Do
not enable the NFC build flag until the exact ST25R3916B module and wiring
checkpoint have been signed off.

## Firmware updates and recovery

The partition table reserves two 5 MiB application slots plus persistent NVS,
OTA metadata, data storage, and crash dumps. The local Update panel hashes a
selected `.bin` in the browser and streams it as `application/octet-stream`;
the station never buffers the whole image. The OTA owner selects the inactive
slot itself and accepts at most one 4 KiB chunk at a time. It verifies declared
and received length, SHA-256, the ESP image/header and appended image hash, the
ESP32-S3 target, and an embedded OpenTag project/hardware manifest.

A successful upload reaches `ready_to_reboot` with the candidate still
unactivated. An explicit authenticated and idempotent reboot request must match
the candidate's operation ID, durable generation, and SHA-256 before the boot
slot changes. On a serial-flashed device with uninitialized OTA metadata, the
pinned adapter first selects the currently running slot and marks it valid,
then re-resolves the inactive target before selecting the candidate. It refuses
candidate selection from an unrelated pending, invalid, or aborted image. The
durable record narrowly recovers a power cut or transient failure while seeding
the pre-existing running image, and the exact reboot request can retry when no
otadata side effect occurred. This ordering preserves a known-good rollback
entry for the first browser update.
The candidate then remains pending through one shared 30-second
local-health window. Core local owners must be running, but Spoolman,
FilaBridge, and NFC availability are deliberately not validity requirements.
Healthy firmware calls the ESP-IDF confirm/cancel-rollback API; a fatal startup
or an unconfirmed reset leaves the pinned rollback-enabled bootloader able to
restore the previous slot.

Image SHA-256 provides integrity, not publisher authenticity: Phase 10 does not
implement signed firmware. The local web server is HTTP rather than HTTPS, so
updates must be performed only on a trusted isolated LAN. There is no URL-based
OTA path. The HTTPS [browser flasher](docs/web-flasher.md), direct PlatformIO
upload, and the bootloader serial protocol remain USB recovery paths when the
local service cannot start. A factory/erase operation can clear configuration
and calibration even though routine local OTA leaves them outside both
application slots. See [OTA architecture](docs/ota.md).

## Known limitations

- Phase 1 display, touch, backlight, PSRAM, persistence, and reset diagnostics
  are compiled but have not been exercised on physical hardware in this
  repository.
- ST RFAL is selected but not yet acquired, pinned, or vendored under ST's terms.
- The ST25R3916B module, antenna, and WT32 pin assignment remain unresolved.
- NFC-V and OpenPrintTag behavior is host-unit-tested but has not been exercised
  through RFAL or on a physical tag.
- Spoolman and FilaBridge contracts were source-inspected and host-fixture
  tested, not tested against running instances.
- The scale subsystem is compiled and host-tested but the NAU7802/load cell,
  calibration accuracy, repeatability, and persistence remain unverified on the
  physical station. The software defaults to the actual 5 kg profile and retains
  backward-compatible 2 kg support; neither profile is physically validated.
- Phase 6 configuration and networking compile and their platform-independent
  policy is host-tested, but Wi-Fi association, DHCP/DNS/mDNS/NTP, HTTP, and
  CA-verified HTTPS remain unverified on the physical station and target LAN.
- The Spoolman/FilaBridge adapters and the touchscreen orchestration are
  host-tested but not yet tested against live pinned servers or the physical
  five-toolhead station.
- The portable 26-route API policy and OTA core/safety policies are host-tested.
  The browser UI, production context, WebSocket reconnect/live-refresh path,
  bearer-token flow, streaming firmware transport, controlled reboot/reset
  path, ESP-IDF OTA adapter, and embedded owner-queue integration are
  firmware-compiled but remain unverified in a physical browser on the target
  LAN and hardware.
- Firmware images are unsigned, and the local HTTP upload is not protected by
  transport TLS. Bearer authentication, same-origin mutation headers,
  idempotency, SHA-256, and image/manifest validation do not replace a signed
  release chain or an encrypted trusted network.
- Physical A→B and B→A updates, confirmation, intentional candidate failure,
  bootloader rollback, power interruption at each cut point, crash-loop
  recovery, and browser reconnect after reboot remain hardware-validation
  requirements. Host tests and a successful firmware link do not prove them.

## Documentation

- [Architecture](docs/architecture.md)
- [Hardware assumptions](docs/hardware.md)
- [Wiring checkpoint](docs/wiring.md)
- [OpenPrintTag strategy](docs/openprinttag.md)
- [Scale subsystem](docs/scale.md)
- [Spoolman adapter](docs/spoolman.md)
- [FilaBridge adapter](docs/filabridge.md)
- [Configuration and migrations](docs/configuration.md)
- [Local web UI and API](docs/web.md)
- [OTA and rollback](docs/ota.md)
- [Testing](docs/testing.md)
- [Upstream compatibility](docs/UPSTREAM_COMPATIBILITY.md)

## License

The project is distributed under the
[PolyForm Noncommercial License 1.0.0](LICENSE.md). It permits noncommercial
use and distribution but is not an OSI-approved open-source license. ST RFAL is
distributed separately under ST's terms and is not currently vendored.
