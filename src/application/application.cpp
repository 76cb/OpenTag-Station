#include "application/application.hpp"

#include <Arduino.h>

#include <cmath>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "hardware/nfc/st25r3916b/wiring_guard.hpp"

namespace opentag::application {
namespace {

void print_scale_i2c_scan(
    std::int8_t sda,
    std::int8_t scl,
    const hardware::scale::I2cScanResult& scan) {
  Serial.printf(
      "Scale I2C scan GPIO%d SDA / GPIO%d SCL:",
      static_cast<int>(sda),
      static_cast<int>(scl));
  if (!scan.bus_started) {
    Serial.println(" bus initialization failed; no devices found");
    return;
  }
  if (!scan.found_any()) {
    Serial.println(" no devices found");
    return;
  }
  Serial.print(" found");
  for (std::uint8_t index = 0U; index < scan.reported_count; ++index) {
    Serial.printf(" 0x%02X", static_cast<unsigned>(scan.addresses[index]));
  }
  if (scan.truncated()) {
    Serial.printf(
        " +%u more",
        static_cast<unsigned>(scan.device_count - scan.reported_count));
  }
  Serial.println();
}

constexpr std::uint32_t boot_health_retry_interval_ms = 1000U;
constexpr std::uint32_t stack_diagnostic_interval_ms = 1000U;
constexpr std::uint32_t stable_stack_diagnostic_delay_ms = 15000U;

std::uint32_t stack_high_water_free_bytes(TaskHandle_t task) {
  if (task == nullptr) return 0U;
  // ESP-IDF reports this value in bytes, unlike vanilla FreeRTOS which reports
  // stack words. Keep the public diagnostic in the framework's native units.
  return static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(task));
}

bool candidate_validation_pending(opentag::ota::UpdateState state) {
  return state == opentag::ota::UpdateState::candidate_boot ||
      state == opentag::ota::UpdateState::validating_candidate;
}

bool pending_bootloader_confirmation(
    opentag::ota::PartitionImageState state) {
  return state == opentag::ota::PartitionImageState::pending_verify ||
      state == opentag::ota::PartitionImageState::new_image;
}

bool rollback_seed_recovery_pending(
    const opentag::ota::UpdateSnapshot& update) {
  const bool recoverable_running =
      pending_bootloader_confirmation(update.running_image_state) ||
      update.running_image_state ==
          opentag::ota::PartitionImageState::valid;
  return update.state == opentag::ota::UpdateState::ready_to_reboot &&
      update.validation_passed && update.calculated_sha_available &&
      update.expected_sha256 == update.calculated_sha256 &&
      update.image_size != 0U && update.bytes_received == update.image_size &&
      update.activation_intent && !update.activated &&
      update.target.present() &&
      opentag::ota::same_partition(update.boot, update.running) &&
      opentag::ota::same_partition(update.inactive, update.target) &&
      !opentag::ota::same_partition(update.running, update.target) &&
      recoverable_running;
}

}  // namespace

