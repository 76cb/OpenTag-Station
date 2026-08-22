# OpenTag Station

<p align="center">
  <strong>A dedicated ESP32-S3 filament-management terminal for OpenPrintTag, Spoolman, and FilaBridge.</strong>
</p>

<p align="center">
  Place spool → identify → weigh → resolve → assign to T1–T5 → verify
</p>

<p align="center">
  <a href="https://76cb.github.io/OpenTag-Station/">
    <img src="https://img.shields.io/badge/FLASH%20OPENTAG%20STATION-OPEN%20WEB%20FLASHER-0A84FF?style=for-the-badge&logo=espressif&logoColor=white" alt="Open the OpenTag Station Web Flasher">
  </a>
</p>

<p align="center">
  <a href="https://github.com/76cb/OpenTag-Station/actions">
    <img src="https://img.shields.io/github/actions/workflow/status/76cb/OpenTag-Station/ci.yml?branch=main&style=flat-square&label=build" alt="Build status">
  </a>
  <a href="https://github.com/76cb/OpenTag-Station">
    <img src="https://img.shields.io/github/last-commit/76cb/OpenTag-Station?style=flat-square" alt="Last commit">
  </a>
  <a href="LICENSE.md">
    <img src="https://img.shields.io/badge/license-PolyForm%20Noncommercial-blue?style=flat-square" alt="PolyForm Noncommercial License">
  </a>
</p>

---

## What is OpenTag Station?

OpenTag Station is a **source-available, standalone filament-management terminal** built around the WT32-SC01 Plus.

It is designed to combine:

- **OpenPrintTag** for portable filament identity and metadata
- **NAU7802 + load cell** for physical spool weight
- **Spoolman** for filament inventory and remaining-weight records
- **FilaBridge** for Prusa XL toolhead assignment
- **ST25R3916B** for NFC-V / ISO15693 tag access

The intended workflow is simple:

> **Place spool → read tag → weigh spool → resolve it in Spoolman → tap T1–T5 → assign through FilaBridge → verify the result**

This is a clean implementation, not a SpoolmanScale fork. SpoolmanScale is used only as a hardware-reference source for the shared physical platform.

There is **no PN532 implementation or fallback**. The NFC architecture is ST25R3916B + RFAL.

---

## Quick start

### 1. Flash the station

The easiest installation path is the browser flasher:

<p>
  <a href="https://76cb.github.io/OpenTag-Station/">
    <img src="https://img.shields.io/badge/Open%20the%20Web%20Flasher-Install%20OpenTag%20Station-0A84FF?style=for-the-badge&logo=googlechrome&logoColor=white" alt="Open OpenTag Station Web Flasher">
  </a>
</p>

Use a current desktop version of **Chrome or Edge**, connect the WT32-SC01 Plus over USB, and follow the installer.

For first installation or USB recovery details, see [Web Flasher](docs/web-flasher.md).

### 2. Configure Wi-Fi

On an unconfigured station, OpenTag Station creates a temporary setup network:

```text
OpenTag-Setup-XXXX
```

Connect to it and open:

```text
http://192.168.4.1/
```

The browser interface is used for Wi-Fi setup, configuration, diagnostics, scale calibration, and local management.

See [Browser provisioning](docs/provisioning.md).

### 3. Calibrate the scale

Open the **Scale** section in the local web UI:

1. Empty the platform.
2. Wait for the raw reading to settle.
3. Tare the empty scale.
4. Place an accurate known reference weight on the platform.
5. Enter the reference mass in grams.
6. Calibrate.
7. Verify repeated zero and loaded readings.

See [Scale subsystem](docs/scale.md).

---

## Hardware

| Component | Role |
|---|---|
| **WT32-SC01 Plus** | ESP32-S3 controller, 480 × 320 display, touch, 16 MB flash, PSRAM |
| **NAU7802** | 24-bit load-cell ADC |
| **YZC-133 5 kg** | Default load-cell profile |
| **YZC-133 2 kg** | Optional supported profile |
| **ST25R3916B** | NFC-V / ISO15693 frontend |
| **Spoolman** | Filament inventory and spool records |
| **FilaBridge** | Printer/toolhead state and spool assignment |

### Current scale wiring

The external NAU7802 bus uses:

| Signal | WT32-SC01 Plus |
|---|---|
| SDA | GPIO10 |
| SCL | GPIO11 |
| Address | `0x2A` |

The scale bus is intentionally isolated from the touchscreen I²C controller.

See [Hardware assumptions](docs/hardware.md) and [Wiring](docs/wiring.md).

---

## Current project status

OpenTag Station is under active hardware bring-up and validation.

| Area | Status |
|---|---|
| Firmware build / CI | Implemented and continuously validated |
| Display | Running on WT32-SC01 Plus; active hardware validation |
| Touch | Running on WT32-SC01 Plus; active hardware validation |
| NAU7802 detection | Confirmed on physical hardware at `0x2A` |
| Load-cell calibration | Implemented; physical calibration/accuracy validation in progress |
| Browser configuration UI | Implemented |
| Browser Wi-Fi provisioning | Implemented; physical reliability validation in progress |
| Web flasher | Published and working through GitHub Pages |
| A/B OTA / rollback | Implemented; full hardware rollback matrix still pending |
| Spoolman integration | Implemented and host-tested; live-instance validation pending |
| FilaBridge integration | Implemented and host-tested; live-instance validation pending |
| ST25R3916B / NFC | Intentionally disabled until RFAL/wiring checkpoint is completed |

