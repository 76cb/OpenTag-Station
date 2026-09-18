"""Validate explicit production version bumps; never edit VERSION in CI."""
import argparse
import subprocess
from pathlib import Path
from release_version import SEMVER

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ('src/', 'boards/', 'partitions.csv', 'platformio.ini',
              'third_party/', 'web-flasher/', 'tools/build_metadata.py',
              'tools/precompress_web_assets.py', 'tools/web_flasher',
              'tools/prepare_release.py', 'tools/web_asset_compression.py')


def requires_version(paths):
    return any(p.startswith(PRODUCTION) for p in paths)


def precedence(version):
    """Return SemVer precedence for a validated version, ignoring build metadata."""
    release, separator, prerelease = version.split('+', 1)[0].partition('-')
    core = tuple(int(part) for part in release.split('.'))
    # Numeric identifiers precede nonnumeric identifiers; tuple ordering also
    # makes a shorter matching identifier sequence precede a longer one.
    identifiers = tuple((0, int(part)) if part.isdigit() else (1, part)
                        for part in prerelease.split('.')) if separator else ()
    return core, not separator, identifiers


def validate(paths, before, after):
    if not SEMVER.fullmatch(after):
        raise ValueError('Invalid VERSION')
    if requires_version(paths):
        if not SEMVER.fullmatch(before):
            raise ValueError('Invalid base VERSION')
        if precedence(after) <= precedence(before):
            raise ValueError('Production behavior changed: VERSION must be semantically greater '
                             'than base VERSION; explicitly advance VERSION and synchronize manifests/docs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True)
    args = parser.parse_args()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()
    paths = git('diff', '--name-only', args.base, 'HEAD').splitlines()
    validate(paths, git('show', args.base + ':VERSION'), (ROOT / 'VERSION').read_text().strip())
    print('Explicit production version policy passed')


if __name__ == '__main__':
    main()
