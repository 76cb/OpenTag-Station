#include "web/application_api_context.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
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

core::Error update_error(const char* message, bool retryable = false) {
  return {core::ErrorCategory::firmware_update, message, retryable};
}

core::Error conflict_error(const char* message) {
  return {core::ErrorCategory::conflict, message, false};
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

std::string sha256_text(const opentag::ota::Sha256Digest& digest) {
  static constexpr char hexadecimal[] = "0123456789abcdef";
  std::string result(opentag::ota::sha256_digest_bytes * 2U, '0');
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    result[index * 2U] = hexadecimal[digest[index] >> 4U];
    result[index * 2U + 1U] = hexadecimal[digest[index] & 0x0FU];
  }
  return result;
}

bool decode_sha256(
    std::string_view encoded,
    opentag::ota::Sha256Digest& digest) {
  if (!api::valid_sha256_hex(encoded)) return false;
  const auto nibble = [](char value) -> std::uint8_t {
    return value <= '9'
        ? static_cast<std::uint8_t>(value - '0')
        : static_cast<std::uint8_t>(value - 'a' + 10);
  };
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    digest[index] = static_cast<std::uint8_t>(
        (nibble(encoded[index * 2U]) << 4U) |
        nibble(encoded[index * 2U + 1U]));
  }
  return true;
}

bool same_sha256(
    const opentag::ota::Sha256Digest& left,
    const opentag::ota::Sha256Digest& right) {
  volatile std::uint32_t difference = 0U;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    difference |= static_cast<std::uint32_t>(left[index] ^ right[index]);
  }
  return difference == 0U;
}

void write_partition(
    JsonObject object,
    const opentag::ota::PartitionDescriptor& partition) {
  object["label"] = partition.label.view();
  object["size"] = partition.size;
  object["subtype"] = partition.subtype;
  object["present"] = partition.present();
}

void write_firmware(
    JsonObject object,
    const opentag::ota::FirmwareDescriptor& firmware) {
  object["project"] = firmware.project_name.view();
  object["version"] = firmware.version.view();
  object["git_sha"] = firmware.git_sha.view();
  object["build_date"] = firmware.build_date.view();
  object["board_id"] = firmware.board_id.view();
  object["idf_version"] = firmware.idf_version.view();
}

const char* update_message(const opentag::ota::UpdateSnapshot& update) {
  switch (update.state) {
    case opentag::ota::UpdateState::idle:
      return "No firmware update is staged";
    case opentag::ota::UpdateState::upload_receiving:
    case opentag::ota::UpdateState::writing:
      return "Firmware is streaming to the inactive application slot";
    case opentag::ota::UpdateState::validating:
      return "Firmware transfer completed and validation is running";
    case opentag::ota::UpdateState::ready_to_activate:
    case opentag::ota::UpdateState::ready_to_reboot:
      if (update.activated) {
        return "Validated candidate is selected and waiting for reboot";
      }
      return update.activation_intent
          ? "Running-slot rollback metadata recovery is pending before candidate selection"
          : "Candidate is validated in the inactive slot but is not selected for boot";
    case opentag::ota::UpdateState::reboot_pending:
      return "Validated candidate reboot is pending";
    case opentag::ota::UpdateState::candidate_boot:
    case opentag::ota::UpdateState::validating_candidate:
      return "Candidate firmware is running inside its health-confirmation window";
    case opentag::ota::UpdateState::confirmed:
      return "Candidate firmware was confirmed healthy";
    case opentag::ota::UpdateState::rollback_pending:
      return "Candidate rollback is pending";
    case opentag::ota::UpdateState::rolled_back:
      return "Candidate firmware was rolled back";
    case opentag::ota::UpdateState::failed:
      return "Firmware update failed";
  }
  return "Firmware update state is unknown";
}

bool upload_available(opentag::ota::UpdateState state) {
  return state == opentag::ota::UpdateState::idle ||
      state == opentag::ota::UpdateState::failed ||
      state == opentag::ota::UpdateState::confirmed ||
      state == opentag::ota::UpdateState::rolled_back;
}

