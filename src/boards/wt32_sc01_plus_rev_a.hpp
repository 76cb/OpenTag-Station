#pragma once

#include <array>
#include <cstdint>

namespace opentag::boards {

struct St25r3916bPins {
  std::int8_t spi_sck;
  std::int8_t spi_mosi;
  std::int8_t spi_miso;
  std::int8_t chip_select;
  std::int8_t interrupt;
  std::int8_t reset;
  std::int8_t power_enable;
  bool external_reset_required{true};
  bool external_power_control_required{false};

  [[nodiscard]] constexpr bool complete() const {
    return spi_sck >= 0 && spi_mosi >= 0 && spi_miso >= 0 &&
           chip_select >= 0 && interrupt >= 0 &&
           (external_reset_required ? reset >= 0 : reset < 0) &&
           (external_power_control_required
                ? power_enable >= 0
                : power_enable < 0);
  }
};

struct Wt32Sc01PlusRevA {
  static constexpr const char* id = "wt32-sc01-plus-rev-a";
  static constexpr std::uint16_t display_width = 480;
  static constexpr std::uint16_t display_height = 320;
  static constexpr std::uint16_t display_native_width = 320;
  static constexpr std::uint16_t display_native_height = 480;
  static constexpr std::uint32_t lcd_write_frequency_hz = 40000000U;
  static constexpr std::uint32_t touch_frequency_hz = 400000U;
  static constexpr std::uint8_t display_rotation = 1;
  static constexpr bool display_invert = true;
  static constexpr bool display_rgb_order = false;
  // LVGL emits native-endian RGB565 (LV_COLOR_16_SWAP=0). LovyanGFX must
  // swap those input bytes while writing them to the ST7796 bus.
  static constexpr bool display_swap_bytes = true;

  // Built-in ST7796 8-bit parallel bus, verified against the physical
  // platform used by the reference hardware. These are not NFC pins.
  static constexpr std::int8_t lcd_write = 47;
  static constexpr std::int8_t lcd_read = -1;
  static constexpr std::int8_t lcd_command = 0;
  static constexpr std::array<std::int8_t, 8> lcd_data = {
      9, 46, 3, 8, 18, 17, 16, 15};
  static constexpr std::int8_t lcd_chip_select = -1;
  static constexpr std::int8_t lcd_reset = 4;
  static constexpr std::int8_t lcd_backlight = 45;
  static constexpr std::uint32_t lcd_backlight_frequency_hz = 44100U;
  static constexpr std::uint8_t lcd_backlight_pwm_channel = 7;

  // Built-in FT6336U-compatible touch controller.
  static constexpr std::int8_t touch_sda = 6;
  static constexpr std::int8_t touch_scl = 5;
  static constexpr std::int8_t touch_interrupt = 7;
  static constexpr std::uint8_t touch_address = 0x38;
  // LovyanGFX 1.2.27 software I2C; hardware units belong to scale and NFC.
  static constexpr int touch_i2c_port = -1;

  // External I2C connector used by the NAU7802 scale ADC.
  static constexpr std::int8_t scale_sda = 10;
  static constexpr std::int8_t scale_scl = 11;
  static constexpr std::uint8_t nau7802_address = 0x2A;

  // Production dual-I2C wiring; diagnostic aliases remain for native test helpers.
  static constexpr std::int8_t diagnostic_nfc_sda = 13;
  static constexpr std::int8_t diagnostic_nfc_scl = 14;
  static constexpr std::uint8_t diagnostic_nfc_i2c_address = 0x50;
  static constexpr std::int8_t diagnostic_nfc_interrupt = 12;
  static constexpr std::int8_t nfc_sda = diagnostic_nfc_sda;
  static constexpr std::int8_t nfc_scl = diagnostic_nfc_scl;
  static constexpr std::int8_t nfc_interrupt = diagnostic_nfc_interrupt;
  static constexpr std::uint8_t nfc_i2c_address = diagnostic_nfc_i2c_address;
  static constexpr std::uint32_t nfc_clock_hz = 100000U;

  // ELECHOUSE NFC_ST25R3916B has neither an external reset nor a power-enable
  // signal. Legacy SPI descriptors are retained for host-only abstraction tests;
  // the production build excludes that backend and binds only I2cReader.
  static constexpr St25r3916bPins nfc = {
      -1, -1, -1, -1, -1, -1, -1, false, false};
};

}  // namespace opentag::boards
