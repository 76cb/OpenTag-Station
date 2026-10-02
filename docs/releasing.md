# Releasing OpenTag Station

The release checklist. `VERSION` is the single source of the version; every other copy is
generated from it.

The candidate is `1.0.0-rc.19`.

## Prerequisites

- Push access to `76cb/OpenTag-Station` and `76cb/OpenTag-Station-Docs`.
- The `github-pages` environment of the firmware repository allows deployments from tags matching
  `v*` (Settings → Environments → github-pages → Deployment branches and tags). Without it the
  installer deployment fails on the tag.
- A station to run the [hardware acceptance](#hardware-acceptance) on.

## 1. Set the version

```sh
python tools/bump_version.py <version>
```

This rewrites `VERSION`, `web-flasher/manifest.json`, `documentation-site/firmware.json` and the
candidate line above and in `documentation-site/docs/contributing/release.md`. It refuses a
version that is not higher than the current one, and build metadata (`+...`).

Then, by hand:

1. Add or finish the `## <version>` section in `CHANGELOG.md`. The whole file becomes the text of
   the GitHub release.
2. For the 1.0.0 release, remove the "Not released yet." line from the `## 1.0.0` changelog
   section (and its copy in the manual, which the next step regenerates).
3. Copy the changelog into the manual and check the copy:

   ```sh
   { echo '# Release notes'; tail -n +2 CHANGELOG.md; } > documentation-site/docs/reference/release-notes.md
   diff <(tail -n +2 CHANGELOG.md) <(tail -n +2 documentation-site/docs/reference/release-notes.md)
   ```

4. If a file in `SOURCES` of `tools/curate_public_images.py` changed, run
   `python tools/refresh_public_images.py`. Never edit a provenance hash by hand.
5. Update the status text in `README.md` ("Project status") and
   `documentation-site/docs/index.md`.

## 2. CI

Open a pull request, let the CI jobs pass (`version-policy`, `documentation`, `ui-review`,
`firmware`) and merge to `main`. Download the artifact `opentag-production-release` from the CI
run on `main` if you want to run the hardware acceptance before tagging: it contains the files
the release will contain.

## 3. Tag

```sh
git checkout main && git pull
git tag v<version>
git push origin v<version>
```

The tag must be `v` followed by the exact content of `VERSION`, for example `v1.0.0`.
`tools/release_version.py --tag` refuses a tag that differs from `VERSION`, a checkout that is
not the tagged commit, and modified tracked files.

## 4. What gets published

The tag starts two workflows independently.

| Workflow | Result |
|---|---|
| `.github/workflows/release.yml` | Runs the `documentation`, `ui-review` and `firmware` jobs of `ci.yml` on the tag (`version-policy` runs only on pull requests), downloads that run's `opentag-production-release` artifact, checks it with `tools/verify_release_artifacts.py` and `sha256sum --check`, and creates the GitHub release "OpenTag Station v<version>" with `CHANGELOG.md` as notes. A version with a hyphen becomes a pre-release. |
| `.github/workflows/web-flasher-pages.yml` | Builds the installer bundle from the tag and deploys it to <https://76cb.github.io/OpenTag-Station/>. See [web-flasher.md](web-flasher.md). |

Release files:

| File | Use |
|---|---|
| `opentag-station-<version>-application.bin` | Update from the station's web page |
| `opentag-station-factory.bin` | Full image for the USB installer |
| `manifest.json` | Installer manifest with `version` and `source_commit` |
| `build-metadata.json` | Version and source commit of the build |
| `SHA256SUMS` | Checksums |

Because the two workflows are independent, the installer can deploy even when the release job
fails. Check both.

After the workflows finish:

- The release exists, has the files listed above, and `SHA256SUMS` matches.
- `manifest.json` on the installer page shows the new version and the tagged commit.
- Install with the USB installer on a station, then update the same station from the browser with
  the application file.

If something is wrong, fix it and release a higher version. Do not move a published tag.

## 5. Publish the manual

The manual is not deployed from this repository. Export it and commit the export to
`76cb/OpenTag-Station-Docs`, whose own workflow deploys <https://76cb.github.io/OpenTag-Station-Docs/>.

```sh
python tools/export_docs.py --output <empty-directory>
```

The export copies `documentation-site/`, writes the version and commit to `firmware.json`, and
rewrites `blob/main/` links to the commit. Copy the result over a clone of the Docs repository
(keep its `.git`), review the diff, commit and push.

## Hardware acceptance

CI cannot test the reader, the scale, the display or live services. Run this script on a station
with the files that will be released, a running Spoolman with both Spool extra fields, FilaBridge
with a printer, and a few expendable blank NXP ICODE SLIX2 tags. Record the firmware version and
commit (browser: **Settings → Advanced**, card **Device**) and pass or fail for every step.

### Install and start

1. Install with the USB installer (**Factory Install / Recovery**). The station shows the setup
   network name and password. Complete setup from the touchscreen or at `http://192.168.4.1/`.
2. Restart. The station joins Wi-Fi, the tag reader becomes ready, Spoolman and FilaBridge show
   as connected, and there is no restart loop.
3. Calibrate the scale with a known weight. Weigh the same object several times in different
   positions on the platform and after ten minutes; note the spread.

### Write

4. Place one blank tag. Touchscreen shows `BLANK TAG`. Start a write for an existing spool, once
   from the touchscreen (**ASSIGN TAG** → **MY SPOOLS**) and once from the browser
   (**Preview tag**).
5. Check the preview: tag UID, spool, the listed values, 80 × 4 bytes, preserved blocks 78–79.
6. Negative checks before confirming. Each must refuse without changing any tag:
   - swap the tag for another one after the preview, then confirm;
   - place two tags;
   - place a tag that holds other data (for example a URL written by a phone);
   - preview, wait, change the spool's extra-field keys in the station settings, then confirm.
7. Confirm (**WRITE TAG** / **Write this tag**) and keep tag and power in place. Progress counts
   blocks. The result is `TAG READY` on the touchscreen and `✓ OpenPrintTag written and verified`
   in the browser.
8. In Spoolman, the spool's `opentag_instance_uuid` and `nfc_uid` hold the values shown by the
   station. No other spool has them.
9. Remove the tag and place it again. The station identifies the same spool without any manual
   step.
10. Change one mapped value of the spool's filament in Spoolman (for example the colour). Start
    **UPDATE TAG**. The preview shows the change and the same UUID. Confirm; only the changed
    blocks are written.
11. Preview again without changing anything. The touchscreen shows `Tag already up to date` and
    **NOTHING TO WRITE**; no block is written.
12. Change the spool's used weight in Spoolman. In the browser choose **Update consumed weight**,
    review and confirm. The consumed weight on the tag changes; everything else, including blocks
    78–79, is unchanged.
13. Write the tag of spool A for spool B (**REASSIGN**). The preview names both spools. After the
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
31. Cut power within 30 seconds of the restart after an update: the station starts the previous
    firmware and `GET /api/v1/update` reports `rolled_back`. This is the intended bootloader rollback; no
    automated test covers it.
32. Leave the station running with a browser open for at least an hour, placing and removing
    tags and weighing now and then. Watch the serial log (`health`, `stack_margin`,
    `NFC owner=` lines; see [runtime-memory.md](runtime-memory.md)): no restart, no steady fall
    of minimum free heap, no NFC bus errors, touchscreen and browser stay responsive.
33. Check every touchscreen view for clipped text and unresponsive buttons, and the browser page
    on a desktop and a phone.