void Application::setup() {
  loop_task_handle_ = xTaskGetCurrentTaskHandle();
  Serial.begin(115200);
  delay(150);

  const auto& build = diagnostics::build_info;
  Serial.println();
  Serial.println("OpenTag Station Phase 10 safe OTA and rollback");
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
  boot_health_policy_ = BootHealthPolicy(now_ms);
  logs_.append(
      now_ms,
      logging::LogSeverity::info,
      logging::LogComponent::application,
      "OpenTag Station Phase 10 boot started");
  storage_ready_ = storage_.initialize(now_ms);

  // Reconcile the bootloader and acquire candidate-validation ownership before
  // any reset owner or network mutation can compete for the lifecycle gate.
  ota_records_ready_ = ota_records_.initialize().ok();
  // The owner must still inspect bootloader state when metadata storage is
  // unavailable so a pending candidate can take the fail-closed rollback path.
  ota_task_started_ = ota_worker_.start(now_ms);

  const auto configuration_result = configuration_.initialize();
  const auto configuration_status = configuration_.status();
  configuration_ready_ = configuration_result.ok() ||
      (configuration_status.initialized &&
       !configuration_status.persistence_available);
  const auto configured = configuration_.snapshot();
  scale_profile_ready_ =
      scale_.configure_hardware(configured.scale_hardware).ok();
  spoolman_.configure(configured.spoolman);
  filabridge_.configure(configured.filabridge);
  spool_resolver_.configure(configured.spoolman);
  display_ready_ = display_.initialize();
  if (display_ready_) {
    display_.set_brightness(configured.device.brightness_percent);
  }
  const auto idle_result = state_machine_.transition(ApplicationState::idle);
  application_idle_ready_ = idle_result.ok();
  configuration_task_started_ = configuration_worker_.start();
  backend_task_started_ = backend_worker_.start();
  scale_commands_ready_ = scale_commands_.initialize();
  // A timed-out or failed OTA-owner start leaves bootloader reconciliation
  // unknown. Keep every destructive control and network mutation unavailable
  // rather than exposing a window where another lifecycle owner can win.
  device_control_started_ = ota_task_started_ && device_control_.start();
  if (device_control_started_ &&
      storage_.factory_reset_recovery_pending()) {
    const auto update = ota_worker_.snapshot();
    if (!candidate_validation_pending(update.state)) {
      // The persisted marker is the authority for this internal recovery;
      // browser authentication is neither available nor required at boot.
      (void)device_control_.submit_factory_reset(millis());
    }
  }

  scale_task_started_ = start_scale_task();
  ui_task_started_ = display_ready_ && configuration_task_started_ &&
      backend_task_started_ &&
      start_ui_task();
  // Start the network/web owner last so an HTTP mutation can never observe an
  // application whose bounded command owners have not yet been initialized.
  network_task_started_ = ota_task_started_ && start_network_task();
  startup_fatal_error_ = !storage_ready_ || !ota_records_ready_ ||
      !ota_task_started_ || !configuration_ready_ ||
      !application_idle_ready_ || !ui_task_started_ ||
      !scale_task_started_ || !network_task_started_ ||
      !configuration_task_started_ || !backend_task_started_ ||
      !scale_profile_ready_ || !scale_commands_ready_ ||
      !device_control_started_;
  if (startup_fatal_error_ && application_idle_ready_) {
    (void)state_machine_.transition(ApplicationState::error);
  }

  const auto status = diagnostics_.snapshot(millis());
  Serial.printf(
      "reset=%s boot=%lu crash_streak=%u state=%s\n",
      status.reset_reason,
      static_cast<unsigned long>(status.boot_count),
      status.crash_streak,
      to_string(state_machine_.state()));
  Serial.printf(
      "display=%s touch=%s nvs=%s littlefs=%s coredump=%s ui_task=%s\n",
      display_ready_ ? "ready" : "ERROR",
      display_.touch_configured() ? "configured" : "ERROR",
      status.nvs_ready ? "ready" : "ERROR",
      status.filesystem_ready ? "ready" : "ERROR",
      status.coredump_partition_present ? "present" : "missing",
      ui_task_started_ ? "started" : "ERROR");
  Serial.printf(
      "heap=%lu min_heap=%lu psram=%lu free_psram=%lu storage=%s config=%s "
      "ota_store=%s ota_task=%s scale_task=%s network_task=%s config_task=%s "
      "backend_task=%s control_task=%s\n",
      static_cast<unsigned long>(status.free_heap_bytes),
      static_cast<unsigned long>(status.minimum_free_heap_bytes),
      static_cast<unsigned long>(status.psram_total_bytes),
      static_cast<unsigned long>(status.psram_free_bytes),
      storage_ready_ ? "ready" : "degraded",
      !configuration_status.initialized
          ? "ERROR"
          : configuration_status.persistence_available
              ? "ready"
              : "safe-degraded",
      ota_records_ready_ ? "ready" : "ERROR",
      ota_task_started_ ? "started" : "ERROR",
      scale_task_started_ ? "started" : "ERROR",
      network_task_started_ ? "started" : "ERROR",
      configuration_task_started_ ? "started" : "ERROR",
      backend_task_started_ ? "started" : "ERROR",
      device_control_started_ ? "started" : "ERROR");
  record_task_stack_margins();
  print_task_stack_margins("boot");
}

