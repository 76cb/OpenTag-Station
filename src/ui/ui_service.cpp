#include "ui/ui_service.hpp"
#include "ui/lvgl_memory.h"
#include "ui/product_layout.hpp"
#include "ui/assignment_dialog.hpp"
#include "ui/weigh_view.hpp"

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
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include "web/local_access_policy.hpp"

namespace opentag::ui {
namespace {

static_assert(LV_COLOR_DEPTH == 16, "WT32 display requires RGB565");
static_assert(
    LV_COLOR_16_SWAP == 0,
    "LovyanGFX owns the LVGL RGB565 byte swap at the display boundary");

constexpr std::uint32_t screen_background = 0x101416;
constexpr std::uint32_t primary_text = 0xF8FAFC;
constexpr std::uint32_t accent_text = 0x72DFBE;
constexpr std::uint32_t warning_text = 0xFDE68A;
// Destructive actions (erase tag, skip recovery, replace toolhead) use a red
// fill so they are never mistaken for the normal primary action.
constexpr std::uint32_t destructive_fill = 0x8F2D2D;
constexpr std::uint32_t destructive_label = 0xFFF1F1;
constexpr std::uint32_t primary_fill = 0x72DFBE;
constexpr std::uint32_t primary_label = 0x112C25;
constexpr std::uint32_t neutral_fill = 0x242C30;
constexpr std::uint32_t neutral_label = 0xF3F5F3;
constexpr std::uint32_t muted_text = 0xA4AFB0;
constexpr std::uint32_t recessed_fill = 0x191F22;

void style_action(lv_obj_t* button, std::uint32_t fill, std::uint32_t label) {
  if (button == nullptr) return;
  lv_obj_set_style_bg_color(button, lv_color_hex(fill), 0);
  auto* text = lv_obj_get_child(button, 0);
  if (text != nullptr) lv_obj_set_style_text_color(text, lv_color_hex(label), 0);
}

// Every button uses the product palette instead of LVGL's default blue:
// primary (mint) for the screen's main forward action, neutral otherwise.
void style_role(lv_obj_t* button, bool primary) {
  if (button == nullptr) return;
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  style_action(button, primary ? primary_fill : neutral_fill,
               primary ? primary_label : neutral_label);
}

void set_enabled(lv_obj_t* button, bool enabled) {
  if (button == nullptr) return;
  if (enabled) {
    lv_obj_clear_state(button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(button, LV_STATE_DISABLED);
  }
}

const char* nfc_status_text(const nfc::ReadSnapshot& tag) {
  switch (tag.state) {
    case nfc::ReadState::deferred:
    case nfc::ReadState::starting: return "NFC reader starting...";
    case nfc::ReadState::idle: return "NFC reader ready. Place a tag on it to test.";
    case nfc::ReadState::stabilizing:
    case nfc::ReadState::reading: return "Reading tag. Hold the spool still.";
    case nfc::ReadState::openprinttag: return "OpenPrintTag found. The reader works.";
    case nfc::ReadState::blank: return "Blank tag found. The reader works.";
    case nfc::ReadState::unsupported: return "Tag found, but it can't be used as it is.";
    case nfc::ReadState::multiple: return "More than one tag. Use one tag at a time.";
    case nfc::ReadState::error: return "NFC reader problem. Check the NFC wiring.";
  }
  return "NFC reader starting...";
}

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
  lv_obj_set_style_shadow_width(button,0,0);lv_obj_set_style_bg_color(button,lv_color_hex(primary?primary_fill:neutral_fill),0);
  lv_obj_set_style_text_color(button,lv_color_hex(primary?primary_label:neutral_label),0);
  lv_obj_set_style_pad_all(button,4,0);lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,user);
  auto* label=lv_label_create(button);lv_label_set_text(label,text);lv_obj_set_style_text_font(label,&lv_font_montserrat_16,0);lv_obj_center(label);return button;
}
void forward_navigation(lv_event_t* event) {
  lv_event_send(static_cast<lv_obj_t*>(lv_event_get_user_data(event)),LV_EVENT_CLICKED,nullptr);
}

// Assigning spools to toolheads needs FilaBridge. Without its URL the station
// has no printers, so the Assign page and buttons are not offered at all.
bool printer_integration_enabled(const config::ConfigurationService& configuration) {
  return !configuration.backend_settings_snapshot().filabridge.url.empty();
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

// Dropdown text for the Wi-Fi scan results, one network name per line.
std::string wifi_options(const std::vector<network::WifiNetwork>& networks) {
  std::string options = networks.empty() ? "Scan results" : "";
  for (const auto& candidate : networks) {
    if (!options.empty()) options += '\n';
    options += candidate.ssid;
  }
  return options;
}

// A failed settings save in words the user can act on.
std::string plain_save_error(const core::Error* error, services::SetupStep step) {
  if (error == nullptr) return "Could not save these settings. Try again.";
  if (error->category == core::ErrorCategory::network)
    return step == services::SetupStep::wifi
               ? "Saved, but the station could not connect to this Wi-Fi network. Check the name and password."
               : "Saved, but the network could not be updated. Try again.";
  if (error->category == core::ErrorCategory::conflict)
    return "Settings were changed somewhere else. Check the values and save again.";
  return "Could not save: " + error->message;
}

static const char* assignment_buttons[] = {
    assignment_dialog::cancel_label, assignment_dialog::assign_label, ""};
static const char* replacement_buttons[] = {
    assignment_dialog::cancel_label, assignment_dialog::replace_label, ""};
static const char* override_buttons[] = {
    assignment_dialog::cancel_label, assignment_dialog::override_label, ""};
static const char* override_replacement_buttons[] = {
    assignment_dialog::cancel_label, assignment_dialog::override_replace_label, ""};

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
  display_revision_ = configuration_.revision();
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
  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    // Kept like any other setting: saved once the finger lifts.
    self->brightness_save_pending_ = true;
    self->save_brightness();
    return;
  }
  const auto value = lv_slider_get_value(self->brightness_slider_);
  self->normal_brightness_percent_ = static_cast<std::uint8_t>(value);
  self->display_.set_brightness(self->normal_brightness_percent_);
  self->dimmed_ = false;
  self->note_interaction(millis());
}

void UiService::save_brightness() {
  if (!brightness_save_pending_ || configuration_worker_.pending() != 0U) return;
  auto current = configuration_.versioned_snapshot();
  if (current.configuration.device.brightness_percent == normal_brightness_percent_) {
    brightness_save_pending_ = false;
    return;
  }
  current.configuration.device.brightness_percent = normal_brightness_percent_;
  brightness_save_pending_ = !configuration_worker_
      .submit_replace(std::move(current.configuration), current.revision, millis())
      .accepted;
}

