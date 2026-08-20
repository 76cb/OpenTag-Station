#include "application/application.hpp"

#include <Arduino.h>

#include <cmath>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "hardware/nfc/st25r3916b/wiring_guard.hpp"

namespace opentag::application {

void Application::setup() {
  Serial.begin(115200);
  delay(150);

  const auto& build = diagnostics::build_info;
  Serial.println();
  Serial.println("OpenTag Station Phase 9 local web UI and versioned API");
  Serial.printf("version=%s git=%s build=%s\n", build.project_version, build.git_sha, build.build_date);
  Serial.printf(
      "board=%s platform=%s framework=%s config_schema=%d\n",
      boards::Wt32Sc01PlusRevA::id,
      build.esp32_platform,
      build.arduino_framework,
      build.config_schema);
  Serial.printf(
      "openprinttag=%s rfal=%s\n",
      build.openprinttag_revision,
      build.rfal_revision);

  if (!hardware::nfc::st25r3916b_wiring_complete) {
    Serial.println("NFC=disabled (ST25R3916B wiring checkpoint unresolved)");
  }

  const auto now_ms = millis();
  logs_.append(
      now_ms,
      logging::LogSeverity::info,
      logging::LogComponent::application,
      "OpenTag Station Phase 9 boot started");
  const bool storage_ready = storage_.initialize(now_ms);
  const bool configuration_ready = configuration_.initialize().ok();
  const auto configured = configuration_.snapshot();
  const bool scale_profile_ready =
      scale_.configure_hardware(configured.scale_hardware).ok();
  spoolman_.configure(configured.spoolman);
  filabridge_.configure(configured.filabridge);
  spool_resolver_.configure(configured.spoolman);
  const bool display_ready = display_.initialize();
  if (display_ready) display_.set_brightness(configured.device.brightness_percent);
  const auto idle_result = state_machine_.transition(ApplicationState::idle);
  const bool configuration_task_started = configuration_worker_.start();
  const bool backend_task_started = backend_worker_.start();
  const bool scale_commands_ready = scale_commands_.initialize();
  const bool device_control_started = device_control_.start();
  const bool scale_task_started = start_scale_task();
  const bool ui_task_started = display_ready && configuration_task_started &&
      backend_task_started &&
      start_ui_task();
  // Start the network/web owner last so an HTTP mutation can never observe an
  // application whose bounded command owners have not yet been initialized.
  const bool network_task_started = start_network_task();
  if (!idle_result.ok() || !ui_task_started || !scale_task_started ||
      !network_task_started || !configuration_task_started ||
      !backend_task_started || !scale_profile_ready || !scale_commands_ready ||
      !device_control_started) {
    if (idle_result.ok()) {
      state_machine_.transition(ApplicationState::error);
    }
  }

  const auto status = diagnostics_.snapshot(now_ms);
  Serial.printf(
      "reset=%s boot=%lu crash_streak=%u state=%s\n",
      status.reset_reason,
      static_cast<unsigned long>(status.boot_count),
      status.crash_streak,
      to_string(state_machine_.state()));
  Serial.printf(
      "display=%s touch=%s nvs=%s littlefs=%s coredump=%s ui_task=%s\n",
      display_ready ? "ready" : "ERROR",
      display_.touch_configured() ? "configured" : "ERROR",
      status.nvs_ready ? "ready" : "ERROR",
      status.filesystem_ready ? "ready" : "ERROR",
      status.coredump_partition_present ? "present" : "missing",
      ui_task_started ? "started" : "ERROR");
  Serial.printf(
      "heap=%lu min_heap=%lu psram=%lu free_psram=%lu storage=%s config=%s "
      "scale_task=%s network_task=%s config_task=%s backend_task=%s control_task=%s\n",
      static_cast<unsigned long>(status.free_heap_bytes),
      static_cast<unsigned long>(status.minimum_free_heap_bytes),
      static_cast<unsigned long>(status.psram_total_bytes),
      static_cast<unsigned long>(status.psram_free_bytes),
      storage_ready ? "ready" : "degraded",
      configuration_ready ? "ready" : "degraded",
      scale_task_started ? "started" : "ERROR",
      network_task_started ? "started" : "ERROR",
      configuration_task_started ? "started" : "ERROR",
      backend_task_started ? "started" : "ERROR",
      device_control_started ? "started" : "ERROR");
}

void Application::loop() {
  const auto now_ms = millis();
  storage_.poll(now_ms);
  if (static_cast<std::uint32_t>(now_ms - last_serial_diagnostics_ms_) >= 30000U) {
    const auto status = diagnostics_.snapshot(now_ms);
    Serial.printf(
        "health uptime=%lus heap=%lu psram_free=%lu ui=%s scale=%s raw=%ld "
        "stable=%s wifi=%s rssi=%ld ntp=%s crash_streak=%u\n",
        static_cast<unsigned long>(status.uptime_ms / 1000U),
        static_cast<unsigned long>(status.free_heap_bytes),
        static_cast<unsigned long>(status.psram_free_bytes),
        status.ui_task_running ? "running" : "stopped",
        services::to_string(status.scale_state),
        static_cast<long>(status.scale_raw_counts),
        status.scale_stable ? "yes" : "no",
        network::to_string(status.wifi_state),
        static_cast<long>(status.wifi_rssi_dbm),
        status.ntp_ready ? "ready" : "pending",
        status.crash_streak);
    last_serial_diagnostics_ms_ = now_ms;
  }
  delay(20);
}

bool Application::start_scale_task() {
  constexpr std::uint32_t scale_stack_bytes = 6144U;
  constexpr UBaseType_t scale_priority = 1U;
  constexpr BaseType_t scale_core = 0;
  return xTaskCreatePinnedToCore(
             scale_task_entry,
             "opentag-scale",
             scale_stack_bytes,
             this,
             scale_priority,
             &scale_task_handle_,
             scale_core) == pdPASS;
}

bool Application::start_network_task() {
  constexpr std::uint32_t network_stack_bytes = 8192U;
  constexpr UBaseType_t network_priority = 1U;
  constexpr BaseType_t network_core = 0;
  return xTaskCreatePinnedToCore(
             network_task_entry,
             "opentag-network",
             network_stack_bytes,
             this,
             network_priority,
             &network_task_handle_,
             network_core) == pdPASS;
}

bool Application::start_ui_task() {
  constexpr std::uint32_t ui_stack_bytes = 12288U;
  constexpr UBaseType_t ui_priority = 2U;
  constexpr BaseType_t ui_core = 1;
  return xTaskCreatePinnedToCore(
             ui_task_entry,
             "opentag-ui",
             ui_stack_bytes,
             this,
             ui_priority,
             &ui_task_handle_,
             ui_core) == pdPASS;
}

void Application::ui_task_entry(void* context) {
  auto* application = static_cast<Application*>(context);
  if (!application->ui_.initialize()) {
    Serial.println("ui=ERROR (LVGL initialization or draw-buffer allocation failed)");
    application->diagnostics_.set_ui_task_running(false);
    application->ui_task_handle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  for (;;) {
    application->ui_.run_once(millis());
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void Application::scale_task_entry(void* context) {
  auto* application = static_cast<Application*>(context);
  constexpr std::uint32_t operation_timeout_ms = 1000U;
  constexpr std::uint32_t retry_interval_ms = 5000U;
  application->diagnostics_.set_scale_task_running(true);

  auto last_initialize_attempt_ms = millis();
  auto initialized = application->scale_.initialize(
      last_initialize_attempt_ms, operation_timeout_ms).ok();
  application->diagnostics_.set_scale_status(
      application->scale_.status(),
      application->scale_.calibration(),
      application->scale_.hardware_settings());
  auto last_profile_check_ms = last_initialize_attempt_ms;

  for (;;) {
    const auto now_ms = millis();
    const bool profile_check_due =
        static_cast<std::uint32_t>(now_ms - last_profile_check_ms) >= 1000U;
    const auto& active_hardware = application->scale_.hardware_settings();
    const auto configured_hardware = profile_check_due
        ? application->configuration_.snapshot().scale_hardware
        : active_hardware;
    const bool hardware_changed = profile_check_due &&
        (active_hardware.load_cell_model !=
             configured_hardware.load_cell_model ||
         std::fabs(active_hardware.rated_capacity_grams -
                   configured_hardware.rated_capacity_grams) > 0.01F ||
         std::fabs(active_hardware.overload_ratio -
                   configured_hardware.overload_ratio) > 0.0001F);
    if (profile_check_due) last_profile_check_ms = now_ms;
    if (hardware_changed) {
      const auto reconfigured = application->scale_.reconfigure_hardware(
          configured_hardware);
      initialized = reconfigured.ok() && application->scale_.initialize(
          now_ms, operation_timeout_ms).ok();
      last_initialize_attempt_ms = now_ms;
    }
    if (!initialized &&
        static_cast<std::uint32_t>(now_ms - last_initialize_attempt_ms) >=
            retry_interval_ms) {
      last_initialize_attempt_ms = now_ms;
      initialized = application->scale_.initialize(
          now_ms, operation_timeout_ms).ok();
    } else if (initialized) {
      const auto poll_result = application->scale_.poll(now_ms);
      (void)poll_result;
    }
    application->scale_commands_.process_one(now_ms);
    application->diagnostics_.set_scale_status(
        application->scale_.status(),
        application->scale_.calibration(),
        application->scale_.hardware_settings());
    vTaskDelay(pdMS_TO_TICKS(20U));
  }
}

void Application::network_task_entry(void* context) {
  auto* application = static_cast<Application*>(context);
  application->diagnostics_.set_network_task_running(true);
  const auto configured = application->configuration_.snapshot();
  auto now_ms = millis();
  const auto initialized = application->network_.initialize(
      configured.device, configured.wifi, now_ms);
  (void)initialized;
  application->diagnostics_.set_network_status(application->network_.status());
  auto last_web_start_attempt_ms = now_ms;
  const auto web_started = application->web_server_.start();
  application->logs_.append(
      now_ms,
      web_started == ESP_OK ? logging::LogSeverity::info
                            : logging::LogSeverity::error,
      logging::LogComponent::web,
      web_started == ESP_OK ? "Local web server started"
                            : "Local web server failed to start");
  for (;;) {
    now_ms = millis();
    application->network_.poll(now_ms);
    application->diagnostics_.set_network_status(application->network_.status());
    if (!application->web_server_.running() &&
        static_cast<std::uint32_t>(now_ms - last_web_start_attempt_ms) >=
            30000U) {
      last_web_start_attempt_ms = now_ms;
      const auto retry = application->web_server_.start();
      application->logs_.append(
          now_ms,
          retry == ESP_OK ? logging::LogSeverity::info
                          : logging::LogSeverity::warning,
          logging::LogComponent::web,
          retry == ESP_OK ? "Local web server retry succeeded"
                          : "Local web server retry failed");
    }
    if (application->web_server_.running()) {
      application->web_server_.publish(now_ms);
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
  }
}

}  // namespace opentag::application