void Application::record_task_stack_margins() {
  diagnostics::TaskStackMargins margins;
  margins.loop_free_bytes = stack_high_water_free_bytes(loop_task_handle_);
  margins.network_free_bytes =
      stack_high_water_free_bytes(network_task_handle_);
  margins.ui_free_bytes = stack_high_water_free_bytes(ui_task_handle_);
  margins.configuration_free_bytes =
      stack_high_water_free_bytes(configuration_worker_.task_handle());
  margins.scale_free_bytes = stack_high_water_free_bytes(scale_task_handle_);
#if INCLUDE_xTaskGetHandle == 1
  margins.httpd_free_bytes =
      stack_high_water_free_bytes(xTaskGetHandle("httpd"));
#endif
  diagnostics_.set_task_stack_margins(margins);
}

void Application::print_task_stack_margins(const char* phase) const {
  const auto margins = diagnostics_.task_stack_margins();
  Serial.printf(
      "stack_margin phase=%s loopTask=%lu opentag-network=%lu "
      "opentag-ui=%lu opentag-config=%lu opentag-scale=%lu httpd=%lu bytes\n",
      phase,
      static_cast<unsigned long>(margins.loop_free_bytes),
      static_cast<unsigned long>(margins.network_free_bytes),
      static_cast<unsigned long>(margins.ui_free_bytes),
      static_cast<unsigned long>(margins.configuration_free_bytes),
      static_cast<unsigned long>(margins.scale_free_bytes),
      static_cast<unsigned long>(margins.httpd_free_bytes));
}

BootHealthSignals Application::boot_health_signals(
    std::uint32_t now_ms) const {
  const auto diagnostics = diagnostics_.snapshot(now_ms);
  const auto configuration = configuration_.status();
  const auto application_state = state_machine_.state();

  BootHealthSignals signals;
  signals.storage_ready =
      diagnostics.nvs_ready && diagnostics.filesystem_ready;
  signals.configuration_initialized =
      configuration.initialized && configuration.persistence_available;
  signals.configuration_safely_degraded =
      configuration.initialized && !configuration.persistence_available;
  signals.application_ready =
      application_state != ApplicationState::booting &&
      application_state != ApplicationState::error;
  signals.display_ready = diagnostics.display_ready;
  signals.ui_task_running = diagnostics.ui_task_running;
  signals.configuration_task_running = configuration_task_started_;
  signals.backend_task_running = backend_task_started_;
  signals.scale_commands_ready = scale_commands_ready_;
  signals.scale_task_running = diagnostics.scale_task_running;
  signals.network_task_running = diagnostics.network_task_running;
  signals.device_control_task_running = device_control_started_;
  signals.web_server_running =
      web_server_running_.load(std::memory_order_acquire);
  signals.ota_task_running = ota_worker_.ready();
  signals.factory_reset_recovery_pending =
      storage_.factory_reset_recovery_pending();
  // The policy checks durable reset recovery first. Keep application error
  // visible, but never reinterpret a deliberate reset-recovery marker as an
  // OTA candidate failure.
  signals.fatal_initialization_error =
      !signals.factory_reset_recovery_pending &&
      (startup_fatal_error_ || application_state == ApplicationState::error);
  return signals;
}

