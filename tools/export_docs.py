"""Deterministically export the dedicated docs project with firmware provenance."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path
from release_version import ROOT, version, git


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh', action='store_true', help='Refresh derived version snapshot in source')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    source = ROOT / 'documentation-site'
    metadata = {'version': version(), 'source_repository': '76cb/OpenTag-Station',
                'source_ref': 'codex/current-spool-experience', 'acceptance': 'pending'}
    snapshot = source / 'firmware.json'
    if args.refresh:
        snapshot.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    if json.loads(snapshot.read_text())['version'] != version():
        raise ValueError('Documentation version snapshot differs from VERSION; run --refresh')
    if args.output:
        destination = args.output.resolve()
        if destination == source.resolve() or destination == ROOT.resolve():
            raise ValueError('Export must use a separate directory')
        if destination.exists() and any(destination.iterdir()):
            raise ValueError('Use an empty output directory to prevent stale publication files')
        shutil.copytree(source, destination, ignore=shutil.ignore_patterns('site', '__pycache__', '.venv', '.git'), dirs_exist_ok=True)
        metadata['source_commit'] = git('rev-parse', 'HEAD')
        metadata['source_ref'] = metadata['source_commit']
        (destination / 'firmware.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        for page in (destination / 'docs').rglob('*.md'):
            content = page.read_text(encoding='utf-8').replace(
                'https://github.com/76cb/OpenTag-Station/blob/codex/current-spool-experience/',
                'https://github.com/76cb/OpenTag-Station/blob/' + metadata['source_commit'] + '/')
            page.write_text(content, encoding='utf-8')
        print(f'Exported {len(list((destination / "docs").rglob("*.md")))} pages to {destination}')
    print('Documentation version snapshot matches VERSION')


if __name__ == '__main__':
    main()
