"""Regenerate the curated public screenshots and their provenance receipts.

Run this whenever a file listed in curate_public_images.SOURCES changes;
check_committed_images.py fails until the receipt matches the sources again.
Never edit a hash in docs/images/provenance.json by hand: the receipt states
that the images were captured from exactly those sources.

Prerequisites (the same ones the ui-review CI job installs):
  python -m pip install Pillow
  npm install --prefix .pio/review-tools playwright
  .pio/review-tools/node_modules/.bin/playwright install chromium
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / '.pio/ui-review'
FIXTURES = ROOT / '.pio/touch-flow-fixtures.json'


def run(*command, **environment):
    print('+', ' '.join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], check=True, cwd=ROOT,
                   env={**os.environ, **environment})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--fixtures-ready', action='store_true',
                        help='Use the existing .pio/touch-flow-fixtures.json instead of running test_touch_flow')
    args = parser.parse_args()
    python = sys.executable
    if not args.fixtures_ready:
        run('pio', 'test', '--environment', 'native', '--filter', 'test_touch_flow')
    if not FIXTURES.is_file():
        raise SystemExit('test_touch_flow did not export .pio/touch-flow-fixtures.json')
    run(python, 'tools/check_public_privacy.py')
    run(python, 'tools/generate_product_ui.py', '--check')
    run(python, 'tools/product_review.py', '--output', OUTPUT)
    run(python, 'tools/render_touch_review.py', '--output', OUTPUT)
    run('node', 'tools/capture_product_review.cjs', OUTPUT,
        NODE_PATH=str(ROOT / '.pio/review-tools/node_modules'))
    run(python, 'tools/check_public_privacy.py', OUTPUT)
    run(python, 'tools/curate_public_images.py', OUTPUT)
    run(python, 'tools/check_committed_images.py')
    run(python, 'documentation-site/scripts/check_images.py')


if __name__ == '__main__':
    main()
