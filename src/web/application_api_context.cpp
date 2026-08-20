#include "web/application_api_context.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <algorithm>
#include <string>
#include <utility>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "web/configuration_patch.hpp"

namespace opentag::web {
namespace {

core::Error unavailable(
    core::ErrorCategory category,
    const char* message,
    bool retryable = false) {
  return {category, message, retryable};
}

constexpr std::size_t constant_time_token_bytes = 512U;

bool constant_time_token_match(
    std::string_view supplied,
    std::string_view configured) {
  volatile std::uint32_t difference = static_cast<std::uint32_t>(
      supplied.size() ^ configured.size());
  for (std::size_t index = 0U; index < constant_time_token_bytes; ++index) {
    const auto supplied_byte = index < supplied.size()
        ? static_cast<unsigned char>(supplied[index])
        : static_cast<unsigned char>(0U);
    const auto configured_byte = index < configured.size()
        ? static_cast<unsigned char>(configured[index])
        : static_cast<unsigned char>(0U);
    difference = difference | static_cast<std::uint32_t>(
        supplied_byte ^ configured_byte);
  }
  const bool bounded = supplied.size() <= constant_time_token_bytes &&
      configured.size() <= constant_time_token_bytes;
  return bounded && !configured.empty() && difference == 0U;
}

const char* printer_state_name(domain::PrinterState state) {
  switch (state) {
    case domain::PrinterState::unknown: return "unknown";
    case domain::PrinterState::idle: return "idle";
    case domain::PrinterState::printing: return "printing";
    case domain::PrinterState::paused: return "paused";
    case domain::PrinterState::attention: return "attention";
    case domain::PrinterState::finished: return "finished";
    case domain::PrinterState::stopped: return "stopped";
    case domain::PrinterState::error: return "error";
    case domain::PrinterState::offline: return "offline";
    case domain::PrinterState::not_configured: return "not_configured";
  }
  return "unknown";
}

domain::PrinterState parse_printer_state(const std::string& value) {
  if (value == "idle") return domain::PrinterState::idle;
  if (value == "printing") return domain::PrinterState::printing;
  if (value == "paused") return domain::PrinterState::paused;
  if (value == "attention") return domain::PrinterState::attention;
  if (value == "finished") return domain::PrinterState::finished;
  if (value == "stopped") return domain::PrinterState::stopped;
  if (value == "error") return domain::PrinterState::error;
  if (value == "offline") return domain::PrinterState::offline;
  if (value == "not_configured") return domain::PrinterState::not_configured;
  return domain::PrinterState::unknown;
}

const char* workflow_stage_name(services::WorkflowStage stage) {
  switch (stage) {
    case services::WorkflowStage::awaiting_spool: return "awaiting_spool";
    case services::WorkflowStage::waiting_for_stable_weight:
      return "waiting_for_stable_weight";
    case services::WorkflowStage::resolving_spool: return "resolving_spool";
    case services::WorkflowStage::spool_resolution_unavailable:
      return "spool_resolution_unavailable";
    case services::WorkflowStage::spool_not_found: return "spool_not_found";
    case services::WorkflowStage::spool_selection_required:
      return "spool_selection_required";
    case services::WorkflowStage::spool_ready: return "spool_ready";
    case services::WorkflowStage::assignment_complete:
      return "assignment_complete";
  }
  return "unknown";
}

const char* availability_name(services::BackendAvailability value) {
  switch (value) {
    case services::BackendAvailability::unknown: return "unknown";
    case services::BackendAvailability::online: return "online";
    case services::BackendAvailability::offline: return "offline";
  }
  return "unknown";
}

const char* reconciliation_name(services::ReconciliationDecision decision) {
  switch (decision) {
    case services::ReconciliationDecision::unavailable: return "unavailable";
    case services::ReconciliationDecision::within_tolerance:
      return "within_tolerance";
    case services::ReconciliationDecision::warning: return "warning";
    case services::ReconciliationDecision::confirmation_required:
      return "confirmation_required";
  }
  return "unavailable";
}

const char* error_category_name(core::ErrorCategory category) {
  switch (category) {
    case core::ErrorCategory::network: return "network";
    case core::ErrorCategory::authentication: return "authentication";
    case core::ErrorCategory::backend_unavailable: return "backend_unavailable";
    case core::ErrorCategory::api_changed: return "api_changed";
    case core::ErrorCategory::invalid_response: return "invalid_response";
    case core::ErrorCategory::conflict: return "conflict";
    case core::ErrorCategory::nfc_communication: return "nfc_communication";
    case core::ErrorCategory::nfc_crc: return "nfc_crc";
    case core::ErrorCategory::multiple_tags: return "multiple_tags";
    case core::ErrorCategory::unsupported_tag: return "unsupported_tag";
    case core::ErrorCategory::invalid_openprinttag: return "invalid_openprinttag";
    case core::ErrorCategory::tag_write_protected: return "tag_write_protected";
    case core::ErrorCategory::tag_removed: return "tag_removed";
    case core::ErrorCategory::scale_unavailable: return "scale_unavailable";
    case core::ErrorCategory::scale_unstable: return "scale_unstable";
    case core::ErrorCategory::configuration: return "configuration";
    case core::ErrorCategory::storage: return "storage";
    case core::ErrorCategory::firmware_update: return "firmware_update";
  }
  return "unknown";
}

void add_error(JsonObject destination, const std::optional<core::Error>& error) {
  if (!error.has_value()) return;
  destination["category"] = error_category_name(error->category);
  destination["message"] = error->message;
  destination["retryable"] = error->retryable;
}

template <typename T>
void add_optional(JsonObject destination, const char* key, const std::optional<T>& value) {
  if (value.has_value()) destination[key] = *value;
}

core::Result<std::string> serialized(JsonDocument& document) {
  std::string result;
  serializeJson(document, result);
  if (result.empty() || result.size() > api::maximum_snapshot_json_bytes) {
    return core::Result<std::string>::failure(unavailable(
        core::ErrorCategory::storage,
        "API snapshot exceeded its bounded serialization buffer"));
  }
  return core::Result<std::string>::success(std::move(result));
}

void write_backend(
    JsonObject object,
    const application::BackendRuntimeSnapshot& backend) {
  object["connected"] = backend.connected;
  object["healthy"] = backend.healthy;
  object["availability"] = backend.connected ? "connected" : "offline";
  object["version_formally_tested"] = backend.version_formally_tested;
  object["version"] = backend.version;
  object["capabilities_bits"] = backend.capability_bits;
  if (backend.last_error.has_value()) {
    add_error(object["error"].to<JsonObject>(), backend.last_error);
  }
}

void write_system(JsonObject object, const diagnostics::SystemSnapshot& value) {
  object["uptime_ms"] = value.uptime_ms;
  object["free_heap_bytes"] = value.free_heap_bytes;
  object["minimum_free_heap_bytes"] = value.minimum_free_heap_bytes;
  object["psram_total_bytes"] = value.psram_total_bytes;
  object["psram_free_bytes"] = value.psram_free_bytes;
  object["boot_count"] = value.boot_count;
  object["crash_streak"] = value.crash_streak;
  object["reset_reason"] = value.reset_reason;
  object["display_ready"] = value.display_ready;
  object["touch_configured"] = value.touch_configured;
  object["nvs_ready"] = value.nvs_ready;
  object["filesystem_ready"] = value.filesystem_ready;
  object["ui_task_running"] = value.ui_task_running;
  object["scale_task_running"] = value.scale_task_running;
  object["network_task_running"] = value.network_task_running;
  auto network = object["network"].to<JsonObject>();
  network["state"] = network::to_string(value.wifi_state);
  network["configured"] = value.wifi_configured;
  network["connected"] = value.wifi_connected;
  network["mdns_ready"] = value.mdns_ready;
  network["ntp_ready"] = value.ntp_ready;
  network["ssid"] = value.wifi_ssid;
  network["rssi_dbm"] = value.wifi_rssi_dbm;
  network["ip_address"] = value.ip_address;
  network["gateway"] = value.gateway;
  network["dns_server"] = value.dns_server;
  network["reconnect_attempts"] = value.wifi_reconnect_attempts;
}

void write_scale(JsonObject scale, const diagnostics::SystemSnapshot& value) {
  scale["revision"] = value.scale_revision;
  scale["state"] = services::to_string(value.scale_state);
  scale["adc_ready"] = value.scale_adc_ready;
  scale["calibrated"] = value.scale_calibrated;
  scale["calibration_loaded"] = value.scale_calibration_loaded;
  scale["calibration_matches_hardware"] =
      value.scale_calibration_matches_hardware;
  scale["persistence_available"] = value.scale_persistence_available;
  auto sample = scale["sample"].to<JsonObject>();
  if (value.scale_weight_available) {
    sample["gross_grams"] =
        static_cast<double>(value.scale_gross_milligrams) / 1000.0;
  }
  sample["stable"] = value.scale_stable;
  sample["negative"] = value.scale_negative;
  sample["overload"] = value.scale_overload;
  sample["creep_warning"] = value.scale_creep_warning;
  sample["raw_counts"] = value.scale_raw_counts;
  sample["filtered_counts"] = value.scale_filtered_counts;
  auto profile = scale["profile"].to<JsonObject>();
  profile["id"] = value.scale_rated_capacity_grams <= 2000.01F
                      ? "yzc-133-2kg"
                      : "yzc-133-5kg";
  profile["model"] = value.scale_load_cell_model;
  profile["display_name"] =
      value.scale_rated_capacity_grams <= 2000.01F
          ? "YZC-133 2 kg"
          : "YZC-133 5 kg";
  profile["rated_capacity_grams"] = value.scale_rated_capacity_grams;
  profile["overload_ratio"] = value.scale_configured_overload_ratio;
  profile["overload_threshold_grams"] = value.scale_overload_threshold_grams;
  auto calibration = scale["calibration"].to<JsonObject>();
  calibration["configured"] = value.scale_calibration_loaded;
  if (value.scale_calibration_loaded) {
    calibration["reference_grams"] = value.scale_calibration_reference_grams;
    calibration["capacity_grams"] = value.scale_calibration_capacity_grams;
    calibration["zero_offset_counts"] = value.scale_zero_offset_counts;
    calibration["counts_per_gram"] =
        static_cast<double>(value.scale_factor_millicounts_per_gram) / 1000.0;
  }
}

void write_spool(JsonObject object, const domain::Spool& spool) {
  object["id"] = spool.id;
  object["filament_id"] = spool.filament_id;
  object["display_name"] = spool.display_name;
  object["vendor"] = spool.vendor;
  object["material"] = spool.material;
  object["subtype"] = spool.subtype;
  add_optional(object, "remaining_grams", spool.remaining_grams);
  add_optional(object, "initial_grams", spool.initial_grams);
  object["used_grams"] = spool.used_grams;
  add_optional(object, "location", spool.location);
  object["archived"] = spool.archived;
}

void write_reconciliation(
    JsonObject object,
    const services::ReconciliationResult& value) {
  object["decision"] = reconciliation_name(value.decision);
  add_optional(object, "measured_remaining_grams", value.measured_remaining_grams);
  add_optional(object, "spoolman_difference_grams", value.spoolman_difference_grams);
  add_optional(object, "tag_difference_grams", value.tag_difference_grams);
  add_optional(
      object,
      "maximum_absolute_difference_grams",
      value.maximum_absolute_difference_grams);
}

void write_printer(
    JsonObject object,
    const domain::Printer& printer,
    std::uint64_t revision) {
  object["id"] = printer.id;
  object["display_name"] = printer.display_name;
  object["state"] = printer_state_name(printer.state);
  object["raw_state"] = printer.raw_state;
  object["revision"] = revision;
  auto toolheads = object["toolheads"].to<JsonArray>();
  for (const auto& toolhead : printer.toolheads) {
    auto encoded = toolheads.add<JsonObject>();
    encoded["printer_id"] = toolhead.printer_id;
    encoded["backend_id"] = toolhead.backend_id;
    encoded["display_number"] = toolhead.display_number;
    encoded["display_name"] = toolhead.display_name;
    if (toolhead.assigned_spool.has_value()) {
      encoded["assigned_spool_id"] = *toolhead.assigned_spool;
    } else {
      encoded["assigned_spool_id"] = nullptr;
    }
  }
}

void write_configuration(
    JsonDocument& document,
    const config::VersionedConfiguration& versioned) {
  const auto& value = versioned.configuration;
  document["revision"] = versioned.revision;
  document["schema_version"] = value.schema_version;
  document["hardware_id"] = value.hardware_id;
  auto device = document["device"].to<JsonObject>();
  device["hostname"] = value.device.hostname;
  device["brightness_percent"] = value.device.brightness_percent;
  device["dim_after_ms"] = value.device.dim_after_ms;
  device["sleep_after_ms"] = value.device.sleep_after_ms;
  device["update_channel"] = value.device.update_channel;
  auto web = document["web"].to<JsonObject>();
  web["access_token_configured"] = !value.web.access_token.empty();
  auto wifi = document["wifi"].to<JsonObject>();
  wifi["ssid"] = value.wifi.ssid;
  wifi["password_configured"] = !value.wifi.password.empty();
  wifi["auto_reconnect"] = value.wifi.auto_reconnect;
  wifi["connect_timeout_ms"] = value.wifi.connect_timeout_ms;
  wifi["reconnect_initial_ms"] = value.wifi.reconnect_initial_ms;
  wifi["reconnect_max_ms"] = value.wifi.reconnect_max_ms;
  auto spoolman = document["spoolman"].to<JsonObject>();
  spoolman["url"] = value.spoolman.url;
  spoolman["authentication_token_configured"] =
      !value.spoolman.authentication_token.empty();
  spoolman["custom_ca_configured"] = !value.spoolman.ca_certificate_pem.empty();
  spoolman["identity_field"] = value.spoolman.identity_field;
  spoolman["nfc_uid_field"] = value.spoolman.nfc_uid_field;
  auto filabridge = document["filabridge"].to<JsonObject>();
  filabridge["url"] = value.filabridge.url;
  filabridge["selected_printer_id"] = value.filabridge.selected_printer_id;
  filabridge["authentication_token_configured"] =
      !value.filabridge.authentication_token.empty();
  filabridge["custom_ca_configured"] =
      !value.filabridge.ca_certificate_pem.empty();
  auto profile = document["scale_profile"].to<JsonObject>();
  profile["id"] = value.scale_hardware.rated_capacity_grams <= 2000.01F
                      ? "yzc-133-2kg"
                      : "yzc-133-5kg";
  profile["model"] = value.scale_hardware.load_cell_model;
  profile["rated_capacity_grams"] =
      value.scale_hardware.rated_capacity_grams;
  profile["overload_ratio"] = value.scale_hardware.overload_ratio;
  document["calibration_configured"] = value.scale_calibration.has_value();
  auto toolheads = document["toolheads"].to<JsonArray>();
  for (const auto& toolhead : value.toolheads) {
    auto encoded = toolheads.add<JsonObject>();
    encoded["backend_id"] = toolhead.backend_id;
    encoded["display_name"] = toolhead.display_name;
    encoded["nozzle_diameter_mm"] = toolhead.nozzle_diameter_mm;
    encoded["enabled"] = toolhead.enabled;
    encoded["nozzle_material"] = toolhead.nozzle_material;
    encoded["maximum_temperature_c"] = toolhead.maximum_temperature_c;
    encoded["notes"] = toolhead.notes;
  }
  auto reconciliation = document["reconciliation"].to<JsonObject>();
  reconciliation["normal_tolerance_grams"] =
      value.reconciliation.normal_tolerance_grams;
  reconciliation["warning_tolerance_grams"] =
      value.reconciliation.warning_tolerance_grams;
}

}  // namespace

bool ApplicationApiContext::authorize_mutation(
    std::string_view bearer_token) {
  const auto configured = configuration_.snapshot().web.access_token;
  return constant_time_token_match(bearer_token, configured);
}

core::Result<std::string> ApplicationApiContext::snapshot_json(
    api::Resource resource) {
  const auto now_ms = millis();
  const auto system = diagnostics_.snapshot(now_ms);
  const auto workflow = workflow_.snapshot();
  const auto backends = backend_worker_.snapshot();

  JsonDocument document;
  switch (resource) {
    case api::Resource::status: {
      write_system(document["system"].to<JsonObject>(), system);
      auto encoded_backends = document["backends"].to<JsonObject>();
      write_backend(encoded_backends["spoolman"].to<JsonObject>(), backends.spoolman);
      write_backend(
          encoded_backends["filabridge"].to<JsonObject>(), backends.filabridge);
      document["spool_generation"] = workflow.spool_generation;
      document["printer_revision"] = workflow.printer_revision;
      document["operations_revision"] = operations_.revision();
      break;
    }
    case api::Resource::device: {
      const auto configured = configuration_.snapshot();
      auto device = document["device"].to<JsonObject>();
      device["hostname"] = configured.device.hostname;
      device["hardware_id"] = boards::Wt32Sc01PlusRevA::id;
      device["ip_address"] = system.ip_address;
      if (!system.ip_address.empty()) {
        device["local_url"] = std::string("http://") + system.ip_address + "/";
      }
      auto build = document["build"].to<JsonObject>();
      build["version"] = diagnostics::build_info.project_version;
      build["git_sha"] = diagnostics::build_info.git_sha;
      build["build_date"] = diagnostics::build_info.build_date;
      build["esp32_platform"] = diagnostics::build_info.esp32_platform;
      build["arduino_framework"] = diagnostics::build_info.arduino_framework;
      build["config_schema"] = diagnostics::build_info.config_schema;
      break;
    }
    case api::Resource::health: {
      const bool essential_ok = system.nvs_ready && system.filesystem_ready &&
          system.display_ready && system.ui_task_running &&
          system.scale_task_running && system.network_task_running;
      const bool backend_degraded =
          workflow.spoolman == services::BackendAvailability::offline ||
          workflow.filabridge == services::BackendAvailability::offline;
      document["status"] =
          !essential_ok ? "unhealthy" : backend_degraded ? "degraded" : "healthy";
      document["local_services_ready"] = essential_ok;
      document["backend_degraded"] = backend_degraded;
      document["nfc_available"] = false;
      break;
    }
    case api::Resource::scale:
      write_scale(document["scale"].to<JsonObject>(), system);
      document["command_queue_depth"] = scale_commands_.pending();
      break;
    case api::Resource::nfc:
    case api::Resource::nfc_tag:
      return core::Result<std::string>::failure(unavailable(
          core::ErrorCategory::nfc_communication,
          "NFC reader is unavailable: ST25R3916B transport is disabled in this build"));
    case api::Resource::spool: {
      auto encoded = document["workflow"].to<JsonObject>();
      encoded["stage"] = workflow_stage_name(workflow.stage);
      encoded["spool_generation"] = workflow.spool_generation;
      encoded["printer_revision"] = workflow.printer_revision;
      encoded["spoolman"] = availability_name(workflow.spoolman);
      encoded["filabridge"] = availability_name(workflow.filabridge);
      if (workflow.spool.has_value()) {
        write_spool(encoded["spool"].to<JsonObject>(), *workflow.spool);
      }
      write_reconciliation(
          encoded["reconciliation"].to<JsonObject>(), workflow.reconciliation);
      break;
    }
    case api::Resource::printers: {
      document["revision"] = workflow.printer_revision;
      auto printers = document["printers"].to<JsonArray>();
      for (const auto& printer : workflow.printers) {
        write_printer(
            printers.add<JsonObject>(), printer, workflow.printer_revision);
      }
      break;
    }
    case api::Resource::toolheads: {
      document["revision"] = workflow.printer_revision;
      const auto configured = configuration_.snapshot();
      auto toolheads = document["toolheads"].to<JsonArray>();
      for (const auto& printer : workflow.printers) {
        for (const auto& toolhead : printer.toolheads) {
          auto encoded = toolheads.add<JsonObject>();
          encoded["printer_id"] = printer.id;
          encoded["printer_state"] = printer_state_name(printer.state);
          encoded["backend_id"] = toolhead.backend_id;
          encoded["display_number"] = toolhead.display_number;
          encoded["display_name"] = toolhead.display_name;
          if (toolhead.assigned_spool.has_value()) {
            encoded["assigned_spool_id"] = *toolhead.assigned_spool;
          } else {
            encoded["assigned_spool_id"] = nullptr;
          }
          const auto profile = std::find_if(
              configured.toolheads.begin(),
              configured.toolheads.end(),
              [&](const auto& candidate) {
                return candidate.backend_id == toolhead.backend_id;
              });
          if (profile != configured.toolheads.end()) {
            encoded["profile_enabled"] = profile->enabled;
            encoded["profile_name"] = profile->display_name;
          }
        }
      }
      break;
    }
    case api::Resource::redacted_configuration:
      write_configuration(document, configuration_.versioned_snapshot());
      break;
    case api::Resource::diagnostics: {
      write_system(document["system"].to<JsonObject>(), system);
      write_scale(document["scale"].to<JsonObject>(), system);
      auto encoded_backends = document["backends"].to<JsonObject>();
      write_backend(encoded_backends["spoolman"].to<JsonObject>(), backends.spoolman);
      write_backend(
          encoded_backends["filabridge"].to<JsonObject>(), backends.filabridge);
      auto queues = document["queues"].to<JsonObject>();
      queues["configuration"] = configuration_worker_.pending();
      queues["backend"] = backends.pending;
      queues["scale"] = scale_commands_.pending();
      queues["device_control"] = device_control_.pending();
      document["operation_registry_revision"] = operations_.revision();
      document["nfc_available"] = false;
      document["nfc_reason"] =
          "ST25R3916B transport disabled at compile time";
      break;
    }
    case api::Resource::logs: {
      const auto snapshot = logs_.snapshot(0U, logging::maximum_log_entries);
      document["oldest_cursor"] = snapshot.oldest_cursor;
      document["latest_cursor"] = snapshot.latest_cursor;
      document["dropped_count"] = snapshot.dropped_count;
      document["history_gap"] = snapshot.history_gap;
      auto logs = document["logs"].to<JsonArray>();
      for (const auto& entry : snapshot.entries) {
        auto encoded = logs.add<JsonObject>();
        encoded["sequence"] = entry.cursor;
        encoded["uptime_ms"] = entry.timestamp_ms;
        encoded["level"] = logging::to_string(entry.severity);
        encoded["source"] = logging::to_string(entry.component);
        encoded["message"] = entry.text();
        encoded["truncated"] = entry.truncated;
        encoded["redacted"] = entry.redacted;
      }
      break;
    }
    case api::Resource::update_boundary:
      document["supported"] = false;
      document["state"] = "not_available_in_phase_9";
      document["message"] =
          "OTA installation, A/B switching, and rollback begin in Phase 10";
      break;
  }
  return serialized(document);
}

core::Result<std::optional<std::string>>
ApplicationApiContext::operation_status_json(std::uint64_t operation_id) {
  const auto record = operations_.get(operation_id);
  if (!record.has_value()) {
    return core::Result<std::optional<std::string>>::success(std::nullopt);
  }
  JsonDocument document;
  document["operation_id"] = record->id;
  document["kind"] = application::to_string(record->kind);
  document["state"] = application::to_string(record->state);
  document["created_at_ms"] = record->created_at_ms;
  document["updated_at_ms"] = record->updated_at_ms;
  document["message"] = record->message;
  if (record->error.has_value()) {
    add_error(document["error"].to<JsonObject>(), record->error);
  }
  const auto encoded = serialized(document);
  if (!encoded.ok()) {
    return core::Result<std::optional<std::string>>::failure(encoded.error());
  }
  return core::Result<std::optional<std::string>>::success(encoded.value());
}

core::Result<api::OperationReceipt> ApplicationApiContext::receipt_result(
    application::CommandReceipt receipt,
    const char* unavailable_message) {
  if (receipt.accepted && receipt.operation_id != 0U) {
    return core::Result<api::OperationReceipt>::success(
        {receipt.operation_id});
  }
  const auto operation = operations_.get(receipt.operation_id);
  if (operation.has_value() && operation->error.has_value()) {
    return core::Result<api::OperationReceipt>::failure(*operation->error);
  }
  return core::Result<api::OperationReceipt>::failure(unavailable(
      core::ErrorCategory::backend_unavailable,
      unavailable_message,
      true));
}

core::Result<api::OperationReceipt> ApplicationApiContext::submit_fresh(
    const api::Mutation& mutation,
    std::uint32_t now_ms) {
  switch (mutation.kind) {
    case api::MutationKind::scale_tare:
      return receipt_result(
          scale_commands_.submit_tare(now_ms), "Scale command queue is unavailable");
    case api::MutationKind::scale_calibration: {
      const auto& payload =
          std::get<api::ScaleCalibrationMutation>(mutation.payload);
      return receipt_result(
          scale_commands_.submit_calibration(payload.reference_grams, now_ms),
          "Scale command queue is unavailable");
    }
    case api::MutationKind::nfc_read:
      return core::Result<api::OperationReceipt>::failure(unavailable(
          core::ErrorCategory::nfc_communication,
          "NFC reader is unavailable in this firmware build"));
    case api::MutationKind::backend_test:
      return receipt_result(
          backend_worker_.submit_refresh(), "Backend command queue is unavailable");
    case api::MutationKind::toolhead_assignment: {
      const auto& payload =
          std::get<api::ToolheadAssignmentMutation>(mutation.payload);
      services::ToolheadMutationPrecondition precondition;
      precondition.supplied = true;
      precondition.expected_previous_spool_id =
          payload.preconditions.expected_current_spool_id;
      precondition.expected_printer_state =
          parse_printer_state(payload.preconditions.expected_printer_state);
      return receipt_result(
          backend_worker_.submit_assignment_operation(
              payload.preconditions.printer_id,
              payload.backend_toolhead_id,
              payload.replace_occupied_confirmed,
              payload.preconditions.advanced_override,
              payload.preconditions.spool_generation,
              payload.expected_spool_id,
              std::move(precondition),
              payload.preconditions.printer_revision),
          "Backend assignment queue is unavailable");
    }
    case api::MutationKind::toolhead_unassignment: {
      const auto& payload =
          std::get<api::ToolheadUnassignmentMutation>(mutation.payload);
      services::ToolheadMutationPrecondition precondition;
      precondition.supplied = true;
      precondition.expected_previous_spool_id =
          payload.preconditions.expected_current_spool_id;
      precondition.expected_printer_state =
          parse_printer_state(payload.preconditions.expected_printer_state);
      return receipt_result(
          backend_worker_.submit_unassignment_operation(
              payload.preconditions.printer_id,
              payload.backend_toolhead_id,
              payload.preconditions.advanced_override,
              payload.preconditions.spool_generation,
              std::move(precondition),
              payload.preconditions.printer_revision),
          "Backend unassignment queue is unavailable");
    }
    case api::MutationKind::configuration_patch: {
      const auto& patch =
          std::get<api::ConfigurationPatchMutation>(mutation.payload);
      const auto proposed = apply_configuration_patch(
          configuration_.versioned_snapshot(), patch);
      if (!proposed.ok()) {
        return core::Result<api::OperationReceipt>::failure(proposed.error());
      }
      return receipt_result(
          configuration_worker_.submit_replace(
              proposed.value(), patch.expected_revision, now_ms),
          "Configuration command queue is unavailable");
    }
    case api::MutationKind::reboot:
      return receipt_result(
          device_control_.submit_reboot(now_ms),
          "Device control queue is unavailable");
    case api::MutationKind::factory_reset:
      return receipt_result(
          device_control_.submit_factory_reset(now_ms),
          "Device control queue is unavailable");
  }
  return core::Result<api::OperationReceipt>::failure(unavailable(
      core::ErrorCategory::configuration,
      "Unsupported API mutation"));
}

core::Result<api::OperationReceipt> ApplicationApiContext::submit(
    const api::Mutation& mutation) {
  const auto now_ms = millis();
  const std::lock_guard<std::mutex> lock(idempotency_mutex_);
  const auto existing = idempotency_.lookup(
      mutation.idempotency_key, mutation.payload_digest, now_ms);
  if (existing.status == IdempotencyLookupStatus::same) {
    return core::Result<api::OperationReceipt>::success(
        {existing.operation_id});
  }
  if (existing.status == IdempotencyLookupStatus::conflict) {
    return core::Result<api::OperationReceipt>::failure(unavailable(
        core::ErrorCategory::conflict,
        "Idempotency-Key was already used with a different request payload"));
  }

  const auto submitted = submit_fresh(mutation, now_ms);
  if (!submitted.ok()) return submitted;
  if (!idempotency_.insert(
          mutation.idempotency_key,
          mutation.payload_digest,
          submitted.value().operation_id,
          now_ms)) {
    return core::Result<api::OperationReceipt>::failure(unavailable(
        core::ErrorCategory::configuration,
        "Idempotency-Key exceeded the bounded ledger limit"));
  }
  logs_.append(
      now_ms,
      logging::LogSeverity::info,
      logging::LogComponent::web,
      std::string("Accepted API operation #") +
          std::to_string(submitted.value().operation_id));
  return submitted;
}

}  // namespace opentag::web
