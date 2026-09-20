#include "ui/ui_service.hpp"
#include "ui/lvgl_memory.h"
#include "ui/product_layout.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "ui/weight_format.hpp"
#include "nfc/presentation.hpp"
#include "services/workflow_presentation.hpp"
#include "web/local_access_policy.hpp"

namespace opentag::ui {
namespace {

static_assert(LV_COLOR_DEPTH == 16, "WT32 display requires RGB565");
static_assert(
    LV_COLOR_16_SWAP == 0,
    "LovyanGFX owns the LVGL RGB565 byte swap at the display boundary");

constexpr std::uint32_t screen_background = 0x101416;
constexpr std::uint32_t primary_text = 0xF8FAFC;
constexpr std::uint32_t secondary_text = 0xCBD5E1;
constexpr std::uint32_t accent_text = 0x72DFBE;
constexpr std::uint32_t warning_text = 0xFDE68A;
constexpr std::uint32_t secondary_surface = 0x344248;
constexpr std::uint32_t secondary_border = 0x667A83;
constexpr std::uint32_t navigation_surface = 0x1B2327;
constexpr std::uint32_t selected_navigation_surface = 0x205A53;
constexpr std::uint32_t danger_surface = 0x8A2E2E;
constexpr std::uint32_t danger_text = 0xFFF1F2;

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


void place(lv_obj_t* object, layout::Box box) {
  lv_obj_set_pos(object, box.x, box.y);lv_obj_set_size(object, box.w, box.h);
}
lv_obj_t* product_label(lv_obj_t* parent, layout::Box box, const char* text, const lv_font_t* font=&lv_font_montserrat_16) {
  auto* label=lv_label_create(parent);place(label,box);lv_label_set_text(label,text);
  lv_obj_set_style_text_font(label,font,0);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);return label;
}
lv_obj_t* product_button(lv_obj_t* parent, layout::Box box, const char* text, lv_event_cb_t callback, void* user, bool primary=false) {
  auto* button=lv_btn_create(parent);place(button,box);lv_obj_set_style_radius(button,8,0);
  lv_obj_set_style_shadow_width(button,0,0);
  lv_obj_set_style_bg_color(button,lv_color_hex(primary?accent_text:secondary_surface),0);
  lv_obj_set_style_text_color(button,lv_color_hex(primary?0x112C25:primary_text),0);
  lv_obj_set_style_border_width(button,1,0);
  lv_obj_set_style_border_color(button,lv_color_hex(primary?accent_text:secondary_border),0);
  lv_obj_set_style_bg_opa(button,LV_OPA_40,LV_STATE_DISABLED);
  lv_obj_set_style_text_opa(button,LV_OPA_55,LV_STATE_DISABLED);
  lv_obj_set_style_pad_all(button,4,0);lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,user);
  auto* label=lv_label_create(button);lv_label_set_text(label,text);lv_obj_set_style_text_font(label,&lv_font_montserrat_16,0);lv_obj_center(label);return button;
}
void forward_navigation(lv_event_t* event) {
  lv_event_send(static_cast<lv_obj_t*>(lv_event_get_user_data(event)),LV_EVENT_CLICKED,nullptr);
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

static const char* assignment_buttons[] = {"Back", "Assign", ""};
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
  if (!display_.initialized() || !opentag_lvgl_pool(LV_MEM_SIZE) || !allocate_buffers()) {
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
      lv_color_hex(0x147D73),
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
  first_run_gate_ = !first_run_setup_.complete();
  showing_setup_ = first_run_gate_;
  if (first_run_gate_ && !configured.wifi.ssid.empty()) {
    (void)first_run_setup_.go_to(services::SetupStep::ready);
  }
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
  workflow_home_state_label_ = nullptr;
  workflow_weight_label_ = nullptr;
  workflow_scale_unit_label_ = nullptr;
  workflow_weigh_button_ = nullptr;
  workflow_tare_button_ = nullptr;
  workflow_calibrate_button_ = nullptr;
  workflow_reference_input_ = nullptr;
  workflow_scale_quality_label_ = nullptr;
  workflow_scale_capture_label_ = nullptr;
  workflow_scale_gauge_ = nullptr;
  workflow_scale_indicator_ = nullptr;
  workflow_calibration_label_ = nullptr;
  workflow_calibration_close_button_ = nullptr;
  scale_calibration_panel_open_ = false;
  workflow_identity_label_ = nullptr;
  workflow_status_label_ = nullptr;
  nfc_detail_ = nullptr;tag_title_=nullptr;tag_buttons_.fill(nullptr);
  writer_preview_ = nullptr; writer_confirm_ = nullptr; clear_preview_=nullptr;weight_update_=nullptr;weight_policy_=nullptr; writer_confirmation_.clear();
  scale_keyboard_ = nullptr;
  display_test_touch_marker_ = nullptr;
  display_test_touch_label_ = nullptr;
  product_nav_buttons_.fill(nullptr);
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

void UiService::build_product_rail() {
  auto* screen=lv_scr_act();
  static const std::array<const char*,5> labels{"Home","Weigh","Assign","Tag","Settings"};
  for(std::size_t i=0;i<labels.size();++i){
    const bool selected=i==static_cast<std::size_t>(active_page_);
    product_nav_buttons_[i]=product_button(screen,{static_cast<std::int16_t>(i*96),layout::nav.y,96,48},labels[i],navigation_callback,this);
    lv_obj_set_style_radius(product_nav_buttons_[i],0,0);
    lv_obj_set_style_bg_color(product_nav_buttons_[i],lv_color_hex(selected?selected_navigation_surface:navigation_surface),0);
    lv_obj_set_style_border_width(product_nav_buttons_[i],selected?2:1,0);
    lv_obj_set_style_border_color(product_nav_buttons_[i],lv_color_hex(selected?accent_text:0x2B3438),0);
    lv_obj_set_style_text_color(product_nav_buttons_[i],lv_color_hex(selected?primary_text:secondary_text),0);
  }
}

void UiService::build_home_page() {
  auto* screen=lv_scr_act();
  workflow_home_state_label_=product_label(screen,layout::home_state,"OpenTag Station");
  lv_obj_set_style_text_color(workflow_home_state_label_,lv_color_hex(accent_text),0);
  workflow_material_label_=product_label(screen,layout::home_material,"Place a spool",&lv_font_montserrat_20);
  workflow_identity_label_=product_label(screen,layout::home_identity,"Set a tagged spool on the station");
  lv_obj_set_style_text_color(workflow_identity_label_,lv_color_hex(0xA4AFB0),0);
  workflow_weight_label_=product_label(screen,layout::home_weight,"",&lv_font_montserrat_32);
  workflow_scale_indicator_=lv_obj_create(screen);place(workflow_scale_indicator_,layout::home_art);
  lv_obj_set_style_radius(workflow_scale_indicator_,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_color(workflow_scale_indicator_,lv_color_hex(0x788583),0);
  lv_obj_set_style_border_width(workflow_scale_indicator_,6,0);
  lv_obj_clear_flag(workflow_scale_indicator_,LV_OBJ_FLAG_SCROLLABLE);
  workflow_scale_gauge_=lv_obj_create(workflow_scale_indicator_);lv_obj_set_size(workflow_scale_gauge_,18,18);lv_obj_center(workflow_scale_gauge_);
  lv_obj_set_style_radius(workflow_scale_gauge_,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(workflow_scale_gauge_,lv_color_hex(0x101416),0);
  workflow_weigh_button_=product_button(screen,layout::home_weigh,"WEIGH",weigh_callback,this,true);
  product_button(screen,layout::home_assign,"ASSIGN",forward_navigation,product_nav_buttons_[2]);
  product_button(screen,layout::home_tag,"MANAGE TAG",forward_navigation,product_nav_buttons_[3]);
}

void UiService::build_scale_page() {
  auto* screen = lv_scr_act();
  auto* title = lv_label_create(screen);
  lv_label_set_text(title, "Scale");
  workflow_material_label_=title;lv_obj_set_width(title,448);lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
  style_title(title);
  place(title,layout::heading);

  workflow_weight_label_=lv_label_create(screen);place(workflow_weight_label_,layout::gross);lv_obj_set_style_text_font(workflow_weight_label_,&lv_font_montserrat_32,0);
  workflow_scale_unit_label_=lv_label_create(screen);lv_label_set_text(workflow_scale_unit_label_,"GROSS WEIGHT (g)");place(workflow_scale_unit_label_,layout::gross_unit);
  workflow_scale_quality_label_=lv_label_create(screen);place(workflow_scale_quality_label_,layout::quality);
  workflow_scale_capture_label_=lv_label_create(screen);place(workflow_scale_capture_label_,layout::receipt);lv_obj_set_style_text_font(workflow_scale_capture_label_,&lv_font_montserrat_16,0);
  const auto make_action = [this, screen](
                               lv_obj_t** output,
                               const char* text,
                               std::int16_t y,
                               lv_event_cb_t callback,
                               bool primary) {
    auto* button = lv_btn_create(screen);
    *output = button;
    lv_obj_set_pos(button, 248, y);
    lv_obj_set_size(button, 216, 48);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(
        button, lv_color_hex(primary ? 0x72DFBE : 0x242C30), 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(
        label, lv_color_hex(primary ? 0x06201B : 0xE7FAF7), 0);
    lv_obj_center(label);
  };
  make_action(
      &workflow_weigh_button_, "WEIGH AGAIN", 180,
      weigh_callback, true);
  make_action(
      &workflow_tare_button_, "TARE", 102,
      tare_callback, false);
  lv_obj_add_flag(workflow_tare_button_,LV_OBJ_FLAG_HIDDEN);
  place(workflow_weigh_button_,layout::weigh);

  workflow_calibration_label_ = lv_label_create(screen);
  lv_label_set_text(workflow_calibration_label_, "CALIBRATION");
  lv_obj_set_pos(workflow_calibration_label_, 248, 46);
  lv_obj_set_style_text_color(
      workflow_calibration_label_, lv_color_hex(0x72DFBE), 0);
  lv_obj_add_flag(workflow_calibration_label_, LV_OBJ_FLAG_HIDDEN);

  workflow_reference_input_ = lv_textarea_create(screen);
  lv_obj_set_pos(workflow_reference_input_, 248, 76);
  lv_obj_set_size(workflow_reference_input_, 216, 48);
  lv_textarea_set_one_line(workflow_reference_input_, true);
  lv_textarea_set_accepted_chars(
      workflow_reference_input_, "0123456789.");
  lv_textarea_set_max_length(workflow_reference_input_, 8);
  lv_textarea_set_placeholder_text(
      workflow_reference_input_, "Reference grams");
  const auto scale = diagnostics_.scale_snapshot();
  const float reference = scale.scale_calibration_reference_grams > 0.0F
      ? scale.scale_calibration_reference_grams
      : std::min(1000.0F, scale.scale_rated_capacity_grams);
  char reference_text[16]{};
  std::snprintf(
      reference_text, sizeof(reference_text), "%ld",
      static_cast<long>(reference + 0.5F));
  lv_textarea_set_text(workflow_reference_input_, reference_text);
  lv_obj_add_event_cb(
      workflow_reference_input_, scale_textarea_callback,
      LV_EVENT_ALL, this);
  lv_obj_add_flag(workflow_reference_input_, LV_OBJ_FLAG_HIDDEN);

  make_action(
      &workflow_calibrate_button_, "CALIBRATE", 160,
      calibrate_callback, false);
  lv_obj_add_flag(workflow_calibrate_button_,LV_OBJ_FLAG_HIDDEN);

  workflow_calibration_close_button_ = lv_btn_create(screen);
  lv_obj_set_pos(workflow_calibration_close_button_, 248, 180);
  lv_obj_set_size(workflow_calibration_close_button_, 216, 48);
  lv_obj_set_style_radius(workflow_calibration_close_button_, 12, 0);
  lv_obj_set_style_bg_color(
      workflow_calibration_close_button_, lv_color_hex(0x191F22), 0);
  lv_obj_add_event_cb(
      workflow_calibration_close_button_,
      scale_calibration_close_callback, LV_EVENT_CLICKED, this);
  auto* close_label = lv_label_create(workflow_calibration_close_button_);
  lv_label_set_text(close_label, "Close");
  lv_obj_set_style_text_color(close_label, lv_color_hex(0xD5E7E8), 0);
  lv_obj_center(close_label);
  lv_obj_add_flag(
      workflow_calibration_close_button_, LV_OBJ_FLAG_HIDDEN);

  weight_update_=product_button(screen,layout::update,"UPDATE SPOOLMAN",weight_update_callback,this);
  weight_policy_=lv_btn_create(screen);place(weight_policy_,layout::update);lv_obj_add_flag(weight_policy_,LV_OBJ_FLAG_HIDDEN);
  auto* policy_label=lv_label_create(weight_policy_);lv_label_set_text(policy_label,"Auto-update OFF");lv_obj_center(policy_label);lv_obj_add_event_cb(weight_policy_,weight_policy_callback,LV_EVENT_CLICKED,this);
  workflow_status_label_ = lv_label_create(screen);
  lv_label_set_text(
      workflow_status_label_, "Waiting for stable empty platform");
  place(workflow_status_label_,layout::feedback);lv_label_set_long_mode(workflow_status_label_,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(
      workflow_status_label_, lv_color_hex(0xF4C95D), 0);
  lv_obj_set_style_text_font(
      workflow_status_label_, &lv_font_montserrat_14, 0);


}

void UiService::build_printer_page() {
  auto* screen = lv_scr_act();
  const auto configured = configuration_.snapshot();
  selected_printer_id_ = configured.filabridge.selected_printer_id;
  workflow_toolhead_enabled_.fill(true);
  for (const auto& profile : configured.toolheads) {
    if (profile.backend_id >= 0 &&
        static_cast<std::size_t>(profile.backend_id) <
            workflow_toolhead_enabled_.size()) {
      workflow_toolhead_enabled_[profile.backend_id] = profile.enabled;
    }
  }

  auto* title = lv_label_create(screen);
  lv_label_set_text(title, "Printer");
  style_title(title);
  lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN);

  workflow_material_label_ = lv_label_create(screen);
  lv_label_set_text(workflow_material_label_, "No printer configured");
  lv_obj_set_width(workflow_material_label_, 448);
  lv_obj_set_style_text_font(
      workflow_material_label_, &lv_font_montserrat_20, 0);
  place(workflow_material_label_,layout::printer_name);

  workflow_identity_label_ = lv_label_create(screen);
  lv_label_set_text(
      workflow_identity_label_, "Choose a printer in Settings.");
  lv_obj_set_width(workflow_identity_label_, 448);
  lv_obj_set_style_text_color(
      workflow_identity_label_, lv_color_hex(0xA4AFB0), 0);
  place(workflow_identity_label_,layout::printer_spool);

  static constexpr std::array<layout::Box,5> boxes{layout::tool1,layout::tool2,layout::tool3,layout::tool4,layout::tool5};
  for (std::size_t index = 0U;
       index < workflow_toolhead_buttons_.size(); ++index) {
    auto* button = lv_btn_create(screen);
    workflow_toolhead_buttons_[index] = button;
    place(button,boxes[index]);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x242C30), 0);
    lv_obj_add_event_cb(
        button, toolhead_callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    lv_label_set_text_fmt(label, "T%u\nEmpty",
                          static_cast<unsigned>(index + 1U));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
  }

  workflow_status_label_ = lv_label_create(screen);
  lv_obj_set_width(workflow_status_label_, 350);
  lv_obj_set_style_text_color(
      workflow_status_label_, lv_color_hex(0xA4AFB0), 0);
  place(workflow_status_label_,layout::feedback);
}

void UiService::build_tags_page() {
  if(!tag_flow_)tag_flow_=network::make_external<TagFlow>([]{return TagFlow{};});
  auto* screen=lv_scr_act();
  tag_title_=product_label(screen,{16,8,448,32},"Manage tag",&lv_font_montserrat_20);
  tag_details_=lv_obj_create(screen);place(tag_details_,{16,48,448,208});
  lv_obj_set_style_bg_opa(tag_details_,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_width(tag_details_,0,0);lv_obj_set_style_outline_width(tag_details_,0,0);
  lv_obj_set_style_shadow_width(tag_details_,0,0);lv_obj_set_style_pad_all(tag_details_,0,0);
  lv_obj_set_scroll_dir(tag_details_,LV_DIR_VER);lv_obj_set_scrollbar_mode(tag_details_,LV_SCROLLBAR_MODE_OFF);
  nfc_detail_=product_label(tag_details_,{0,0,440,160},"Loading tag…");
  lv_label_set_long_mode(nfc_detail_,LV_LABEL_LONG_WRAP);
  lv_obj_set_height(nfc_detail_,LV_SIZE_CONTENT);
  for(auto& button:tag_buttons_)button=product_button(screen,{16,212,448,44},"",tag_action_callback,this);
  if(!tag_flow_){lv_label_set_text(nfc_detail_,"Tag workspace unavailable. Please restart.");build_product_rail();return;}
  refresh_tags();
}
void UiService::open_input(InputSpec spec,std::function<void(const std::string&)> accepted) {
  if(!input_screen_)input_screen_=network::make_external<TouchInputScreen>([]{return TouchInputScreen{};});
  if(input_screen_)input_screen_->open(std::move(spec),std::move(accepted));
}
void UiService::tag_command(std::string command) {
  if(command.empty()||!tag_flow_)return;
  const auto receipt=backend_worker_.submit_writer(command);
  if(!receipt.accepted)tag_flow_->fail("Station is busy. Please try again.");
  else {tag_flow_->waiting=true;tag_flow_->operation=receipt.operation_id;tag_flow_->page=TagPage::progress;tag_flow_->phase="queued";tag_flow_->message="Please wait…";}
  draw_tags();
}
void UiService::tag_action_callback(lv_event_t* event) {
  auto* self=static_cast<UiService*>(lv_event_get_user_data(event));
  const auto target=lv_event_get_target(event);
  for(std::size_t i=0;i<self->tag_screen_.count;++i)if(self->tag_buttons_[i]==target) {
    self->tag_action(self->tag_screen_.buttons[i].action);return;
  }
}
void UiService::tag_action(TagAction action) {
  if(!tag_flow_||tag_flow_->waiting)return;
  if(action==TagAction::home||action==TagAction::weigh||action==TagAction::printer) {
    tag_flow_->page=TagPage::tag;tag_flow_->phase.clear();
    active_page_=action==TagAction::weigh?ProductPage::scale:action==TagAction::printer?ProductPage::printer:ProductPage::home;
    build_current_screen();return;
  }
  if(action==TagAction::search) {
    InputSpec spec;spec.title="Search "+(tag_flow_->entity=="community"?std::string("Community"):tag_flow_->entity=="spool"?"My Spools":"My Filaments");
    spec.initial=tag_flow_->query;spec.action="SEARCH";spec.required=tag_flow_->entity=="community";
    open_input(spec,[this](const std::string& text){tag_flow_->query=text;tag_flow_->offset=tag_flow_->row=0;tag_command(tag_flow_->browse());});return;
  }
  if(action==TagAction::name) {
    InputSpec spec;spec.title="Spoolman display name";spec.required=true;
    spec.initial=tag_flow_->import_name.empty()?std::string(tag_flow_->selected["name"]|""):tag_flow_->import_name;
    // Keep an overlong source visible/editable; Done stays disabled until it
    // meets the Spoolman limit. Never silently truncate the source identity.
    open_input(spec,[this](const std::string& text){tag_flow_->import_name=text;draw_tags();});return;
  }
  if(action==TagAction::initial||action==TagAction::remaining||action==TagAction::tare) {
    InputSpec spec;spec.mode=InputMode::numeric;spec.unit="g";spec.maximum_length=9;spec.required=true;
    spec.minimum=action==TagAction::initial?0.001:0;
    spec.title=action==TagAction::initial?"Initial filament":action==TagAction::remaining?"Remaining filament":"Empty spool weight";
    spec.initial=weight_text(action==TagAction::initial?tag_flow_->initial:action==TagAction::remaining?tag_flow_->remaining:tag_flow_->tare);
    open_input(spec,[this,action](const std::string& text){const auto n=std::strtod(text.c_str(),nullptr);if(action==TagAction::initial)tag_flow_->initial=n;else if(action==TagAction::remaining)tag_flow_->remaining=n;else tag_flow_->tare=n;draw_tags();});return;
  }
  tag_command(tag_flow_->act(action));draw_tags();
}
void UiService::draw_tags() {
  if(!tag_flow_||!tag_title_)return;
  tag_screen_=tag_flow_->screen();
  lv_label_set_text(tag_title_,tag_screen_.title.c_str());
  lv_obj_set_width(tag_title_,tag_flow_->page==TagPage::catalog?300:448);
  lv_label_set_text(nfc_detail_,tag_screen_.body.c_str());
  place(tag_details_,tag_body_box(tag_screen_));
  for(std::size_t i=0;i<tag_buttons_.size();++i) {
    auto* b=tag_buttons_[i];
    if(i>=tag_screen_.count){lv_obj_add_flag(b,LV_OBJ_FLAG_HIDDEN);continue;}
    const auto& item=tag_screen_.buttons[i];lv_obj_clear_flag(b,LV_OBJ_FLAG_HIDDEN);place(b,item.box);
    auto* label=lv_obj_get_child(b,0);lv_label_set_text(label,item.text.c_str());
    lv_obj_set_width(label,item.box.w-12);
    lv_obj_set_height(label,item.text.find('\n')==std::string::npos?20:40);
    lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_obj_center(label);
    std::uint32_t background=secondary_surface,text_color=primary_text,border=secondary_border;
    switch(item.action) {
      case TagAction::clear:
        background=danger_surface;text_color=danger_text;border=0xF87171;break;
      case TagAction::back:
      case TagAction::previous:
      case TagAction::next:
      case TagAction::home:
        background=navigation_surface;text_color=secondary_text;border=0x3D4A50;break;
      case TagAction::none:
        background=0x1F3336;text_color=accent_text;border=0x376A66;break;
      case TagAction::use:
      case TagAction::write:
      case TagAction::create:
      case TagAction::update:
      case TagAction::retry:
      case TagAction::weigh:
      case TagAction::printer:
      case TagAction::search:
      case TagAction::sources:
        background=accent_text;text_color=0x112C25;border=accent_text;break;
      default:
        break;
    }
    lv_obj_set_style_bg_color(b,lv_color_hex(background),0);
    lv_obj_set_style_border_width(b,1,0);
    lv_obj_set_style_border_color(b,lv_color_hex(border),0);
    lv_obj_set_style_text_color(b,lv_color_hex(text_color),0);
    if(label)lv_obj_set_style_text_color(label,lv_color_hex(text_color),0);
    if(item.enabled)lv_obj_clear_state(b,LV_STATE_DISABLED);else lv_obj_add_state(b,LV_STATE_DISABLED);
  }
}
void UiService::refresh_tags() {
  if(!tag_flow_||!nfc_detail_)return;
  auto& flow=*tag_flow_;
  const auto tag=nfc_.snapshot();
  const std::string incoming_uid=tag.uid?tag.uid->hex():"";
  if(!flow.waiting&&!incoming_uid.empty()&&!flow.uid.empty()&&incoming_uid!=flow.uid&&
     flow.phase!="unlink_pending"&&flow.phase!="association_pending"&&flow.phase!="write_recovery"&&flow.phase!="clear_recovery") {
    flow.page=TagPage::tag;flow.phase.clear();flow.selected.clear();flow.from_spool=0;
  }
  flow.uid=incoming_uid;
  workflow_.visit([&](const services::WorkflowSnapshot& current) {
    flow.current_spool=tag.tag&&tag.uid&&current.openprinttag_available&&current.uid==*tag.uid&&current.spool?current.spool->id:0;
  });
  flow.material=tag.tag?tag.tag->decoded.material.material_name.value_or("Filament spool"):"";
  flow.lifecycle=services::tag_lifecycle({tag.present,tag.blank_compatible,bool(tag.tag),flow.current_spool>0,tag.state==nfc::ReadState::unsupported,flow.phase,false});
  if(tag.error&&flow.page==TagPage::tag)flow.message=tag.error->message;
  auto body=backend_worker_.writer_snapshot();
  const auto checksum=nfc::nfcv::diagnostic_checksum(reinterpret_cast<const std::uint8_t*>(body.data()),body.size());
  if(checksum!=writer_view_checksum_) {
    if(flow.consume(body))writer_view_checksum_=checksum;
  }
  if(flow.operation) {
    const auto op=backend_worker_.writer_operation(flow.operation);
    if(op&&op->state==application::OperationState::failed) {flow.operation=0;flow.fail(op->error?op->error->message:op->message);}
  }
  draw_tags();
}

void UiService::weight_update_callback(lv_event_t* event) {
  auto* self=static_cast<UiService*>(lv_event_get_user_data(event));const auto measured=self->backend_worker_.weigh_snapshot();
  if(measured.phase=="ready"&&!measured.consumed)(void)self->backend_worker_.submit_weight_update(measured.measurement_id);
}
void UiService::weight_policy_callback(lv_event_t* event) {
  auto* self=static_cast<UiService*>(lv_event_get_user_data(event));if(self->configuration_worker_.pending())return;
  auto current=self->configuration_.versioned_snapshot();current.configuration.reconciliation.auto_update_after_weigh=!current.configuration.reconciliation.auto_update_after_weigh;
  (void)self->configuration_worker_.submit_replace(std::move(current.configuration),current.revision,millis());
}
void UiService::build_settings_page() {
  auto* screen=lv_scr_act();
  product_label(screen,layout::heading,"Display brightness",&lv_font_montserrat_20);
  auto* brightness=lv_slider_create(screen);brightness_slider_=brightness;place(brightness,layout::brightness);
  lv_slider_set_range(brightness,5,100);lv_slider_set_value(brightness,configuration_.snapshot().device.brightness_percent,LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness,brightness_callback,LV_EVENT_VALUE_CHANGED,this);
  weight_policy_=product_button(screen,layout::policy,"Auto-update weight",weight_policy_callback,this);
  product_button(screen,layout::calibration,"Calibrate scale",[](lv_event_t* e){
    auto* self=static_cast<UiService*>(lv_event_get_user_data(e));self->active_page_=ProductPage::scale;self->build_current_screen();self->set_scale_calibration_panel_open(true);
  },this);
  product_button(screen,layout::about,"About / Advanced",diagnostics_toggle_callback,this);
  workflow_status_label_=product_label(screen,layout::wifi,"Wi-Fi status");
}

void UiService::build_workflow_screen() {
  auto* screen = lv_scr_act();
  style_screen(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101416), 0);
  if(active_page_!=ProductPage::tags)build_product_rail();
  switch (active_page_) {
    case ProductPage::home: build_home_page(); break;
    case ProductPage::scale: build_scale_page(); break;
    case ProductPage::printer: build_printer_page(); break;
    case ProductPage::tags: build_tags_page(); break;
    case ProductPage::settings: build_settings_page(); break;
  }
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
  lv_obj_add_event_cb(input, setup_textarea_callback, LV_EVENT_CLICKED, this);
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
        configured.web.access_token.empty()
            ? "Local API token (optional, 16-128 characters)"
            : "New local API token (blank keeps current)",
        "",
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
    self->first_run_gate_ = false;
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
  self->first_run_gate_ = false;
  self->showing_diagnostics_ = false;
  self->setup_feedback_.clear();
  self->build_current_screen();
  if (!self->showing_setup_) self->refresh_current(millis());
}

void UiService::navigation_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  const auto selected = std::find(
      self->product_nav_buttons_.begin(),
      self->product_nav_buttons_.end(),
      target);
  if (selected == self->product_nav_buttons_.end()) return;
  self->active_page_ = static_cast<ProductPage>(
      std::distance(self->product_nav_buttons_.begin(), selected));
  self->showing_setup_ = false;
  self->showing_diagnostics_ = false;
  self->build_current_screen();
  self->refresh_current(millis());
}

void UiService::weigh_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  if(self->active_page_==ProductPage::home&&self->nfc_.snapshot().blank_compatible){self->active_page_=ProductPage::tags;self->build_current_screen();self->tag_action(TagAction::sources);return;}
  if (self->active_page_ == ProductPage::home &&
      !self->diagnostics_.scale_snapshot().scale_calibrated) {
    self->active_page_ = ProductPage::scale;
    self->workflow_feedback_.clear();
    self->build_current_screen();
    self->set_scale_calibration_panel_open(true);
    self->refresh_workflow();
    return;
  }
  const auto receipt = self->scale_commands_.submit_weigh(millis());
  if (receipt.accepted) {
    self->weigh_operation_id_ = receipt.operation_id;
    self->scale_action_ = ScaleAction::weigh;
    self->workflow_feedback_ = "Measuring";
  } else {
    self->weigh_operation_id_.reset();
    self->scale_action_ = ScaleAction::none;
    self->workflow_feedback_ =
        "Weigh request rejected or the scale queue is full";
  }
  if (self->active_page_ == ProductPage::home) {
    self->active_page_ = ProductPage::scale;
    self->build_current_screen();
  }
  self->refresh_workflow();
}

void UiService::tare_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  const auto receipt = self->scale_commands_.submit_tare(millis());
  if (receipt.accepted) {
    self->weigh_operation_id_ = receipt.operation_id;
    self->scale_action_ = ScaleAction::tare;
    self->workflow_feedback_ = "Taring empty platform";
  } else {
    self->weigh_operation_id_.reset();
    self->scale_action_ = ScaleAction::none;
    self->workflow_feedback_ =
        "Tare request rejected or the scale queue is full";
  }
  self->refresh_workflow();
}

void UiService::set_scale_calibration_panel_open(bool open) {
  scale_calibration_panel_open_ = open;
  const auto set_hidden = [open](lv_obj_t* object, bool panel_object) {
    if (object == nullptr) return;
    const bool hidden = panel_object ? !open : open;
    if (hidden) {
      lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
  };
  set_hidden(workflow_weigh_button_, false);
  set_hidden(workflow_tare_button_, true);
  set_hidden(weight_update_,false);
  if(weight_policy_)lv_obj_add_flag(weight_policy_,LV_OBJ_FLAG_HIDDEN);
  set_hidden(workflow_scale_capture_label_,false);
  set_hidden(workflow_calibration_label_, true);
  set_hidden(workflow_reference_input_, true);
  set_hidden(workflow_calibration_close_button_, true);
  if (workflow_calibrate_button_ != nullptr) {
    lv_obj_set_pos(
        workflow_calibrate_button_, 248, 128);
    if(open)lv_obj_clear_flag(workflow_calibrate_button_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(workflow_calibrate_button_,LV_OBJ_FLAG_HIDDEN);
    if(workflow_tare_button_){lv_obj_set_pos(workflow_tare_button_,16,180);}
    auto* label = lv_obj_get_child(workflow_calibrate_button_, 0);
    if (label != nullptr) {
      lv_label_set_text(label, open ? "RUN CALIBRATION" : "CALIBRATE");
    }
  }
  if (!open && scale_keyboard_ != nullptr) {
    lv_keyboard_set_textarea(scale_keyboard_, nullptr);
    lv_obj_add_flag(scale_keyboard_, LV_OBJ_FLAG_HIDDEN);
  }
}

void UiService::calibrate_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  if (!self->scale_calibration_panel_open_) {
    self->set_scale_calibration_panel_open(true);
    self->refresh_workflow();
    return;
  }
  if (self->workflow_reference_input_ == nullptr) return;
  char* end = nullptr;
  const float reference = std::strtof(
      lv_textarea_get_text(self->workflow_reference_input_), &end);
  if (end == nullptr || *end != '\0') {
    self->workflow_feedback_ = "Enter a valid reference weight";
    self->refresh_workflow();
    return;
  }
  const auto receipt =
      self->scale_commands_.submit_calibration(reference, millis());
  if (receipt.accepted) {
    self->weigh_operation_id_ = receipt.operation_id;
    self->scale_action_ = ScaleAction::calibrate;
    self->workflow_feedback_ = "Calibrating";
  } else {
    self->weigh_operation_id_.reset();
    self->scale_action_ = ScaleAction::none;
    self->workflow_feedback_ =
        "Calibration rejected or the scale queue is full";
  }
  self->refresh_workflow();
}

void UiService::scale_calibration_close_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->set_scale_calibration_panel_open(false);
  self->refresh_workflow();
}

void UiService::scale_textarea_callback(lv_event_t* event) {
  auto* self=static_cast<UiService*>(lv_event_get_user_data(event));
  if(lv_event_get_code(event)!=LV_EVENT_CLICKED)return;
  auto* target=static_cast<lv_obj_t*>(lv_event_get_target(event));
  InputSpec spec;spec.mode=InputMode::numeric;spec.title="Calibration weight";spec.unit="g";spec.maximum_length=9;spec.required=true;spec.minimum=1;
  spec.initial=lv_textarea_get_text(target);
  self->open_input(spec,[self,target](const std::string& value){lv_textarea_set_text(target,value.c_str());self->refresh_workflow();});
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
                                             : occupied ? "Replace spool?" : "Assign spool?";
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
  } else if (!occupied) {
    message="Assign "+state.spool->display_name+" to "+toolhead->display_name+"?";buttons=assignment_buttons;
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
  lv_obj_set_width(message_box,448);
  lv_obj_set_style_min_height(lv_msgbox_get_btns(message_box),48,0);
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
  } else if (step == services::SetupStep::ready && !first.empty()) {
    updated.web.access_token = first;
  }
  updated.setup.completed_steps |=
      1U << static_cast<std::uint8_t>(step);
  const auto receipt = self->configuration_worker_.submit_replace(
      std::move(updated), versioned.revision, millis());
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
  auto* self=static_cast<UiService*>(lv_event_get_user_data(event));
  auto* target=static_cast<lv_obj_t*>(lv_event_get_target(event));
  const auto step=self->first_run_setup_.current();
  InputSpec spec;spec.initial=lv_textarea_get_text(target);spec.maximum_length=lv_textarea_get_max_length(target);
  spec.mode=lv_textarea_get_password_mode(target)?InputMode::password:
      target==self->setup_input_one_&&(step==services::SetupStep::spoolman||step==services::SetupStep::filabridge)?InputMode::url:InputMode::text;
  spec.title=spec.mode==InputMode::url?"Server URL":spec.mode==InputMode::password?"Password / token":"Configuration";
  self->open_input(spec,[target](const std::string& value){lv_textarea_set_text(target,value.c_str());});
}