bool upload_in_progress(opentag::ota::UpdateState state) {
  return state == opentag::ota::UpdateState::upload_receiving ||
      state == opentag::ota::UpdateState::writing ||
      state == opentag::ota::UpdateState::validating;
}

bool validated_unselected_candidate(
    const opentag::ota::UpdateSnapshot& update) {
  return update.state == opentag::ota::UpdateState::ready_to_reboot &&
      update.validation_passed && update.calculated_sha_available &&
      same_sha256(update.expected_sha256, update.calculated_sha256) &&
      !update.activated && !update.activation_intent;
}

bool activation_retry_candidate(
    const opentag::ota::UpdateSnapshot& update) {
  const bool running_seed_pending =
      update.running_image_state ==
          opentag::ota::PartitionImageState::new_image ||
      update.running_image_state ==
          opentag::ota::PartitionImageState::pending_verify ||
      update.running_image_state ==
          opentag::ota::PartitionImageState::valid ||
      update.running_image_state ==
          opentag::ota::PartitionImageState::undefined;
  return update.state == opentag::ota::UpdateState::ready_to_reboot &&
      update.validation_passed && update.calculated_sha_available &&
      same_sha256(update.expected_sha256, update.calculated_sha256) &&
      !update.activated && update.activation_intent &&
      running_seed_pending && update.target.present() &&
      opentag::ota::same_partition(update.boot, update.running) &&
      opentag::ota::same_partition(update.inactive, update.target) &&
      !opentag::ota::same_partition(update.running, update.target);
}

bool safely_selected_candidate(
    const opentag::ota::UpdateSnapshot& update) {
  return update.state == opentag::ota::UpdateState::ready_to_reboot &&
      update.validation_passed && update.calculated_sha_available &&
      same_sha256(update.expected_sha256, update.calculated_sha256) &&
      update.activated && update.activation_intent &&
      update.target.present() &&
      opentag::ota::same_partition(update.boot, update.target) &&
      !opentag::ota::same_partition(update.running, update.target);
}

