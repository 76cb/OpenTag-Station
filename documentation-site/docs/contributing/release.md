# Release process

A release is made by merging a version change to `main`. GitHub Actions builds, checks and
publishes it, then updates the USB installer from the same files.
The full checklist, including the tests on real hardware, is in
[docs/releasing.md](https://github.com/76cb/OpenTag-Station/blob/main/docs/releasing.md).

The candidate is `1.0.0-rc.23`.

## Steps

1. Set the version with `python tools/bump_version.py <version>` and add the changelog entry.
   See [Contributing changes](pull-requests.md).
2. Open a pull request, wait for CI to pass, and merge it to `main`. Do not push a tag.
3. Check the published release and the installer page.
4. Export this manual and publish it.

## What the merge starts

`release.yml` runs when a push to `main` changes `VERSION`:

| Job | What it does |
|---|---|
| `version` | Reads `VERSION` and checks whether the release `v<VERSION>` exists. A tag without a release stops the run. |
| `validate` | Runs the `documentation`, `ui-review` and `firmware` jobs of `ci.yml` on that commit. |
| `publish` | Verifies the files that run built and their checksums, and creates the GitHub release and its tag `v<VERSION>`, with the version's changelog section as its text. A version with a hyphen is a pre-release. |
| `installer`, `deploy-installer` | Build the installer page from the release's own factory image and manifest and deploy it to <https://76cb.github.io/OpenTag-Station/>. |

The release and the installer therefore always carry the same build, and a failure stops
everything after it. A push to `main` that does not change `VERSION` publishes nothing.
To redeploy the installer, start `release.yml` by hand on `main` (Actions → Release → Run
workflow); when the release already exists it only redeploys the installer from it.

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
