# Build from source

Build the firmware, run the host tests and produce the same files a release contains.

## Before you start

- Git, and a C++17 compiler for the host tests (the `native` environments build with it).
- Python. CI uses 3.12.
- Node.js, only for the browser checks. CI uses 24.16.0.
- CI runs on Ubuntu 24.04. Use Linux or WSL to match it.

PlatformIO and every library are pinned; see [Supported versions](../reference/upstream.md).

## Get the tools

```sh
git clone https://github.com/76cb/OpenTag-Station.git
cd OpenTag-Station
python -m venv .venv
. .venv/bin/activate
python -m pip install --requirement requirements-dev.txt
```

`requirements-dev.txt` installs PlatformIO only.
PlatformIO downloads the ESP32 platform and the libraries on the first build.

## Build and test

```sh
python tools/release_version.py                 # VERSION and the installer manifest agree
pio test --environment native                   # host tests
pio run --environment wt32-sc01-plus            # firmware
```

`wt32-sc01-plus` is the only firmware environment and the default one.

## Flash your own build

Connect the WT32-SC01 Plus over USB.

```sh
pio run --environment wt32-sc01-plus --target upload
pio device monitor
```

The serial monitor runs at 115200 baud.
You can also upload `.pio/build/wt32-sc01-plus/firmware.bin` from the station's web page; it is
the same image a release ships as the update file. See
[Update the firmware](../installation/ota.md).

## Build the USB installer bundle and release files

```sh
pio run --environment wt32-sc01-plus --target web-flasher
python tools/web_flasher.py validate-bundle --bundle-dir .pio/build/wt32-sc01-plus/web-flasher --maximum-size 16777216
python tools/web_flasher.py assemble-pages --factory-bundle .pio/build/wt32-sc01-plus/web-flasher --output-dir .pio/build/web-flasher-pages --maximum-size 16777216
python tools/web_flasher.py validate-pages --bundle-dir .pio/build/web-flasher-pages --maximum-size 16777216
python tools/prepare_release.py
```

`prepare_release.py` needs an empty `.pio/release` directory and a build made from the
checked-out commit.

## What you get

| Path | Content |
|---|---|
| `.pio/build/wt32-sc01-plus/firmware.bin` | Application image (the browser update file) |
| `.pio/build/wt32-sc01-plus/firmware.elf`, `firmware.map` | Debug files used by the CI checks |
| `.pio/build/wt32-sc01-plus/build-metadata.json` | Version and source commit of the build |
| `.pio/build/wt32-sc01-plus/web-flasher/` | Installer page, `manifest.json`, `opentag-station-factory.bin` |
| `.pio/build/web-flasher-pages/` | The same files, as published to GitHub Pages |
| `.pio/release/` | The release files and `SHA256SUMS` |

The firmware embeds the version from `VERSION` and the Git commit it was built from.

## After changing the browser pages

The browser assets in `src/web/web_assets.cpp` are generated from `src/web/ui/product.css` and
`src/web/ui/product.js`.

```sh
python tools/generate_product_ui.py             # regenerate
python tools/generate_product_ui.py --check     # what CI runs
python tools/check_web_assets.py                # syntax and compression check, needs Node.js
```

## Related

- [Testing and CI](testing.md)
- [Contributing changes](pull-requests.md)
