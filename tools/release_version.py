"""VERSION authority, release tag checks, and binary provenance verification."""
import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NUMBER = r'(?:0|[1-9][0-9]*)'
SEMVER = re.compile(rf'{NUMBER}\.{NUMBER}\.{NUMBER}(?:-(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?')


def version(root=ROOT):
    value = (root / 'VERSION').read_text().strip()
    if not SEMVER.fullmatch(value):
        raise ValueError('VERSION must be valid semantic versioning')
    return value


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def validate_tag(tag, value):
    if not re.fullmatch(rf'v{NUMBER}\.{NUMBER}\.{NUMBER}', tag) or tag != 'v' + value:
        raise ValueError('Production release tag must be vX.Y.Z and exactly match VERSION')


def verify_binary(binary, metadata):
    data = json.loads(metadata.read_text())
    expected = {'version': version(), 'source_commit': git('rev-parse', 'HEAD')}
    if any(data.get(k) != v for k, v in expected.items()):
        raise ValueError('Build metadata differs from VERSION or checked-out commit')
    image = binary.read_bytes()
    for token in (data['version'], data['source_commit'][:12]):
        if token.encode() + b'\0' not in image:
            raise ValueError('Firmware does not embed exact build metadata')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sync-manifest', action='store_true')
    parser.add_argument('--tag')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--metadata', type=Path)
    args = parser.parse_args()
    value = version()
    manifest = ROOT / 'web-flasher/manifest.json'
    data = json.loads(manifest.read_text())
    if args.sync_manifest:
        data['version'] = value
        manifest.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    if data['version'] != value:
        raise ValueError('Run python tools/release_version.py --sync-manifest after changing VERSION')
    if args.tag:
        validate_tag(args.tag, value)
        if git('rev-parse', args.tag + '^{commit}') != git('rev-parse', 'HEAD'):
            raise ValueError('Release is not built from the exact tagged commit')
        if git('status', '--porcelain', '--untracked-files=no'):
            raise ValueError('Release tracked sources must be clean')
    if args.binary:
        if not args.metadata:
            parser.error('--metadata is required with --binary')
        verify_binary(args.binary, args.metadata)
    print(f'Version/provenance valid: {value}')


if __name__ == '__main__':
    main()
