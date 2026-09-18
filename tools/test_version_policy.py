import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
from check_version_policy import main
from check_version_policy import validate


class VersionPolicy(unittest.TestCase):
    def test_docs_exempt(self):
        validate(['README.md', 'docs/guide.md'], '1.0.0-rc.2', '1.0.0-rc.2')

    def test_production_requires_change(self):
        for path in ['src/ui/ui_service.cpp', 'src/web/ui/product.js',
                     'src/config/configuration_service.cpp', 'boards/wt32.json',
                     'platformio.ini', 'src/hardware/nfc/driver.cpp']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                validate([path], '1.0.0-rc.2', '1.0.0-rc.2')

    def test_requested_transitions(self):
        for before, after, allowed in [
            ('1.0.0-rc.1', '1.0.0-rc.2', True),
            ('1.0.0-rc.2', '1.0.0-rc.3', True),
            ('1.0.0-rc.2', '1.0.0', True),
            ('1.0.0-rc.2', '1.0.0-rc.1', False),
            ('1.0.0-rc.2', '0.9.0', False),
            ('1.0.0', '1.0.1', True),
            ('1.0.1', '1.0.0', False),
        ]:
            with self.subTest(before=before, after=after):
                if allowed:
                    validate(['src/ui/ui_service.cpp'], before, after)
                else:
                    with self.assertRaisesRegex(ValueError, 'semantically greater'):
                        validate(['src/ui/ui_service.cpp'], before, after)

    def test_semver_precedence(self):
        ordered = ['1.0.0-alpha', '1.0.0-alpha.1', '1.0.0-alpha.beta',
                   '1.0.0-beta', '1.0.0-beta.2', '1.0.0-beta.11',
                   '1.0.0-rc.1', '1.0.0-rc.9', '1.0.0-rc.10', '1.0.0',
                   '1.0.1-0', '1.0.1-9', '1.0.1-A', '1.0.1-a',
                   '1.0.1', '1.9.0', '1.10.0', '2.0.0', '10.0.0']
        for before, after in zip(ordered, ordered[1:]):
            with self.subTest(before=before, after=after):
                validate(['src/main.cpp'], before, after)
                with self.assertRaises(ValueError):
                    validate(['src/main.cpp'], after, before)

    def test_build_metadata_does_not_advance_version(self):
        for before, after in [('1.0.0', '1.0.0+build.1'),
                              ('1.0.0-rc.2+old', '1.0.0-rc.2+new')]:
            with self.subTest(before=before, after=after), self.assertRaises(ValueError):
                validate(['src/main.cpp'], before, after)
        validate(['src/main.cpp'], '1.0.0-rc.2+old', '1.0.0+new')

    def test_invalid_base_fails_closed_for_production(self):
        with self.assertRaisesRegex(ValueError, 'Invalid base VERSION'):
            validate(['src/main.cpp'], 'latest', '1.0.0')

    def test_invalid_prerelease(self):
        with self.assertRaises(ValueError):
            validate(['src/main.cpp'], '1.0.0', '1.0.1-rc.01')

    def test_invalid_version(self):
        with self.assertRaises(ValueError):
            validate(['README.md'], '1.0.0-rc.1', 'latest')

    def test_cli_never_edits_version(self):
        for after, allowed in [('1.0.0-rc.3', True), ('1.0.0-rc.1', False)]:
            with self.subTest(after=after), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                version_file = root / 'VERSION'
                original = (after + '\n').encode('ascii')
                version_file.write_bytes(original)
                with patch('check_version_policy.ROOT', root), \
                     patch('sys.argv', ['check_version_policy.py', '--base', 'base']), \
                     patch('check_version_policy.subprocess.check_output',
                           side_effect=['src/main.cpp\n', '1.0.0-rc.2\n']) as git, \
                     patch('builtins.print'):
                    if allowed:
                        main()
                    else:
                        with self.assertRaises(ValueError):
                            main()
                    self.assertEqual([call.args[0][3:] for call in git.call_args_list],
                                     [['diff', '--name-only', 'base', 'HEAD'],
                                      ['show', 'base:VERSION']])
                self.assertEqual(version_file.read_bytes(), original)
