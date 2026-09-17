# Build from source

Rebuild the production firmware using the pinned PlatformIO environment.

## Before you start

Git, Python and a supported native C++ toolchain for host tests. Linux/WSL is the CI-equivalent environment; Windows filesystem newlines can affect source-text contract tests.

## Steps

1. Clone the firmware repository and check out the intended reviewed commit. Record git rev-parse HEAD and read VERSION.
2. Create a virtual environment and install requirements-dev.txt. PlatformIO and embedded libraries/platform revisions are pinned in project files.
3. Run python tools/release_version.py, python tools/generate_product_ui.py --check and python tools/check_public_privacy.py.
4. Run pio test --environment native and the sanitizer environment on Linux; then build pio run --environment wt32-sc01-plus.
5. Run the production ELF, RAM and stack checks listed in CI. Build pio run --environment wt32-sc01-plus --target web-flasher for USB packaging.
6. Use tools/web_flasher.py assemble-pages with only --factory-bundle, --output-dir and --maximum-size 16777216, then validate-pages.

## Expected result

The application embeds VERSION and source SHA; the factory manifest matches VERSION and records the full source commit. Only one embedded firmware environment is shipped.

## If it fails

Do not mix artifacts from different commits or ignore stack failures. Native diagnostic helpers remain test-only. A compiled image does not establish physical acceptance. See [testing](testing.md) and [release process](release.md).
