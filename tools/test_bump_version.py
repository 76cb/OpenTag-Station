import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from bump_version import bump

ROOT = Path(__file__).resolve().parents[1]
HISTORY = {'CHANGELOG.md', 'documentation-site/docs/reference/release-notes.md'}


class BumpVersion(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / 'repo'
        tracked = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files'], text=True).splitlines()
        for relative in tracked:
            source = ROOT / relative
            if relative.startswith(('community/', 'third_party/', 'test/', 'src/')) or not source.is_file():
                continue
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)

    def tearDown(self):
        self.temporary.cleanup()

    def mentions(self, version):
        found = set()
        for path in self.root.rglob('*'):
            relative = path.relative_to(self.root).as_posix()
            if not path.is_file() or relative.startswith('.git/') or relative in HISTORY:
                continue
            try:
                if version in path.read_text(encoding='utf-8'):
                    found.add(relative)
            except UnicodeDecodeError:
                pass
        return found

    def test_every_mirror_is_rewritten(self):
        old = (self.root / 'VERSION').read_text().strip()
        # A version no committed prose can already mention.
        new = old.split('-')[0] + '-rc.9999' if '-' in old else '9999.0.0'
        self.assertTrue(self.mentions(old), 'fixture must start with mirrors of the old version')
        missing = bump(self.root, new)
        self.assertEqual(set(), self.mentions(old), 'a mirror of the old version was left behind')
        self.assertIn('VERSION', self.mentions(new))
        self.assertEqual(set(HISTORY), set(missing))

    def test_refuses_non_increasing_and_build_metadata(self):
        old = (self.root / 'VERSION').read_text().strip()
        for bad in (old, '0.0.1', old + '+build.1', 'not-a-version'):
            with self.subTest(version=bad), self.assertRaises(ValueError):
                bump(self.root, bad)
        self.assertEqual(old, (self.root / 'VERSION').read_text().strip())


if __name__ == '__main__':
    unittest.main()