// Brightness, dimming and sleep follow the saved settings, including changes
// made in the browser, without a restart.
void UiService::apply_display_settings() {
  const auto revision = configuration_.revision();
  if (revision == display_revision_) return;
  display_revision_ = revision;
  std::uint8_t brightness = normal_brightness_percent_;
  configuration_.visit([&](const config::Configuration& current, std::uint64_t) {
    brightness = current.device.brightness_percent;
    dim_after_ms_ = current.device.dim_after_ms;
    sleep_after_ms_ = current.device.sleep_after_ms;
  });
  const bool dragging = brightness_slider_ != nullptr &&
      lv_obj_has_state(brightness_slider_, LV_STATE_PRESSED);
  if (brightness == normal_brightness_percent_ || brightness_save_pending_ || dragging) return;
  normal_brightness_percent_ = brightness;
  if (!dimmed_ && !display_.sleeping()) display_.set_brightness(brightness);
  if (brightness_slider_ != nullptr)
    lv_slider_set_value(brightness_slider_, brightness, LV_ANIM_OFF);
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
  setup_printer_dropdown_ = nullptr;
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
  tag_spinner_=tag_bar_=tag_art_=nullptr;
  weight_update_=nullptr;weight_policy_=nullptr;
  display_test_touch_marker_ = nullptr;
  display_test_touch_label_ = nullptr;
  product_nav_buttons_.fill(nullptr);
  workflow_toolhead_buttons_.fill(nullptr);
  setup_printer_ids_.clear();
  setup_printer_signature_.clear();
  // Messages belong to the page they were shown on; a running scale request
  // writes its own again on the next refresh.
  if (active_page_ != feedback_page_) {
    workflow_feedback_.clear();
    feedback_terminal_ = false;
    feedback_page_ = active_page_;
  }
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
      display_test_touch_label_, lv_color_hex(warning_text), 0);
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
  const bool printers=printer_integration_enabled(configuration_);
  const std::int16_t slot_width=printers?96:120;
  std::int16_t slot=0;
  for(std::size_t i=0;i<labels.size();++i){
    // The Assign slot stays empty so the page indices do not move.
    if(i==static_cast<std::size_t>(ProductPage::printer)&&!printers)continue;
    const bool selected=i==static_cast<std::size_t>(active_page_);
    product_nav_buttons_[i]=product_button(screen,{static_cast<std::int16_t>(slot++*slot_width),layout::nav.y,slot_width,48},labels[i],navigation_callback,this);
    lv_obj_set_style_radius(product_nav_buttons_[i],0,0);
    lv_obj_set_style_bg_color(product_nav_buttons_[i],lv_color_hex(selected?0x303A3F:recessed_fill),0);
    lv_obj_set_style_text_color(product_nav_buttons_[i],lv_color_hex(selected?accent_text:muted_text),0);
  }
}

void UiService::build_home_page() {
  auto* screen=lv_scr_act();
  workflow_home_state_label_=product_label(screen,layout::home_state,"OpenTag Station");
  lv_obj_set_style_text_color(workflow_home_state_label_,lv_color_hex(accent_text),0);
  workflow_material_label_=product_label(screen,layout::home_material,"Place a spool",&lv_font_montserrat_20);
  workflow_identity_label_=product_label(screen,layout::home_identity,"Set a tagged spool on the station");
  lv_obj_set_style_text_color(workflow_identity_label_,lv_color_hex(muted_text),0);
  workflow_weight_label_=product_label(screen,layout::home_weight,"",&lv_font_montserrat_32);
  workflow_scale_indicator_=lv_obj_create(screen);place(workflow_scale_indicator_,layout::home_art);
  lv_obj_set_style_radius(workflow_scale_indicator_,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_color(workflow_scale_indicator_,lv_color_hex(0x788583),0);
  lv_obj_set_style_border_width(workflow_scale_indicator_,6,0);
  lv_obj_clear_flag(workflow_scale_indicator_,LV_OBJ_FLAG_SCROLLABLE);
  workflow_scale_gauge_=lv_obj_create(workflow_scale_indicator_);lv_obj_set_size(workflow_scale_gauge_,18,18);lv_obj_center(workflow_scale_gauge_);
  lv_obj_set_style_radius(workflow_scale_gauge_,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(workflow_scale_gauge_,lv_color_hex(screen_background),0);
  workflow_weigh_button_=product_button(screen,layout::home_weigh,"WEIGH",weigh_callback,this,true);
  if(printer_integration_enabled(configuration_)){
    product_button(screen,layout::home_assign,"ASSIGN",forward_navigation,product_nav_buttons_[2]);
    product_button(screen,layout::home_tag,"MANAGE TAG",forward_navigation,product_nav_buttons_[3]);
    product_button(screen,layout::home_more,"SETTINGS",forward_navigation,product_nav_buttons_[4]);
  }else{
    product_button(screen,layout::home_assign,"MANAGE TAG",forward_navigation,product_nav_buttons_[3]);
    product_button(screen,{16,218,448,46},"SETTINGS",forward_navigation,product_nav_buttons_[4]);
  }
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
                               lv_event_cb_t callback,
                               bool primary) {
    auto* button = lv_btn_create(screen);
    *output = button;
    lv_obj_set_size(button, 216, 48);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(
        button, lv_color_hex(primary ? primary_fill : neutral_fill), 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(
        label, lv_color_hex(primary ? 0x06201B : 0xE7FAF7), 0);
    lv_obj_center(label);
  };
  make_action(
      &workflow_weigh_button_, "WEIGH",
      weigh_callback, true);
  make_action(
      &workflow_tare_button_, "TARE EMPTY PLATFORM",
      tare_callback, false);
  lv_obj_add_flag(workflow_tare_button_,LV_OBJ_FLAG_HIDDEN);
  place(workflow_weigh_button_,layout::weigh);

  workflow_calibration_label_ = lv_label_create(screen);
  lv_label_set_text(workflow_calibration_label_, "CALIBRATION");
  lv_obj_set_pos(workflow_calibration_label_, 248, 46);
  lv_obj_set_style_text_color(
      workflow_calibration_label_, lv_color_hex(accent_text), 0);
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
      &workflow_calibrate_button_, "CALIBRATE",
      calibrate_callback, false);
  lv_obj_add_flag(workflow_calibrate_button_,LV_OBJ_FLAG_HIDDEN);

  workflow_calibration_close_button_ = lv_btn_create(screen);
  lv_obj_set_pos(workflow_calibration_close_button_, 248, 180);
  lv_obj_set_size(workflow_calibration_close_button_, 216, 48);
  lv_obj_set_style_radius(workflow_calibration_close_button_, 12, 0);
  lv_obj_set_style_bg_color(
      workflow_calibration_close_button_, lv_color_hex(recessed_fill), 0);
  lv_obj_add_event_cb(
      workflow_calibration_close_button_,
      scale_calibration_close_callback, LV_EVENT_CLICKED, this);
  auto* close_label = lv_label_create(workflow_calibration_close_button_);
  lv_label_set_text(close_label, "Close");
  lv_obj_set_style_text_color(close_label, lv_color_hex(0xD5E7E8), 0);
  lv_obj_center(close_label);
  lv_obj_add_flag(
      workflow_calibration_close_button_, LV_OBJ_FLAG_HIDDEN);

  weight_update_=lv_btn_create(screen);place(weight_update_,layout::update);lv_obj_set_style_radius(weight_update_,12,0);
  auto* update_label=lv_label_create(weight_update_);lv_label_set_text(update_label,"UPDATE SPOOLMAN");lv_obj_center(update_label);lv_obj_add_event_cb(weight_update_,weight_update_callback,LV_EVENT_CLICKED,this);
  style_action(weight_update_,neutral_fill,neutral_label);
  workflow_status_label_ = lv_label_create(screen);
  lv_label_set_text(
      workflow_status_label_, "Waiting for stable empty platform");
  place(workflow_status_label_,layout::feedback);lv_label_set_long_mode(workflow_status_label_,LV_LABEL_LONG_WRAP);
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
      workflow_identity_label_, lv_color_hex(muted_text), 0);
  place(workflow_identity_label_,layout::printer_spool);

  static constexpr std::array<layout::Box,5> boxes{layout::tool1,layout::tool2,layout::tool3,layout::tool4,layout::tool5};
  for (std::size_t index = 0U;
       index < workflow_toolhead_buttons_.size(); ++index) {
    auto* button = lv_btn_create(screen);
    workflow_toolhead_buttons_[index] = button;
    place(button,boxes[index]);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(neutral_fill), 0);
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
      workflow_status_label_, lv_color_hex(muted_text), 0);
  place(workflow_status_label_,layout::feedback);
}

