# Release process

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


The candidate is `1.0.0-rc.16`. `VERSION` is authoritative; generated manifests and
documentation metadata must be refreshed from it. A successful build is not
physical acceptance. Keep the release PR open and unmerged until external signoff.

## Acceptance record

Record reviewer, date, exact firmware SHA, board revision, service versions,
result and sanitized evidence for every item. All boxes below are **pending**.

- [ ] Browser visual review: all 65 desktop/tablet/mobile captures and real browser flows.
- [ ] WT32 physical visual, color, clipping and touch review of all five views.
- [ ] No Community action appears on WT32 or browser; Community API requests fail closed.
- [ ] Approved tag write and complete readback; reject a changed/unsupported tag.
- [ ] Clear / Reuse and full blank verification, including reserved-tail preservation.
- [ ] Spoolman UID/instance unlink verification; preserve usage and unrelated fields.
- [ ] Rewrite after clear and successful identity resolution.
- [ ] Explicit Weigh → Spoolman synchronization, default-off policy and conflict handling.
- [ ] Prusa XL T1–T5 assignment, replacement confirmation and backend readback.
- [ ] Scale calibration, repeatability, placement sensitivity and drift.
- [ ] Short stability soak with NFC, scale, browser and touch active; record duration,
      errors, heap and stack high-water marks. Agree duration before running.
- [ ] Configuration backup/restore, Wi-Fi recovery and A/B rollback hardware matrix.
- [ ] Final production-only USB flasher install and subsequent normal OTA update.
- [ ] Documentation build guide reproduced and approved by an external builder.

## Prepare the accepted commit

1. Complete the record above and link evidence in the PR. Do not check boxes based
   on native tests or layout fixture renders.
2. Change the version only with `python tools/bump_version.py <version>` (`1.0.0`
   only after acceptance). It rewrites `VERSION`, the installer manifest, the
   documentation snapshot and the candidate line above, and refuses a version
   that is not greater. Add the changelog and release-note entries yourself.
   If the change touched a screenshot source (the list is `SOURCES` in
   `tools/curate_public_images.py`), run `python tools/refresh_public_images.py`;
   never edit a provenance hash by hand.
3. Update the changelog from pending to the accepted release date. Review the diff,
   run complete CI, and require approval of the final exact commit.
4. Merge only after the maintainer authorizes it. This PR update does not authorize merging.
5. Tag the accepted main commit `v1.0.0` and push that tag. Release candidates are
   tagged the same way (`v1.0.0-rc.10`) and published as GitHub pre-releases so
   testers can download the OTA application image. The production workflow
   refuses a tag that differs from VERSION, dirty tracked sources and a checkout
   different from the tagged commit.
6. Confirm release files/checksums, source commit in the manifest and build metadata,
   documentation version, and installer deployment. Do not substitute binaries
   from an earlier CI run or a local working tree.

## Release workflow and rollback

Ordinary main pushes neither create releases nor change the public installer. A
`v*` tag starts full CI and then a fresh production build on that exact tag, and
the installer (GitHub Pages) is rebuilt from the same tag. The `github-pages`
environment must allow deployments from tags matching `v*`
(Settings → Environments → github-pages → Deployment branches and tags). Packaging
includes the application binary for OTA, merged factory binary for USB, manifest,
build metadata and SHA-256 checksums. There is no test-firmware distribution.

If validation fails, fix the source and choose a new appropriate version; do not
silently move a published tag. Retain the prior known-good production release for
USB recovery. OTA consumes the application binary, never the offset-zero factory image.

Production PRs must explicitly change `VERSION` and synchronize generated firmware, flasher and docs metadata. CI validates the base-to-head diff without mutating the version. README/docs-only changes are exempt.