void Application::process_boot_health(std::uint32_t now_ms) {
  const auto evaluation = boot_health_policy_.evaluate(
      now_ms, boot_health_signals(now_ms));
  auto decision = opentag::ota::CandidateHealthDecision::stabilizing;
  switch (evaluation.state) {
    case BootHealthState::stabilizing:
      decision = opentag::ota::CandidateHealthDecision::stabilizing;
      break;
    case BootHealthState::healthy:
      decision = opentag::ota::CandidateHealthDecision::healthy;
      break;
    case BootHealthState::unhealthy:
      decision = opentag::ota::CandidateHealthDecision::unhealthy;
      break;
    case BootHealthState::factory_reset_recovery:
      decision =
          opentag::ota::CandidateHealthDecision::factory_reset_recovery;
      break;
  }

  const auto update = ota_worker_.snapshot();
  const bool candidate_pending = candidate_validation_pending(update.state);
  const bool rollback_seed_pending =
      rollback_seed_recovery_pending(update);
  bool action_still_pending = false;
  switch (decision) {
    case opentag::ota::CandidateHealthDecision::stabilizing:
      action_still_pending =
          update.state == opentag::ota::UpdateState::candidate_boot ||
          rollback_seed_pending;
      break;
    case opentag::ota::CandidateHealthDecision::healthy:
      action_still_pending = candidate_pending ||
          storage_.boot_confirmation_pending() || rollback_seed_pending;
      break;
    case opentag::ota::CandidateHealthDecision::unhealthy:
      action_still_pending = candidate_pending || rollback_seed_pending;
      break;
    case opentag::ota::CandidateHealthDecision::factory_reset_recovery:
      // Recovery remains pending until a reset owner restarts the device. The
      // one-second retry below coalesces with an accepted device-control
      // operation instead of allocating a new operation on every loop.
      action_still_pending = true;
      break;
  }

  const bool decision_changed = !boot_health_decision_initialized_ ||
      decision != last_boot_health_decision_;
  const bool retry_due = static_cast<std::uint32_t>(
      now_ms - last_boot_health_attempt_ms_) >=
      boot_health_retry_interval_ms;
  if (boot_health_decision_initialized_ && !retry_due) {
    return;
  }
  if (!decision_changed && boot_health_submit_accepted_ &&
      !action_still_pending) {
    return;
  }

  boot_health_decision_initialized_ = true;
  last_boot_health_decision_ = decision;
  last_boot_health_attempt_ms_ = now_ms;
  if (decision ==
          opentag::ota::CandidateHealthDecision::factory_reset_recovery &&
      !candidate_pending) {
    // The durable reset marker is the authority for this internal handoff.
    // Candidate boots stay with OtaWorker so its candidate-validation lease
    // cannot be displaced or released by the ordinary reset owner.
    boot_health_submit_accepted_ =
        device_control_.submit_factory_reset(now_ms).accepted;
  } else {
    boot_health_submit_accepted_ =
        ota_worker_.submit_boot_health(decision, now_ms);
  }
}