void UiService::refresh_setup() {
  if (!showing_setup_ || setup_progress_label_ == nullptr) return;
  if (first_run_gate_ && first_run_setup_.complete()) {
    showing_setup_ = false;
    first_run_gate_ = false;
    build_current_screen();
    refresh_current(millis());
    return;
  }
  if (setup_feedback_ == "Save queued" &&
      configuration_worker_.pending() == 0U) {
    setup_feedback_ = configuration_worker_.last_operation_succeeded()
                          ? "Saved"
                          : "Save or network apply failed; review persisted settings";
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
      body = "SCALE CALIBRATION\nTap Main, then Weigh to tare and calibrate.\nEnter the known mass on this station.";
      break;
    case services::SetupStep::nfc_status:
      body = "NFC STATUS\nPresent an OpenPrintTag spool. View recognition on Tags.";
      break;
    case services::SetupStep::ready:
      body = "READY\nLocal API authentication is optional.";
      break;
  }
  lv_label_set_text(setup_body_label_, body);
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
      if (web::local_access_policy(configured.web.access_token)
              .authentication_enabled) {
        status = "Local API authentication: ENABLED\n"
                 "Local browser control: ENABLED";
      } else {
        status = "Local API authentication: DISABLED\n"
                 "Local browser control: ENABLED\n"
                 "Trusted LAN mode — set an API token in Configuration to require authentication.";
      }
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
  if(input_screen_&&input_screen_->active())return;
  if (showing_display_self_test_) {
    return;
  }
  if (showing_setup_) {
    refresh_setup();
  } else if (showing_diagnostics_) {
    refresh_diagnostics(now_ms);
  } else if(active_page_==ProductPage::tags) {
    refresh_tags();
  } else {
    refresh_workflow();
  }
}

