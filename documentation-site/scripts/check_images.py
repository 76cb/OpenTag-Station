"""Verify committed images came from the guarded public capture run."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
metadata = json.loads((ROOT / 'image-provenance.json').read_text())
assert metadata['privacy_checked_before_capture'] is True
expected = metadata['images']
actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / 'docs/assets/images').rglob('*.png')}
assert actual == set(expected), 'Every public PNG must have guarded capture provenance'
for relative, digest in expected.items():
    assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == digest, relative
print(f'Guarded screenshot provenance passed: {len(expected)} images')
