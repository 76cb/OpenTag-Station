#include "ui/ui_service.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"

namespace opentag::ui {
namespace {

static_assert(LV_COLOR_DEPTH == 16, "WT32 display requires RGB565");
static_assert(
    LV_COLOR_16_SWAP == 0,
    "LovyanGFX owns the LVGL RGB565 byte swap at the display boundary");

constexpr std::uint32_t screen_background = 0x111827;
constexpr std::uint32_t primary_text = 0xF8FAFC;
constexpr std::uint32_t secondary_text = 0xCBD5E1;
constexpr std::uint32_t accent_text = 0x93C5FD;
constexpr std::uint32_t warning_text = 0xFDE68A;

void style_screen(lv_obj_t* screen) {
  lv_obj_set_style_bg_color(screen, lv_color_hex(screen_background), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(primary_text), 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void style_title(lv_obj_t* title) {
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(accent_text), 0);
}

void format_milli(char* output, std::size_t size, std::int32_t value) {
  const auto wide = static_cast<std::int64_t>(value);
  const auto absolute = wide < 0 ? -wide : wide;
  std::snprintf(
      output,
      size,
      "%s%lld.%03lld",
      wide < 0 ? "-" : "",
      static_cast<long long>(absolute / 1000),
      static_cast<long long>(absolute % 1000));
}

const char* availability_text(services::BackendAvailability availability) {
  switch (availability) {
    case services::BackendAvailability::online: return "ONLINE";
    case services::BackendAvailability::offline: return "OFFLINE";
    case services::BackendAvailability::unknown: return "WAITING";
  }
  return "WAITING";
}

static const char* replacement_buttons[] = {"Cancel", "Replace", ""};
static const char* override_buttons[] = {"Cancel", "ADVANCED", ""};
static const char* override_replacement_buttons[] = {
    "Cancel", "ADVANCED REPLACE", ""};

}  // namespace

using Board = boards::Wt32Sc01PlusRevA;

bool UiService::allocate_buffers() {
  buffer_pixels_ = static_cast<std::size_t>(Board::display_width) * primary_buffer_rows;
  const std::size_t bytes = buffer_pixels_ * sizeof(lv_color_t);
  buffer_one_ = static_cast<lv_color_t*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  buffer_two_ = static_cast<lv_color_t*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  buffers_in_psram_ = buffer_one_ != nullptr;

  if (buffer_one_ == nullptr) {
    if (buffer_two_ != nullptr) {
      heap_caps_free(buffer_two_);
      buffer_two_ = nullptr;
    }
    buffer_pixels_ = static_cast<std::size_t>(Board::display_width) * fallback_buffer_rows;
    buffer_one_ = static_cast<lv_color_t*>(heap_caps_malloc(
        buffer_pixels_ * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    buffers_in_psram_ = false;
  }

  // A single draw buffer is valid. Keep rendering if the optional second PSRAM
  // allocation failed instead of consuming another large internal block.
  return buffer_one_ != nullptr;
}

bool UiService::initialize() {
  if (!display_.initialized() || !allocate_buffers()) {
    return false;
  }

  lv_init();
  lv_disp_draw_buf_init(&draw_buffer_, buffer_one_, buffer_two_, buffer_pixels_);

  lv_disp_drv_init(&display_driver_);
  display_driver_.hor_res = Board::display_width;
  display_driver_.ver_res = Board::display_height;
  display_driver_.flush_cb = flush_callback;
  display_driver_.draw_buf = &draw_buffer_;
  display_driver_.user_data = this;
  auto* lv_display = lv_disp_drv_register(&display_driver_);
  auto* theme = lv_theme_default_init(
      lv_display,
      lv_color_hex(0x2563EB),
      lv_color_hex(0x475569),
      true,
      &lv_font_montserrat_16);
  lv_disp_set_theme(lv_display, theme);

  lv_indev_drv_init(&input_driver_);
  input_driver_.type = LV_INDEV_TYPE_POINTER;
  input_driver_.read_cb = touch_callback;
  input_driver_.user_data = this;
  lv_indev_drv_register(&input_driver_);

  const auto configured = configuration_.snapshot();
  normal_brightness_percent_ = configured.device.brightness_percent;
  display_.set_brightness(normal_brightness_percent_);
  dim_after_ms_ = configured.device.dim_after_ms;
  sleep_after_ms_ = configured.device.sleep_after_ms;
  showing_setup_ = !first_run_setup_.complete();
  build_current_screen();
  const auto now_ms = millis();
  last_tick_ms_ = now_ms;
  last_refresh_ms_ = now_ms - refresh_interval_ms;
  last_interaction_ms_ = now_ms;
  initialized_ = true;
  diagnostics_.set_ui_task_running(true);
  refresh_current(now_ms);
  return true;
}

void UiService::flush_callback(
    lv_disp_drv_t* driver,
    const lv_area_t* area,
    lv_color_t* colors) {
  auto* self = static_cast<UiService*>(driver->user_data);
  const auto width = area->x2 - area->x1 + 1;
  const auto height = area->y2 - area->y1 + 1;
  self->display_.push_pixels(
      area->x1,
      area->y1,
      width,
      height,
      reinterpret_cast<const std::uint16_t*>(colors));
  lv_disp_flush_ready(driver);
}

void UiService::touch_callback(lv_indev_drv_t* driver, lv_indev_data_t* data) {
  auto* self = static_cast<UiService*>(driver->user_data);
  const auto now_ms = millis();
  const auto point = self->display_.read_touch(now_ms);
  if (point.pressed) {
    if (self->display_.sleeping()) {
      self->display_.wake();
      self->dimmed_ = false;
      self->note_interaction(now_ms);
      lv_obj_invalidate(lv_scr_act());
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    self->note_interaction(now_ms);
    self->update_display_self_test_touch(point);
    data->point.x = static_cast<lv_coord_t>(point.x);
    data->point.y = static_cast<lv_coord_t>(point.y);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void UiService::brightness_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  const auto value = lv_slider_get_value(self->brightness_slider_);
  self->normal_brightness_percent_ = static_cast<std::uint8_t>(value);
  self->display_.set_brightness(self->normal_brightness_percent_);
  self->dimmed_ = false;
  self->note_interaction(millis());
}

void UiService::sleep_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->display_.sleep();
}

void UiService::build_current_screen() {
  auto* screen = lv_scr_act();
  lv_obj_clean(screen);
  system_label_ = nullptr;
  memory_label_ = nullptr;
  storage_label_ = nullptr;
  touch_label_ = nullptr;
  brightness_slider_ = nullptr;
  setup_progress_label_ = nullptr;
  setup_body_label_ = nullptr;
  setup_status_label_ = nullptr;
  setup_input_one_ = nullptr;
  setup_input_two_ = nullptr;
  setup_network_dropdown_ = nullptr;
  setup_keyboard_ = nullptr;
  workflow_material_label_ = nullptr;
  workflow_weight_label_ = nullptr;
  workflow_identity_label_ = nullptr;
  workflow_status_label_ = nullptr;
  display_test_touch_marker_ = nullptr;
  display_test_touch_label_ = nullptr;
  workflow_toolhead_buttons_.fill(nullptr);
  if (showing_display_self_test_) {
    build_display_self_test_screen();
  } else if (showing_setup_) {
    build_setup_screen();
    refresh_setup();
  } else if (showing_diagnostics_) {
    build_diagnostics_screen();
  } else {
    build_workflow_screen();
    refresh_workflow();
  }
}

void UiService::build_display_self_test_screen() {
  auto* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_text_color(screen, lv_color_white(), 0);
  lv_obj_set_style_border_color(screen, lv_color_white(), 0);
  lv_obj_set_style_border_width(screen, 3, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  const auto make_label = [screen](
                              const char* text,
                              lv_align_t alignment,
                              std::int16_t x,
                              std::int16_t y) {
    auto* label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, alignment, x, y);
    return label;
  };

  auto* title = make_label(
      "DISPLAY SELF-TEST", LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  make_label("TOP", LV_ALIGN_TOP_LEFT, 7, 7);
  make_label("BOTTOM", LV_ALIGN_BOTTOM_LEFT, 7, -7);
  make_label("LEFT", LV_ALIGN_LEFT_MID, 7, 34);
  make_label("RIGHT", LV_ALIGN_RIGHT_MID, -7, 34);

  static constexpr std::array<const char*, 8> names{
      "RED", "GREEN", "BLUE", "WHITE",
      "BLACK", "YELLOW", "CYAN", "MAGENTA"};
  static constexpr std::array<std::uint32_t, 8> colors{
      0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF,
      0x000000, 0xFFFF00, 0x00FFFF, 0xFF00FF};
  static constexpr std::array<bool, 8> dark_labels{
      false, true, false, true, false, true, true, false};
  for (std::size_t index = 0; index < colors.size(); ++index) {
    auto* swatch = lv_obj_create(screen);
    lv_obj_set_size(swatch, 106, 48);
    lv_obj_set_pos(
        swatch,
        17 + static_cast<lv_coord_t>(index % 4U) * 112,
        38 + static_cast<lv_coord_t>(index / 4U) * 54);
    lv_obj_set_style_radius(swatch, 0, 0);
    lv_obj_set_style_pad_all(swatch, 0, 0);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(colors[index]), 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(swatch, lv_color_white(), 0);
    lv_obj_set_style_border_width(swatch, 1, 0);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
    auto* label = lv_label_create(swatch);
    lv_label_set_text(label, names[index]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(
        label,
        dark_labels[index] ? lv_color_black() : lv_color_white(),
        0);
    lv_obj_center(label);
  }

  static constexpr std::array<std::uint32_t, 8> grayscale{
      0x000000, 0x242424, 0x494949, 0x6D6D6D,
      0x929292, 0xB6B6B6, 0xDBDBDB, 0xFFFFFF};
  for (std::size_t index = 0; index < grayscale.size(); ++index) {
    auto* segment = lv_obj_create(screen);
    lv_obj_set_size(segment, 55, 26);
    lv_obj_set_pos(
        segment, 20 + static_cast<lv_coord_t>(index) * 55, 150);
    lv_obj_set_style_radius(segment, 0, 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_set_style_bg_color(segment, lv_color_hex(grayscale[index]), 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
  }

  auto* horizontal = lv_obj_create(screen);
  lv_obj_set_size(horizontal, 42, 2);
  lv_obj_set_pos(horizontal, 219, 159);
  lv_obj_set_style_border_width(horizontal, 0, 0);
  lv_obj_set_style_bg_color(horizontal, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(horizontal, LV_OPA_COVER, 0);
  auto* vertical = lv_obj_create(screen);
  lv_obj_set_size(vertical, 2, 42);
  lv_obj_set_pos(vertical, 239, 139);
  lv_obj_set_style_border_width(vertical, 0, 0);
  lv_obj_set_style_bg_color(vertical, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(vertical, LV_OPA_COVER, 0);
  make_label("CENTER 240,160", LV_ALIGN_TOP_MID, 0, 181);

  display_test_touch_marker_ = lv_obj_create(screen);
  lv_obj_set_size(display_test_touch_marker_, 20, 20);
  lv_obj_set_style_radius(display_test_touch_marker_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(display_test_touch_marker_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(
      display_test_touch_marker_, lv_color_hex(0xF97316), 0);
  lv_obj_set_style_border_width(display_test_touch_marker_, 3, 0);
  lv_obj_add_flag(display_test_touch_marker_, LV_OBJ_FLAG_HIDDEN);

  display_test_touch_label_ = make_label(
      "Touch panel to plot a point", LV_ALIGN_BOTTOM_MID, 0, -7);
  lv_obj_set_style_text_color(
      display_test_touch_label_, lv_color_hex(0xFDE68A), 0);
}

void UiService::update_display_self_test_touch(
    const hardware::display::TouchPoint& point) {
  if (!showing_display_self_test_ ||
      display_test_touch_marker_ == nullptr ||
      display_test_touch_label_ == nullptr ||
      !point.pressed) {
    return;
  }
  const auto x = std::clamp<std::int32_t>(
      point.x - 10, 3, Board::display_width - 23);
  const auto y = std::clamp<std::int32_t>(
      point.y - 10, 3, Board::display_height - 23);
  lv_obj_set_pos(display_test_touch_marker_, x, y);
  lv_obj_clear_flag(display_test_touch_marker_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_fmt(
      display_test_touch_label_, "TOUCH  x=%ld  y=%ld",
      static_cast<long>(point.x), static_cast<long>(point.y));
}

void UiService::build_workflow_screen() {
  auto* screen = lv_scr_act();
  style_screen(screen);

  auto* title = lv_label_create(screen);
  lv_label_set_text(title, "OpenTag Station");
  style_title(title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 8);

  const auto make_nav = [this, screen](
                            const char* text,
                            std::int16_t x,
                            lv_event_cb_t callback) {
    auto* button = lv_btn_create(screen);
    lv_obj_set_size(button, 94, 40);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, x, 4);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
  };
  make_nav("Setup", -10, setup_toggle_callback);
  make_nav("Details", -110, diagnostics_toggle_callback);

  workflow_material_label_ = lv_label_create(screen);
  lv_obj_set_width(workflow_material_label_, 280);
  lv_obj_set_style_text_font(
      workflow_material_label_, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(
      workflow_material_label_, lv_color_hex(primary_text), 0);
  lv_obj_align(workflow_material_label_, LV_ALIGN_TOP_LEFT, 14, 56);

  workflow_weight_label_ = lv_label_create(screen);
  lv_obj_set_width(workflow_weight_label_, 158);
  lv_obj_set_style_text_font(
      workflow_weight_label_, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(
      workflow_weight_label_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(
      workflow_weight_label_, lv_color_hex(accent_text), 0);
  lv_obj_align(workflow_weight_label_, LV_ALIGN_TOP_RIGHT, -14, 56);

  workflow_identity_label_ = lv_label_create(screen);
  lv_obj_set_width(workflow_identity_label_, 452);
  lv_obj_set_style_text_color(
      workflow_identity_label_, lv_color_hex(secondary_text), 0);
  lv_obj_align(workflow_identity_label_, LV_ALIGN_TOP_LEFT, 14, 108);

  static constexpr std::array<std::int16_t, 5> x_positions{
      12, 104, 196, 288, 380};
  for (std::size_t index = 0U; index < workflow_toolhead_buttons_.size(); ++index) {
    auto* button = lv_btn_create(screen);
    workflow_toolhead_buttons_[index] = button;
    lv_obj_set_size(button, 88, 62);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x_positions[index], 142);
    lv_obj_add_event_cb(button, toolhead_callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    const auto normalized = domain::Toolhead::from_zero_based_backend(
        {}, static_cast<int>(index));
    lv_label_set_text_fmt(label, "%s\n--", normalized.display_name.c_str());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
  }

  workflow_status_label_ = lv_label_create(screen);
  lv_obj_set_width(workflow_status_label_, 456);
  lv_obj_set_style_text_color(
      workflow_status_label_, lv_color_hex(warning_text), 0);
  lv_obj_align(workflow_status_label_, LV_ALIGN_TOP_LEFT, 12, 220);
}

void UiService::build_diagnostics_screen() {
  auto* screen = lv_scr_act();
  style_screen(screen);

  auto* title = lv_label_create(screen);
  lv_label_set_text(title, "Hardware diagnostics");
  style_title(title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 9);

  auto* version = lv_label_create(screen);
  lv_label_set_text_fmt(
      version,
      "v%s  %s",
      diagnostics::build_info.project_version,
      diagnostics::build_info.git_sha);
  lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
  lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -14, 12);

  system_label_ = lv_label_create(screen);
  lv_obj_set_style_text_font(system_label_, &lv_font_montserrat_14, 0);
  lv_obj_set_width(system_label_, 220);
  lv_obj_align(system_label_, LV_ALIGN_TOP_LEFT, 18, 55);

  memory_label_ = lv_label_create(screen);
  lv_obj_set_style_text_font(memory_label_, &lv_font_montserrat_14, 0);
  lv_obj_set_width(memory_label_, 220);
  lv_obj_align(memory_label_, LV_ALIGN_TOP_RIGHT, -18, 55);

  storage_label_ = lv_label_create(screen);
  lv_obj_set_style_text_font(storage_label_, &lv_font_montserrat_14, 0);
  lv_obj_set_width(storage_label_, 220);
  lv_obj_align(storage_label_, LV_ALIGN_TOP_LEFT, 18, 148);

  touch_label_ = lv_label_create(screen);
  lv_obj_set_style_text_font(touch_label_, &lv_font_montserrat_14, 0);
  lv_obj_set_width(touch_label_, 220);
  lv_obj_align(touch_label_, LV_ALIGN_TOP_RIGHT, -18, 148);

  auto* brightness_label = lv_label_create(screen);
  lv_label_set_text(brightness_label, "Brightness");
  lv_obj_align(brightness_label, LV_ALIGN_BOTTOM_LEFT, 18, -31);

  brightness_slider_ = lv_slider_create(screen);
  lv_obj_set_size(brightness_slider_, 240, 18);
  lv_slider_set_range(brightness_slider_, 5, 100);
  lv_slider_set_value(brightness_slider_, display_.brightness(), LV_ANIM_OFF);
  lv_obj_align(brightness_slider_, LV_ALIGN_BOTTOM_LEFT, 105, -30);
  lv_obj_add_event_cb(
      brightness_slider_, brightness_callback, LV_EVENT_VALUE_CHANGED, this);

  auto* sleep_button = lv_btn_create(screen);
  lv_obj_set_size(sleep_button, 92, 38);
  lv_obj_align(sleep_button, LV_ALIGN_BOTTOM_RIGHT, -18, -16);
  lv_obj_add_event_cb(sleep_button, sleep_callback, LV_EVENT_CLICKED, this);
  auto* sleep_label = lv_label_create(sleep_button);
  lv_label_set_text(sleep_label, "Sleep");
  lv_obj_center(sleep_label);

  auto* setup_button = lv_btn_create(screen);
  lv_obj_set_size(setup_button, 92, 38);
  lv_obj_align(setup_button, LV_ALIGN_BOTTOM_RIGHT, -120, -16);
  lv_obj_add_event_cb(
      setup_button, setup_toggle_callback, LV_EVENT_CLICKED, this);
  auto* setup_label = lv_label_create(setup_button);
  lv_label_set_text(setup_label, "Setup");
  lv_obj_center(setup_label);

  auto* main_button = lv_btn_create(screen);
  lv_obj_set_size(main_button, 92, 38);
  lv_obj_align(main_button, LV_ALIGN_BOTTOM_RIGHT, -222, -16);
  lv_obj_add_event_cb(
      main_button, diagnostics_toggle_callback, LV_EVENT_CLICKED, this);
  auto* main_label = lv_label_create(main_button);
  lv_label_set_text(main_label, "Main");
  lv_obj_center(main_label);
}

lv_obj_t* UiService::create_setup_textarea(
    std::int16_t y,
    const char* placeholder,
    const std::string& value,
    std::size_t maximum_length,
    bool password) {
  auto* input = lv_textarea_create(lv_scr_act());
  lv_obj_set_size(input, 286, 44);
  lv_obj_align(input, LV_ALIGN_TOP_LEFT, 16, y);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_placeholder_text(input, placeholder);
  lv_textarea_set_max_length(input, maximum_length);
  lv_textarea_set_password_mode(input, password);
  lv_textarea_set_text(input, value.c_str());
  lv_obj_add_event_cb(input, setup_textarea_callback, LV_EVENT_FOCUSED, this);
  return input;
}

void UiService::build_setup_screen() {
  auto* screen = lv_scr_act();
  style_screen(screen);

  auto* title = lv_label_create(screen);
  lv_label_set_text(title, "First-run setup");
  style_title(title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 8);

  setup_progress_label_ = lv_label_create(screen);
  lv_obj_align(setup_progress_label_, LV_ALIGN_TOP_RIGHT, -14, 10);
  setup_body_label_ = lv_label_create(screen);
  lv_obj_set_width(setup_body_label_, 452);
  lv_obj_align(setup_body_label_, LV_ALIGN_TOP_LEFT, 14, 44);
  setup_status_label_ = lv_label_create(screen);
  lv_obj_set_width(setup_status_label_, 452);
  lv_obj_align(setup_status_label_, LV_ALIGN_TOP_LEFT, 14, 202);
  lv_obj_set_style_text_color(
      setup_status_label_, lv_color_hex(warning_text), 0);

  const auto configured = configuration_.snapshot();
  const auto step = first_run_setup_.current();
  if (step == services::SetupStep::wifi) {
    lv_obj_set_width(setup_status_label_, 286);
  }
  if (step == services::SetupStep::wifi) {
    setup_network_dropdown_ = lv_dropdown_create(screen);
    lv_obj_set_size(setup_network_dropdown_, 156, 44);
    lv_obj_align(setup_network_dropdown_, LV_ALIGN_TOP_RIGHT, -14, 94);
    const auto networks = network_.scan_results();
    setup_scan_generation_ =
        diagnostics_.snapshot(millis()).wifi_scan_generation;
    std::string options = networks.empty() ? "Scan results" : "";
    for (const auto& network : networks) {
      if (!options.empty()) options += '\n';
      options += network.ssid;
    }
    lv_dropdown_set_options(setup_network_dropdown_, options.c_str());
    lv_obj_add_event_cb(
        setup_network_dropdown_, setup_network_callback, LV_EVENT_VALUE_CHANGED, this);
    setup_input_one_ = create_setup_textarea(
        94, "Wi-Fi network name", configured.wifi.ssid, 32U, false);
    setup_input_two_ = create_setup_textarea(
        146, "Wi-Fi password", configured.wifi.password, 64U, true);
  } else if (step == services::SetupStep::spoolman) {
    setup_input_one_ = create_setup_textarea(
        94, "Spoolman URL", configured.spoolman.url, 256U, false);
    setup_input_two_ = create_setup_textarea(
        146,
        "Authentication token (optional)",
        configured.spoolman.authentication_token,
        512U,
        true);
  } else if (step == services::SetupStep::filabridge) {
    setup_input_one_ = create_setup_textarea(
        94, "FilaBridge URL", configured.filabridge.url, 256U, false);
    setup_input_two_ = create_setup_textarea(
        146,
        "Authentication token (optional)",
        configured.filabridge.authentication_token,
        512U,
        true);
  } else if (step == services::SetupStep::printer_selection) {
    setup_input_one_ = create_setup_textarea(
        94,
        "Stable printer ID",
        configured.filabridge.selected_printer_id,
        128U,
        false);
  } else if (step == services::SetupStep::ready) {
    setup_input_one_ = create_setup_textarea(
        94,
        "Local API token (16-128 characters)",
        configured.web.access_token,
        128U,
        true);
    lv_textarea_set_accepted_chars(
        setup_input_one_,
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~");
  }

  const auto make_button = [this, screen](
                               const char* text,
                               std::int16_t x,
                               lv_event_cb_t callback) {
    auto* button = lv_btn_create(screen);
    lv_obj_set_size(button, 110, 46);
    lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, x, -10);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
  };
  make_button("Back", 14, setup_back_callback);
  make_button("Main", 185, setup_toggle_callback);
  make_button(
      step == services::SetupStep::ready ? "Finish" : "Next",
      356,
      setup_next_callback);
  if (setup_input_one_ != nullptr) {
    auto* save_button = lv_btn_create(screen);
    lv_obj_set_size(save_button, 156, 42);
    lv_obj_align(
        save_button,
        LV_ALIGN_TOP_RIGHT,
        -14,
        step == services::SetupStep::wifi ? 198 : 94);
    lv_obj_add_event_cb(
        save_button, setup_save_callback, LV_EVENT_CLICKED, this);
    auto* save_label = lv_label_create(save_button);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
  }
  if (step == services::SetupStep::wifi) {
    auto* scan_button = lv_btn_create(screen);
    lv_obj_set_size(scan_button, 156, 42);
    lv_obj_align(scan_button, LV_ALIGN_TOP_RIGHT, -14, 146);
    lv_obj_add_event_cb(
        scan_button, setup_scan_callback, LV_EVENT_CLICKED, this);
    auto* scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);
  }

  setup_keyboard_ = lv_keyboard_create(screen);
  lv_obj_set_size(setup_keyboard_, 480, 180);
  lv_obj_align(setup_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(setup_keyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(setup_keyboard_, setup_keyboard_callback, LV_EVENT_ALL, this);
}

void UiService::setup_back_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->setup_feedback_.clear();
  const auto result = self->first_run_setup_.previous();
  if (result.ok()) self->build_current_screen();
}

void UiService::setup_next_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  const auto step = self->first_run_setup_.current();
  const auto configured = self->configuration_.snapshot();
  const bool complete =
      step == services::SetupStep::welcome ||
      (step == services::SetupStep::wifi && !configured.wifi.ssid.empty()) ||
      (step == services::SetupStep::spoolman && !configured.spoolman.url.empty()) ||
      (step == services::SetupStep::filabridge && !configured.filabridge.url.empty()) ||
      (step == services::SetupStep::printer_selection &&
       !configured.filabridge.selected_printer_id.empty()) ||
      (step == services::SetupStep::scale_calibration &&
       configured.scale_calibration.has_value()) ||
      step == services::SetupStep::nfc_status ||
      step == services::SetupStep::ready;
  if (complete) {
    if (!self->configuration_worker_.submit_setup_completion(step)) {
      self->setup_feedback_ = "Configuration queue is unavailable";
    }
  }
  if (step == services::SetupStep::ready) {
    self->showing_setup_ = false;
    self->showing_diagnostics_ = false;
    self->build_current_screen();
    self->refresh_current(millis());
    return;
  }
  const auto advanced = self->first_run_setup_.next();
  if (advanced.ok()) {
    self->setup_feedback_.clear();
    self->build_current_screen();
  }
}

void UiService::setup_toggle_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->showing_setup_ = !self->showing_setup_;
  self->showing_diagnostics_ = false;
  self->setup_feedback_.clear();
  self->build_current_screen();
  if (!self->showing_setup_) self->refresh_current(millis());
}

void UiService::diagnostics_toggle_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->showing_setup_ = false;
  self->showing_diagnostics_ = !self->showing_diagnostics_;
  self->build_current_screen();
  self->refresh_current(millis());
}

void UiService::toolhead_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  const auto selected = std::find(
      self->workflow_toolhead_buttons_.begin(),
      self->workflow_toolhead_buttons_.end(),
      target);
  if (selected == self->workflow_toolhead_buttons_.end()) return;
  const auto backend_id = static_cast<int>(
      std::distance(self->workflow_toolhead_buttons_.begin(), selected));
  const auto configured = self->configuration_.snapshot();
  const auto state = self->workflow_.snapshot();
  const auto printer = std::find_if(
      state.printers.begin(), state.printers.end(), [&](const auto& candidate) {
        return candidate.id == configured.filabridge.selected_printer_id;
      });
  if (!state.spool.has_value() || printer == state.printers.end()) {
    self->workflow_feedback_ = "Assignment unavailable";
    self->refresh_workflow();
    return;
  }
  const auto toolhead = std::find_if(
      printer->toolheads.begin(), printer->toolheads.end(),
      [&](const auto& candidate) { return candidate.backend_id == backend_id; });
  if (toolhead == printer->toolheads.end()) return;
  const bool occupied = toolhead->assigned_spool.has_value() &&
      *toolhead->assigned_spool != state.spool->id;
  const bool active = domain::is_active_print_state(printer->state);
  const bool state_unverified =
      printer->state == domain::PrinterState::unknown ||
      printer->state == domain::PrinterState::offline ||
      printer->state == domain::PrinterState::not_configured;
  if (!occupied && !active && !state_unverified) {
    services::ToolheadMutationPrecondition precondition;
    precondition.supplied = true;
    precondition.expected_previous_spool_id = toolhead->assigned_spool;
    precondition.expected_printer_state = printer->state;
    const auto receipt = self->backend_worker_.submit_assignment_operation(
        printer->id,
        backend_id,
        false,
        false,
        state.spool_generation,
        state.spool->id,
        std::move(precondition),
        state.printer_revision);
    self->workflow_feedback_ =
        receipt.accepted ? "Assignment queued for verification"
                         : "Assignment rejected or backend queue is full";
    self->refresh_workflow();
    return;
  }

  self->pending_printer_id_ = printer->id;
  self->pending_backend_toolhead_id_ = backend_id;
  self->pending_replace_confirmation_ = occupied;
  self->pending_active_override_ = active || state_unverified;
  self->pending_spool_generation_ = state.spool_generation;
  self->pending_spool_id_ = state.spool->id;
  self->pending_printer_revision_ = state.printer_revision;
  self->pending_previous_spool_id_ = toolhead->assigned_spool;
  self->pending_printer_state_ = printer->state;

  const char* title = active
                          ? "ACTIVE PRINT"
                          : state_unverified ? "PRINTER STATE UNVERIFIED"
                                             : "TOOLHEAD OCCUPIED";
  std::string message;
  const char** buttons = replacement_buttons;
  if ((active || state_unverified) && occupied) {
    message = active ? "This printer is actively printing and T"
                     : "This printer state cannot be verified and T";
    message +=
        toolhead->display_name + " contains spool #" +
        std::to_string(*toolhead->assigned_spool) +
        ". Replacing it may corrupt consumption accounting.";
    buttons = override_replacement_buttons;
  } else if (active || state_unverified) {
    message = active
                  ? "This printer is actively printing. Mapping T"
                  : "This printer state cannot be verified. Mapping T";
    message +=
        toolhead->display_name +
        " may corrupt consumption accounting.";
    buttons = override_buttons;
  } else {
    message = toolhead->display_name + " currently contains spool #" +
        std::to_string(*toolhead->assigned_spool) +
        ". Replace it with spool #" + std::to_string(state.spool->id) + "?";
  }
  auto* message_box = lv_msgbox_create(
      nullptr, title, message.c_str(), buttons, false);
  lv_obj_add_event_cb(
      message_box,
      assignment_confirmation_callback,
      LV_EVENT_VALUE_CHANGED,
      self);
  lv_obj_center(message_box);
}

void UiService::assignment_confirmation_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  auto* message_box = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  const char* action = lv_msgbox_get_active_btn_text(message_box);
  if (action != nullptr && std::strcmp(action, "Cancel") != 0) {
    services::ToolheadMutationPrecondition precondition;
    precondition.supplied = true;
    precondition.expected_previous_spool_id =
        self->pending_previous_spool_id_;
    precondition.expected_printer_state = self->pending_printer_state_;
    const auto receipt = self->backend_worker_.submit_assignment_operation(
        self->pending_printer_id_,
        self->pending_backend_toolhead_id_,
        self->pending_replace_confirmation_,
        self->pending_active_override_,
        self->pending_spool_generation_,
        self->pending_spool_id_,
        std::move(precondition),
        self->pending_printer_revision_);
    self->workflow_feedback_ =
        receipt.accepted ? "Safety override queued for verification"
                         : "Assignment rejected or backend queue is full";
  }
  self->pending_printer_id_.clear();
  self->pending_backend_toolhead_id_ = -1;
  self->pending_replace_confirmation_ = false;
  self->pending_active_override_ = false;
  self->pending_spool_generation_.reset();
  self->pending_spool_id_.reset();
  self->pending_printer_revision_.reset();
  self->pending_previous_spool_id_.reset();
  self->pending_printer_state_ = domain::PrinterState::unknown;
  lv_msgbox_close(message_box);
  self->refresh_workflow();
}

void UiService::setup_save_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  const auto versioned = self->configuration_.versioned_snapshot();
  auto updated = versioned.configuration;
  const auto step = self->first_run_setup_.current();
  const std::string first = self->setup_input_one_ != nullptr
                                ? lv_textarea_get_text(self->setup_input_one_)
                                : "";
  const std::string second = self->setup_input_two_ != nullptr
                                 ? lv_textarea_get_text(self->setup_input_two_)
                                 : "";
  if (step == services::SetupStep::wifi) {
    updated.wifi.ssid = first;
    updated.wifi.password = second;
  } else if (step == services::SetupStep::spoolman) {
    updated.spoolman.url = first;
    updated.spoolman.authentication_token = second;
  } else if (step == services::SetupStep::filabridge) {
    updated.filabridge.url = first;
    updated.filabridge.authentication_token = second;
  } else if (step == services::SetupStep::printer_selection) {
    updated.filabridge.selected_printer_id = first;
  } else if (step == services::SetupStep::ready) {
    updated.web.access_token = first;
  }
  updated.setup.completed_steps |=
      1U << static_cast<std::uint8_t>(step);
  const auto receipt = self->configuration_worker_.submit_replace(
      updated, versioned.revision, millis());
  if (!receipt.accepted) {
    self->setup_feedback_ = "Configuration queue is unavailable";
  } else {
    self->setup_feedback_ = "Save queued";
  }
  self->refresh_setup();
}

void UiService::setup_scan_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->network_.request_scan();
  self->setup_feedback_ = "Scanning asynchronously...";
  self->refresh_setup();
}

void UiService::setup_network_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  if (self->setup_network_dropdown_ == nullptr ||
      self->setup_input_one_ == nullptr) {
    return;
  }
  char selected[40]{};
  lv_dropdown_get_selected_str(
      self->setup_network_dropdown_, selected, sizeof(selected));
  if (std::string(selected) != "Scan results") {
    lv_textarea_set_text(self->setup_input_one_, selected);
  }
}

void UiService::setup_textarea_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  if (self->setup_keyboard_ == nullptr) return;
  lv_keyboard_set_textarea(
      self->setup_keyboard_, static_cast<lv_obj_t*>(lv_event_get_target(event)));
  lv_obj_clear_flag(self->setup_keyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(self->setup_keyboard_);
}

void UiService::setup_keyboard_callback(lv_event_t* event) {
  const auto code = lv_event_get_code(event);
  if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  lv_keyboard_set_textarea(self->setup_keyboard_, nullptr);
  lv_obj_add_flag(self->setup_keyboard_, LV_OBJ_FLAG_HIDDEN);
}

void UiService::refresh_setup() {
  if (!showing_setup_ || setup_progress_label_ == nullptr) return;
  if (setup_feedback_ == "Save queued" &&
      configuration_worker_.pending() == 0U) {
    setup_feedback_ = configuration_worker_.last_operation_succeeded()
                          ? "Saved"
                          : "Save failed; configuration was not changed";
  }
  const auto step = first_run_setup_.current();
  const auto configured = configuration_.snapshot();
  const auto network = diagnostics_.snapshot(millis());
  const auto step_number = static_cast<unsigned>(step) + 1U;
  lv_label_set_text_fmt(
      setup_progress_label_, "Step %u of 8", step_number);

  const char* body = "Review this step, save when applicable, or continue incomplete.";
  switch (step) {
    case services::SetupStep::welcome:
      body = "WELCOME\nConfigure each station service in order.";
      break;
    case services::SetupStep::wifi:
      body = "WI-FI\nChoose a scan result or enter the network manually.";
      break;
    case services::SetupStep::spoolman:
      body = "SPOOLMAN\nEnter and save the service base URL.";
      break;
    case services::SetupStep::filabridge:
      body = "FILABRIDGE\nEnter and save the service base URL.";
      break;
    case services::SetupStep::printer_selection:
      body = "PRINTER\nEnter the stable ID from FilaBridge discovery.";
      break;
    case services::SetupStep::scale_calibration:
      body = "SCALE CALIBRATION\nUse browser Scale controls: tare empty, then use a known mass.";
      break;
    case services::SetupStep::nfc_status:
      body = "NFC STATUS\nDisabled until the reader is physically integrated.";
      break;
    case services::SetupStep::ready:
      body = "READY\nSet and save a 16-128 character local API token.";
      break;
  }
  if (!network.wifi_configured && network.provisioning_active) {
    const std::string setup_body =
        "SETUP REQUIRED\nConnect a phone or computer to\n" +
        network.setup_ap_ssid + "\nthen open http://192.168.4.1/";
    lv_label_set_text(setup_body_label_, setup_body.c_str());
  } else {
    lv_label_set_text(setup_body_label_, body);
  }
  if (setup_network_dropdown_ != nullptr &&
      setup_scan_generation_ != network.wifi_scan_generation) {
    const auto networks = network_.scan_results();
    std::string options = networks.empty() ? "Scan results" : "";
    for (const auto& candidate : networks) {
      if (!options.empty()) options += '\n';
      options += candidate.ssid;
    }
    lv_dropdown_set_options(setup_network_dropdown_, options.c_str());
    setup_scan_generation_ = network.wifi_scan_generation;
  }
  std::string status;
  switch (step) {
    case services::SetupStep::wifi:
      status = std::string("State: ") + network::to_string(network.wifi_state) +
          "  scan results: " + std::to_string(network_.scan_results().size());
      if (network.wifi_connected) status += "  IP: " + network.ip_address;
      if (network.provisioning_active) {
        status += "\nAP: " + network.setup_ap_ssid +
            "  http://192.168.4.1/";
      }
      break;
    case services::SetupStep::spoolman:
      status = configured.spoolman.url.empty() ? "Not configured" : "URL saved";
      break;
    case services::SetupStep::filabridge:
      status = configured.filabridge.url.empty() ? "Not configured" : "URL saved";
      break;
    case services::SetupStep::printer_selection:
      status = configured.filabridge.selected_printer_id.empty()
                   ? "No printer selected"
                   : "Printer ID saved";
      break;
    case services::SetupStep::scale_calibration:
      status = configured.scale_calibration.has_value()
                   ? "Calibration loaded"
                   : "Calibration required";
      break;
    case services::SetupStep::nfc_status:
      status = "NFC disabled by wiring guard";
      break;
    case services::SetupStep::ready:
      status = configured.web.access_token.empty()
                   ? "Browser mutations disabled until a local API token is configured"
                   : "Local API token configured; browser mutations may be authorized";
      if (first_run_setup_.complete()) status += "\nSetup previously confirmed";
      break;
    case services::SetupStep::welcome:
      status = "Configuration schema " +
          std::to_string(configured.schema_version);
      break;
  }
  if (!setup_feedback_.empty()) status += "\n" + setup_feedback_;
  lv_label_set_text(setup_status_label_, status.c_str());
}

void UiService::refresh_current(std::uint32_t now_ms) {
  if (showing_display_self_test_) {
    return;
  }
  if (showing_setup_) {
    refresh_setup();
  } else if (showing_diagnostics_) {
    refresh_diagnostics(now_ms);
  } else {
    refresh_workflow();
  }
}

void UiService::refresh_workflow() {
  if (showing_setup_ || showing_diagnostics_ ||
      workflow_material_label_ == nullptr) {
    return;
  }
  const auto state = workflow_.snapshot();
  const auto configured = configuration_.snapshot();

  if (!state.openprinttag_available) {
    lv_label_set_text(workflow_material_label_, "PLACE A SPOOL\nWaiting for OpenPrintTag");
    lv_label_set_text(workflow_weight_label_, "-- g");
    lv_label_set_text(workflow_identity_label_, "OpenPrintTag --   Spoolman --");
  } else {
    const std::string material_name = state.material.material_name.value_or(
        state.material.material_abbreviation.value_or("OpenPrintTag material"));
    lv_label_set_text(workflow_material_label_, material_name.c_str());
    if (state.reconciliation.measured_remaining_grams.has_value()) {
      lv_label_set_text_fmt(
          workflow_weight_label_,
          "%.0f g\nremaining",
          *state.reconciliation.measured_remaining_grams);
    } else {
      lv_label_set_text_fmt(
          workflow_weight_label_,
          "%.0f g\nphysical",
          state.physical_weight.gross_grams);
    }
    if (state.spool.has_value()) {
      lv_label_set_text_fmt(
          workflow_identity_label_,
          "Spoolman #%ld     OpenPrintTag OK",
          static_cast<long>(state.spool->id));
    } else {
      lv_label_set_text(workflow_identity_label_, "Spoolman unresolved     OpenPrintTag OK");
    }
  }

  const auto printer = std::find_if(
      state.printers.begin(), state.printers.end(), [&](const auto& candidate) {
        return candidate.id == configured.filabridge.selected_printer_id;
      });
  for (std::size_t index = 0U; index < workflow_toolhead_buttons_.size(); ++index) {
    auto* button = workflow_toolhead_buttons_[index];
    if (button == nullptr) continue;
    std::optional<domain::SpoolId> mapped;
    bool present = false;
    auto display_name = domain::Toolhead::from_zero_based_backend(
        {}, static_cast<int>(index)).display_name;
    if (printer != state.printers.end()) {
      const auto toolhead = std::find_if(
          printer->toolheads.begin(), printer->toolheads.end(),
          [&](const auto& candidate) {
            return candidate.backend_id == static_cast<int>(index);
          });
      if (toolhead != printer->toolheads.end()) {
        present = true;
        mapped = toolhead->assigned_spool;
        display_name = toolhead->display_name;
      }
    }
    auto* label = lv_obj_get_child(button, 0);
    if (label != nullptr) {
      if (mapped.has_value()) {
        lv_label_set_text_fmt(
            label,
            "%s\n#%ld",
            display_name.c_str(),
            static_cast<long>(*mapped));
      } else {
        lv_label_set_text_fmt(
            label,
            "%s\n%s",
            display_name.c_str(),
            present ? "EMPTY" : "--");
      }
    }
    const auto profile = std::find_if(
        configured.toolheads.begin(), configured.toolheads.end(),
        [&](const auto& candidate) {
          return candidate.backend_id == static_cast<int>(index);
        });
    const bool profile_enabled =
        profile == configured.toolheads.end() || profile->enabled;
    const bool enabled = state.spool.has_value() && present &&
        state.filabridge == services::BackendAvailability::online &&
        state.filabridge_assignment_available &&
        profile_enabled && backend_worker_.pending() == 0U;
    if (enabled) {
      lv_obj_clear_state(button, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(button, LV_STATE_DISABLED);
    }
  }

  std::string status;
  switch (state.stage) {
    case services::WorkflowStage::awaiting_spool:
      status = "PLACE SPOOL";
      break;
    case services::WorkflowStage::waiting_for_stable_weight:
      status = "Stabilizing scale...";
      break;
    case services::WorkflowStage::resolving_spool:
      status = "Resolving in Spoolman...";
      break;
    case services::WorkflowStage::spool_resolution_unavailable:
      status = "SPOOLMAN OFFLINE\nDatabase operations unavailable";
      break;
    case services::WorkflowStage::spool_not_found:
      status = "NO MATCH\nCreate or link explicitly";
      break;
    case services::WorkflowStage::spool_selection_required:
      status = "MULTIPLE MATCHES\nManual selection required";
      break;
    case services::WorkflowStage::spool_ready:
      status = "Select T1-T5";
      break;
    case services::WorkflowStage::assignment_complete:
      status = "ASSIGNMENT VERIFIED";
      break;
  }
  if (state.filabridge == services::BackendAvailability::offline) {
    status = "FILABRIDGE OFFLINE\nAssignment unavailable";
  } else if (state.filabridge == services::BackendAvailability::online &&
             !state.filabridge_assignment_available) {
    status = "FILABRIDGE READ ONLY\nMapping capability unavailable";
  } else if (state.assignment_error.has_value()) {
    status = "ASSIGNMENT NOT VERIFIED\n" + state.assignment_error->message;
  } else if (state.last_assignment.has_value() &&
             !state.last_assignment->verified()) {
    switch (state.last_assignment->outcome) {
      case services::AssignmentOutcome::active_print_override_required:
        status = "ACTIVE PRINT\nAdvanced override required";
        break;
      case services::AssignmentOutcome::printer_state_override_required:
        status = "PRINTER STATE UNVERIFIED\nAdvanced override required";
        break;
      default:
        status = "TOOLHEAD OCCUPIED\nReplacement confirmation required";
        break;
    }
  } else if (!state.compatibility_advisories.empty()) {
    status += "\nADVISORY: " + state.compatibility_advisories.front().title;
  } else if (!workflow_feedback_.empty()) {
    status += "\n" + workflow_feedback_;
  }
  status += "\n\nSpoolman ";
  status += availability_text(state.spoolman);
  status += "\nFilaBridge ";
  status += availability_text(state.filabridge);
  const auto network_status = diagnostics_.snapshot(millis());
  if (network_status.wifi_connected && !network_status.ip_address.empty()) {
    status += "\nIP " + network_status.ip_address;
  }
  lv_label_set_text(workflow_status_label_, status.c_str());
}

void UiService::refresh_diagnostics(std::uint32_t now_ms) {
  if (showing_setup_) {
    refresh_setup();
    return;
  }
  const auto status = diagnostics_.snapshot(now_ms);
  char weight[24] = "not calibrated";
  if (status.scale_weight_available) {
    format_milli(weight, sizeof(weight), status.scale_gross_milligrams);
  }
  char factor[24] = "n/a";
  if (status.scale_calibrated) {
    format_milli(
        factor, sizeof(factor), status.scale_factor_millicounts_per_gram);
  }
  const char* scale_quality = status.scale_overload
                                  ? "OVERLOAD"
                                  : status.scale_negative
                                        ? "NEGATIVE"
                                        : status.scale_creep_warning
                                              ? "CREEP"
                                              : status.scale_stable ? "stable" : "moving";
  lv_label_set_text_fmt(
      system_label_,
      "SYSTEM\nReset: %s\nUptime: %lus\nBoots: %lu  crash streak: %u\n"
      "Storage: NVS %s / FS %s  Setup: %s",
      status.reset_reason,
      static_cast<unsigned long>(status.uptime_ms / 1000U),
      static_cast<unsigned long>(status.boot_count),
      status.crash_streak,
      status.nvs_ready ? "OK" : "ERR",
      status.filesystem_ready ? "OK" : "ERR",
      first_run_setup_.complete() ? "ready" : "incomplete");
  lv_label_set_text_fmt(
      memory_label_,
      "MEMORY\nHeap: %lu KiB (min %lu)\nPSRAM: %lu / %lu KiB\nLVGL buffers: %s",
      static_cast<unsigned long>(status.free_heap_bytes / 1024U),
      static_cast<unsigned long>(status.minimum_free_heap_bytes / 1024U),
      static_cast<unsigned long>(status.psram_free_bytes / 1024U),
      static_cast<unsigned long>(status.psram_total_bytes / 1024U),
      buffers_in_psram_ ? "PSRAM" : "internal fallback");
  lv_label_set_text_fmt(
      storage_label_,
      "NETWORK\nWi-Fi: %s\nSSID: %s  RSSI: %ld\nIP: %s\nGateway: %s\n"
      "DNS: %s\nmDNS: %s  NTP: %s",
      network::to_string(status.wifi_state),
      status.wifi_ssid.empty() ? "-" : status.wifi_ssid.c_str(),
      static_cast<long>(status.wifi_rssi_dbm),
      status.ip_address.empty() ? "-" : status.ip_address.c_str(),
      status.gateway.empty() ? "-" : status.gateway.c_str(),
      status.dns_server.empty() ? "-" : status.dns_server.c_str(),
      status.mdns_ready ? "ready" : "pending",
      status.ntp_ready ? "ready" : "pending");
  lv_label_set_text_fmt(
      touch_label_,
      "HARDWARE\nDisplay: %s  Touch: %s\nScale: %s (%s)\n"
      "ADC: %ld / %ld\nGross: %s g  %s\nCal: z=%ld f=%s",
      status.display_ready ? "ST7796 ready" : "ERROR",
      status.touch_configured ? "FT6336 ready" : "ERROR",
      services::to_string(status.scale_state),
      status.scale_persistence_available ? "saved" : "not saved",
      static_cast<long>(status.scale_raw_counts),
      static_cast<long>(status.scale_filtered_counts),
      weight,
      scale_quality,
      static_cast<long>(status.scale_zero_offset_counts),
      factor);
}

void UiService::note_interaction(std::uint32_t now_ms) {
  last_interaction_ms_ = now_ms;
  if (dimmed_) {
    display_.set_brightness(normal_brightness_percent_);
    dimmed_ = false;
  }
}

void UiService::run_once(std::uint32_t now_ms) {
  if (!initialized_) {
    return;
  }
  const auto elapsed = static_cast<std::uint32_t>(now_ms - last_tick_ms_);
  if (elapsed > 0U) {
    lv_tick_inc(elapsed);
    last_tick_ms_ = now_ms;
  }

  if (static_cast<std::uint32_t>(now_ms - last_refresh_ms_) >= refresh_interval_ms) {
    refresh_current(now_ms);
    last_refresh_ms_ = now_ms;
  }

  const auto idle_ms = static_cast<std::uint32_t>(now_ms - last_interaction_ms_);
  if (idle_ms >= sleep_after_ms_) {
    display_.sleep();
  } else if (idle_ms >= dim_after_ms_ && !dimmed_) {
    display_.set_brightness(
        normal_brightness_percent_ > 20U ? 20U : normal_brightness_percent_);
    dimmed_ = true;
  }
  lv_timer_handler();
}

}  // namespace opentag::ui
