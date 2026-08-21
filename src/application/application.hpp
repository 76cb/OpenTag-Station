#pragma once

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application/backend_worker.hpp"
#include "application/boot_health_policy.hpp"
#include "application/device_control_worker.hpp"
#include "application/device_lifecycle_gate.hpp"
#include "application/ota_worker.hpp"
#include "application/state_machine.hpp"
#include "application/operation_registry.hpp"
#include "application/scale_command_queue.hpp"
#include "application/configuration_worker.hpp"
#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "config/configuration_service.hpp"
#include "diagnostics/system_diagnostics.hpp"
#include "hardware/display/wt32_display.hpp"
#include "hardware/scale/nau7802_device.hpp"
#include "integrations/filabridge/filabridge_adapter.hpp"
#include "integrations/spoolman/spoolman_adapter.hpp"
#include "logging/bounded_log.hpp"
#include "network/http_transport.hpp"
#include "network/wifi_service.hpp"
#include "platform/ota/esp32_ota_platform.hpp"
#include "platform/ota/update_record_store.hpp"
#include "platform/storage/storage_service.hpp"
#include "services/first_run_setup.hpp"
#include "services/scale_service.hpp"
#include "services/spool_identity_resolver.hpp"
#include "services/station_workflow.hpp"
#include "ui/ui_service.hpp"
#include "web/application_api_context.hpp"
#include "web/local_web_server.hpp"

namespace opentag::application {

class Application {
 public:
  void setup();
  void loop();

 private:
  static void ui_task_entry(void* context);
  static void scale_task_entry(void* context);
  static void network_task_entry(void* context);
  bool start_ui_task();
  bool start_scale_task();
  bool start_network_task();
  [[nodiscard]] BootHealthSignals boot_health_signals(
      std::uint32_t now_ms) const;
  void process_boot_health(std::uint32_t now_ms);

  hardware::display::Wt32Display display_;
  platform::storage::StorageService storage_;
  OperationRegistry operations_;
  DeviceLifecycleGate lifecycle_;
  platform::ota::Esp32OtaPlatform ota_platform_;
  platform::ota::MbedTlsSha256 ota_sha256_;
  platform::ota::Esp32UpdateRecordStore ota_records_;
  opentag::ota::UpdateManager ota_manager_{
      ota_platform_, ota_sha256_, ota_records_};
  OtaWorker ota_worker_{
      ota_manager_, storage_, operations_, lifecycle_};
  diagnostics::SystemDiagnostics diagnostics_{display_, storage_};
  config::ConfigurationService configuration_{storage_, storage_};
  hardware::scale::Nau7802Device scale_adc_{
      Wire,
      {boards::Wt32Sc01PlusRevA::scale_sda,
       boards::Wt32Sc01PlusRevA::scale_scl}};
  services::ScaleService scale_{scale_adc_, configuration_};
  services::FirstRunSetup first_run_setup_{configuration_};
  network::WifiService network_;
  network::HttpTransport http_transport_;
  integrations::spoolman::SpoolmanAdapter spoolman_{http_transport_, {}};
  integrations::filabridge::FilaBridgeAdapter filabridge_{http_transport_, {}};
  services::SpoolIdentityResolver spool_resolver_{
      spoolman_, configuration_, {}};
  services::StationWorkflow workflow_{spool_resolver_, filabridge_};
  ConfigurationWorker configuration_worker_{
      configuration_, network_, operations_};
  BackendWorker backend_worker_{
      configuration_, spoolman_, filabridge_, spool_resolver_, workflow_, operations_};
  ScaleCommandQueue scale_commands_{configuration_, scale_, operations_};
  DeviceControlWorker device_control_{storage_, operations_, lifecycle_};
  logging::BoundedLog logs_;
  web::ApplicationApiContext api_context_{
      diagnostics_,
      configuration_,
      configuration_worker_,
      backend_worker_,
      scale_commands_,
      workflow_,
      operations_,
      logs_,
      device_control_,
      ota_worker_,
      network_};
  web::api::Router api_router_{api_context_};
  web::LocalWebServer web_server_{api_router_, api_context_};
  ui::UiService ui_{
      display_,
      diagnostics_,
      configuration_,
      first_run_setup_,
      network_,
      configuration_worker_,
      workflow_,
      backend_worker_};
  ApplicationStateMachine state_machine_;
  BootHealthPolicy boot_health_policy_{0U};
  TaskHandle_t ui_task_handle_{nullptr};
  TaskHandle_t scale_task_handle_{nullptr};
  TaskHandle_t network_task_handle_{nullptr};
  bool storage_ready_{false};
  bool ota_records_ready_{false};
  bool ota_task_started_{false};
  bool configuration_ready_{false};
  bool scale_profile_ready_{false};
  bool display_ready_{false};
  bool application_idle_ready_{false};
  bool configuration_task_started_{false};
  bool backend_task_started_{false};
  bool scale_commands_ready_{false};
  bool device_control_started_{false};
  bool scale_task_started_{false};
  bool ui_task_started_{false};
  bool network_task_started_{false};
  bool startup_fatal_error_{false};
  std::atomic_bool web_server_running_{false};
  bool boot_health_decision_initialized_{false};
  bool boot_health_submit_accepted_{false};
  opentag::ota::CandidateHealthDecision last_boot_health_decision_{
      opentag::ota::CandidateHealthDecision::stabilizing};
  std::uint32_t last_boot_health_attempt_ms_{0U};
  std::uint32_t last_serial_diagnostics_ms_{0};
};

}  // namespace opentag::application
