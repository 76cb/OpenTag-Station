# Testing and CI

What runs on every pull request and on `main`, and how to run the same checks yourself.
The workflow is
[`.github/workflows/ci.yml`](https://github.com/76cb/OpenTag-Station/blob/main/.github/workflows/ci.yml).
Set up the tools first: [Build from source](build.md).

## Test environments

Defined in `platformio.ini`. Tests use the Unity framework and live in `test/`, one directory per
suite.

| Environment | What it is |
|---|---|
| `native` | Host build of the portable code with every suite in `test/` |
| `native-writer-sanitized` | The same suites under AddressSanitizer and UndefinedBehaviorSanitizer |
| `native-community` | The suites in its `test_filter`, built with the postponed Community catalog switched on |
| `wt32-sc01-plus` | The firmware itself. CI builds it; no test runs on a device. |

```sh
pio test --environment native
pio test --environment native --filter test_tag_writer     # one suite
pio test --environment native-writer-sanitized
pio test --environment native-community
```

CI runs the sanitized environment with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`.

## CI jobs

### `version-policy` (pull requests only)

Fails when a change to production files does not raise `VERSION`.
See [Contributing changes](pull-requests.md).

```sh
python tools/check_version_policy.py --base origin/main
python -m unittest discover -s tools -p test_version_policy.py
```

### `documentation`

Checks this manual and the committed images.

```sh
python -m pip install -r documentation-site/requirements.txt
python tools/check_public_privacy.py
python tools/check_hardware_docs.py          # wiring page and diagrams match the board header
python tools/export_docs.py --check          # documentation version matches VERSION
python tools/check_committed_images.py       # screenshots match their recorded sources
python documentation-site/scripts/check_images.py
python -m mkdocs build --strict -f documentation-site/mkdocs.yml
python documentation-site/scripts/validate.py documentation-site/site    # links and anchors
```

### `ui-review`

Renders touchscreen layouts and browser views from the shipped sources with demo data and
uploads them as the artifact `opentag-ui-review`. Browser views are captured in Chromium at
widths 1440, 1280, 1024, 768 and 390.

```sh
python -m pip install Pillow==11.3.0
npm install --prefix .pio/review-tools playwright@1.62.1
.pio/review-tools/node_modules/.bin/playwright install --with-deps chromium
python tools/generate_product_ui.py --check
pio test --environment native --filter test_touch_flow
python tools/product_review.py --output .pio/ui-review
python tools/render_touch_review.py --output .pio/ui-review
NODE_PATH=.pio/review-tools/node_modules node tools/capture_product_review.cjs .pio/ui-review
python tools/check_public_privacy.py .pio/ui-review
```

### `firmware`

Runs in this order and stops at the first failure.

| Step | Command | What it checks |
|---|---|---|
| Whitespace | `git diff --check 4b825dc642cb6eb9a060e54bf8d69288fbee4904 HEAD` | No whitespace errors in any tracked file |
| Tag image reference | `python tools/verify_openprinttag_initializer_reference.py --spec-root <spec checkout>` | Blank-image layout matches the pinned OpenPrintTag specification |
| Version | `python tools/release_version.py` | `VERSION` is valid and the installer manifest matches it |
| Tool tests | `python -m unittest discover -s tools -p test_release_packaging.py`, `... -p test_bump_version.py`, `python -m unittest tools.test_community_catalog`, `python tools/community_catalog.py inspect community/community.pack` | Release packaging, version bump and catalog tools |
| Browser assets | `python tools/check_web_assets.py`, `node --test tools/test_web_transport.mjs`, `python tools/test_writer_display.py` | Embedded JavaScript parses; browser request handling; writer display |
| Installer source | `python tools/web_flasher.py validate-source --page web-flasher/index.html --manifest web-flasher/manifest.json` | Installer page and manifest |
| Host tests | `pio test --environment native`, `native-community`, `native-writer-sanitized`; `python -m unittest tools.test_production_features` | Portable code; feature switches |
| Writer reference | `python tools/verify_openprinttag_writer_reference.py --spec-root <spec checkout>` | A written tag image decodes with the upstream tools |
| NFC write rules, source | `python tools/check_production_nfc.py` | Only the tag writer can reach the NFC write command |
| Build | `pio run --environment wt32-sc01-plus` | Firmware compiles |
| NFC write rules, binary | `python tools/check_production_nfc.py --elf` | The linked firmware contains no other write, lock or password command |
| Build identity | `python tools/release_version.py --binary ... --metadata ...` | The binary embeds `VERSION` and the commit |
| Memory | `python tools/check_production_memory.py` | Static internal RAM limit; Community code absent; display memory pool in PSRAM |
| Stack | `python tools/analyze_stack_usage.py`, `check_production_nfc_stack_usage.py`, `check_production_http_stack_usage.py`, `check_touch_stack_usage.py` | Largest stack frames are listed; the backend, web server and touchscreen tasks keep their required reserve |
| Installer bundle | `pio run --environment wt32-sc01-plus --target web-flasher`, then `web_flasher.py validate-bundle`, `assemble-pages`, `validate-pages` | Factory image and Pages directory |
| Release files | `python tools/prepare_release.py` | Stages the release files; uploaded as artifact `opentag-production-release` |

The two reference checks need a checkout of
<https://github.com/OpenPrintTag/openprinttag-specification> at commit
`7e09cc38df1c8e7824a67f5b1ae93071f52519ad` and its `requirements.txt` installed.

On pull requests the job also uploads `firmware.elf`, `firmware.map` and `build-metadata.json`
as `opentag-production-debug-pr`.

Every GitHub Action is pinned to a commit.

## What the tests cannot cover

The host tests run the portable logic against simulated hardware and simulated Spoolman and
FilaBridge responses. The stack and memory checks read compiler output; they do not measure a
running station. These need a real station:

- Reading and writing real tags, including removing a tag or cutting power during a write.
- Scale accuracy, drift and calibration.
- Touch response, colours and clipping on the real display.
- Wi-Fi setup, live Spoolman and FilaBridge servers, and a real printer.
- A firmware update followed by a restart, and the fallback to the previous firmware.
- Free memory and stack use over hours of operation.

The script for these checks is the "Hardware acceptance" section of
[docs/releasing.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/releasing.md).