void Application::loop() {
  const auto now_ms = millis();
  process_boot_health(now_ms);
  if (static_cast<std::uint32_t>(now_ms - last_stack_sample_ms_) >=
      stack_diagnostic_interval_ms) {
    record_task_stack_margins();
    last_stack_sample_ms_ = now_ms;
  }
  if (!stable_stack_margins_printed_ &&
      now_ms >= stable_stack_diagnostic_delay_ms) {
    print_task_stack_margins("stable");
    stable_stack_margins_printed_ = true;
  }
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
  constexpr std::uint32_t network_stack_bytes = 16384U;
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

  const auto bus_diagnostic = application->scale_adc_.diagnose_bus();
  print_scale_i2c_scan(
      boards::Wt32Sc01PlusRevA::scale_sda,
      boards::Wt32Sc01PlusRevA::scale_scl,
      bus_diagnostic.expected);
  if (bus_diagnostic.reversed_scanned) {
    print_scale_i2c_scan(
        boards::Wt32Sc01PlusRevA::scale_scl,
        boards::Wt32Sc01PlusRevA::scale_sda,
        bus_diagnostic.reversed);
  }

  const auto diagnostic_time_ms = millis();
  switch (bus_diagnostic.outcome()) {
    case hardware::scale::ScaleI2cOutcome::present_on_expected_bus:
      Serial.println("NAU7802: PRESENT at 0x2A");
      application->logs_.append(
          diagnostic_time_ms,
          logging::LogSeverity::info,
          logging::LogComponent::scale,
          "NAU7802 present at 0x2A on SDA GPIO10 / SCL GPIO11");
      break;
    case hardware::scale::ScaleI2cOutcome::present_on_reversed_bus:
      Serial.println("NAU7802: FOUND WITH SDA/SCL REVERSED");
      Serial.println("Expected: SDA=GPIO10 SCL=GPIO11");
      Serial.println("Detected: SDA=GPIO11 SCL=GPIO10");
      Serial.println("Check/swap SDA and SCL wiring");
      application->logs_.append(
          diagnostic_time_ms,
          logging::LogSeverity::warning,
          logging::LogComponent::scale,
          "NAU7802 found only with SDA/SCL reversed; restore GPIO10 SDA / GPIO11 SCL wiring");
      break;
    case hardware::scale::ScaleI2cOutcome::target_missing_with_other_devices:
      Serial.println("NAU7802 0x2A not present");
      application->logs_.append(
          diagnostic_time_ms,
          logging::LogSeverity::warning,
          logging::LogComponent::scale,
          "Scale I2C scan found other device(s), but NAU7802 0x2A was absent");
      break;
    case hardware::scale::ScaleI2cOutcome::no_devices:
      Serial.println("NAU7802: NOT DETECTED");
      Serial.println("No I2C devices found on GPIO10/11 in either orientation");
      Serial.println("Check NAU7802 power, harness, connector orientation, SDA/SCL wiring, or module");
      application->logs_.append(
          diagnostic_time_ms,
          logging::LogSeverity::warning,
          logging::LogComponent::scale,
          "No I2C devices found on scale GPIO10/11 in either orientation");
      break;
  }
  if (!bus_diagnostic.expected_bus_restored) {
    Serial.println("Scale I2C: ERROR restoring SDA=GPIO10 SCL=GPIO11");
    application->logs_.append(
        diagnostic_time_ms,
        logging::LogSeverity::error,
        logging::LogComponent::scale,
        "Scale I2C could not restore production GPIO10 SDA / GPIO11 SCL");
  }

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
  application->web_server_running_.store(
      web_started == ESP_OK,
      std::memory_order_release);
  application->logs_.append(
      now_ms,
      web_started == ESP_OK ? logging::LogSeverity::info
                            : logging::LogSeverity::error,
      logging::LogComponent::web,
      web_started == ESP_OK ? "Local web server started"
                            : "Local web server failed to start");
  auto previous_network_state = network::WifiState::uninitialized;
  bool previous_provisioning_active = false;
  bool previous_grace_active = false;
  std::string previous_ip;
  for (;;) {
    now_ms = millis();
    application->network_.poll(now_ms);
    const auto& network_status = application->network_.status();
    application->diagnostics_.set_network_status(network_status);
    if (network_status.state != previous_network_state ||
        network_status.provisioning_active != previous_provisioning_active ||
        network_status.provisioning_grace_active != previous_grace_active ||
        network_status.ip_address != previous_ip) {
      Serial.printf(
          "Network: state=%s ssid=%s ip=%s setup_ap=%s reason=%s failures=%lu\n",
          network::to_string(network_status.state),
          network_status.ssid.empty() ? "-" : network_status.ssid.c_str(),
          network_status.ip_address.empty()
              ? "-" : network_status.ip_address.c_str(),
          network_status.provisioning_active
              ? network_status.setup_ap_ssid.c_str() : "off",
          network::to_string(network_status.provisioning_reason),
          static_cast<unsigned long>(network_status.provisioning_failures));
      Serial.printf(
          "ap_grace=%s remaining_ms=%lu\n",
          network_status.provisioning_grace_active ? "active" : "inactive",
          static_cast<unsigned long>(
              network_status.provisioning_grace_remaining_ms));
      if (network_status.last_error.has_value()) {
        Serial.printf(
            "Network error: %s\n",
            network_status.last_error->message.c_str());
      }
      previous_network_state = network_status.state;
      previous_provisioning_active = network_status.provisioning_active;
      previous_grace_active = network_status.provisioning_grace_active;
      previous_ip = network_status.ip_address;
    }
    auto web_running = application->web_server_.running();
    application->web_server_running_.store(
        web_running,
        std::memory_order_release);
    if (!web_running &&
        static_cast<std::uint32_t>(now_ms - last_web_start_attempt_ms) >=
            30000U) {
      last_web_start_attempt_ms = now_ms;
      const auto retry = application->web_server_.start();
      application->web_server_running_.store(
          retry == ESP_OK,
          std::memory_order_release);
      web_running = retry == ESP_OK;
      application->logs_.append(
          now_ms,
          retry == ESP_OK ? logging::LogSeverity::info
                          : logging::LogSeverity::warning,
          logging::LogComponent::web,
          retry == ESP_OK ? "Local web server retry succeeded"
                          : "Local web server retry failed");
    }
    if (web_running) {
      application->web_server_.publish(now_ms);
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
  }
}

}  // namespace opentag::application
