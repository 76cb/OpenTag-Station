# USB installer page

How the page at <https://76cb.github.io/OpenTag-Station/> is built, checked and deployed.

Looking for how to install the firmware? See the manual:
<https://76cb.github.io/OpenTag-Station-Docs/installation/web-flasher/>.
The installer writes a complete factory image and always replaces the station's settings and
scale calibration. To update a working station and keep its settings, use the update card on the
station's own web page instead:
<https://76cb.github.io/OpenTag-Station-Docs/installation/ota/>.

## What the page is

| Part | Source |
|---|---|
| Page | `web-flasher/index.html`. One action, **Factory Install / Recovery**, using ESP Web Tools 10.4.0 (loaded from unpkg) over Web Serial. Needs desktop Chrome or Edge. |
| Manifest | `web-flasher/manifest.json`: one ESP32-S3 build, one part `opentag-station-factory.bin` at offset 0, `new_install_prompt_erase: true`, no Improv. `version` is kept equal to `VERSION` by `tools/release_version.py --sync-manifest`. |
| Factory image | Built by the PlatformIO target `web-flasher` (`tools/web_flasher_target.py`, `tools/web_flasher.py`). |

Both files under `web-flasher/` are production paths: a pull request that changes them must raise
`VERSION`.

## Factory image

One merged image starting at flash offset 0:

| Content | Offset |
|---|---|
| Bootloader, partition table, `boot_app0` | as evaluated by PlatformIO for the board |
| Application (`firmware.bin`) | 0x10000 |
| LittleFS image, full partition size 0x5D0000, empty | 0xA10000 |

`esptool merge_bin` fills the gaps, so flashing the image overwrites NVS (calibration), the OTA
selection and the whole LittleFS partition (settings, recovery record) whatever the user answers
to the installer's erase question. QIO/QOUT flash mode is written as DIO in the merged image for
ESP Web Tools; the partition layout is unchanged.

The build writes `version` and the full `source_commit` into the bundle's `manifest.json`.

## Build and check locally

```sh
python tools/release_version.py
python tools/web_flasher.py validate-source --page web-flasher/index.html --manifest web-flasher/manifest.json
pio run --environment wt32-sc01-plus --target web-flasher
python tools/web_flasher.py validate-bundle --bundle-dir .pio/build/wt32-sc01-plus/web-flasher --maximum-size 16777216
python tools/web_flasher.py assemble-pages --factory-bundle .pio/build/wt32-sc01-plus/web-flasher --output-dir .pio/build/web-flasher-pages --maximum-size 16777216
python tools/web_flasher.py validate-pages --bundle-dir .pio/build/web-flasher-pages --maximum-size 16777216
```

The `web-flasher` target refuses every environment except `wt32-sc01-plus`.

| Command | Checks |
|---|---|
| `validate-source` | The page and the source manifest (product name, single ESP32-S3 build, single part at offset 0, erase prompt on, Improv off) |
| `validate-bundle` | The built bundle: manifest, image present, starts with an ESP image header, size within 16 MiB, `.nojekyll` present |
| `assemble-pages` | Copies the publishable files to a new directory |
| `validate-pages` | The directory contains exactly `index.html`, `manifest.json`, `.nojekyll` and `opentag-station-factory.bin` |

## Deployment

The installer is deployed only by `.github/workflows/release.yml`, from the files of a published
GitHub release, so the installer and the release always carry the same build.

| Trigger | Result |
|---|---|
| Merge to `main` that changes `VERSION` | Full CI on that commit, then the release `v<VERSION>`, then the installer built from that release's files |
| Manual run (Actions → Release → Run workflow) on `main` | The same for the current `VERSION`; if that release already exists, only the installer is redeployed from it |
| Any other push, pull request | Nothing is deployed |

The `installer` job downloads `opentag-station-factory.bin`, `manifest.json` and `SHA256SUMS`
from the release, checks the checksums, adds `index.html` from `web-flasher/` and `.nojekyll`,
runs `validate-pages`, and checks that the manifest version equals the release tag. The
`github-pages` environment must allow deployments from `main`.

CI also builds and validates the bundle on every pull request and push to `main` (job `firmware`
in `ci.yml`) without deploying it.
