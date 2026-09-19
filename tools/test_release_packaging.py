"""Regression checks for public artifacts and release refusal paths."""
import json
import tempfile
import unittest
from pathlib import Path
from release_version import SEMVER, validate_tag
from check_public_privacy import assert_public_text, scan
from web_flasher import validate_pages_bundle, assemble_pages_bundle, FlasherError


class ReleasePackagingTests(unittest.TestCase):
    def test_only_production_firmware_is_distributed(self):
        import check_production_nfc_stack_usage
        import check_production_http_stack_usage
        root = Path(__file__).resolve().parents[1]
        import re
        environments = re.findall(r'^\[env:([^\]]+)\]', (root / 'platformio.ini').read_text(), re.M)
        self.assertEqual(environments, ['wt32-sc01-plus', 'native', 'native-writer-sanitized', 'native-community'])
        production = (root / 'platformio.ini').read_text()
        self.assertIn('board_build.filesystem = littlefs', production)
        self.assertIn('platformio/tool-mklittlefs@1.203.210628', production)
        self.assertFalse((root / 'src/diagnostics/shared_i2c_firmware.cpp').exists())
        self.assertEqual({p.name for p in (root / 'web-flasher').glob('*.json')}, {'manifest.json'})
        from community_catalog import inspect_pack
        catalog = inspect_pack(root / 'community/community.pack')
        self.assertLessEqual(catalog['size'], 2_621_440)
        self.assertEqual(catalog['records'], 53424)
        for workflow in (root / '.github/workflows').glob('*.yml'):
            self.assertNotIn('wt32-sc01-plus-i2c-test', workflow.read_text())
            self.assertNotIn('opentag-nfc-v-diagnostic-pr', workflow.read_text())

    def test_semver(self):
        for value in ['1.0.0', '1.0.0-rc.1', '1.0.0+build.42', '0.0.0']:
            self.assertIsNotNone(SEMVER.fullmatch(value))
        for value in ['1.0', 'v1.0.0', '01.0.0', '1.0.0-01', 'development']:
            self.assertIsNone(SEMVER.fullmatch(value))

    def test_release_refuses_candidate_and_mismatch(self):
        validate_tag('v1.0.0', '1.0.0')
        for tag, value in [('v1.0.0', '1.0.0-rc.1'), ('v1.0.1', '1.0.0'), ('v1.0.0-rc.1', '1.0.0-rc.1')]:
            with self.assertRaises(ValueError):
                validate_tag(tag, value)

    def test_privacy_rejects_case_variations_before_render(self):
        for value in [('Ca' + 'sy'), ('CA' + 'SY'), ('ca' + 'sy')]:
            with self.assertRaises(ValueError):
                assert_public_text('Printer: ' + value)
        assert_public_text('Prusa XL / SUNLU / 192.0.2.42')

    def test_privacy_rejects_artifact_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'metadata.json'
            path.write_text(json.dumps({'printer': 'ca' + 'sy'}))
            with self.assertRaises(ValueError):
                scan([Path(folder)])

    def test_pages_refuses_extra_distribution_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = Path(__file__).resolve().parents[1] / 'web-flasher'
            for name in ['index.html', 'manifest.json']:
                (root / name).write_bytes((source / name).read_bytes())
            (root / '.nojekyll').touch()
            (root / 'opentag-station-factory.bin').write_bytes(b'\xe9test')
            (root / 'community.pack').write_bytes((Path(__file__).resolve().parents[1] / 'community/community.pack').read_bytes())
            (root / 'community-manifest.json').write_bytes((Path(__file__).resolve().parents[1] / 'community/manifest.json').read_bytes())
            validate_pages_bundle(root, 16777216)
            (root / 'unapproved-test.bin').write_bytes(b'\xe9test')
            with self.assertRaises(FlasherError):
                validate_pages_bundle(root, 16777216)
            with self.assertRaises(FlasherError):
                assemble_pages_bundle(root, root / 'out', 16777216)


if __name__ == '__main__':
    unittest.main()
