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

```sh
python tools/export_docs.py --output <empty-directory>
```

Copy the exported files over a clone of `76cb/OpenTag-Station-Docs` (keep its `.git` directory),
commit and push. That repository's workflow runs the same checks and deploys to GitHub Pages.
The export stamps the firmware commit and version into `firmware.json`.

The manual is under the firmware's [PolyForm Noncommercial license](LICENSE.md).
