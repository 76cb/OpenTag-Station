# Upstream compatibility baseline

Research baseline established **2026-08-17** and OpenPrintTag rechecked
**2026-08-20**. Source/API inspection and host-fixture success are not physical
interoperability claims.

## Application integrations

| Upstream | Revision/version | Status | Dependency boundary |
|---|---|---|---|
| OpenPrintTag | [`e0dab1a`](https://github.com/prusa3d/OpenPrintTag/commit/e0dab1ae16838d2c342e7cfc509455441b7d8eba), 2026-07-02 | Rechecked 2026-08-20; implemented and host-tested against official fixtures; physical tag pending | MIME record, regions, field maps, transaction rules |
| Spoolman | v0.26.1 / current `master` [`8d9eb73`](https://github.com/Donkie/Spoolman/commit/8d9eb7395da9553bdbf14b21231afe4e153f0a79), 2026-08-20 | Source contract revalidated and host fixtures pass; no live instance tested | `integrations/spoolman` only |
| FilaBridge | latest tag v1.2.2; main [`f35cde8`](https://github.com/sargonas/filabridge/commit/f35cde87505e7a617307527b8e8431dd2dc65f62), 2026-08-11 | Correct maintained repository revalidated; adapter and host contract fixtures pass; no live instance tested | `integrations/filabridge` only |
| SpoolmanScale | [`ea0515a`](https://github.com/Niko11111/SpoolmanScale/commit/ea0515ad92ec2fcb65af8c5f0e2bc1a4d01d305b), 2026-08-16 | Hardware facts inspected only | No code/architecture dependency |

OpenPrintTag intentionally avoids an explicit format version. Compatibility is
therefore recorded by Git revision, MIME type, and fixture corpus revision.

## NFC/RFAL

| Component | Baseline | Status |
|---|---|---|
| ST25R3916B | [ST product documentation](https://www.st.com/en/nfc/st25r3916b.html) | Device capability inspected; module unknown |
| RFAL | [STSW-ST25RFAL002](https://www.st.com/en/embedded-software/stsw-st25rfal002.html) and [UM2890 Rev 7](https://www.st.com/resource/en/user_manual/um2890-rfnfc-abstraction-layer-rfal-stmicroelectronics.pdf) | ESP32 platform primitives compiled; vendor source release not yet acquired/pinned |
| X-CUBE-NFC6 | [ST product package](https://www.st.com/en/embedded-software/x-cube-nfc6.html) | Port/reference source only, not a build dependency |

RFAL cannot yet be called reproducibly pinned. ST's product delivery is not a
stable public source revision suitable for an unattended vendoring step, and
the exact delivered archive plus its license/redistribution terms have not been
accepted and captured for this repository. Before import, record the exact ST
archive/release identifier, archive SHA-256, internal RFAL version, complete
license text, redistribution decision, and any project modifications. Until
then diagnostics say `not-vendored`, the frontend backend is unbound, and the
production enable flag remains false.

## Build dependencies

| Component | Pin |
|---|---|
| PlatformIO Core | 6.1.19 |
| PlatformIO Espressif 32 | 6.13.0 |
| Arduino-ESP32 framework | 2.0.17 (provided by platform 6.13.0) |
| LVGL | 8.3.11 |
| LovyanGFX | 1.2.27 |
| Adafruit NAU7802 | 1.0.8 |
| Adafruit BusIO | 1.17.4 |
| ArduinoJson | 7.4.3 |
| Native PlatformIO platform | 1.2.1 |
| Configuration schema | 3 |

## Required Spoolman capabilities

- runtime info and health;
- list/filter/retrieve spool;
- set or measure remaining weight with read-after-write verification;
- locations;
- configurable extra-field discovery and merge-safe values;
- optional spool WebSocket updates.

## Required FilaBridge capabilities

- health/runtime version;
- configured printers and stable printer IDs;
- complete toolhead list/mappings;
- map and unmap through zero-based IDs;
- re-read mapping verification;
- printer state for remapping warnings;
- optional WebSocket status.

An unknown upstream version is reported as untested rather than disconnected.
Read capabilities remain available when their concrete probes succeed; mapping
writes remain guarded unless the response contract and an explicitly supported
runtime line both pass. Current-main development builds report `dev`, so that
compatibility lane is source-pinned to the commit recorded above and is not a
formal release-support claim.