void UiService::build_tags_page() {
  if(!tag_flow_)tag_flow_=network::make_external<TagFlow>([]{return TagFlow{};});
  auto* screen=lv_scr_act();
  tag_title_=product_label(screen,{16,8,448,32},"Manage tag",&lv_font_montserrat_20);
  tag_details_=lv_obj_create(screen);place(tag_details_,{16,48,448,208});
  lv_obj_set_style_bg_opa(tag_details_,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_width(tag_details_,0,0);lv_obj_set_style_pad_all(tag_details_,0,0);
  lv_obj_set_scroll_dir(tag_details_,LV_DIR_VER);
  nfc_detail_=product_label(tag_details_,{0,0,440,160},"Loading tag...");
  lv_label_set_long_mode(nfc_detail_,LV_LABEL_LONG_WRAP);
  lv_obj_set_height(nfc_detail_,LV_SIZE_CONTENT);
  for(auto& button:tag_buttons_)button=product_button(screen,{16,212,448,44},"",tag_action_callback,this);
  // Built once per page and only shown/restyled by draw_tags: no per-refresh
  // LVGL allocation. Hidden objects are not redrawn; the spinner's small animation timer keeps running.
  tag_spinner_=lv_spinner_create(screen,1000,60);place(tag_spinner_,layout::tag_spinner);
  lv_obj_set_style_arc_width(tag_spinner_,6,LV_PART_MAIN);lv_obj_set_style_arc_width(tag_spinner_,6,LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(tag_spinner_,lv_color_hex(neutral_fill),LV_PART_MAIN);
  lv_obj_set_style_arc_color(tag_spinner_,lv_color_hex(accent_text),LV_PART_INDICATOR);
  lv_obj_add_flag(tag_spinner_,LV_OBJ_FLAG_HIDDEN);
  tag_bar_=lv_bar_create(screen);place(tag_bar_,layout::tag_bar);
  lv_obj_set_style_bg_color(tag_bar_,lv_color_hex(neutral_fill),LV_PART_MAIN);
  lv_obj_set_style_bg_color(tag_bar_,lv_color_hex(accent_text),LV_PART_INDICATOR);
  lv_obj_add_flag(tag_bar_,LV_OBJ_FLAG_HIDDEN);
  // Spool ring like the Home art: the filament colour with a dark hub.
  tag_art_=lv_obj_create(screen);place(tag_art_,layout::tag_art);
  lv_obj_set_style_radius(tag_art_,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_color(tag_art_,lv_color_hex(0x788583),0);
  lv_obj_set_style_border_width(tag_art_,5,0);lv_obj_set_style_pad_all(tag_art_,0,0);
  lv_obj_clear_flag(tag_art_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_clear_flag(tag_art_,LV_OBJ_FLAG_CLICKABLE);
  auto* hub=lv_obj_create(tag_art_);lv_obj_set_size(hub,16,16);lv_obj_center(hub);
  lv_obj_set_style_radius(hub,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(hub,lv_color_hex(screen_background),0);
  lv_obj_set_style_border_width(hub,0,0);lv_obj_clear_flag(hub,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(tag_art_,LV_OBJ_FLAG_HIDDEN);
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
  else tag_flow_->begin_wait(receipt.operation_id,millis());
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
  if(!tag_flow_)return;
  if(tag_flow_->waiting) {
    // Only a stalled Spoolman-only request may be left; physical writes never.
    if(action!=TagAction::home||!tag_flow_->stalled||physical_phase(tag_flow_->phase))return;
    tag_flow_->abandon_wait();
  }
  if(action==TagAction::home||action==TagAction::weigh||action==TagAction::printer) {
    tag_flow_->leave_page();
    active_page_=action==TagAction::weigh?ProductPage::scale:action==TagAction::printer?ProductPage::printer:ProductPage::home;
    build_current_screen();
    // "WEIGH" on the tag-ready page means weigh now, not just open the page.
    if(action==TagAction::weigh&&diagnostics_.scale_snapshot().scale_calibrated)start_weigh();
    return;
  }
  if(action==TagAction::search) {
    InputSpec spec;spec.title="Search "+(tag_flow_->entity=="community"?std::string("Community"):tag_flow_->entity=="spool"?"My Spools":"My Filaments");
    spec.initial=tag_flow_->query;spec.action="SEARCH";spec.required=tag_flow_->entity=="community";
    open_input(spec,[this](const std::string& text){tag_command(tag_flow_->search(text));});return;
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
    spec.initial=action==TagAction::tare&&!tag_flow_->tare_known?std::string():spec.initial;
    open_input(spec,[this,action](const std::string& text){const auto n=std::strtod(text.c_str(),nullptr);if(action==TagAction::initial)tag_flow_->initial=n;else if(action==TagAction::remaining)tag_flow_->remaining=n;else{tag_flow_->tare=n;tag_flow_->tare_known=true;}draw_tags();});return;
  }
  tag_command(tag_flow_->act(action));
  if(tag_flow_->confirm_spool_id>0) {
    const auto spool=tag_flow_->confirm_spool_id;tag_flow_->confirm_spool_id=0;
    const auto receipt=backend_worker_.submit_spool_confirmation(spool,tag_candidate_generation_);
    tag_flow_->message=receipt.accepted?"Linked to spool #"+std::to_string(spool)+".":"Station is busy. Please try again.";
  }
  draw_tags();
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
    if(item.enabled)lv_obj_clear_state(b,LV_STATE_DISABLED);else lv_obj_add_state(b,LV_STATE_DISABLED);
    if(item.destructive)style_action(b,destructive_fill,destructive_label);
    else if(item.primary)style_action(b,primary_fill,primary_label);
    else style_action(b,neutral_fill,neutral_label);
  }
  const auto indicator=tag_screen_.indicator;
  if(indicator==TagIndicator::spinner)lv_obj_clear_flag(tag_spinner_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(tag_spinner_,LV_OBJ_FLAG_HIDDEN);
  if(indicator==TagIndicator::bar) {
    lv_bar_set_range(tag_bar_,0,tag_screen_.total>0?tag_screen_.total:1);
    lv_bar_set_value(tag_bar_,tag_screen_.done,LV_ANIM_OFF);
    lv_obj_clear_flag(tag_bar_,LV_OBJ_FLAG_HIDDEN);
  } else lv_obj_add_flag(tag_bar_,LV_OBJ_FLAG_HIDDEN);
  if(tag_screen_.art) {
    lv_obj_set_style_bg_color(tag_art_,lv_color_hex(tag_screen_.art_rgb),0);
    lv_obj_clear_flag(tag_art_,LV_OBJ_FLAG_HIDDEN);
  } else lv_obj_add_flag(tag_art_,LV_OBJ_FLAG_HIDDEN);
}
void UiService::refresh_tags() {
  if(!tag_flow_||!nfc_detail_)return;
  auto& flow=*tag_flow_;
  const auto tag=nfc_.snapshot();
  flow.observe_tag(tag.uid?tag.uid->hex():std::string(),tag.error?&tag.error->message:nullptr);
  workflow_.visit([&](const services::WorkflowSnapshot& current) {
    flow.current_spool=tag.tag&&tag.uid&&current.openprinttag_available&&current.uid==*tag.uid&&current.spool?current.spool->id:0;
    // As on Home: "not linked" only once Spoolman answered for this tag.
    const bool this_tag=tag.uid&&current.uid==*tag.uid;
    flow.lookup=this_tag&&(current.stage==services::WorkflowStage::spool_selection_required||current.stage==services::WorkflowStage::spool_not_found)?SpoolLookup::checked:
        this_tag&&current.stage==services::WorkflowStage::spool_resolution_unavailable?
            (current.lookup_failure_is_connection()||!current.lookup_error?SpoolLookup::unavailable:SpoolLookup::failed):SpoolLookup::resolving;
    flow.lookup_error=flow.lookup==SpoolLookup::failed?current.lookup_error->message:std::string();
    // Product/metadata matches must be chosen explicitly (never auto-linked).
    flow.candidates.clear();
    if(tag.uid&&current.uid==*tag.uid&&current.stage==services::WorkflowStage::spool_selection_required) {
      for(const auto& candidate:current.spool_candidates)flow.candidates.emplace_back(candidate.id,candidate.display_name);
      tag_candidate_generation_=current.spool_generation;
    }
  });
  flow.material=tag.tag?tag.tag->decoded.material.material_name.value_or("Filament spool"):"";
  flow.lifecycle=services::tag_lifecycle({tag.present,tag.blank_compatible,bool(tag.tag),flow.current_spool>0,tag.state==nfc::ReadState::unsupported,flow.phase,false});
  flow.multiple_tags=tag.state==nfc::ReadState::multiple;
  auto body=backend_worker_.writer_snapshot();
  const auto checksum=nfc::nfcv::diagnostic_checksum(reinterpret_cast<const std::uint8_t*>(body.data()),body.size());
  if(checksum!=writer_view_checksum_) {
    if(flow.consume(body))writer_view_checksum_=checksum;
    // A spool this screen just created goes straight to its tag review.
    tag_command(flow.take_auto_preview());
  }
  if(flow.operation) {
    const auto op=backend_worker_.writer_operation(flow.operation);
    if(op&&op->state==application::OperationState::failed)flow.operation_failed(op->error?op->error->message:op->message);
  }
  flow.update_stall(millis());
  draw_tags();
}

void UiService::weight_update_callback(lv_event_t* event) {
  auto* self=static_cast<UiService*>(lv_event_get_user_data(event));const auto measured=self->backend_worker_.weigh_snapshot();
  // Only a measurement of the spool now on the station; the backend fences too.
  if(weigh_view(measured,self->weigh_station()).can_save)(void)self->backend_worker_.submit_weight_update(measured.measurement_id);
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
  // The live value, which may be ahead of a save still in progress.
  lv_slider_set_range(brightness,5,100);lv_slider_set_value(brightness,normal_brightness_percent_,LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness,brightness_callback,LV_EVENT_VALUE_CHANGED,this);
  lv_obj_add_event_cb(brightness,brightness_callback,LV_EVENT_RELEASED,this);
  weight_policy_=product_button(screen,layout::policy,"Auto-update weight",weight_policy_callback,this);
  product_button(screen,layout::calibration,"Calibrate scale",[](lv_event_t* e){
    auto* self=static_cast<UiService*>(lv_event_get_user_data(e));self->active_page_=ProductPage::scale;self->build_current_screen();self->set_scale_calibration_panel_open(true);
  },this);
  product_button(screen,layout::about,"About / Advanced",diagnostics_toggle_callback,this);
  // Change Wi-Fi, Spoolman or FilaBridge after first-run setup.
  product_button(screen,layout::network,"Wi-Fi & services",[](lv_event_t* e){
    auto* self=static_cast<UiService*>(lv_event_get_user_data(e));
    (void)self->first_run_setup_.go_to(services::SetupStep::wifi);
    self->showing_setup_=true;self->showing_diagnostics_=false;self->setup_feedback_.clear();
    self->build_current_screen();
  },this);
  workflow_status_label_=product_label(screen,layout::wifi,"Wi-Fi status");
  lv_label_set_long_mode(workflow_status_label_,LV_LABEL_LONG_WRAP);
}

void UiService::build_workflow_screen() {
  auto* screen = lv_scr_act();
  style_screen(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(screen_background), 0);
  if(active_page_==ProductPage::printer&&!printer_integration_enabled(configuration_))active_page_=ProductPage::home;
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
  lv_label_set_text(title, "About this station");
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

  // One scrolling column of short "name: value" lines in a 16 px font, with a
  // heading per section. The former four-column 14 px block was unreadable.
  auto* details = lv_obj_create(screen);
  lv_obj_set_size(details, 460, 214);
  lv_obj_align(details, LV_ALIGN_TOP_MID, 0, 42);
  lv_obj_set_style_bg_color(details, lv_color_hex(recessed_fill), 0);
  lv_obj_set_style_border_width(details, 0, 0);
  lv_obj_set_style_radius(details, 10, 0);
  lv_obj_set_style_pad_all(details, 12, 0);
  lv_obj_set_style_pad_row(details, 4, 0);
  lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(details, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(details, LV_SCROLLBAR_MODE_ACTIVE);
  const auto add_section = [details](const char* heading) {
    auto* title_label = lv_label_create(details);
    lv_label_set_text(title_label, heading);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(accent_text), 0);
    lv_obj_set_style_pad_top(title_label, 6, 0);
    auto* body = lv_label_create(details);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(primary_text), 0);
    lv_obj_set_style_text_line_space(body, 4, 0);
    lv_label_set_text(body, "");
    return body;
  };
  storage_label_ = add_section("NETWORK");
  touch_label_ = add_section("SCALE AND SCREEN");
  system_label_ = add_section("SYSTEM");
  memory_label_ = add_section("MEMORY");

  // Brightness is set on the Settings page (one full-size slider).

  auto* sleep_button = lv_btn_create(screen);
  lv_obj_set_size(sleep_button, 104, 44);
  lv_obj_align(sleep_button, LV_ALIGN_BOTTOM_RIGHT, -14, -10);
  lv_obj_add_event_cb(sleep_button, sleep_callback, LV_EVENT_CLICKED, this);
  auto* sleep_label = lv_label_create(sleep_button);
  lv_label_set_text(sleep_label, "Sleep");
  lv_obj_center(sleep_label);
  style_role(sleep_button, false);

  auto* setup_button = lv_btn_create(screen);
  lv_obj_set_size(setup_button, 104, 44);
  lv_obj_align(setup_button, LV_ALIGN_BOTTOM_RIGHT, -130, -10);
  lv_obj_add_event_cb(
      setup_button, setup_toggle_callback, LV_EVENT_CLICKED, this);
  auto* setup_label = lv_label_create(setup_button);
  lv_label_set_text(setup_label, "Setup");
  lv_obj_center(setup_label);
  style_role(setup_button, false);

  auto* main_button = lv_btn_create(screen);
  lv_obj_set_size(main_button, 104, 44);
  lv_obj_align(main_button, LV_ALIGN_BOTTOM_RIGHT, -246, -10);
  lv_obj_add_event_cb(
      main_button, diagnostics_toggle_callback, LV_EVENT_CLICKED, this);
  auto* main_label = lv_label_create(main_button);
  lv_label_set_text(main_label, "Home");
  lv_obj_center(main_label);
  style_role(main_button, true);
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
    const std::string options = wifi_options(networks);
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
        146,
        "Printer ID",
        configured.filabridge.selected_printer_id,
        128U,
        false);
    // Offer the printers FilaBridge reported instead of asking the user to
    // know an internal ID.
    setup_printer_dropdown_ = lv_dropdown_create(screen);
    lv_obj_set_size(setup_printer_dropdown_, 286, 44);
    lv_obj_align(setup_printer_dropdown_, LV_ALIGN_TOP_LEFT, 16, 94);
    // Shows the configured printer; refresh_setup() rebuilds the list when
    // FilaBridge reports a different set of printers.
    rebuild_setup_printers();
    lv_obj_add_event_cb(setup_printer_dropdown_, [](lv_event_t* event) {
      static_cast<UiService*>(lv_event_get_user_data(event))->sync_setup_printer_field();
    }, LV_EVENT_VALUE_CHANGED, this);
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
                               lv_event_cb_t callback,
                               bool primary) {
    auto* button = lv_btn_create(screen);
    lv_obj_set_size(button, 110, 46);
    lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, x, -10);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    style_role(button, primary);
    return button;
  };
  make_button("Back", 14, setup_back_callback, false);
  make_button("Home", 185, setup_toggle_callback, false);
  make_button(
      step == services::SetupStep::ready ? "Finish" : "Next",
      356,
      setup_next_callback, true);
  if (setup_input_one_ != nullptr) {
    auto* save_button = lv_btn_create(screen);
    lv_obj_set_size(save_button, 156, 46);
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
    style_role(save_button, true);
  }
  if (step == services::SetupStep::scale_calibration) {
    auto* calibrate = lv_btn_create(screen);
    lv_obj_set_size(calibrate, 220, 46);
    lv_obj_align(calibrate, LV_ALIGN_TOP_LEFT, 14, 140);
    lv_obj_add_event_cb(calibrate, [](lv_event_t* event) {
      auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
      self->return_to_setup_after_calibration_ = true;
      self->showing_setup_ = false;
      self->active_page_ = ProductPage::scale;
      self->build_current_screen();
      self->set_scale_calibration_panel_open(true);
      self->refresh_workflow();
    }, LV_EVENT_CLICKED, this);
    auto* label = lv_label_create(calibrate);
    lv_label_set_text(label, "CALIBRATE NOW");
    lv_obj_center(label);
    style_role(calibrate, !configured.scale_calibration.has_value());
  }
  if (step == services::SetupStep::wifi) {
    auto* scan_button = lv_btn_create(screen);
    lv_obj_set_size(scan_button, 156, 46);
    lv_obj_align(scan_button, LV_ALIGN_TOP_RIGHT, -14, 146);
    lv_obj_add_event_cb(
        scan_button, setup_scan_callback, LV_EVENT_CLICKED, this);
    auto* scan_label = lv_label_create(scan_button);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);
    style_role(scan_button, false);
  }


}

void UiService::setup_back_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->setup_feedback_.clear();
  self->setup_advance_after_save_ = false;
  const auto result = self->first_run_setup_.previous();
  if (result.ok()) self->build_current_screen();
}

