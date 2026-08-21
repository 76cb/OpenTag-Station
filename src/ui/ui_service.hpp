#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <lvgl.h>

#include "application/configuration_worker.hpp"
#include "application/backend_worker.hpp"
#include "config/configuration_service.hpp"
#include "diagnostics/system_diagnostics.hpp"
#include "hardware/display/wt32_display.hpp"
#include "network/wifi_service.hpp"
#include "services/first_run_setup.hpp"
#include "services/station_workflow.hpp"

namespace opentag::ui {

#ifndef OPENTAG_DISPLAY_SELF_TEST
#define OPENTAG_DISPLAY_SELF_TEST 0
#endif

static_assert(
    OPENTAG_DISPLAY_SELF_TEST == 0 || OPENTAG_DISPLAY_SELF_TEST == 1,
    "OPENTAG_DISPLAY_SELF_TEST must be 0 or 1");

class UiService {
 public:
  UiService(
      hardware::display::Wt32Display& display,
      diagnostics::SystemDiagnostics& diagnostics,
      config::ConfigurationService& configuration,
      services::FirstRunSetup& first_run_setup,
      network::WifiService& network,
      application::ConfigurationWorker& configuration_worker,
      services::StationWorkflow& workflow,
      application::BackendWorker& backend_worker)
      : display_(display),
        diagnostics_(diagnostics),
        configuration_(configuration),
        first_run_setup_(first_run_setup),
        network_(network),
        configuration_worker_(configuration_worker),
        workflow_(workflow),
        backend_worker_(backend_worker) {}

  bool initialize();
  void run_once(std::uint32_t now_ms);
  [[nodiscard]] bool buffers_in_psram() const { return buffers_in_psram_; }

 private:
  static constexpr std::uint16_t primary_buffer_rows = 40U;
  static constexpr std::uint16_t fallback_buffer_rows = 20U;
  static constexpr std::uint32_t refresh_interval_ms = 1000U;
  static constexpr std::uint32_t dim_after_ms = 120000U;
  static constexpr std::uint32_t sleep_after_ms = 300000U;

  static void flush_callback(
      lv_disp_drv_t* driver,
      const lv_area_t* area,
      lv_color_t* colors);
  static void touch_callback(lv_indev_drv_t* driver, lv_indev_data_t* data);
  static void brightness_callback(lv_event_t* event);
  static void sleep_callback(lv_event_t* event);
  static void setup_back_callback(lv_event_t* event);
  static void setup_next_callback(lv_event_t* event);
  static void setup_toggle_callback(lv_event_t* event);
  static void setup_save_callback(lv_event_t* event);
  static void setup_scan_callback(lv_event_t* event);
  static void setup_network_callback(lv_event_t* event);
  static void setup_textarea_callback(lv_event_t* event);
  static void setup_keyboard_callback(lv_event_t* event);
  static void diagnostics_toggle_callback(lv_event_t* event);
  static void toolhead_callback(lv_event_t* event);
  static void assignment_confirmation_callback(lv_event_t* event);

  bool allocate_buffers();
  void build_workflow_screen();
  void build_diagnostics_screen();
  void build_setup_screen();
  void build_display_self_test_screen();
  void update_display_self_test_touch(
      const hardware::display::TouchPoint& point);
  void build_current_screen();
  void refresh_current(std::uint32_t now_ms);
  void refresh_workflow();
  void refresh_diagnostics(std::uint32_t now_ms);
  void refresh_setup();
  lv_obj_t* create_setup_textarea(
      std::int16_t y,
      const char* placeholder,
      const std::string& value,
      std::size_t maximum_length,
      bool password);
  void note_interaction(std::uint32_t now_ms);

  hardware::display::Wt32Display& display_;
  diagnostics::SystemDiagnostics& diagnostics_;
  config::ConfigurationService& configuration_;
  services::FirstRunSetup& first_run_setup_;
  network::WifiService& network_;
  application::ConfigurationWorker& configuration_worker_;
  services::StationWorkflow& workflow_;
  application::BackendWorker& backend_worker_;
  lv_color_t* buffer_one_{nullptr};
  lv_color_t* buffer_two_{nullptr};
  std::size_t buffer_pixels_{0};
  bool buffers_in_psram_{false};
  bool initialized_{false};
  bool dimmed_{false};
  bool showing_setup_{false};
  bool showing_diagnostics_{false};
  bool showing_display_self_test_{OPENTAG_DISPLAY_SELF_TEST == 1};
  std::uint8_t normal_brightness_percent_{80U};
  std::uint32_t dim_after_ms_{dim_after_ms};
  std::uint32_t sleep_after_ms_{sleep_after_ms};
  std::string setup_feedback_;
  std::uint32_t last_tick_ms_{0};
  std::uint32_t last_refresh_ms_{0};
  std::uint32_t last_interaction_ms_{0};
  std::uint32_t setup_scan_generation_{0U};
  lv_disp_draw_buf_t draw_buffer_{};
  lv_disp_drv_t display_driver_{};
  lv_indev_drv_t input_driver_{};
  lv_obj_t* system_label_{nullptr};
  lv_obj_t* memory_label_{nullptr};
  lv_obj_t* storage_label_{nullptr};
  lv_obj_t* touch_label_{nullptr};
  lv_obj_t* brightness_slider_{nullptr};
  lv_obj_t* setup_progress_label_{nullptr};
  lv_obj_t* setup_body_label_{nullptr};
  lv_obj_t* setup_status_label_{nullptr};
  lv_obj_t* setup_input_one_{nullptr};
  lv_obj_t* setup_input_two_{nullptr};
  lv_obj_t* setup_network_dropdown_{nullptr};
  lv_obj_t* setup_keyboard_{nullptr};
  lv_obj_t* workflow_material_label_{nullptr};
  lv_obj_t* workflow_weight_label_{nullptr};
  lv_obj_t* workflow_identity_label_{nullptr};
  lv_obj_t* workflow_status_label_{nullptr};
  lv_obj_t* display_test_touch_marker_{nullptr};
  lv_obj_t* display_test_touch_label_{nullptr};
  std::array<lv_obj_t*, 5> workflow_toolhead_buttons_{};
  std::string workflow_feedback_;
  std::string pending_printer_id_;
  int pending_backend_toolhead_id_{-1};
  bool pending_replace_confirmation_{false};
  bool pending_active_override_{false};
  std::optional<std::uint64_t> pending_spool_generation_;
  std::optional<domain::SpoolId> pending_spool_id_;
  std::optional<std::uint64_t> pending_printer_revision_;
  std::optional<domain::SpoolId> pending_previous_spool_id_;
  domain::PrinterState pending_printer_state_{domain::PrinterState::unknown};
};

}  // namespace opentag::ui
