"""Verify the curated README images and their fixture-source provenance."""
from pathlib import Path
import hashlib
import json
ROOT = Path(__file__).resolve().parents[1]
metadata = json.loads((ROOT / 'docs/images/provenance.json').read_text())
for group in ('images', 'sources'):
    for relative, digest in metadata[group].items():
        content = (ROOT / relative).read_bytes()
        if group == 'sources':
            content = content.replace(b'\r\n', b'\n')
        assert hashlib.sha256(content).hexdigest() == digest, f'Stale screenshot provenance: {relative}'
assert metadata['privacy_checked_before_capture'] is True
assert {p.relative_to(ROOT).as_posix() for p in (ROOT / 'docs/images').glob('*.png')} == set(metadata['images'])
print(f'Curated public screenshot/source provenance passed: {len(metadata["images"])} images')