void write_update(
    JsonDocument& document,
    const opentag::ota::UpdateSnapshot& update,
    bool owner_ready) {
  document["revision"] = update.revision;
  document["generation"] = update.generation;
  document["operation_id"] = update.operation_id;
  document["upload_operation_id"] = update.operation_id;
  document["state"] = opentag::ota::to_string(update.state);
  document["message"] = update_message(update);
  document["image_size"] = update.image_size;
  document["received_bytes"] = update.bytes_received;
  document["started_at_ms"] = update.started_at_ms;
  document["updated_at_ms"] = update.updated_at_ms;
  document["candidate_started_at_ms"] = update.candidate_started_at_ms;
  document["expected_sha256"] = sha256_text(update.expected_sha256);
  if (update.calculated_sha_available) {
    document["calculated_sha256"] = sha256_text(update.calculated_sha256);
  }

  write_firmware(document["current"].to<JsonObject>(), update.current);
  auto candidate = document["candidate"].to<JsonObject>();
  write_firmware(candidate, update.candidate);
  candidate["operation_id"] = update.operation_id;
  candidate["upload_operation_id"] = update.operation_id;
  candidate["image_size"] = update.image_size;
  candidate["expected_sha256"] = sha256_text(update.expected_sha256);
  if (update.calculated_sha_available) {
    candidate["calculated_sha256"] = sha256_text(update.calculated_sha256);
  }

  auto partitions = document["partitions"].to<JsonObject>();
  write_partition(partitions["running"].to<JsonObject>(), update.running);
  write_partition(partitions["boot"].to<JsonObject>(), update.boot);
  write_partition(partitions["inactive"].to<JsonObject>(), update.inactive);
  write_partition(partitions["target"].to<JsonObject>(), update.target);
  if (update.last_invalid.has_value()) {
    write_partition(
        partitions["last_invalid"].to<JsonObject>(), *update.last_invalid);
  }

  auto progress = document["progress"].to<JsonObject>();
  progress["received_bytes"] = update.bytes_received;
  progress["image_size"] = update.image_size;
  progress["percent"] = update.image_size == 0U
      ? 0.0
      : std::min(
            100.0,
            static_cast<double>(update.bytes_received) * 100.0 /
                static_cast<double>(update.image_size));

  auto validation = document["validation"].to<JsonObject>();
  validation["passed"] = update.validation_passed;
  validation["calculated_sha_available"] = update.calculated_sha_available;
  validation["result"] = update.validation_passed
      ? "passed"
      : update.state == opentag::ota::UpdateState::failed
          ? "failed"
          : "not_complete";

  auto activation = document["activation"].to<JsonObject>();
  activation["intent"] = update.activation_intent;
  activation["activated"] = update.activated;
  activation["boot_target_selected"] = update.activated;

  auto rollback = document["rollback"].to<JsonObject>();
  rollback["supported"] = update.rollback_available;
  rollback["running_image_state"] =
      opentag::ota::to_string(update.running_image_state);
  rollback["state"] = update.state == opentag::ota::UpdateState::rolled_back
      ? "rolled_back"
      : update.state == opentag::ota::UpdateState::rollback_pending
          ? "pending"
          : "not_started";

  const bool validated_not_selected =
      validated_unselected_candidate(update);
  const bool activation_retry = activation_retry_candidate(update);
  const bool safely_selected = safely_selected_candidate(update);
  const bool inactive_topology_available = update.inactive.present();
  auto capabilities = document["capabilities"].to<JsonObject>();
  capabilities["owner_ready"] = owner_ready;
  capabilities["maximum_image_bytes"] = inactive_topology_available
      ? std::min(
            static_cast<std::uint32_t>(api::maximum_firmware_image_bytes),
            update.inactive.size)
      : 0U;
  capabilities["upload_available"] = owner_ready &&
      inactive_topology_available && upload_available(update.state);
  capabilities["cancel_available"] = owner_ready &&
      (upload_in_progress(update.state) || validated_not_selected);
  capabilities["reboot_available"] = owner_ready &&
      (validated_not_selected || activation_retry || safely_selected);

  if (!update.last_error.empty()) {
    auto error = document["last_error"].to<JsonObject>();
    error["category"] = "firmware_update";
    error["message"] = update.last_error.view();
  }
}

bool valid_idempotency_key(std::string_view value) {
  return !value.empty() &&
      value.size() <= IdempotencyLedger::maximum_key_bytes &&
      std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' ||
            character == '_' || character == '.' || character == ':';
      });
}

