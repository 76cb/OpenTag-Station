"""Copy five reviewed images only after all guarded capture receipts verify."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from check_public_privacy import scan

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['VERSION', 'src/config/product_features.hpp', 'tools/product_features.py', 'tools/product_journeys.cjs', 'tools/product_review.py', 'tools/test_writer_display.py',
           'tools/capture_product_review.cjs', 'tools/render_touch_review.py',
           'tools/check_public_privacy.py', 'src/web/web_assets.cpp',
           'src/web/writer_assets.cpp', 'src/web/writer_layout.inc', 'src/ui/product_layout.hpp', 'src/ui/tag_flow.hpp', 'src/ui/touch_input.hpp', 'test/test_touch_flow/fixtures.hpp']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture_dir', type=Path)
    args = parser.parse_args()
    scan([args.capture_dir])
    touch_count=len(json.loads((ROOT/'.pio/touch-flow-fixtures.json').read_text()))+5
    for name, count in [('capture-provenance.json', 80), ('touch-provenance.json', touch_count)]:
        receipt = json.loads((args.capture_dir / name).read_text())
        assert receipt['privacy_checked_before_capture'] is True and len(receipt['images']) == count
        for image, expected in receipt['images'].items():
            assert digest(args.capture_dir / image) == expected, image
    images = {}
    site_images = {}
    selections = {'dashboard': '1440-dashboard', 'weigh': '1440-weigh',
                  'assignment': '1440-assignment', 'tag-management': '1440-manage', 'wt32-home': 'wt32-home'}
    for name, source in selections.items():
        relative = f'docs/images/{name}.png'
        target = ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.capture_dir / (source + '.png'), target)
        images[relative] = digest(target)
        relative = f'docs/assets/images/{"touchscreen" if name.startswith("wt32") else "browser"}/{name}.png'
        target = ROOT / 'documentation-site' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.capture_dir / (source + '.png'), target)
        site_images[relative] = digest(target)
    metadata = {'privacy_checked_before_capture': True,
                'browser_captures': 80, 'touch_layout_fixtures': touch_count,
                'sources': {p: hashlib.sha256((ROOT / p).read_bytes().replace(b'\r\n', b'\n')).hexdigest() for p in SOURCES}, 'images': images}
    (ROOT / 'docs/images/provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
    metadata['images'] = site_images
    (ROOT / 'documentation-site/image-provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Curated five guarded screenshots; {80+touch_count} capture receipts verified')


if __name__ == '__main__':
    main()
