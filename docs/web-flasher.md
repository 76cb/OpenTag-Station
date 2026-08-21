# Browser first-install and recovery flasher

The OpenTag Station web flasher is for a **first installation** or **USB
recovery** of a WT32-SC01 Plus. It is separate from the authenticated local A/B
OTA updater. After OpenTag Station is running, use the station's local Update
panel for normal future application updates and retain its rollback protection.

The published installer is expected at:

`https://76cb.github.io/OpenTag-Station/`

The site uses ESP Web Tools and the browser Web Serial API. Use a current
desktop Chrome or Edge release, open the HTTPS page, connect the WT32-SC01 Plus
to the computer with a USB data cable, select **Install OpenTag Station**, and
choose the board's serial device when prompted. Web Serial is not supported by
Firefox or Safari.

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
python3 tools/web_flasher.py validate-bundle \
  --bundle-dir .pio/build/wt32-sc01-plus/web-flasher \
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

GitHub Actions repeats the native suite, WT32 build, stack check, source-asset
validation, merge, and bundle validation. The Pages artifact contains only
`index.html`, `manifest.json`, `.nojekyll`, and
`opentag-station-factory.bin`; deployment never commits generated data to
`main`.
