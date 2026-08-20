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

  [[nodiscard]] constexpr bool complete() const {
    return spi_sck >= 0 && spi_mosi >= 0 && spi_miso >= 0 &&
           chip_select >= 0 && interrupt >= 0 && reset >= 0;
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

  // External I2C connector used by the NAU7802 scale ADC.
  static constexpr std::int8_t scale_sda = 10;
  static constexpr std::int8_t scale_scl = 11;
  static constexpr std::uint8_t nau7802_address = 0x2A;

  // The selected ST25R3916B module and its wiring are not yet specified.
  // Keeping every signal unassigned makes accidental hardware enablement fail.
  static constexpr St25r3916bPins nfc = {-1, -1, -1, -1, -1, -1, -1};
};

}  // namespace opentag::boards
