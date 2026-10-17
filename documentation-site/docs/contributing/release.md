# Release process

A release is made by pushing a version tag. GitHub Actions builds, checks and publishes it.
The full checklist, including the tests on real hardware, is in
[docs/releasing.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/releasing.md).

The candidate is `1.0.0-rc.19`.

## Steps

1. Set the version with `python tools/bump_version.py <version>` and add the changelog entry.
   See [Contributing changes](pull-requests.md).
2. Merge to `main` and wait for CI to pass.
3. Tag that commit `v<version>`, for example `v1.0.0`, and push the tag.
4. Check the published release and the installer page.
5. Export this manual and publish it.

## What the tag starts

| Workflow | What it does |
|---|---|
| `release.yml` | Checks that the tag equals `v` + `VERSION` and points at the checked-out commit. Runs the `documentation`, `ui-review` and `firmware` jobs of `ci.yml` on the tag (`version-policy` runs only on pull requests). Downloads the files that run built, verifies them and their checksums, and creates the GitHub release with `CHANGELOG.md` as its text. |
| `web-flasher-pages.yml` | Builds the firmware and the installer bundle from the tag and deploys it to <https://76cb.github.io/OpenTag-Station/>. |

A tag `v<version>` whose version contains a hyphen is published as a pre-release.
A push to `main` without a tag creates no release and does not change the installer.
Starting `web-flasher-pages.yml` by hand builds the bundle; it deploys only when the run is
started on a `v*` tag.

## Release files

| File | Use |
|---|---|
| `opentag-station-<version>-application.bin` | Update from the station's web page |
| `opentag-station-factory.bin` | Full image used by the USB installer |
| `manifest.json` | Installer manifest |
| `build-metadata.json` | Version and source commit |
| `SHA256SUMS` | Checksums of the files above |

## Publish the manual

The manual is not published automatically. It is exported from the firmware repository and
committed to <https://github.com/76cb/OpenTag-Station-Docs>, which deploys it.

```sh
python tools/export_docs.py --output <empty-directory>
```

The export copies `documentation-site/`, records the firmware version and commit, and points
links to repository files at that commit. Copy the result into a clone of the Docs repository
and commit it there.
