#pragma once

#include <cstdint>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "boards/wt32_sc01_plus_rev_a.hpp"

namespace opentag::hardware::display {

class Wt32DisplayDevice final : public lgfx::LGFX_Device {
 public:
  Wt32DisplayDevice();

 private:
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_Parallel8 bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_FT5x06 touch_;
};

struct TouchPoint {
  std::int32_t x{0};
  std::int32_t y{0};
  bool pressed{false};
};

class Wt32Display {
 public:
  bool initialize();
  void set_brightness(std::uint8_t percent);
  void sleep();
  void wake();
  void push_pixels(
      std::int32_t x,
      std::int32_t y,
      std::int32_t width,
      std::int32_t height,
      const std::uint16_t* pixels);
  TouchPoint read_touch(std::uint32_t now_ms);

  [[nodiscard]] bool initialized() const { return initialized_; }
  [[nodiscard]] bool sleeping() const { return sleeping_; }
  [[nodiscard]] bool touch_configured() const { return initialized_; }
  [[nodiscard]] std::uint8_t brightness() const { return brightness_percent_; }

 private:
  static constexpr std::uint32_t touch_debounce_ms = 20U;

  Wt32DisplayDevice device_;
  bool initialized_{false};
  bool sleeping_{false};
  std::uint8_t brightness_percent_{80};
  bool touch_candidate_{false};
  bool touch_stable_{false};
  std::uint32_t touch_candidate_since_ms_{0};
  std::int32_t last_x_{0};
  std::int32_t last_y_{0};
};

}  // namespace opentag::hardware::display