std::uint64_t streaming_upload_digest(const StreamingUploadRequest& request) {
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  std::uint64_t digest = offset_basis;
  const auto absorb = [&](std::uint8_t byte) {
    digest ^= static_cast<std::uint64_t>(byte);
    digest *= prime;
  };
  constexpr std::string_view domain = "POST:/api/v1/update/upload:v1";
  for (const auto character : domain) {
    absorb(static_cast<std::uint8_t>(character));
  }
  for (std::size_t shift = 0U; shift < sizeof(request.expected_generation); ++shift) {
    absorb(static_cast<std::uint8_t>(
        request.expected_generation >> (shift * 8U)));
  }
  for (std::size_t shift = 0U; shift < sizeof(request.expected_length); ++shift) {
    absorb(static_cast<std::uint8_t>(
        request.expected_length >> (shift * 8U)));
  }
  for (const auto byte : request.expected_sha256) absorb(byte);
  return digest;
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
      queues["ota"] = ota_worker_.pending();
      document["ota_owner_ready"] = ota_worker_.ready();
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
    case api::Resource::update:
      write_update(document, ota_worker_.snapshot(), ota_worker_.ready());
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
    case api::MutationKind::update_reboot:
    case api::MutationKind::update_cancel: {
      const auto& payload =
          std::get<api::UpdateControlMutation>(mutation.payload);
      opentag::ota::Sha256Digest supplied_sha256{};
      if (!decode_sha256(payload.expected_sha256, supplied_sha256)) {
        return core::Result<api::OperationReceipt>::failure(update_error(
            "OTA control requires a 64-character lowercase SHA-256 digest"));
      }
      const auto update = ota_worker_.snapshot();
      if (payload.upload_operation_id != update.operation_id ||
          payload.expected_generation != update.generation ||
          !same_sha256(supplied_sha256, update.expected_sha256)) {
        return core::Result<api::OperationReceipt>::failure(conflict_error(
            "OTA operation, generation, or image digest changed before control was submitted"));
      }
      const bool validated_not_selected =
          validated_unselected_candidate(update);
      const bool activation_retry = activation_retry_candidate(update);
      const bool safely_selected = safely_selected_candidate(update);
      const bool reboot_candidate =
          validated_not_selected || activation_retry || safely_selected;
      const bool cancellable_upload =
          update.state == opentag::ota::UpdateState::upload_receiving ||
          update.state == opentag::ota::UpdateState::writing;
      if ((mutation.kind == api::MutationKind::update_reboot &&
           !reboot_candidate) ||
          (mutation.kind == api::MutationKind::update_cancel &&
           !validated_not_selected && !cancellable_upload)) {
        return core::Result<api::OperationReceipt>::failure(conflict_error(
            mutation.kind == api::MutationKind::update_reboot
                ? "OTA candidate is not validated and ready for reboot"
                : "OTA upload or validated inactive candidate is not cancellable"));
      }
      const opentag::ota::OperationPrecondition precondition{
          update.operation_id,
          update.generation,
      };
      const auto receipt = mutation.kind == api::MutationKind::update_reboot
          ? ota_worker_.submit_reboot(precondition, now_ms)
          : ota_worker_.submit_cancel(precondition, now_ms);
      if (receipt.accepted && receipt.operation_id != 0U) {
        return core::Result<api::OperationReceipt>::success(
            {receipt.operation_id});
      }
      const auto operation = operations_.get(receipt.operation_id);
      if (operation.has_value() && operation->error.has_value()) {
        return core::Result<api::OperationReceipt>::failure(
            *operation->error);
      }
      return core::Result<api::OperationReceipt>::failure(update_error(
          "OTA control queue is unavailable", true));
    }
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

core::Result<StreamingUploadSession>
ApplicationApiContext::begin_streaming_upload(
    const StreamingUploadRequest& request) {
  if (!valid_idempotency_key(request.idempotency_key) ||
      request.expected_length == 0U ||
      request.expected_length > api::maximum_firmware_image_bytes) {
    return core::Result<StreamingUploadSession>::failure(update_error(
        "Firmware upload metadata is invalid or exceeds the 5 MiB limit"));
  }

  const auto now_ms = millis();
  const auto digest = streaming_upload_digest(request);
  const std::lock_guard<std::mutex> lock(idempotency_mutex_);
  const auto existing = idempotency_.lookup(
      request.idempotency_key, digest, now_ms);
  if (existing.status == IdempotencyLookupStatus::same) {
    const auto current = ota_worker_.snapshot();
    return core::Result<StreamingUploadSession>::success({
        {existing.operation_id, current.generation},
        existing.operation_id,
        true,
        current.operation_id == existing.operation_id
            ? std::optional<opentag::ota::UpdateSnapshot>{current}
            : std::nullopt,
    });
  }
  if (existing.status == IdempotencyLookupStatus::conflict) {
    return core::Result<StreamingUploadSession>::failure(conflict_error(
        "Idempotency-Key was already used with different firmware metadata"));
  }

  const auto operation_id = operations_.begin(
      application::OperationKind::firmware_upload,
      now_ms,
      "Firmware upload accepted");
  opentag::ota::BeginUploadRequest begin;
  begin.operation_id = operation_id;
  begin.expected_generation = request.expected_generation;
  begin.expected_length = request.expected_length;
  begin.expected_sha256 = request.expected_sha256;
  const auto started = ota_worker_.begin_upload(begin, now_ms);
  if (!started.ok()) {
    const auto cleanup_now_ms = millis();
    const auto current = ota_worker_.snapshot();
    if (current.operation_id == operation_id &&
        current.generation != 0U &&
        (current.state == opentag::ota::UpdateState::upload_receiving ||
         current.state == opentag::ota::UpdateState::writing)) {
      const opentag::ota::OperationPrecondition precondition{
          current.operation_id,
          current.generation,
      };
      (void)ota_worker_.abort_upload(precondition, cleanup_now_ms);
    }
    operations_.fail(operation_id, cleanup_now_ms, started.error());
    return core::Result<StreamingUploadSession>::failure(started.error());
  }
  const opentag::ota::OperationPrecondition precondition{
      started.value().operation_id,
      started.value().generation,
  };
  if (!idempotency_.insert(
          request.idempotency_key,
          digest,
          operation_id,
          now_ms)) {
    const auto error = update_error(
        "Idempotency-Key exceeded the bounded ledger limit");
    (void)ota_worker_.abort_upload(precondition, now_ms);
    operations_.fail(operation_id, now_ms, error);
    return core::Result<StreamingUploadSession>::failure(error);
  }
  operations_.mark_running(
      operation_id,
      now_ms,
      "Firmware is streaming to the inactive application slot");
  logs_.append(
      now_ms,
      logging::LogSeverity::info,
      logging::LogComponent::web,
      std::string("Accepted firmware upload operation #") +
          std::to_string(operation_id));
  return core::Result<StreamingUploadSession>::success({
      precondition,
      operation_id,
      false,
      std::nullopt,
  });
}

core::Result<opentag::ota::UpdateSnapshot>
ApplicationApiContext::write_streaming_upload(
    const StreamingUploadSession& session,
    core::ByteView chunk) {
  if (session.duplicate || session.operation_id == 0U ||
      chunk.data == nullptr || chunk.empty() ||
      chunk.size > opentag::ota::maximum_upload_chunk_bytes) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(update_error(
        "Firmware upload session or chunk is invalid"));
  }
  return ota_worker_.write_chunk(session.precondition, chunk, millis());
}

