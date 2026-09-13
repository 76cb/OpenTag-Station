# Browser first-install and recovery flasher

The OpenTag Station web flasher is for a **first installation** or **USB
recovery** of a WT32-SC01 Plus. It is separate from the authenticated local A/B
OTA updater. After OpenTag Station is running, use the station's local Update
panel for normal future application updates and retain its rollback protection.

The published installer is expected at:

`https://76cb.github.io/OpenTag-Station/`

The site uses ESP Web Tools and the browser Web Serial API. Use a current
desktop Chrome or Edge release, open the HTTPS page, connect the WT32-SC01 Plus
to the computer with a USB data cable, then select either the default
**Install OpenTag Station** production image or the opt-in
**WT32-SC01 Plus — Dual I2C / NFC-V RF Test** image. Choose the board's serial
device when prompted. Web Serial is not supported by Firefox or Safari.

The dual-I2C diagnostic samples the NAU7802 bus on GPIO10/11 (`Wire`) and NFC
bus on GPIO13/14 (`Wire1`) independently at 100 kHz, probes `0x2A` and `0x50`,
and retains the direct-register ST25R3916B chip-ID/IRQ checks. It then uses the
pinned ELECHOUSE object API to initialize RFAL and the NFC-V poller and run
bounded ISO15693 inventory rounds while continuing scale sampling. Zero tags is
a healthy inventory PASS; one stable tag reports its normalized eight-byte UID,
system information, validated geometry, and two read-only full-memory images.
The images must match exactly before the diagnostic exposes the first image at
`GET /api/v1/nfcv-dump` as `application/octet-stream`; summary JSON never embeds
the dump.

The same opt-in diagnostic generates the deterministic 312-byte OpenPrintTag
reference image and offers a read-only preview and download. The one-time
guarded blank-tag initialization has passed physical acceptance, so its browser
control is removed and its POST route and loop processing are compiled behind a
false developer gate. The shipped diagnostic exposes no NFC write action,
reinitialize path, or force path. The normal factory bundle now includes
production read-only NFC; see [production NFC](production-nfc.md).

The RF field is scoped to every RF operation. The display is the primary result
view; touchscreen input is disabled to reserve I2C controller 1 for NFC. For
the secondary browser view, join the open `OpenTag-I2C-Test` access point and
open `http://192.168.4.1`.

## Download mode

The ESP32-S3 USB/serial bootloader will usually connect automatically. If it
does not:

1. Hold the board's **BOOT** button (GPIO0).
2. While holding BOOT, briefly press and release **RESET/EN**.
3. Release BOOT, retry the installer, and select the newly appearing serial
   device.

Button labels can vary between board revisions. Disconnect other serial
monitors before retrying. A USB data cable is required; charge-only cables do
not expose a serial device.

## Data and recovery safety

The merged first-install image starts at flash offset zero and includes the
bootloader, partition table, Arduino OTA initialization data, and application.
It therefore initializes early flash used by NVS and OTA selection. Treat a
factory installation as potentially erasing local configuration and scale
calibration. Selecting a full-device erase option also erases data partitions.
Export configuration first when the running station is still reachable.

The custom 16 MiB partition table is preserved exactly. The browser installer
does not replace or alter the local Phase 10 A/B OTA design. If application
startup or local OTA becomes unavailable, the ROM serial downloader remains a
USB recovery path.

Serial flashing has not been physically validated in this repository. A real
WT32-SC01 Plus, supported browser, USB connection, first boot, persistence, and
subsequent A/B OTA must still be exercised before relying on the installer.

## Reproducible factory bundle

Install the pinned development dependencies, then run:

```bash
.venv/bin/pio run --environment wt32-sc01-plus --target web-flasher
.venv/bin/pio run --environment wt32-sc01-plus-i2c-test --target web-flasher
python3 tools/web_flasher.py validate-bundle \
  --bundle-dir .pio/build/wt32-sc01-plus/web-flasher \
  --maximum-size 16777216
python3 tools/web_flasher.py validate-bundle --kind diagnostic \
  --bundle-dir .pio/build/wt32-sc01-plus-i2c-test/web-flasher \
  --maximum-size 16777216
python3 tools/web_flasher.py assemble-pages \
  --factory-bundle .pio/build/wt32-sc01-plus/web-flasher \
  --diagnostic-bundle .pio/build/wt32-sc01-plus-i2c-test/web-flasher \
  --output-dir .pio/build/web-flasher-pages \
  --maximum-size 16777216
python3 tools/web_flasher.py validate-pages \
  --bundle-dir .pio/build/web-flasher-pages \
  --maximum-size 16777216
```

The PlatformIO post-script derives the inputs from the evaluated upload
environment instead of duplicating offsets. For the current pinned board and
platform, the evaluated layout is:

| Offset | Evaluated upload input | Purpose |
|---:|---|---|
| `0x0000` | `bootloader.bin` | ESP32-S3 second-stage bootloader |
| `0x8000` | `partitions.bin` | exact custom 16 MiB partition table |
| `0xe000` | framework `boot_app0.bin` | initialized Arduino OTA selection data |
| `0x10000` | `firmware.bin` | OpenTag Station application in `app0` |

The evaluated target is ESP32-S3, 16 MB flash, 80 MHz, QIO/QSPI. ESP Web Tools'
documented merged-image compatibility rule changes only the merged boot header
mode from QIO to DIO; it does not change an offset, partition, or input binary.
The generator rejects unexpected upload inputs, overlaps, missing files, an
oversized result, a wrong chip family, or an application that does not contain
the current 12-character source Git SHA.

GitHub Actions repeats the native suite, both WT32 builds, stack check,
source-asset validation, merge, and bundle validation. The Pages artifact
contains only `index.html`, both manifests, `.nojekyll`,
`opentag-station-factory.bin`, and `opentag-station-i2c-test.bin`; deployment
never commits generated data to `main`.
