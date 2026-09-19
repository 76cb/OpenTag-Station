"""Production release boundaries, including app-only OTA and factory wording."""
from pathlib import Path
import unittest
from tools.product_features import community_enabled

ROOT = Path(__file__).resolve().parents[1]


class ProductionBoundaries(unittest.TestCase):
    def test_community_default_is_disabled(self):
        self.assertFalse(community_enabled())
        config = (ROOT / 'platformio.ini').read_text()
        production = config.split('[env:wt32-sc01-plus]', 1)[1].split('[env:native]', 1)[0]
        self.assertNotIn('OPENTAG_ENABLE_COMMUNITY=1', production)

    def test_backend_installs_no_catalog_callbacks_in_production(self):
        source = (ROOT / 'src/application/tag_writer_commands.cpp').read_text()
        import re
        production = re.sub(r'#if OPENTAG_ENABLE_COMMUNITY.*?#endif', '', source, flags=re.S)
        for call in ('community_catalog_.search', 'community_catalog_.detail',
                     'community_catalog_.status', 'community_updater_.update'):
            self.assertNotIn(call, production)
        for path in (ROOT / 'src/application').glob('*.cpp'):
            if path.name != 'tag_writer_commands.cpp':
                self.assertNotIn('community_catalog_.', path.read_text())
                self.assertNotIn('community_updater_.', path.read_text())

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
        self.assertIn('firmware.bin', page)


if __name__ == '__main__':
    unittest.main()
