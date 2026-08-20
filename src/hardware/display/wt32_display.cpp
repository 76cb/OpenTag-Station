#include "hardware/display/wt32_display.hpp"

#include <algorithm>

namespace opentag::hardware::display {

using Board = boards::Wt32Sc01PlusRevA;

Wt32DisplayDevice::Wt32DisplayDevice() {
  {
    auto cfg = bus_.config();
    cfg.freq_write = Board::lcd_write_frequency_hz;
    cfg.pin_wr = Board::lcd_write;
    cfg.pin_rd = Board::lcd_read;
    cfg.pin_rs = Board::lcd_command;
    cfg.pin_d0 = Board::lcd_data[0];
    cfg.pin_d1 = Board::lcd_data[1];
    cfg.pin_d2 = Board::lcd_data[2];
    cfg.pin_d3 = Board::lcd_data[3];
    cfg.pin_d4 = Board::lcd_data[4];
    cfg.pin_d5 = Board::lcd_data[5];
    cfg.pin_d6 = Board::lcd_data[6];
    cfg.pin_d7 = Board::lcd_data[7];
    bus_.config(cfg);
    panel_.setBus(&bus_);
  }

  {
    auto cfg = panel_.config();
    cfg.pin_cs = Board::lcd_chip_select;
    cfg.pin_rst = Board::lcd_reset;
    cfg.pin_busy = -1;
    cfg.panel_width = Board::display_native_width;
    cfg.panel_height = Board::display_native_height;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.offset_rotation = 0;
    cfg.dummy_read_pixel = 8;
    cfg.dummy_read_bits = 1;
    cfg.readable = false;
    cfg.invert = true;
    cfg.rgb_order = false;
    cfg.dlen_16bit = false;
    cfg.bus_shared = false;
    panel_.config(cfg);
  }

  {
    auto cfg = light_.config();
    cfg.pin_bl = Board::lcd_backlight;
    cfg.invert = false;
    cfg.freq = Board::lcd_backlight_frequency_hz;
    cfg.pwm_channel = Board::lcd_backlight_pwm_channel;
    light_.config(cfg);
    panel_.setLight(&light_);
  }

  {
    auto cfg = touch_.config();
    cfg.x_min = 0;
    cfg.x_max = Board::display_native_width - 1;
    cfg.y_min = 0;
    cfg.y_max = Board::display_native_height - 1;
    cfg.pin_int = Board::touch_interrupt;
    cfg.bus_shared = true;
    cfg.offset_rotation = 0;
    cfg.i2c_port = 1;
    cfg.i2c_addr = Board::touch_address;
    cfg.pin_sda = Board::touch_sda;
    cfg.pin_scl = Board::touch_scl;
    cfg.freq = Board::touch_frequency_hz;
    touch_.config(cfg);
    panel_.setTouch(&touch_);
  }

  setPanel(&panel_);
}

bool Wt32Display::initialize() {
  initialized_ = device_.init();
  if (!initialized_) {
    return false;
  }
  device_.setRotation(Board::display_rotation);
  device_.setColorDepth(16);
  device_.setBrightness(static_cast<std::uint8_t>(brightness_percent_ * 255U / 100U));
  device_.fillScreen(TFT_BLACK);
  sleeping_ = false;
  initialized_ = device_.width() == Board::display_width &&
                 device_.height() == Board::display_height;
  return initialized_;
}

void Wt32Display::set_brightness(std::uint8_t percent) {
  brightness_percent_ = std::min<std::uint8_t>(percent, 100U);
  if (initialized_ && !sleeping_) {
    device_.setBrightness(static_cast<std::uint8_t>(brightness_percent_ * 255U / 100U));
  }
}

void Wt32Display::sleep() {
  if (initialized_ && !sleeping_) {
    device_.sleep();
    sleeping_ = true;
  }
}

void Wt32Display::wake() {
  if (initialized_ && sleeping_) {
    device_.wakeup();
    sleeping_ = false;
    set_brightness(brightness_percent_);
  }
}

void Wt32Display::push_pixels(
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height,
    const std::uint16_t* pixels) {
  if (!initialized_ || sleeping_ || pixels == nullptr) {
    return;
  }
  device_.startWrite();
  device_.pushImage(x, y, width, height, pixels);
  device_.endWrite();
}

TouchPoint Wt32Display::read_touch(std::uint32_t now_ms) {
  if (!initialized_) {
    return {};
  }

  std::int32_t x = last_x_;
  std::int32_t y = last_y_;
  const bool raw_pressed = device_.getTouch(&x, &y) != 0U;
  const bool in_range = x >= 0 && y >= 0 &&
                        x < Board::display_width && y < Board::display_height;
  const bool candidate = raw_pressed && in_range;

  if (candidate != touch_candidate_) {
    touch_candidate_ = candidate;
    touch_candidate_since_ms_ = now_ms;
  } else if (touch_stable_ != touch_candidate_ &&
             static_cast<std::uint32_t>(now_ms - touch_candidate_since_ms_) >=
                 touch_debounce_ms) {
    touch_stable_ = touch_candidate_;
  }

  if (candidate) {
    last_x_ = x;
    last_y_ = y;
  }
  return {last_x_, last_y_, touch_stable_};
}

}  // namespace opentag::hardware::display