void UiService::setup_next_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  // A save is still running: Next continues once it has finished.
  if (self->setup_save_operation_.has_value()) return;
  const auto step = self->first_run_setup_.current();
  if (step == services::SetupStep::printer_selection) self->sync_setup_printer_field();
  {
    // Next means "save and continue": never silently drop typed values.
    const auto stored = self->configuration_.snapshot();
    const std::string first = self->setup_input_one_ != nullptr
        ? lv_textarea_get_text(self->setup_input_one_) : "";
    const std::string second = self->setup_input_two_ != nullptr
        ? lv_textarea_get_text(self->setup_input_two_) : "";
    const bool dirty =
        (step == services::SetupStep::wifi &&
         (first != stored.wifi.ssid || second != stored.wifi.password)) ||
        (step == services::SetupStep::spoolman &&
         (first != stored.spoolman.url || second != stored.spoolman.authentication_token)) ||
        (step == services::SetupStep::filabridge &&
         (first != stored.filabridge.url || second != stored.filabridge.authentication_token)) ||
        (step == services::SetupStep::printer_selection &&
         first != stored.filabridge.selected_printer_id) ||
        (step == services::SetupStep::ready && !first.empty());
    if (dirty) {
      // Next means "save and continue", and continues only once the save
      // succeeded; a failed save keeps this step open with the reason.
      self->setup_advance_after_save_ = self->submit_setup_save();
      self->refresh_setup();
      return;
    }
  }
  self->finish_setup_step();
}

