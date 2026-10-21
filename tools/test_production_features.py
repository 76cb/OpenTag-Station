"""Production release boundaries, including app-only OTA and factory wording."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProductionBoundaries(unittest.TestCase):
    def test_community_catalog_stays_deleted(self):
        # Deleted in 1.0.1-beta.4 (#86). The sources, the feature switch, its test environment
        # and the catalog pack must not come back.
        for path in ('src/config/product_features.hpp', 'src/services/community_catalog.cpp',
                     'src/services/community_catalog_updater.cpp', 'src/network/gzip_stream.hpp',
                     'src/network/miniz', 'src/platform/storage/community_catalog_store.cpp',
                     'community/community.pack', 'test/test_community_catalog'):
            self.assertFalse((ROOT / path).exists(), path)
        config = (ROOT / 'platformio.ini').read_text()
        self.assertNotIn('OPENTAG_ENABLE_COMMUNITY', config)
        self.assertNotIn('native-community', config)
        for path in list((ROOT / 'src').rglob('*.cpp')) + list((ROOT / 'src').rglob('*.hpp')):
            text = path.read_text()
            self.assertNotIn('OPENTAG_ENABLE_COMMUNITY', text, path)
            self.assertNotIn('community_catalog', text, path)
            self.assertNotIn('GzipStream', text, path)
        # A8: a factory reset still deletes the catalog files an old station may hold.
        storage = (ROOT / 'src/platform/storage/storage_service.cpp').read_text()
        reset_paths = storage.split('reset_paths[] = {', 1)[1].split('};', 1)[0]
        for leftover in ('"/community.new"', '"/community.old"', '"/community.pack"'):
            self.assertIn(leftover, reset_paths)

    def test_ota_uses_only_inactive_application_partition(self):
        source = (ROOT / 'src/platform/ota/esp32_ota_platform.cpp').read_text()
        self.assertIn('esp_ota_get_next_update_partition(running)', source)
        self.assertIn('ESP_PARTITION_TYPE_APP', source)
        self.assertIn('esp_ota_begin(partition, expected_size, &write_handle_)', source)
        for path in [ROOT / 'src/platform/ota/esp32_ota_platform.cpp', ROOT / 'src/application/ota_worker.cpp']:
            text = path.read_text()
            for forbidden in ('LittleFS', 'nvs_flash_erase', 'esp_partition_erase_range',
                              'esp_flash_erase_chip', '.factory_reset(', 'clear_scale_calibration'):
                self.assertNotIn(forbidden, text)

    def test_factory_is_explicitly_destructive_before_activation(self):
        page = (ROOT / 'web-flasher/index.html').read_text()
        self.assertLess(page.index('Erases station configuration and calibration'),
                        page.index('<esp-web-install-button'))
        self.assertIn('Update Existing Station', page)
        self.assertIn('Factory Install / Recovery', page)
        self.assertIn('opentag-station-&lt;version&gt;-application.bin', page)


if __name__ == '__main__':
    unittest.main()
