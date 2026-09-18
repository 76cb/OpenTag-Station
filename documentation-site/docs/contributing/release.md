# Release process

This technical reference describes implementation contracts. Dated test figures
below are historical checkpoints, not the current candidate’s acceptance record.
See the release checklist for outstanding physical checks.


The candidate is `1.0.0-rc.8`. `VERSION` is authoritative; generated manifests and
documentation metadata must be refreshed from it. A successful build is not
physical acceptance. Keep the release PR open and unmerged until external signoff.

## Acceptance record

Record reviewer, date, exact firmware SHA, board revision, service versions,
result and sanitized evidence for every item. All boxes below are **pending**.

- [ ] Browser visual review: all 65 desktop/tablet/mobile captures and real browser flows.
- [ ] WT32 physical visual, color, clipping and touch review of all five views.
- [ ] Community catalog update, offline local search, import, and canonical Spoolman result.
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
2. Change `VERSION` to `1.0.0` only after acceptance. Run
   `python tools/release_version.py --sync-manifest` and
   `python tools/export_docs.py --refresh` to refresh derived metadata.
3. Update the changelog from pending to the accepted release date. Review the diff,
   run complete CI, and require approval of the final exact commit.
4. Merge only after the maintainer authorizes it. This PR update does not authorize merging.
5. Tag the accepted main commit `v1.0.0` and push that tag. The production workflow
   refuses prerelease tags, mismatched VERSION, dirty tracked sources and a
   checkout different from the tagged commit.
6. Confirm release files/checksums, source commit in the manifest and build metadata,
   documentation version, and installer deployment. Do not substitute binaries
   from an earlier CI run or a local working tree.

## Release workflow and rollback

Ordinary main pushes update the installer but do not create releases. A `v*` tag
starts full CI and then a fresh production build on that exact tag. Packaging
includes the application binary for OTA, merged factory binary for USB, manifest,
build metadata and SHA-256 checksums. There is no test-firmware distribution.

If validation fails, fix the source and choose a new appropriate version; do not
silently move a published tag. Retain the prior known-good production release for
USB recovery. OTA consumes the application binary, never the offset-zero factory image.

Production PRs must explicitly change `VERSION` and synchronize generated firmware, flasher and docs metadata. CI validates the base-to-head diff without mutating the version. README/docs-only changes are exempt.
