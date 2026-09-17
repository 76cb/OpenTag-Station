# OpenTag Station

A touchscreen and local browser companion for filament spools. Identify a spool
with OpenPrintTag, weigh it, manage canonical inventory in Spoolman, and assign it
to a Prusa XL toolhead through FilaBridge.

**1.0 release candidate — final acceptance pending.** The authoritative version is
in [VERSION](VERSION). External browser review, WT32 visual/touch inspection and
physical/live-service acceptance remain open in the [release checklist](docs/releasing.md).

**[INSTALL / WEB FLASHER](https://76cb.github.io/OpenTag-Station/)** ·
**[DOCUMENTATION](https://76cb.github.io/OpenTag-Station-Docs/)** ·
**[SOURCE](https://github.com/76cb/OpenTag-Station)**

![OpenTag Station dashboard](docs/images/dashboard.png)

| Weigh | Assign |
|---|---|
| ![Weigh receipt](docs/images/weigh.png) | ![Prusa XL assignment](docs/images/assignment.png) |

| Manage tags | WT32 Home |
|---|---|
| ![Tag management](docs/images/tag-management.png) | ![WT32 Home layout](docs/images/wt32-home.png) |

Images use generic deterministic demo data. The WT32 image is a layout
approximation with approximate fonts, not a physical panel capture.

## Features

- Current-spool dashboard, searchable inventory, Community filament import and
  canonical Spoolman editing in a responsive local browser.
- Explicit Weigh receipts with gross, empty-spool tare, measured filament and
  canonical remaining weight. Auto-update after Weigh is off by default.
- T1–T5 printer assignment with replacement confirmation and exact backend readback.
- Guarded OpenPrintTag write/update, full readback, recovery journaling and
  Clear / Reuse with verified Spoolman identity cleanup.
- WT32 Home, Weigh, Assign, Tag and Settings views with large touch actions.
- Wi-Fi setup, optional local API token, redacted configuration backup, and local
  A/B OTA. The local interface uses HTTP on a trusted LAN; images are not publisher-signed.

## Install

1. Assemble and inspect the supported hardware using the [build manual](https://76cb.github.io/OpenTag-Station-Docs/hardware/wiring/).
2. Connect a data-capable USB cable and open the installer in desktop Chrome or Edge.
3. Install **OpenTag Station**, then join its setup network and configure Wi-Fi,
   Spoolman, optional FilaBridge and the matching scale profile.
4. Tare/calibrate the platform and follow the [first-spool guide](https://76cb.github.io/OpenTag-Station-Docs/getting-started/quick-start/).

The public installer tracks firmware main. While this candidate PR is unmerged,
check the installer manifest version; use the PR's production CI bundle for the
candidate. A final v1.0.0 release is not yet published. USB recovery can erase
configuration/calibration; normal OTA takes an application binary, not a factory image.

## Hardware

| Part | Supported configuration |
|---|---|
| Controller | WT32-SC01 Plus / ESP32-S3, 16 MiB flash, 480 × 320 touchscreen |
| Scale | NAU7802 and YZC-133 5 kg (default) or 2 kg with matching calibration |
| Reader | ELECHOUSE NFC_ST25R3916B, I²C bridge closed, integrated antenna |
| Tags | NXP ICODE SLIX2 NFC-V / ISO15693, 80 × 4-byte approved profile |
| Supply / mechanics | Regulated 5 V, qualified breakout/harness, rigid free-moving platform; no reference enclosure is defined |

```text
WT32-SC01 Plus
├─ Wire  / controller 0: GPIO10 SDA, GPIO11 SCL → NAU7802 (0x2A, 400 kHz)
├─ Wire1 / controller 1: GPIO13 SDA, GPIO14 SCL → NFC (0x50, 100 kHz), IRQ GPIO12
└─ Software I²C: GPIO6 SDA, GPIO5 SCL → built-in touch
```

NFC uses EXT 5 V/common ground; CS/BSS and MOSI remain disconnected. GPIO is
3.3 V logic. Check the scale breakout's supply and pull-up ratings before wiring.
Full [BOM](https://76cb.github.io/OpenTag-Station-Docs/hardware/bill-of-materials/),
[connector orientation and wiring](https://76cb.github.io/OpenTag-Station-Docs/hardware/wiring/),
[power](https://76cb.github.io/OpenTag-Station-Docs/hardware/power/) and
[mechanical setup](https://76cb.github.io/OpenTag-Station-Docs/hardware/mechanical/)
are in the separate manual.

## Build and contribute

```sh
python -m venv .venv
# Activate the environment for your shell.
python -m pip install -r requirements-dev.txt
python tools/release_version.py
pio test --environment native
pio run --environment wt32-sc01-plus
pio run --environment wt32-sc01-plus --target web-flasher
```

PlatformIO and embedded dependencies are pinned. Linux/WSL matches CI for native
and sanitizer checks. VERSION drives firmware metadata, About/browser build info,
flasher manifests and exported documentation. CI checks source/ELF ownership,
memory, stack reserves, browser behavior, public-image privacy and documentation.
See [testing](docs/testing.md) and [release process](docs/releasing.md).

The separate documentation project is maintained in [documentation-site](documentation-site/README.md)
and exported to its own repository. Firmware Pages remains the installer.

## License

[PolyForm Noncommercial License 1.0.0](LICENSE.md) permits noncommercial use and
distribution; it is not an OSI-approved open-source license. Vendored ELECHOUSE
libraries retain their included licenses; see [third-party notices](third_party/ELECHOUSE_ST25R3916/README.md).
