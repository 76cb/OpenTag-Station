"""Stage production-only, versioned release files with exact build provenance."""
from pathlib import Path
import hashlib
import json
import shutil
from release_version import ROOT, version, git, verify_binary
from web_flasher import validate_pages_bundle

build = ROOT / '.pio/build/wt32-sc01-plus'
bundle = build / 'web-flasher'
output = ROOT / '.pio/release'
verify_binary(build / 'firmware.bin', build / 'build-metadata.json')
validate_pages_bundle(bundle, 16777216)
manifest = json.loads((bundle / 'manifest.json').read_text())
assert manifest['version'] == version() and manifest['source_commit'] == git('rev-parse', 'HEAD')
output.mkdir(parents=True, exist_ok=True)
if any(output.iterdir()):
    raise ValueError('Release staging directory must be empty; stale artifacts cannot be published')
for source, name in [(build / 'firmware.bin', f'opentag-station-{version()}-application.bin'),
                     (bundle / 'opentag-station-factory.bin', 'opentag-station-factory.bin'),
                     (build / 'community-littlefs.bin', 'community-littlefs.bin'),
                     (bundle / 'community.pack', 'community.pack'),
                     (bundle / 'community-manifest.json', 'community-manifest.json'),
                     (bundle / 'manifest.json', 'manifest.json'),
                     (build / 'build-metadata.json', 'build-metadata.json')]:
    shutil.copy2(source, output / name)
checksums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(output.iterdir()))
(output / 'SHA256SUMS').write_text(checksums, encoding='utf-8')
print(f'Production release staging complete: {version()}, {git("rev-parse", "HEAD")}')
