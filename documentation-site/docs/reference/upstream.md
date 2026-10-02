# Supported versions

Which versions of Spoolman, FilaBridge and the OpenPrintTag specification this firmware accepts,
and what it switches off when it meets a different one.

## Spoolman

| | |
|---|---|
| Accepted version | `0.26.1` |
| Rule | The `version` string from Spoolman's `/api/v1/info` must be exactly `0.26.1`. |
| On any other version | The station stays connected and can still read. Saving weights is off. |

What you see on another version:

- Browser, **Settings → Integrations**: `Spoolman <version> has not been tested with this
  firmware, so saving weights to Spoolman is turned off.`
- After **Update Spoolman**: `Saving weights is turned off: this Spoolman version has not been
  tested with this firmware, or Spoolman is offline. See Settings.`

The check is repeated while the station runs. Changing the Spoolman version takes effect without
a restart.

## FilaBridge

| | |
|---|---|
| Accepted versions | `v1.2.2`, `1.2.2`, `v1.2.1`, `1.2.1`, and development builds that report `dev` |
| Rule | The `version` string from FilaBridge's `/healthz` must be exactly one of the above. |
| On any other version | Printers and toolheads are still shown. Assigning and unassigning are off. |

What you see on another version when you try to assign in the browser:
`FilaBridge is connected read-only; mapping capability is unavailable and no assignment was queued`.
On the touchscreen the toolhead buttons stay grey.

If FilaBridge reports a different version than at the last full check, assignment is paused
with `FilaBridge version changed; use Test backends to verify capabilities`.
Press **Test** on the Integrations card in the browser to check again.

FilaBridge is optional. Without it everything except toolhead assignment works.

## OpenPrintTag specification

| Use | Pinned commit |
|---|---|
| Reader field definitions | `e0dab1ae16838d2c342e7cfc509455441b7d8eba` |
| Writer and blank-image layout | `7e09cc38df1c8e7824a67f5b1ae93071f52519ad` |

CI checks out the writer revision of
<https://github.com/OpenPrintTag/openprinttag-specification> and compares the images the station
produces with the upstream tools. See [OpenPrintTag format](openprinttag.md).

## Hardware

One board is supported: WT32-SC01 Plus (ESP32-S3, 16 MB flash), hardware ID
`wt32-sc01-plus-rev-a`. A firmware file built for another board is refused by the updater.

## Build toolchain

| Component | Version | Pinned in |
|---|---|---|
| PlatformIO Core | 6.1.19 | `requirements-dev.txt` |
| PlatformIO platform `espressif32` | 6.13.0 | `platformio.ini` |
| PlatformIO platform `native` (host tests) | 1.2.1 | `platformio.ini` |
| `tool-mklittlefs` | 1.203.210628 | `platformio.ini` |
| LVGL | 8.3.11 | `platformio.ini` |
| LovyanGFX | 1.2.27 | `platformio.ini` |
| ArduinoJson | 7.4.3 | `platformio.ini` |
| Adafruit NAU7802 Library | 1.0.8 | `platformio.ini` |
| Adafruit BusIO | 1.17.4 | `platformio.ini` |
| ELECHOUSE NFC-RFAL / ST25R3916 | 1.0.2 / 1.1.1, commit `16eb6c7fb13e502d320924040d768a9e564209b2` | `third_party/ELECHOUSE_ST25R3916/` (copied into the repository) |
| ESP Web Tools (USB installer page) | 10.4.0 | `web-flasher/index.html` |
| MkDocs / Material for MkDocs (this manual) | 1.6.1 / 9.7.7 | `documentation-site/requirements.txt` |
| Python (CI) | 3.12 | `.github/workflows/ci.yml` |
| Node.js (CI) | 24.16.0 | `.github/workflows/ci.yml` |
| Pillow / Playwright (CI screenshots) | 11.3.0 / 1.62.1 | `.github/workflows/ci.yml` |

The firmware is built with C++17 on the Arduino framework.

## Changing a pinned version

Change the version in the file named above.
A change to `platformio.ini`, `third_party/` or `web-flasher/` needs a version bump;
see [Contributing changes](../contributing/pull-requests.md).
The accepted Spoolman version is the constant `tested_version` in
`src/integrations/spoolman/spoolman_adapter.cpp`.
The accepted FilaBridge versions are `tested_version` plus the literals compared in
`FilaBridgeAdapter::probe()` in `src/integrations/filabridge/filabridge_adapter.cpp`.