void UiService::finish_setup_step() {
  auto* self = this;
  const auto step = first_run_setup_.current();
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
      self->setup_feedback_ = "The station is busy. Try again in a moment.";
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
  self->setup_advance_after_save_ = false;
  self->first_run_gate_ = false;
  self->showing_diagnostics_ = false;
  self->setup_feedback_.clear();
  self->build_current_screen();
  if (!self->showing_setup_) self->refresh_current(millis());
}

void UiService::navigation_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->return_to_setup_after_calibration_ = false;
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
  if(self->active_page_==ProductPage::home&&self->home_shows_blank_tag()){self->active_page_=ProductPage::tags;self->build_current_screen();self->tag_action(TagAction::sources);return;}
  if (self->active_page_ == ProductPage::home &&
      !self->diagnostics_.scale_snapshot().scale_calibrated) {
    self->active_page_ = ProductPage::scale;
    self->workflow_feedback_.clear();
    self->build_current_screen();
    self->set_scale_calibration_panel_open(true);
    self->refresh_workflow();
    return;
  }
  if (self->active_page_ == ProductPage::home) {
    self->active_page_ = ProductPage::scale;
    self->build_current_screen();
  }
  self->start_weigh();
}

void UiService::start_weigh() {
  const auto receipt = scale_commands_.submit_weigh(millis());
  feedback_terminal_ = !receipt.accepted;
  if (receipt.accepted) {
    weigh_operation_id_ = receipt.operation_id;
    scale_action_ = ScaleAction::weigh;
    workflow_feedback_ = "Measuring. Keep the spool still.";
  } else {
    weigh_operation_id_.reset();
    scale_action_ = ScaleAction::none;
    // A refused weigh carries its reason (for example a weight still being
    // saved to Spoolman) on its failed operation.
    const auto refused = receipt.operation_id != 0U
        ? backend_worker_.writer_operation(receipt.operation_id)
        : std::nullopt;
    workflow_feedback_ = refused && refused->error
        ? refused->error->message
        : "The scale is busy. Try again in a moment.";
    feedback_measurement_id_ = backend_worker_.weigh_snapshot().measurement_id;
  }
  refresh_workflow();
}

void UiService::tare_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  const auto receipt = self->scale_commands_.submit_tare(millis());
  self->feedback_terminal_ = !receipt.accepted;
  self->feedback_measurement_id_ = self->backend_worker_.weigh_snapshot().measurement_id;
  if (receipt.accepted) {
    self->weigh_operation_id_ = receipt.operation_id;
    self->scale_action_ = ScaleAction::tare;
    self->workflow_feedback_ = "Taring empty platform";
  } else {
    self->weigh_operation_id_.reset();
    self->scale_action_ = ScaleAction::none;
    self->workflow_feedback_ = "The scale is busy. Try TARE again in a moment.";
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
    self->feedback_terminal_ = true;
    self->feedback_measurement_id_ = self->backend_worker_.weigh_snapshot().measurement_id;
    self->refresh_workflow();
    return;
  }
  const auto receipt =
      self->scale_commands_.submit_calibration(reference, millis());
  self->feedback_terminal_ = !receipt.accepted;
  self->feedback_measurement_id_ = self->backend_worker_.weigh_snapshot().measurement_id;
  if (receipt.accepted) {
    self->weigh_operation_id_ = receipt.operation_id;
    self->scale_action_ = ScaleAction::calibrate;
    self->workflow_feedback_ = "Calibrating";
  } else {
    self->weigh_operation_id_.reset();
    self->scale_action_ = ScaleAction::none;
    self->workflow_feedback_ = "The scale is busy. Try calibrating again in a moment.";
  }
  self->refresh_workflow();
}

