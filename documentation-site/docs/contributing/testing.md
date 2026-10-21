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

The driver that talks to the reader chip, `hardware/nfc/st25r3916b/i2c_reader.cpp`, has no host
test. The tag writer and the read service above it are tested against a fake reader, so a bug in
the driver itself only shows on a real station.

The script for these checks is [Hardware acceptance](#hardware-acceptance) below.

## Hardware acceptance

CI cannot test the reader, the scale, the display or live services. Run this script on a station
with the files that will be released, a running Spoolman with both Spool extra fields, FilaBridge
with a printer, and a few expendable blank NXP ICODE SLIX2 tags. Record the firmware version and
commit (browser: **Settings → Advanced**, card **Device**) and pass or fail for every step.

Start and finish with `python tools/station_check.py --host <station>` (add `--token` if one is
set) and keep both reports. It only reads from the station and covers the version, the access-token
check, a 40-request spool-list burst and the live-update connections.

### Install and start

1. Install with the USB installer (**Factory Install / Recovery**). The station shows the open setup
   network's name. Complete setup from the touchscreen or at `http://192.168.4.1/`.
2. Restart. The station joins Wi-Fi, the tag reader becomes ready, Spoolman and FilaBridge show
   as connected, and there is no restart loop.
   - Browser **Settings → Advanced → Device controls → Start setup access point**, confirm. The
     touchscreen **Settings** line reads `Setup Wi-Fi … (no password) - closes in 10 min`; the
     browser shows `Setup network open for 10 more min.` and **Stop setup network**. Click
     **Stop setup network**: the setup network disappears and normal Wi-Fi stays connected.
   - Start it again and leave it: it closes by itself after 10 minutes, and normal Wi-Fi stays
     connected.
   - With Wi-Fi details that cannot connect (or none saved), the setup network runs and
     `POST /api/v1/network/setup-mode` with `{"stop": true}` returns `409`.
3. Calibrate the scale with a known weight. Weigh the same object several times in different
   positions on the platform and after ten minutes; note the spread.
   - Before calibrating (or after deleting the calibration by changing the load-cell size),
     place a linked tag: the station identifies the spool (name and `Spool #N` on Home) although
     the Home button reads **CALIBRATE**.
   - With the scale calibrated, put a spool on the platform and tap **TARE EMPTY PLATFORM**
     (browser: **Tare**): refused with `The platform is not empty (reads X g). Remove everything,
     then tare.`, and the touchscreen keeps `Tare failed: …` visible. The zero is unchanged
     (weigh a known object afterwards).
   - With the station running, unplug and replug the scale's power (NAU7802 VCC). The station
     shows `The scale stopped responding. Reconnecting to it; weighing is unavailable until it
     answers again.`, then recovers by itself without a restart; a known weight reads as before
     (calibration kept).

### Write

4. Place one blank tag. Touchscreen shows `BLANK TAG`. Start a write for an existing spool, once
   from the touchscreen (**WRITE TAG** → **MY SPOOLS** → tap the spool) and once from the browser
   (select the spool → **Continue**).
5. Check the preview: tag UID, spool, the listed values, 80 × 4 bytes, preserved blocks 78–79.
   Preview a spool whose filament material in Spoolman is `PLA Matte`: the touchscreen review
   shows `Not written: material, material type`, the browser lists both under
   `Not written to the tag:`. After writing, the tag holds no material and no material type; the
   other values are written.
6. Negative checks before confirming. Each must refuse without changing any tag:
   - swap the tag for another one after the preview, then confirm;
   - place two tags;
   - place a tag that holds other data (for example a URL written by a phone);
   - preview, wait, change the spool's extra-field keys in the station settings, then confirm.
7. Confirm (**WRITE TAG** / **Write tag**; an ordinary write asks nothing more) and keep tag and
   power in place. Progress counts blocks. The result is `TAG READY` on the touchscreen and `✓ OpenPrintTag written and verified`
   in the browser.
8. In Spoolman, the spool's `opentag_instance_uuid` and `nfc_uid` hold the values shown by the
   station. No other spool has them.
9. Remove the tag and place it again. The station identifies the same spool without any manual
   step.
   - Leave the spool on the station and stop Spoolman, then lift and replace the spool. Touchscreen
     **Manage tag** and the browser Dashboard say `Spoolman is not reachable…` (not "not linked").
     Start Spoolman again without touching the spool: within about 30 seconds after Spoolman
     answers, the station shows the spool again by itself.
10. Change one mapped value of the spool's filament in Spoolman (for example the colour). Start
    **UPDATE TAG**. The preview shows the change and the same UUID. Confirm; only the changed
    blocks are written.
11. Preview again without changing anything. The touchscreen shows `Tag already up to date` and
    **NOTHING TO WRITE**; no block is written.
12. Change the spool's used weight in Spoolman. In the browser choose **Update consumed weight**,
    review and confirm. The consumed weight on the tag changes; everything else, including blocks
    78–79, is unchanged.
13. Write the tag of spool A for spool B (**USE FOR ANOTHER SPOOL**). The preview names both spools. After the
    write, Spoolman lists the UID only on spool B; spool A keeps its UUID.

### Interruption and recovery

14. Preview a write, stop Spoolman, then confirm. The tag is written and the station reports
    that the link is not saved (`Link not saved yet`). Start Spoolman, choose **TRY SAVING LINK AGAIN** /
    **Retry Spoolman link**. The link is saved without another NFC write.
15. Repeat step 14 but restart the station before retrying. After the restart the same prompt
    appears and the retry succeeds.
16. Lift the tag during a write. Place the same tag: the station offers **FINISH WRITING** and
    completes it. Place a different tag instead: it is refused and names the unfinished tag.
17. With an unfinished write, choose **SKIP RECOVERY**. The prompt is gone; other tags work.

### Clear and reuse

18. On a linked tag choose **CLEAR / REUSE** → **ERASE TAG** (browser: **Clear / Reuse Tag** →
    **Clear tag**). Result `TAG READY TO REUSE`. In Spoolman both extra fields of the spool are
    empty; its weights and other fields are unchanged.
19. Repeat with Spoolman stopped: the station reports the tag as cleared and the unlink as
    pending. Start Spoolman and retry (**TRY UNLINKING AGAIN** / **Retry Spoolman cleanup**).
20. Write the cleared tag for another spool and identify it.

### Weigh

21. Place a linked spool. **WEIGH**, then **UPDATE SPOOLMAN**. Spoolman's remaining weight equals
    the measured filament weight. Leave the spool on the scale: nothing else is saved.
22. With **Auto-update weight** off (the default), a Weigh alone changes nothing in Spoolman.
23. Change the spool's used weight in Spoolman between Weigh and Update. The update is refused
    and asks for a new Weigh.

### Printer

24. The Assign page lists the printer from FilaBridge and toolheads T1–T5 (FilaBridge toolhead
    IDs 0–4).
25. Assign the spool on the station to T1. Check in FilaBridge itself that toolhead 0 holds that
    spool ID. The station shows the assignment as verified.
26. Assign another spool to the same toolhead: the station asks `Replace spool?`. Try while the
    printer is printing: the station shows `ACTIVE PRINT` and needs an explicit override.
27. Unassign T1 in the browser. FilaBridge shows the toolhead empty; other toolheads are
    unchanged.

### Update, backup and soak

28. In the browser, export the settings, change one, import the export, and confirm the value is
    back. Passwords and tokens are not in the file.
29. Update from the browser with the application file. Settings and calibration survive. After
    the restart the update shows as confirmed after 30 seconds.
30. Upload the factory image to the updater: it is refused.
    With an access token set, upload the application file with a wrong token (browser: enter a
    wrong token when asked; or `curl` with a wrong `Authorization: Bearer`): refused with
    `401 authentication_required`, `GET /api/v1/update` shows no new upload, and the station keeps
    running the current firmware.
31. Cut power within 30 seconds of the restart after an update: the station starts the previous
    firmware and `GET /api/v1/update` reports `rolled_back`. This is the intended bootloader rollback; no
    automated test covers it.
32. Leave the station running with a browser open for at least an hour, placing and removing
    tags and weighing now and then. Watch the serial log (`health`, `stack_margin`,
    `NFC owner=` lines; see [runtime-memory.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/runtime-memory.md)): no restart, no steady fall
    of minimum free heap, no NFC bus errors, touchscreen and browser stay responsive.
33. Check every touchscreen view for clipped text and unresponsive buttons, and the browser page
    on a desktop and a phone.
34. Run `curl -i http://<station>/api/v1/events` (plain HTTP, no WebSocket upgrade). It returns
    promptly instead of hanging, the station stays responsive, and afterwards a browser tab still
    gets live updates (the request does not use up one of the two WebSocket client places).