The firmware intentionally reports unavailable hardware rather than pretending a subsystem is ready.

---

## Major features

### Scale

- dedicated external I²C controller
- NAU7802 detection and diagnostics
- moving-average filtering
- stability detection
- tare
- signed reference calibration
- overload handling
- 5 kg default profile
- optional 2 kg profile
- CRC-protected persisted calibration
- raw counts, filtered counts, zero offset, counts-per-gram, and reference-mass diagnostics

### OpenPrintTag / NFC-V

- Type 5 TLV parsing
- NDEF
- CBOR OpenPrintTag codec
- UID normalization
- tag geometry and lock-state handling
- full-image reads
- minimal block-diff writes
- per-block presence checks
- exact readback verification

NFC hardware remains disabled until the physical ST25R3916B integration is signed off.

### Spoolman

- health and capability probing
- deterministic spool identity resolution
- remaining-weight reconciliation
- location and custom-field support
- guarded remaining-weight updates
- conflict-aware local mappings

### FilaBridge

- printer state
- five-toolhead mapping
- assign / unassign operations
- live-state revalidation
- active-print safety checks
- exact mapping readback before success is reported

### Local web interface

The embedded browser UI provides:

- system status
- device information
- Wi-Fi configuration
- scale diagnostics and calibration
- NFC status
- spool state
- printer/toolhead state
- configuration
- logs and diagnostics
- local firmware update
- device controls

The local API is exposed under `/api/v1`.

### Firmware update architecture

OpenTag Station uses:

- two 5 MiB application slots
- validated A/B updates
- rollback support
- inactive-slot installation
- SHA-256 verification
- ESP32-S3 image validation
- project/hardware manifest validation
- explicit reboot into the candidate
- local boot-health confirmation

See [OTA and rollback](docs/ota.md).

---

## Build from source

### Requirements

- Python 3.12
- Git

### Build

```bash
python3 -m venv .venv
.venv/bin/python -m pip install --requirement requirements-dev.txt
.venv/bin/pio test --environment native
.venv/bin/pio run --environment wt32-sc01-plus
```

PlatformIO Core is pinned in `requirements-dev.txt`. Firmware libraries and the ESP32 platform are pinned in `platformio.ini`.

### Direct serial upload

```bash
.venv/bin/pio run --environment wt32-sc01-plus --target upload --upload-port PORT
.venv/bin/pio device monitor --baud 115200 --port PORT
```

For normal users, the browser flasher is preferred over direct PlatformIO installation.

---

## Architecture

OpenTag Station uses task ownership and bounded queues instead of allowing every subsystem to call hardware or storage directly.

| Area | Owner |
|---|---|
| UI / LVGL | UI task |
| Configuration | Configuration worker |
| Scale | Scale task |
| Network / Wi-Fi | Network task |
| Backend HTTP | Backend task |
| Reboot / reset | Device-control worker |
| OTA / candidate validation | OTA worker |

This keeps long-running network, flash, and backend operations away from LVGL and hardware owners.

See [Architecture](docs/architecture.md).

---

## Responsibilities

| Component | Authority |
|---|---|
| **Spoolman** | Inventory, spool records, remaining database weight, locations |
| **FilaBridge** | Printer state, toolhead mappings, print monitoring, consumption |
| **Scale** | Gross physical weight at the time of weighing |
| **OpenPrintTag** | Portable identity, material/container metadata, portable usage data |
| **OpenTag Station** | Orchestration, safe reconciliation, interaction, diagnostics, OTA |

---

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
- [Browser provisioning and local management](docs/provisioning.md)
- [Web flasher](docs/web-flasher.md)
- [OTA and rollback](docs/ota.md)
- [Testing](docs/testing.md)
- [Upstream compatibility](docs/UPSTREAM_COMPATIBILITY.md)

---

## Known limitations

- ST RFAL is selected but not yet acquired/pinned/vendored.
- ST25R3916B physical wiring and antenna integration remain unresolved.
- NFC-V/OpenPrintTag behavior is host-tested but not yet physically validated through RFAL.
- Scale calibration accuracy, repeatability, drift, and persistence still require complete physical validation.
- Wi-Fi provisioning and reconnect behavior are still undergoing physical hardware validation.
- Spoolman and FilaBridge integrations still require validation against live target instances.
- A/B rollback, candidate failure, power interruption, and recovery need the complete hardware-in-the-loop matrix.
- Firmware images currently use SHA-256 integrity checks but are not publisher-signed.
- The local management interface uses HTTP and is intended for a trusted LAN.

---

## License

OpenTag Station is distributed under the [PolyForm Noncommercial License 1.0.0](LICENSE.md).

It permits noncommercial use and distribution but is **not** an OSI-approved open-source license.

ST RFAL is distributed separately under ST's terms and is not currently vendored.
