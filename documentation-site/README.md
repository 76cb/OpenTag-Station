# OpenTag Station documentation

Dedicated source for <https://76cb.github.io/OpenTag-Station-Docs/>.
The firmware installer remains at <https://76cb.github.io/OpenTag-Station/>.

This project is reviewed in the firmware PR under `documentation-site/` and
exported to `76cb/OpenTag-Station-Docs`. Firmware `VERSION` is the authority;
`firmware.json` is its generated provenance snapshot, never an independent version.
The footer shows the described firmware and candidate acceptance status.

## Build and review

```sh
python -m pip install -r requirements.txt
python scripts/validate.py docs
python scripts/check_images.py
python -m mkdocs build --strict
python scripts/validate.py site
python -m mkdocs serve
```

CI performs these checks for pull requests. Main deploys to this repository's
GitHub Pages through pinned Actions. There is no backend or analytics service;
fonts are local/system and search runs in the browser.

## Refresh from firmware

Make content corrections in the firmware project's `documentation-site/`, run
`python tools/check_hardware_docs.py` there, regenerate guarded images when needed,
then run `python tools/export_docs.py --output <empty-directory>` from the firmware
checkout. Copy that reviewed export into this repository and commit normally.
Never overwrite this repository's `.git` directory. The export stamps the exact
firmware commit and VERSION. Change the firmware VERSION first for releases.

To initialize the requested repository after a validated export:

```sh
git init -b main
git add .
git commit -m "Publish OpenTag Station release candidate documentation"
gh repo create 76cb/OpenTag-Station-Docs --public --source . --remote origin --push
gh api --method POST repos/76cb/OpenTag-Station-Docs/pages -f build_type=workflow
```

Run these commands **inside the validated export directory**. If the repository
already exists, clone it and commit the refreshed files instead of creating it.
In Settings → Pages, select GitHub Actions if API creation is unavailable.
Do not deploy this project into the firmware installer repository.

Documentation retains the firmware project's PolyForm Noncommercial license;
third-party software and referenced manufacturer material retain their licenses.
