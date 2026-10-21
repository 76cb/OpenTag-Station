# OpenTag Station manual

Source of the manual published at <https://76cb.github.io/OpenTag-Station-Docs/>.
(The USB installer is a separate page: <https://76cb.github.io/OpenTag-Station/>.)

Edit the manual here, in the firmware repository. It is then exported to
[`76cb/OpenTag-Station-Docs`](https://github.com/76cb/OpenTag-Station-Docs), which builds and
publishes it. Nothing publishes it automatically: until you export, the public manual stays as it
was.

## Layout

| Path | Content |
|---|---|
| `docs/` | The pages |
| `mkdocs.yml` | Site settings and the navigation (JSON syntax) |
| `hooks.py` | Puts the firmware version in the site title and footer |
| `firmware.json` | Copy of the firmware `VERSION`; written by `tools/export_docs.py --refresh`, never by hand |
| `hardware.json` | Pins and addresses that `tools/check_hardware_docs.py` compares with the firmware source |
| `image-provenance.json` | Hashes of the screenshots; written by `tools/refresh_public_images.py` |
| `scripts/` | Link, anchor and image checks |

## Preview and check

From the repository root:

```sh
python -m pip install -r documentation-site/requirements.txt
python tools/check_hardware_docs.py
python tools/export_docs.py --check
python documentation-site/scripts/check_images.py
python -m mkdocs build --strict -f documentation-site/mkdocs.yml
python documentation-site/scripts/validate.py documentation-site/site
python -m mkdocs serve -f documentation-site/mkdocs.yml
```

CI runs the same checks on every pull request.

## Rules for pages

- Write for someone who owns a printer and runs Spoolman, not for a firmware developer.
- Quote button labels and messages exactly as the firmware shows them.
- Describe what the firmware does now. History belongs in the changelog.
- One fact, one page. Link instead of repeating.
- `docs/reference/release-notes.md` is a copy of `../CHANGELOG.md` from its second line on. Edit the
  changelog, then copy.
- `docs/hardware/wiring.md` must keep the pin numbers, addresses and bus speeds that
  `tools/check_hardware_docs.py` looks for.
- The wiring diagrams are generated: `python tools/check_hardware_docs.py --write`.

## Publish

A merge to `main` that changes `documentation-site/`, `VERSION` or `tools/export_docs.py` publishes
the manual automatically: the workflow `.github/workflows/docs-publish.yml` ("Publish manual") runs
the checks above, exports the manual, syncs the export over a checkout of
`76cb/OpenTag-Station-Docs` (removing files that no longer exist there, keeping that repository's
`.git`, `.github` and `.gitignore`), runs the Docs repository's own checks, and pushes a commit
`Manual for <version> from 76cb/OpenTag-Station@<commit>` to its `main`. That repository's workflow
then builds and deploys GitHub Pages. Pre-release versions are published too. When nothing changed,
the workflow prints `Manual already up to date` and stops.

To inspect the export locally:

```sh
python tools/export_docs.py --output <empty-directory>
```

The export stamps the firmware commit and version into `firmware.json`.

One-time setup, needed before the first run:

1. Generate a key pair: `ssh-keygen -t ed25519 -N "" -f docs-deploy-key`.
2. On `76cb/OpenTag-Station-Docs`, add `docs-deploy-key.pub` as a deploy key with write access
   (Settings → Deploy keys → Add deploy key, tick **Allow write access**).
3. On `76cb/OpenTag-Station`, add the private key `docs-deploy-key` as the repository secret
   `DOCS_DEPLOY_KEY` (Settings → Secrets and variables → Actions → New repository secret).
4. Run the workflow once by hand: Actions → **Publish manual** → **Run workflow**. This first run
   brings the Docs repository up to date.

The manual is under the firmware's [PolyForm Noncommercial license](LICENSE.md).
