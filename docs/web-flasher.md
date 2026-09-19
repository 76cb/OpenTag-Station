# Production web flasher

**FACTORY INSTALL — Erases station configuration and calibration.** For **Update Existing Station**, use the station browser Update page with the application firmware image. Normal OTA preserves LittleFS and NVS; the USB factory flasher is for new devices/recovery only.

The [installer](https://76cb.github.io/OpenTag-Station/) offers one action:
**Factory Install / Recovery**. It installs the complete production factory image
over desktop Chrome/Edge Web Serial for first use or USB recovery. The separate
[documentation site](https://76cb.github.io/OpenTag-Station-Docs/) is a build/user
manual, not another firmware installer.

## Install or recover

Prerequisites: supported WT32-SC01 Plus, stable power and a data-capable USB cable.
Back up reachable configuration before recovery; erase can remove calibration and
credentials. During an unmerged candidate review, the public installer follows
main. Check its manifest version or use the PR's production factory artifact.

1. Open the HTTPS installer and connect USB. Close other serial programs.
2. Choose Install OpenTag Station, select the correct port and review erase behavior.
3. Wait for flash verification without disconnecting power.
4. Reboot normal firmware and confirm VERSION/About metadata.
5. Follow Wi-Fi setup, restore compatible settings, and validate touch, scale and NFC.

A missing port suggests a cable/driver/permissions issue. Follow the actual board's
download-mode procedure if automatic entry fails. Do not use the factory image for
OTA: the local A/B updater consumes the application binary.

## Build the bundle

```sh
python tools/release_version.py
python tools/web_flasher.py validate-source --page web-flasher/index.html --manifest web-flasher/manifest.json
pio run --environment wt32-sc01-plus --target web-flasher
python tools/web_flasher.py validate-bundle --bundle-dir .pio/build/wt32-sc01-plus/web-flasher --maximum-size 16777216
python tools/web_flasher.py assemble-pages --factory-bundle .pio/build/wt32-sc01-plus/web-flasher --output-dir .pio/build/web-flasher-pages --maximum-size 16777216
python tools/web_flasher.py validate-pages --bundle-dir .pio/build/web-flasher-pages --maximum-size 16777216
```

The Pages directory contains exactly `index.html`, `manifest.json`, `.nojekyll`
and `opentag-station-factory.bin`. Assembly rejects extra files, including stale
test images/manifests. Start with a new output directory after changing bundle layout.

PlatformIO supplies evaluated bootloader, partitions, boot_app0 and application
offsets. The application begins at 0x10000 in the current layout. The tool checks
nonoverlap, 16 MiB bounds, ESP image header, embedded VERSION/SHA and preservation
of non-bootloader parts after esptool merge. ESP Web Tools compatibility normalizes
QIO/QOUT to DIO; it does not alter the partition layout. The merged manifest uses
offset zero, exact VERSION and a separate full `source_commit` provenance value.

CI validates these properties. A successful bundle check is not a physical USB
flashing result; that remains an explicit [release acceptance](releasing.md) item.
Main pushes deploy the installer, while production releases run only on matching
stable version tags. No test firmware is built or offered.