core::Result<opentag::ota::UpdateSnapshot>
ApplicationApiContext::finish_streaming_upload(
    const StreamingUploadSession& session) {
  if (session.duplicate || session.operation_id == 0U) {
    return core::Result<opentag::ota::UpdateSnapshot>::failure(update_error(
        "Duplicate firmware upload sessions cannot finish a new write"));
  }
  const auto now_ms = millis();
  const auto finished = ota_worker_.finish_and_activate(
      session.precondition, now_ms);
  if (!finished.ok()) {
    operations_.fail(session.operation_id, now_ms, finished.error());
    return finished;
  }
  const auto& update = finished.value();
  if (update.state != opentag::ota::UpdateState::ready_to_reboot ||
      !update.validation_passed || update.activated) {
    const auto error = update_error(
        "Validated firmware did not reach the safe inactive-slot reboot boundary");
    operations_.fail(session.operation_id, now_ms, error);
    return core::Result<opentag::ota::UpdateSnapshot>::failure(error);
  }
  operations_.succeed(
      session.operation_id,
      now_ms,
      "Firmware validated in the inactive slot; reboot confirmation required");
  return finished;
}

void ApplicationApiContext::abort_streaming_upload(
    const StreamingUploadSession& session,
    const core::Error& reason) {
  if (session.duplicate || session.operation_id == 0U) return;
  const auto now_ms = millis();
  (void)ota_worker_.abort_upload(session.precondition, now_ms);
  operations_.fail(session.operation_id, now_ms, reason);
  logs_.append(
      now_ms,
      logging::LogSeverity::warning,
      logging::LogComponent::web,
      std::string("Aborted firmware upload operation #") +
          std::to_string(session.operation_id));
}

}  // namespace opentag::web
