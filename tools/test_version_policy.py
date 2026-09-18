import unittest
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

    def test_explicit_bump(self):
        validate(['src/ui/ui_service.cpp'], '1.0.0-rc.1', '1.0.0-rc.2')

    def test_invalid_version(self):
        with self.assertRaises(ValueError):
            validate(['README.md'], '1.0.0-rc.1', 'latest')