void UiService::refresh_workflow() {
  if (showing_setup_ || showing_diagnostics_ ||
      product_nav_buttons_[0] == nullptr) {
    return;
  }

  if (weigh_operation_id_.has_value()) {
    const auto operation = scale_commands_.operation(*weigh_operation_id_);
    const char* action = scale_action_ == ScaleAction::tare
        ? "Tare"
        : scale_action_ == ScaleAction::calibrate ? "Calibration" : "Weigh";
    if (!operation.has_value()) {
      workflow_feedback_ = std::string(action) +
          " status unavailable; retry if needed";
      weigh_operation_id_.reset();
      scale_action_ = ScaleAction::none;
    } else if (operation->state == application::OperationState::queued ||
               operation->state == application::OperationState::running) {
      workflow_feedback_ = operation->message.empty()
          ? std::string(action) + " in progress"
          : operation->message;
    } else if (operation->state == application::OperationState::succeeded) {
      const bool calibration_complete =
          scale_action_ == ScaleAction::calibrate;
      workflow_feedback_ = std::string(action) + " complete";
      weigh_operation_id_.reset();
      scale_action_ = ScaleAction::none;
      if (calibration_complete) {
        set_scale_calibration_panel_open(false);
      }
    } else {
      workflow_feedback_ = operation->error.has_value()
          ? std::string(action) + " failed: " + operation->error->message
          : std::string(action) + " did not complete";
      weigh_operation_id_.reset();
      scale_action_ = ScaleAction::none;
    }
  }

  const auto scale = diagnostics_.scale_snapshot();
  const bool measurement_active =
      scale.scale_measurement_state ==
          services::ScaleMeasurementState::settling ||
      scale.scale_measurement_state ==
          services::ScaleMeasurementState::ready;
  const bool busy = measurement_active ||
      scale_commands_.pending() != 0U ||
      weigh_operation_id_.has_value();
  const auto set_enabled = [](lv_obj_t* button, bool enabled) {
    if (button == nullptr) return;
    if (enabled) {
      lv_obj_clear_state(button, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(button, LV_STATE_DISABLED);
    }
  };
  const auto set_weight = [&](lv_obj_t* label, bool inline_unit) {
    if (label == nullptr) return;
    if (measurement_active && scale.scale_weight_available) {
      lv_label_set_text_fmt(
          label, inline_unit ? "%ld g" : "%ld",
          static_cast<long>(rounded_grams_from_milligrams(
              scale.scale_gross_milligrams)));
    } else if (scale.scale_last_completed_available) {
      lv_label_set_text_fmt(
          label, inline_unit ? "%ld g" : "%ld",
          static_cast<long>(rounded_grams_from_milligrams(
              scale.scale_last_completed_milligrams)));
    } else {
      lv_label_set_text(label, inline_unit ? "-- g" : "--");
    }
  };

  if (active_page_ == ProductPage::home) {
    const auto workflow=workflow_.snapshot();
    const auto tag=nfc_.snapshot();
    const bool blank=tag.present&&tag.blank_compatible;
    const bool present=workflow.openprinttag_available;
    place(workflow_scale_indicator_,present?layout::home_art:layout::empty_art);
    const auto color=workflow.material.primary_color;
    lv_obj_set_style_bg_color(workflow_scale_indicator_,color?lv_color_make(color->red,color->green,color->blue):lv_color_hex(0x242C30),0);
    lv_obj_set_style_text_align(workflow_home_state_label_,present?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_align(workflow_material_label_,present?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_align(workflow_identity_label_,present?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(workflow_identity_label_,present?348:448);
    lv_obj_set_width(workflow_weight_label_,348);
    lv_label_set_text(workflow_home_state_label_,present?workflow.material.brand_name.value_or("Current spool").c_str():"OpenTag Station");
    lv_label_set_text(workflow_material_label_,present?workflow.material.material_name.value_or("Filament spool").c_str():"Place a spool");
    const std::string identity=workflow.spool?"Spool #"+std::to_string(workflow.spool->id)+"  |  Tag valid":present?"Tag recognized - looking up spool":"Set a tagged spool on the station";
    lv_label_set_text(workflow_identity_label_,identity.c_str());
    const auto remaining=workflow.spool?workflow.spool->remaining_grams:std::optional<float>{};
    if(remaining)lv_label_set_text_fmt(workflow_weight_label_,"%ld g remaining",static_cast<long>(std::lround(*remaining)));
    else lv_label_set_text(workflow_weight_label_,present?"Weight not known":"");
    if(blank){lv_label_set_text(workflow_home_state_label_,"COMPATIBLE BLANK TAG");lv_label_set_text(workflow_material_label_,"Ready to assign");lv_label_set_text(workflow_identity_label_,"Open Manage tag to choose a spool");}
    set_enabled(workflow_weigh_button_,blank||(scale.scale_adc_ready&&!busy));
    lv_label_set_text(lv_obj_get_child(workflow_weigh_button_,0),busy?"WEIGHING...":scale.scale_calibrated?"WEIGH":"CALIBRATE");
    if(blank)lv_label_set_text(lv_obj_get_child(workflow_weigh_button_,0),"ASSIGN TAG");
    return;
  }

  if (active_page_ == ProductPage::scale) {
    set_weight(workflow_weight_label_, false);
    const bool stable_for_action =
        scale.scale_measurement_purpose ==
                services::ScaleMeasurementPurpose::weigh
            ? scale.scale_stable
            : scale.scale_raw_stable;
    if (workflow_scale_quality_label_ != nullptr) {
      if (!scale.scale_adc_ready) {
        lv_label_set_text(workflow_scale_quality_label_, "ERROR");
      } else if (measurement_active) {
        lv_label_set_text(
            workflow_scale_quality_label_,
            stable_for_action ? "STABLE" :
            scale.scale_samples_in_filter < 3U ? "MEASURING" : "SETTLING");
      } else if (!scale.scale_calibrated) {
        lv_label_set_text(workflow_scale_quality_label_, "SETUP REQUIRED");
      } else if (scale.scale_last_completed_available) {
        lv_label_set_text(
            workflow_scale_quality_label_,
            scale.scale_last_completed_milligrams == 0 ? "ZERO" : "STABLE");
      } else {
        lv_label_set_text(workflow_scale_quality_label_, "READY");
      }
    }
    if (workflow_scale_capture_label_ != nullptr) {
      if (scale.scale_last_completed_available) {
        lv_label_set_text_fmt(
            workflow_scale_capture_label_, "Last: %ld g",
            static_cast<long>(rounded_grams_from_milligrams(
                scale.scale_last_completed_milligrams)));
      } else {
        lv_label_set_text(workflow_scale_capture_label_, "Last: -- g");
      }
    }

    std::uint32_t gauge_color = 0x526B77;
    std::int32_t gauge_value = 0;
    switch (scale.scale_measurement_state) {
      case services::ScaleMeasurementState::settling:
        gauge_color = scale.scale_samples_in_filter < 3U
            ? 0x72DFBE : 0xF4C95D;
        gauge_value = scale.scale_samples_in_filter < 3U ? 42 : 72;
        break;
      case services::ScaleMeasurementState::ready:
      case services::ScaleMeasurementState::completed:
        gauge_color = 0x22C55E;
        gauge_value = 100;
        break;
      case services::ScaleMeasurementState::timed_out:
      case services::ScaleMeasurementState::failed:
        gauge_color = 0xEF4444;
        gauge_value = 100;
        break;
      case services::ScaleMeasurementState::idle:
        break;
    }
    if (!scale.scale_adc_ready) {
      gauge_color = 0xEF4444;
      gauge_value = 100;
    } else if (!scale.scale_calibrated && !measurement_active) {
      gauge_color = 0xF4C95D;
      gauge_value = 28;
    }
    if (workflow_scale_gauge_ != nullptr) {
      lv_arc_set_value(workflow_scale_gauge_, gauge_value);
      lv_obj_set_style_arc_color(
          workflow_scale_gauge_, lv_color_hex(gauge_color),
          LV_PART_INDICATOR);
    }
    if (workflow_scale_indicator_ != nullptr) {
      lv_obj_set_style_border_color(
          workflow_scale_indicator_, lv_color_hex(gauge_color), 0);
    }
    if (workflow_scale_quality_label_ != nullptr) {
      lv_obj_set_style_text_color(
          workflow_scale_quality_label_, lv_color_hex(gauge_color), 0);
    }

    char* end = nullptr;
    const float reference = workflow_reference_input_ == nullptr
        ? 0.0F
        : std::strtof(
              lv_textarea_get_text(workflow_reference_input_), &end);
    const bool reference_valid = end != nullptr && *end == '\0' &&
        reference > 0.0F &&
        reference <= scale.scale_rated_capacity_grams;
    set_enabled(
        workflow_weigh_button_,
        scale.scale_adc_ready && scale.scale_calibrated && !busy);
    set_enabled(
        workflow_tare_button_,
        scale.scale_adc_ready && scale.scale_raw_stable && !busy);
    set_enabled(
        workflow_calibrate_button_,
        scale_calibration_panel_open_
            ? scale.scale_adc_ready && scale.scale_tare_ready &&
                scale.scale_raw_stable && reference_valid && !busy
            : scale.scale_adc_ready && !busy);
    if (workflow_weigh_button_ != nullptr) {
      lv_obj_set_style_bg_color(
          workflow_weigh_button_,
          lv_color_hex(scale.scale_calibrated ? 0x72DFBE : 0x242C30), 0);
      auto* label = lv_obj_get_child(workflow_weigh_button_, 0);
      if (label != nullptr) {
        lv_obj_set_style_text_color(
            label,
            lv_color_hex(scale.scale_calibrated ? 0x06201B : 0xE7FAF7), 0);
      }
    }
    if (workflow_calibrate_button_ != nullptr) {
      lv_obj_set_style_bg_color(
          workflow_calibrate_button_,
          lv_color_hex(scale.scale_calibrated ? 0x242C30 : 0x72DFBE), 0);
      auto* label = lv_obj_get_child(workflow_calibrate_button_, 0);
      if (label != nullptr) {
        lv_obj_set_style_text_color(
            label,
            lv_color_hex(scale.scale_calibrated ? 0xE7FAF7 : 0x06201B), 0);
      }
    }

    std::string guidance;
    if (!scale.scale_adc_ready) {
      guidance = "Scale hardware unavailable";
    } else if (!scale.scale_tare_ready && !scale.scale_raw_stable) {
      guidance = "1. Waiting for stable empty platform";
    } else if (!scale.scale_tare_ready) {
      guidance = "2. Ready to tare";
    } else if (scale.scale_samples_in_filter == 0U) {
      guidance = "3. Tare complete — place reference weight";
    } else if (!scale.scale_raw_stable) {
      guidance = "4. Waiting for stable reference weight";
    } else if (!reference_valid) {
      guidance = "Enter the known reference mass";
    } else {
      guidance = "5. Ready to calibrate";
    }
    if (busy && !workflow_feedback_.empty()) {
      guidance = workflow_feedback_;
    }
    lv_label_set_text(workflow_status_label_, guidance.c_str());
    const auto measured=backend_worker_.weigh_snapshot();
    if(workflow_material_label_)lv_label_set_text(workflow_material_label_,measured.name.empty()?"Scale":measured.name.c_str());
    const auto grams=[](const std::optional<float>& value){return value?std::to_string(static_cast<int>(std::lround(*value)))+" g":"Not set";};
    if(!scale_calibration_panel_open_){
      const auto receipt="Empty spool  "+grams(measured.tare)+"\nFilament  "+grams(measured.measured)+"\nSpoolman  "+grams(measured.canonical_remaining)+"\nDifference  "+grams(measured.difference);
      lv_label_set_text(workflow_scale_capture_label_,receipt.c_str());
      const auto message=measured.message.substr(0,88);lv_label_set_text(workflow_status_label_,message.c_str());
    }
    bool automatic=false;configuration_.visit([&](const auto& config,auto){automatic=config.reconciliation.auto_update_after_weigh;});
    lv_label_set_text(lv_obj_get_child(weight_policy_,0),automatic?"Auto-update ON":"Auto-update OFF");
    const bool can_update=!automatic&&!busy&&measured.phase=="ready"&&!measured.consumed;
    set_enabled(weight_update_,can_update);
    if(weight_update_) {
      auto* update_label=lv_obj_get_child(weight_update_,0);
      const bool updating=measured.phase=="updating";
      const bool updated=measured.phase=="updated"||measured.phase=="unchanged";
      if(update_label)lv_label_set_text(update_label,updating?"UPDATING...":updated?"SPOOLMAN UPDATED":"UPDATE SPOOLMAN");
      lv_obj_set_style_bg_color(weight_update_,lv_color_hex(can_update?accent_text:secondary_surface),0);
      lv_obj_set_style_border_color(weight_update_,lv_color_hex(can_update?accent_text:secondary_border),0);
      const auto update_text=can_update?0x112C25:primary_text;
      lv_obj_set_style_text_color(weight_update_,lv_color_hex(update_text),0);
      if(update_label)lv_obj_set_style_text_color(update_label,lv_color_hex(update_text),0);
    }
    set_enabled(weight_policy_,!busy&&!configuration_worker_.pending());
    return;
  }

  if (active_page_ == ProductPage::printer) {
    const auto workflow = workflow_.snapshot();
    const auto printer = std::find_if(
        workflow.printers.begin(), workflow.printers.end(),
        [&](const auto& candidate) {
          return candidate.id == selected_printer_id_;
        });
    if (selected_printer_id_.empty() ||
        printer == workflow.printers.end()) {
      lv_label_set_text(
          workflow_material_label_, "No printer configured");
      lv_label_set_text(
          workflow_identity_label_, "Choose a printer in Settings.");
    } else {
      lv_label_set_text(
          workflow_material_label_,
          printer->display_name.empty()
              ? "Selected printer"
              : printer->display_name.c_str());
      lv_label_set_text(
          workflow_identity_label_,
          printer->state == domain::PrinterState::offline
              ? "Offline"
              : printer->state == domain::PrinterState::unknown
                  ? "Checking connection"
                  : "Connected");
    }
    for (std::size_t index = 0U;
         index < workflow_toolhead_buttons_.size(); ++index) {
      auto* button = workflow_toolhead_buttons_[index];
      if (button == nullptr) continue;
      const domain::Toolhead* found = nullptr;
      if (printer != workflow.printers.end()) {
        const auto toolhead = std::find_if(
            printer->toolheads.begin(), printer->toolheads.end(),
            [&](const auto& candidate) {
              return candidate.backend_id == static_cast<int>(index);
            });
        if (toolhead != printer->toolheads.end()) found = &*toolhead;
      }
      auto* label = lv_obj_get_child(button, 0);
      if (label != nullptr) {
        lv_label_set_text_fmt(
            label, "T%u\n%s",
            static_cast<unsigned>(index + 1U),
            found != nullptr && found->assigned_spool.has_value()
                ? "Spool assigned" : "Unassigned");
      }
      set_enabled(
          button,
          workflow.spool.has_value() && found != nullptr &&
              workflow.filabridge ==
                  services::BackendAvailability::online &&
              workflow.filabridge_assignment_available &&
              workflow_toolhead_enabled_[index] &&
              backend_worker_.pending() == 0U);
    }
    std::string status = "Spoolman ";
    status += availability_text(workflow.spoolman);
    status += "   FilaBridge ";
    status += availability_text(workflow.filabridge);
    if (!workflow_feedback_.empty()) status += "   " + workflow_feedback_;
    if (workflow.assignment_error) status = workflow.assignment_error->message;
    else if (workflow.filabridge_error) status = workflow.filabridge_error->message;
    else if (workflow.stage == services::WorkflowStage::assignment_complete)
      status = "Assignment verified by FilaBridge readback";
    lv_label_set_text(workflow_status_label_, status.c_str());
    return;
  }

  if (active_page_ == ProductPage::settings &&
      workflow_status_label_ != nullptr) {
    const auto system = diagnostics_.snapshot(millis());
    const auto workflow = workflow_.snapshot();
    std::string status = system.wifi_connected
        ? "Connected"
        : "Wi-Fi not connected";
    if (system.wifi_connected && !system.ip_address.empty()) {
      status += " · " + system.ip_address;
    }
    bool automatic=false;configuration_.visit([&](const auto& c,auto){automatic=c.reconciliation.auto_update_after_weigh;});
    lv_label_set_text(lv_obj_get_child(weight_policy_,0),automatic?"Auto-update ON":"Auto-update OFF");
    lv_label_set_text(workflow_status_label_, status.c_str());
  }
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
      "MEMORY\nHeap: %lu KiB (min %lu, block %lu)\n"
      "PSRAM: %lu KiB (min %lu, block %lu) / %lu\nLVGL buffers: %s",
      static_cast<unsigned long>(status.free_heap_bytes / 1024U),
      static_cast<unsigned long>(status.minimum_free_heap_bytes / 1024U),
      static_cast<unsigned long>(
          status.largest_free_internal_block_bytes / 1024U),
      static_cast<unsigned long>(status.psram_free_bytes / 1024U),
      static_cast<unsigned long>(status.minimum_free_psram_bytes / 1024U),
      static_cast<unsigned long>(
          status.largest_free_psram_block_bytes / 1024U),
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
