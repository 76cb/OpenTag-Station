"""Reject stale, unapproved, or mismatched downloaded release artifacts."""
from pathlib import Path
import hashlib
import json
import sys
from release_version import version, git, verify_binary

root = Path(sys.argv[1])
app = f'opentag-station-{version()}-application.bin'
expected = {app, 'opentag-station-factory.bin', 'manifest.json', 'build-metadata.json', 'SHA256SUMS'}
assert {p.name for p in root.iterdir()} == expected, 'Unapproved release files'
manifest = json.loads((root / 'manifest.json').read_text())
assert manifest['version'] == version() and manifest['source_commit'] == git('rev-parse', 'HEAD')
verify_binary(root / app, root / 'build-metadata.json')
factory = (root / 'opentag-station-factory.bin').read_bytes()
application = (root / app).read_bytes()
assert factory[0x10000:0x10000+len(application)] == application, 'Factory/application provenance mismatch'
checksums = {}
for line in (root / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert name in expected - {'SHA256SUMS'} and name not in checksums
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest
    checksums[name] = digest
assert set(checksums) == expected - {'SHA256SUMS'}
print('Exact-commit production release artifacts verified')
