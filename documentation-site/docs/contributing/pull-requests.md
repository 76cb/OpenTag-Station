# Contributing changes

How to prepare a change: what CI checks, and what the project expects by convention.
The project is licensed under the PolyForm Noncommercial License 1.0.0
([LICENSE.md](https://github.com/76cb/OpenTag-Station/blob/main/LICENSE.md)).

## Where things live

| What | Where |
|---|---|
| Firmware | `src/` |
| Host tests | `test/` |
| Build, check and release tools | `tools/` |
| USB installer page | `web-flasher/` |
| This manual | `documentation-site/docs/` |
| Developer notes | `docs/` |

## Version policy

A pull request that changes what is shipped must raise `VERSION`.
The `version-policy` CI job runs `tools/check_version_policy.py` and fails otherwise.

Paths that need a higher version:

- `src/`, `boards/`, `third_party/`, `web-flasher/`
- `partitions.csv`, `platformio.ini`
- `tools/build_metadata.py`, `tools/precompress_web_assets.py`, `tools/prepare_release.py`,
  `tools/web_asset_compression.py` and every `tools/web_flasher*` file

Changes to `README.md`, `CHANGELOG.md`, `docs/`, `documentation-site/`, `test/` and the other
tools do not need a new version.

Raise the version with one command. Never edit `VERSION` or its copies by hand.

```sh
python tools/bump_version.py <new-version>
```

It refuses a version that is not higher, then rewrites `VERSION`, `web-flasher/manifest.json`,
`documentation-site/firmware.json` and the "The candidate is" line in `docs/releasing.md` and
in [Release process](release.md). It reminds you if the changelog has no entry for the new
version.

## Changelog

Add a `## <new-version>` section to `CHANGELOG.md` that says what changed for the person using
the station. CI does not check the changelog; it enforces only the `VERSION` bump
(`tools/check_version_policy.py`). The entry is a convention that `tools/bump_version.py` reminds
you about. The file is used as the text of the GitHub release, so:

- write plain sentences, newest version first;
- use only absolute `https://` links.

[Release notes](../reference/release-notes.md) in this manual is a copy of `CHANGELOG.md` from
its second line on, under the heading `# Release notes`. After editing the changelog, copy it:

```sh
{ echo '# Release notes'; tail -n +2 CHANGELOG.md; } > documentation-site/docs/reference/release-notes.md
```

CI does not compare the two files either. Check that they match; this command must print nothing:

```sh
diff <(tail -n +2 CHANGELOG.md) <(tail -n +2 documentation-site/docs/reference/release-notes.md)
```

## Screenshots

The screenshots in `docs/images/` and in this manual are generated from the firmware sources
with demo data. Each one is recorded with a hash of the sources it was made from, and
`tools/check_committed_images.py` fails in CI when a source changed but the images did not.

The sources are the `SOURCES` list in
[`tools/curate_public_images.py`](https://github.com/76cb/OpenTag-Station/blob/main/tools/curate_public_images.py).
If your change touches one of them, regenerate the images:

```sh
python tools/refresh_public_images.py
```

It needs Pillow, Playwright and Chromium; the top of the script lists the install commands.
Never edit a hash in `docs/images/provenance.json` or
`documentation-site/image-provenance.json` by hand, and do not add or replace an image manually.

## Documentation

- User documentation goes in this manual (`documentation-site/docs/`). Add a new page to the
  `nav` in `documentation-site/mkdocs.yml`.
- Notes for developers go in `docs/`.
- If you change a pin, bus address or clock, run `python tools/check_hardware_docs.py`; it
  compares the wiring page and diagrams with the board header.
- Do not publish private addresses, credentials, real tag serial numbers or printer names. Use
  generic example data.

## Before you open the pull request

```sh
git diff --check
pio test --environment native
pio run --environment wt32-sc01-plus
python -m mkdocs build --strict -f documentation-site/mkdocs.yml
```

See [Testing and CI](testing.md) for the full list of checks.
A change to tag writing, clearing or weight saving needs tests for the refusal and recovery
paths as well as the success path.
Checks on real hardware are separate from CI; say in the pull request what you tested on a
station.