void UiService::scale_calibration_close_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->set_scale_calibration_panel_open(false);
  if (self->return_to_setup_after_calibration_) {
    self->return_to_setup_after_calibration_ = false;
    self->showing_setup_ = true;
    self->build_current_screen();
    return;
  }
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
    self->workflow_feedback_ = !state.spool.has_value()
        ? "Place a spool that is linked in Spoolman on the station first."
        : "Choose a printer in Settings > Wi-Fi & services first.";
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
    message = active ? "This printer is actively printing and "
                     : "This printer state cannot be verified and ";
    message +=
        toolhead->display_name + " contains spool #" +
        std::to_string(*toolhead->assigned_spool) +
        ". Replacing it may corrupt consumption accounting.";
    buttons = override_replacement_buttons;
  } else if (active || state_unverified) {
    message = active
                  ? "This printer is actively printing. Mapping "
                  : "This printer state cannot be verified. Mapping ";
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
  auto* choices = lv_msgbox_get_btns(message_box);
  lv_obj_set_style_min_height(choices,48,0);
  // Cancel is neutral. The confirming choice is mint for a plain assignment
  // and red when it replaces a spool or overrides an active print. LVGL draws
  // a button with the CHECKED control in the items' checked style.
  lv_obj_set_style_bg_color(choices, lv_color_hex(neutral_fill), LV_PART_ITEMS);
  lv_obj_set_style_text_color(choices, lv_color_hex(neutral_label), LV_PART_ITEMS);
  const bool plain_assign = buttons == assignment_buttons;
  lv_obj_set_style_bg_color(
      choices, lv_color_hex(plain_assign ? primary_fill : destructive_fill),
      LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(
      choices, lv_color_hex(plain_assign ? primary_label : destructive_label),
      LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_btnmatrix_set_btn_ctrl(choices, 1, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_center(message_box);
}

void UiService::assignment_confirmation_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  auto* message_box = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  const char* action = lv_msgbox_get_active_btn_text(message_box);
  if (assignment_dialog::is_confirmation(action)) {
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
        !receipt.accepted ? "Station is busy. Try the assignment again."
        : self->pending_active_override_ ? "Assigning anyway; checking with FilaBridge..."
                                         : "Assigning; checking with FilaBridge...";
    // Followed until it ends (refresh_printer_page), then replaced by the result.
    if (receipt.accepted) self->assignment_operation_id_ = receipt.operation_id;
    else self->assignment_operation_id_.reset();
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
  if (self->setup_save_operation_.has_value()) return;
  if (self->first_run_setup_.current() == services::SetupStep::printer_selection)
    self->sync_setup_printer_field();
  self->setup_advance_after_save_ = false;
  (void)self->submit_setup_save();
  self->refresh_setup();
}

bool UiService::submit_setup_save() {
  auto* self = this;
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
    self->setup_feedback_ = "The station is busy. Try Save again in a moment.";
    return false;
  }
  self->setup_save_operation_ = receipt.operation_id;
  self->setup_feedback_ = step == services::SetupStep::wifi
                              ? "Saving and connecting to Wi-Fi..."
                              : "Saving...";
  return true;
}

// The printer list in setup, with the configured printer selected. A
// configured printer FilaBridge does not report stays listed (and selected)
// so it is never silently replaced by the first entry.
void UiService::rebuild_setup_printers() {
  if (setup_printer_dropdown_ == nullptr || setup_input_one_ == nullptr) return;
  const std::string chosen = lv_textarea_get_text(setup_input_one_);
  std::string options;
  std::vector<std::string> ids;
  setup_printer_signature_.clear();
  workflow_.visit([&](const services::WorkflowSnapshot& current) {
    for (const auto& printer : current.printers) {
      ids.push_back(printer.id);
      if (!options.empty()) options += '\n';
      options += printer.display_name.empty() ? printer.id : printer.display_name;
      setup_printer_signature_ += printer.id + '\x1f' + printer.display_name + '\n';
    }
  });
  if (!chosen.empty() && std::find(ids.begin(), ids.end(), chosen) == ids.end()) {
    ids.insert(ids.begin(), chosen);
    options = chosen + " (not found)" + (options.empty() ? "" : "\n" + options);
  }
  if (options.empty()) options = "No printers found yet";
  lv_dropdown_set_options(setup_printer_dropdown_, options.c_str());
  const auto selected = std::find(ids.begin(), ids.end(), chosen);
  lv_dropdown_set_selected(
      setup_printer_dropdown_,
      selected == ids.end() ? 0U : static_cast<std::uint16_t>(selected - ids.begin()));
  setup_printer_ids_ = std::move(ids);
}

// The printer shown in the list is the one saved, unless the user typed an
// ID FilaBridge does not report.
void UiService::sync_setup_printer_field() {
  if (setup_printer_dropdown_ == nullptr || setup_input_one_ == nullptr ||
      setup_printer_ids_.empty()) return;
  const std::string typed = lv_textarea_get_text(setup_input_one_);
  if (!typed.empty() &&
      std::find(setup_printer_ids_.begin(), setup_printer_ids_.end(), typed) ==
          setup_printer_ids_.end()) return;
  const auto index = lv_dropdown_get_selected(setup_printer_dropdown_);
  if (index < setup_printer_ids_.size())
    lv_textarea_set_text(setup_input_one_, setup_printer_ids_[index].c_str());
}

void UiService::setup_scan_callback(lv_event_t* event) {
  auto* self = static_cast<UiService*>(lv_event_get_user_data(event));
  self->network_.request_scan();
  self->setup_feedback_ = "Looking for Wi-Fi networks...";
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
  const auto step = first_run_setup_.current();
  if (setup_save_operation_.has_value()) {
    const auto operation = backend_worker_.writer_operation(*setup_save_operation_);
    const bool finished = operation.has_value()
        ? operation->state == application::OperationState::succeeded ||
              operation->state == application::OperationState::failed
        : configuration_worker_.pending() == 0U;
    if (finished) {
      const bool saved = operation.has_value()
          ? operation->state == application::OperationState::succeeded
          : configuration_worker_.last_operation_succeeded();
      const bool advance = setup_advance_after_save_;
      setup_save_operation_.reset();
      setup_advance_after_save_ = false;
      if (saved) {
        setup_feedback_ = "Saved";
        if (advance) {
          finish_setup_step();
          return;
        }
      } else {
        setup_feedback_ = plain_save_error(
            operation.has_value() && operation->error.has_value() ? &*operation->error : nullptr,
            step);
      }
    }
  }
  if (setup_printer_dropdown_ != nullptr) {
    std::string signature;
    workflow_.visit([&](const services::WorkflowSnapshot& current) {
      for (const auto& printer : current.printers)
        signature += printer.id + '\x1f' + printer.display_name + '\n';
    });
    if (signature != setup_printer_signature_) rebuild_setup_printers();
  }
  const auto configured = configuration_.snapshot();
  const auto network = diagnostics_.snapshot(millis());
  lv_label_set_text_fmt(
      setup_progress_label_, "Step %u of %u",
      static_cast<unsigned>(first_run_setup_.step_position(step)),
      static_cast<unsigned>(first_run_setup_.step_total()));

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
      body = "FILABRIDGE (OPTIONAL)\nOnly needed to assign spools to printer toolheads.\nLeave empty and tap Next to skip it.";
      break;
    case services::SetupStep::printer_selection:
      body = "PRINTER\nChoose the printer FilaBridge found.";
      break;
    case services::SetupStep::scale_calibration:
      body = "SCALE CALIBRATION\nHave a known weight ready, then tap CALIBRATE NOW.\nMost accurate after 15 minutes switched on.";
      break;
    case services::SetupStep::nfc_status:
      body = "NFC READER\nPlace a tag on the reader to check that it works.";
      break;
    case services::SetupStep::ready:
      body = "READY\nOptional: set an access token. Without one, anyone\non your network can control and update this station.";
      break;
  }
  lv_label_set_text(setup_body_label_, body);
  if (setup_network_dropdown_ != nullptr &&
      setup_scan_generation_ != network.wifi_scan_generation) {
    const auto networks = network_.scan_results();
    const std::string options = wifi_options(networks);
    lv_dropdown_set_options(setup_network_dropdown_, options.c_str());
    setup_scan_generation_ = network.wifi_scan_generation;
  }
  std::string status;
  switch (step) {
    case services::SetupStep::wifi:
      if (network.provisioning_active && !network.setup_ap_ssid.empty()) {
        // Short lines that fit above the Back/Home/Next buttons. The setup
        // network is open, so there is no password to show.
        status = "Setup Wi-Fi: " + network.setup_ap_ssid + " (no password)" +
            (network.wifi_last_error ? "\n" + network.wifi_last_error->message
             : setup_feedback_.empty() ? std::string("\nOr join it and open 192.168.4.1") : std::string());
      } else {
        status = std::string("State: ") + network::to_string(network.wifi_state) +
            "  scan results: " + std::to_string(network_.scan_results().size());
        if (network.wifi_connected) status += "\nIP: " + network.ip_address;
        else if (network.wifi_last_error) status += "\n" + network.wifi_last_error->message;
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
      status = nfc_status_text(nfc_.snapshot());
      break;
    case services::SetupStep::ready:
      if (web::local_access_policy(configured.web.access_token)
              .authentication_enabled) {
        status = "Local API authentication: ENABLED\n"
                 "Local browser control: ENABLED";
      } else {
        status = "Local API authentication: DISABLED\n"
                 "Local browser control: ENABLED\n"
                 "Trusted LAN mode - set an API token in Configuration to require authentication.";
      }
      if (first_run_setup_.complete()) status += "\nSetup is complete.";
      break;
    case services::SetupStep::welcome:
      status = first_run_setup_.complete()
                   ? "This station is already set up.\nTap Next to review a step, or Home to leave."
                   : "Tap Next to start.";
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

  if (track_scale_operation()) {
    return;
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
    refresh_home_page(scale, busy);
    return;
  }

  if (active_page_ == ProductPage::scale) {
    const auto measured = backend_worker_.weigh_snapshot();
    const auto view = weigh_view(measured, weigh_station());
    // A finished tare/calibration/weigh result stays until the next action,
    // a page change or a new measurement.
    if (feedback_terminal_ && measured.measurement_id != feedback_measurement_id_) {
      feedback_terminal_ = false;
      workflow_feedback_.clear();
    }
    const bool show_feedback = (busy || feedback_terminal_) && !workflow_feedback_.empty();
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
    switch (scale.scale_measurement_state) {
      case services::ScaleMeasurementState::settling:
        gauge_color = scale.scale_samples_in_filter < 3U
            ? 0x72DFBE : 0xF4C95D;
        break;
      case services::ScaleMeasurementState::ready:
      case services::ScaleMeasurementState::completed:
        gauge_color = 0x22C55E;
        break;
      case services::ScaleMeasurementState::timed_out:
      case services::ScaleMeasurementState::failed:
        gauge_color = 0xEF4444;
        break;
      case services::ScaleMeasurementState::idle:
        break;
    }
    if (!scale.scale_adc_ready) {
      gauge_color = 0xEF4444;
    } else if (!scale.scale_calibrated && !measurement_active) {
      gauge_color = 0xF4C95D;
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
    // A Spoolman save in progress refuses a new weigh.
    set_enabled(
        workflow_weigh_button_,
        scale.scale_adc_ready && scale.scale_calibrated && !busy && !view.saving);
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
      guidance = "3. Tare complete - place reference weight";
    } else if (!scale.scale_raw_stable) {
      guidance = "4. Waiting for stable reference weight";
    } else if (!reference_valid) {
      guidance = "Enter the known reference mass";
    } else {
      guidance = "5. Ready to calibrate";
    }
    if (show_feedback) {
      guidance = workflow_feedback_;
    }
    lv_label_set_text(workflow_status_label_, guidance.c_str());
    // The name, receipt and save offer belong to the spool now on the
    // station; after a swap the previous spool's result is not shown.
    if(workflow_material_label_)lv_label_set_text(workflow_material_label_,view.title.c_str());
    const bool can_save=!busy&&view.can_save;
    if(!scale_calibration_panel_open_){
      const auto receipt=view.show_receipt?"Empty spool  "+weigh_grams(measured.tare)+"\nFilament now  "+weigh_grams(measured.measured)+"\nIn Spoolman  "+weigh_grams(measured.canonical_remaining):std::string();
      lv_label_set_text(workflow_scale_capture_label_,receipt.c_str());
      std::string message=!view.saving&&show_feedback?workflow_feedback_:view.message;
      if(message.size()>150U)message.resize(150U);
      lv_label_set_text(workflow_status_label_,message.c_str());
      // "Again" only once there is a measurement of this spool to repeat.
      lv_label_set_text(lv_obj_get_child(workflow_weigh_button_,0),busy?"WEIGHING...":view.weigh_label);
    }
    set_enabled(weight_update_,can_save);
    // The next expected step is the highlighted one: save once a weight is
    // ready, otherwise weigh.
    if(scale.scale_calibrated){
      style_action(weight_update_,can_save?primary_fill:neutral_fill,can_save?primary_label:neutral_label);
      style_action(workflow_weigh_button_,can_save?neutral_fill:primary_fill,can_save?neutral_label:primary_label);
    }
    return;
  }

  if (active_page_ == ProductPage::printer) {
    refresh_printer_page();
    return;
  }

  if (active_page_ == ProductPage::settings &&
      workflow_status_label_ != nullptr) {
    refresh_settings_page();
  }
}

bool UiService::track_scale_operation() {
  if (weigh_operation_id_.has_value()) {
    const auto operation = scale_commands_.operation(*weigh_operation_id_);
    const char* action = scale_action_ == ScaleAction::tare
        ? "Tare"
        : scale_action_ == ScaleAction::calibrate ? "Calibration" : "Weigh";
    if (!operation.has_value()) {
      workflow_feedback_ = std::string(action) +
          " result unknown. Try again if needed.";
      weigh_operation_id_.reset();
      scale_action_ = ScaleAction::none;
      feedback_terminal_ = true;
      feedback_measurement_id_ = backend_worker_.weigh_snapshot().measurement_id;
    } else if (operation->state == application::OperationState::queued ||
               operation->state == application::OperationState::running) {
      workflow_feedback_ = operation->message.empty()
          ? std::string(action) + " in progress"
          : operation->message;
    } else if (operation->state == application::OperationState::succeeded) {
      const bool calibration_complete =
          scale_action_ == ScaleAction::calibrate;
      // A weigh shows its own result (the receipt); tare and calibration
      // results stay visible until the next action or page change. A tare
      // inside the calibration panel is a step, not a result: the numbered
      // guidance ("3. Tare complete - place reference weight") takes over.
      feedback_terminal_ = scale_action_ == ScaleAction::calibrate ||
          (scale_action_ == ScaleAction::tare && !scale_calibration_panel_open_);
      if (feedback_terminal_) workflow_feedback_ = std::string(action) + " complete";
      else workflow_feedback_.clear();
      feedback_measurement_id_ = backend_worker_.weigh_snapshot().measurement_id;
      weigh_operation_id_.reset();
      scale_action_ = ScaleAction::none;
      if (calibration_complete) {
        set_scale_calibration_panel_open(false);
        if (return_to_setup_after_calibration_) {
          return_to_setup_after_calibration_ = false;
          showing_setup_ = true;
          build_current_screen();
          return true;
        }
      }
    } else {
      workflow_feedback_ = operation->error.has_value()
          ? std::string(action) + " failed: " + operation->error->message
          : std::string(action) + " did not complete";
      feedback_terminal_ = true;
      feedback_measurement_id_ = backend_worker_.weigh_snapshot().measurement_id;
      weigh_operation_id_.reset();
      scale_action_ = ScaleAction::none;
    }
  }
  return false;
}

bool UiService::home_shows_blank_tag() const {
  const auto tag=nfc_.snapshot();
  if(!tag.present||!tag.blank_compatible)return false;
  // A blank tag that Spoolman links to a spool is shown (and weighed) as
  // that spool instead.
  bool linked=false;
  workflow_.visit([&](const services::WorkflowSnapshot& current){
    linked=current.openprinttag_available&&current.spool&&tag.uid&&current.uid==*tag.uid;
  });
  return !linked;
}

WeighStation UiService::weigh_station() const {
  WeighStation station;
  workflow_.visit([&](const services::WorkflowSnapshot& current) {
    station.tag_present = current.openprinttag_available || current.uid_only || current.spool.has_value();
    station.generation = current.spool_generation;
    station.spool_id = current.spool ? current.spool->id : 0;
    station.name = current.spool && !current.spool->display_name.empty()
                       ? current.spool->display_name
                       : current.material.material_name.value_or(std::string());
  });
  return station;
}

void UiService::refresh_home_page(
    const diagnostics::ScaleDiagnosticSnapshot& scale, bool busy) {
  const auto workflow=workflow_.snapshot();
  const auto tag=nfc_.snapshot();
  const bool present=workflow.openprinttag_available;
  const bool blank=home_shows_blank_tag();
  place(workflow_scale_indicator_,present?layout::home_art:layout::empty_art);
  // The tag's own colour, else the Spoolman spool's (UID-linked spools).
  const auto& tag_color=workflow.material.primary_color;
  const domain::Color* spool_color=workflow.spool&&workflow.spool->primary_color?&*workflow.spool->primary_color:nullptr;
  lv_obj_set_style_bg_color(workflow_scale_indicator_,
      tag_color?lv_color_make(tag_color->red,tag_color->green,tag_color->blue):
      spool_color?lv_color_make(spool_color->red,spool_color->green,spool_color->blue):lv_color_hex(neutral_fill),0);
  lv_obj_set_style_text_align(workflow_home_state_label_,present?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_align(workflow_material_label_,present?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_align(workflow_identity_label_,present?LV_TEXT_ALIGN_LEFT:LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_width(workflow_identity_label_,present?348:448);
  lv_obj_set_width(workflow_weight_label_,348);
  lv_label_set_text(workflow_home_state_label_,present?workflow.material.brand_name.value_or("Current spool").c_str():"OpenTag Station");
  const std::string material_name=workflow.material.material_name.value_or(
      workflow.spool&&!workflow.spool->display_name.empty()?workflow.spool->display_name:std::string("Filament spool"));
  lv_label_set_text(workflow_material_label_,present?material_name.c_str():"Place a spool");
  const std::string identity=workflow.spool?"Spool #"+std::to_string(workflow.spool->id):
      !present?"Set a tagged spool on the station":
      workflow.stage==services::WorkflowStage::spool_selection_required||workflow.stage==services::WorkflowStage::spool_not_found?"Not linked yet: Manage tag > Link to a spool":
      workflow.stage==services::WorkflowStage::spool_resolution_unavailable?"Spoolman unavailable; spool not looked up":"Finding this spool in Spoolman...";
  lv_label_set_text(workflow_identity_label_,identity.c_str());
  const auto remaining=workflow.spool?workflow.spool->remaining_grams:std::optional<float>{};
  if(remaining)lv_label_set_text_fmt(workflow_weight_label_,"%ld g remaining",static_cast<long>(std::lround(*remaining)));
  else lv_label_set_text(workflow_weight_label_,present?"Weight not known":"");
  if(blank){lv_label_set_text(workflow_home_state_label_,"COMPATIBLE BLANK TAG");lv_label_set_text(workflow_material_label_,"Ready to write");lv_label_set_text(workflow_identity_label_,"Open Manage tag to choose a spool");}
  // A Spoolman save in progress refuses a new weigh.
  const bool saving=backend_worker_.weigh_snapshot().phase=="updating";
  set_enabled(workflow_weigh_button_,blank||(scale.scale_adc_ready&&!busy&&!saving));
  lv_label_set_text(lv_obj_get_child(workflow_weigh_button_,0),busy?"WEIGHING...":scale.scale_calibrated?"WEIGH":"CALIBRATE");
  if(blank)lv_label_set_text(lv_obj_get_child(workflow_weigh_button_,0),"WRITE TAG");
}

void UiService::refresh_printer_page() {
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
      // Show which spool is loaded, and mark the one on the station.
      std::string loaded = "Empty";
      if (found != nullptr && found->assigned_spool.has_value()) {
        loaded = "#" + std::to_string(*found->assigned_spool);
        if (workflow.spool.has_value() && workflow.spool->id == *found->assigned_spool)
          loaded += " (this spool)";
      }
      lv_label_set_text_fmt(
          label, "T%u\n%s",
          static_cast<unsigned>(index + 1U), loaded.c_str());
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
  // "Assigning..." lasts only while that request runs; a failure the
  // workflow does not report itself stays until the page changes.
  bool assigning = false;
  if (assignment_operation_id_.has_value()) {
    const auto operation = backend_worker_.writer_operation(*assignment_operation_id_);
    assigning = operation.has_value() &&
        (operation->state == application::OperationState::queued ||
         operation->state == application::OperationState::running);
    if (!assigning) {
      workflow_feedback_ = operation.has_value() && operation->state == application::OperationState::failed
          ? plain_error(operation->error.has_value() ? operation->error->message : operation->message)
          : std::string();
      assignment_operation_id_.reset();
    }
  }
  std::string status = "Spoolman ";
  status += availability_text(workflow.spoolman);
  status += "   FilaBridge ";
  status += availability_text(workflow.filabridge);
  if (!workflow_feedback_.empty()) status += "   " + workflow_feedback_;
  if (assigning) status = workflow_feedback_;
  else if (workflow.assignment_error) status = plain_error(workflow.assignment_error->message);
  else if (workflow.filabridge_error) status = workflow.filabridge_error->message;
  else if (workflow.stage == services::WorkflowStage::assignment_complete)
    status = "Assigned. FilaBridge confirmed the spool is on this toolhead.";
  lv_label_set_text(workflow_status_label_, status.c_str());
}

void UiService::refresh_settings_page() {
  const auto system = diagnostics_.snapshot(millis());
  std::string status = system.wifi_connected
      ? "Connected"
      : "Wi-Fi not connected";
  if (system.wifi_connected && !system.ip_address.empty()) {
    status += " - " + system.ip_address;
  }
  if (system.provisioning_active && !system.setup_ap_ssid.empty()) {
    status = "Setup Wi-Fi " + system.setup_ap_ssid + " (no password)";
    const auto remaining_ms = system.provisioning_requested_remaining_ms;
    if (remaining_ms > 0U)
      status += " - closes in " + std::to_string((remaining_ms + 59999U) / 60000U) + " min";
  }
  bool automatic=false;configuration_.visit([&](const auto& c,auto){automatic=c.reconciliation.auto_update_after_weigh;});
  lv_label_set_text(lv_obj_get_child(weight_policy_,0),automatic?"Auto-update ON":"Auto-update OFF");
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
    std::strncat(weight, " g", sizeof(weight) - std::strlen(weight) - 1U);
  }
  char factor[24] = "n/a";
  if (status.scale_calibrated) {
    format_milli(
        factor, sizeof(factor), status.scale_factor_millicounts_per_gram);
  }
  const char* scale_quality = status.scale_overload
                                  ? "overload"
                                  : status.scale_negative
                                        ? "below zero"
                                        : status.scale_creep_warning
                                              ? "drifting"
                                              : status.scale_stable ? "steady" : "settling";
  const auto uptime_minutes = static_cast<unsigned long>(status.uptime_ms / 60000U);
  lv_label_set_text_fmt(
      storage_label_,
      "Wi-Fi: %s\nNetwork: %s (signal %ld dBm)\nAddress: %s\n"
      "Router: %s   DNS: %s\nopentag-station.local: %s   Clock: %s",
      network::to_string(status.wifi_state),
      status.wifi_ssid.empty() ? "-" : status.wifi_ssid.c_str(),
      static_cast<long>(status.wifi_rssi_dbm),
      status.ip_address.empty() ? "-" : status.ip_address.c_str(),
      status.gateway.empty() ? "-" : status.gateway.c_str(),
      status.dns_server.empty() ? "-" : status.dns_server.c_str(),
      status.mdns_ready ? "ready" : "waiting",
      status.ntp_ready ? "set" : "waiting");
  lv_label_set_text_fmt(
      touch_label_,
      "Weight on scale: %s (%s)\nScale: %s, calibration %s\n"
      "Raw reading: %ld (filtered %ld)\nZero: %ld   Factor: %s counts/g\n"
      "Screen: %s   Touch: %s",
      weight,
      scale_quality,
      services::to_string(status.scale_state),
      status.scale_persistence_available ? "saved" : "not saved",
      static_cast<long>(status.scale_raw_counts),
      static_cast<long>(status.scale_filtered_counts),
      static_cast<long>(status.scale_zero_offset_counts),
      factor,
      status.display_ready ? "OK" : "ERROR",
      status.touch_configured ? "OK" : "ERROR");
  lv_label_set_text_fmt(
      system_label_,
      "Running for: %luh %02lum\nStarts: %lu   Crash streak: %u\n"
      "Last restart: %s\nSettings storage: %s   Files: %s\nSetup: %s",
      uptime_minutes / 60UL,
      uptime_minutes % 60UL,
      static_cast<unsigned long>(status.boot_count),
      status.crash_streak,
      status.reset_reason,
      status.nvs_ready ? "OK" : "ERROR",
      status.filesystem_ready ? "OK" : "ERROR",
      first_run_setup_.complete() ? "complete" : "not finished");
  lv_label_set_text_fmt(
      memory_label_,
      "Free RAM: %lu KiB (lowest %lu, largest block %lu)\n"
      "Free PSRAM: %lu of %lu KiB (lowest %lu)\nScreen buffers: %s",
      static_cast<unsigned long>(status.free_heap_bytes / 1024U),
      static_cast<unsigned long>(status.minimum_free_heap_bytes / 1024U),
      static_cast<unsigned long>(
          status.largest_free_internal_block_bytes / 1024U),
      static_cast<unsigned long>(status.psram_free_bytes / 1024U),
      static_cast<unsigned long>(status.psram_total_bytes / 1024U),
      static_cast<unsigned long>(status.minimum_free_psram_bytes / 1024U),
      buffers_in_psram_ ? "PSRAM" : "internal RAM");
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
    apply_display_settings();
    save_brightness();
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
