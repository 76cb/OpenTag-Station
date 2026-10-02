"""Advance VERSION and every file that mirrors it, in one step.

Usage: python tools/bump_version.py <new-version>

VERSION stays authoritative. This script only removes the manual part of a
bump: it refuses a version that is not semantically greater, rewrites the
derived manifest and documentation snapshot through their own tools, and
updates the two "The candidate is" lines. The changelog and release notes are
prose and are left to the author; the script reminds you when they lack an
entry for the new version.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

from check_version_policy import precedence
from release_version import SEMVER

ROOT = Path(__file__).resolve().parents[1]
CANDIDATE_LINES = ('docs/releasing.md', 'documentation-site/docs/contributing/release.md')
CANDIDATE = re.compile(r'The candidate is `[^`]+`\.')
PROSE = ('CHANGELOG.md', 'documentation-site/docs/reference/release-notes.md')


def bump(root, new):
    """Rewrite every mirror of VERSION under root; return prose files lacking an entry."""
    old = (root / 'VERSION').read_text().strip()
    if not SEMVER.fullmatch(new):
        raise ValueError(f'{new!r} is not valid semantic versioning')
    if '+' in new:
        raise ValueError('Build metadata does not change precedence; choose a real version')
    if precedence(new) <= precedence(old):
        raise ValueError(f'{new} must be semantically greater than {old}')
    (root / 'VERSION').write_text(new + '\n', encoding='utf-8')
    tools = root / 'tools'
    for command in (['release_version.py', '--sync-manifest'], ['export_docs.py', '--refresh']):
        subprocess.run([sys.executable, str(tools / command[0]), *command[1:]],
                       check=True, cwd=root, stdout=subprocess.DEVNULL)
    for relative in CANDIDATE_LINES:
        path = root / relative
        text, count = CANDIDATE.subn(f'The candidate is `{new}`.', path.read_text(encoding='utf-8'))
        if count != 1:
            raise ValueError(f'{relative}: expected exactly one "The candidate is" line')
        path.write_text(text, encoding='utf-8')
    return [p for p in PROSE if f'## {new}' not in (root / p).read_text(encoding='utf-8')]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('version')
    args = parser.parse_args()
    missing = bump(ROOT, args.version)
    print(f'VERSION and its mirrors now read {args.version}')
    for relative in missing:
        print(f'Reminder: add a "## {args.version}" entry to {relative}')


if __name__ == '__main__':
    main()
