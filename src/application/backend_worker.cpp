#include "application/backend_worker.hpp"

#include <Arduino.h>
#include <WiFi.h>

#include <new>
#include <utility>

namespace opentag::application {
namespace {

core::Error wifi_offline_error() {
  return {
      core::ErrorCategory::network,
      "Wi-Fi is offline; backend requests are paused",
      true,
  };
}

bool same_settings(
    const config::SpoolmanSettings& left,
    const config::SpoolmanSettings& right) {
  return left.url == right.url &&
      left.authentication_token == right.authentication_token &&
      left.identity_field == right.identity_field &&
      left.nfc_uid_field == right.nfc_uid_field &&
      left.ca_certificate_pem == right.ca_certificate_pem;
}

std::string bounded_status_text(std::string value) {
  constexpr std::size_t maximum = 128U;
  if (value.size() > maximum) value.resize(maximum);
  return value;
}

BackendRuntimeSnapshot runtime_snapshot(
    const integrations::spoolman::SpoolmanStatus& status) {
  BackendRuntimeSnapshot result;
  result.connected = status.connected;
  result.healthy = status.healthy;
  result.version_formally_tested = status.version_formally_tested;
  result.version = bounded_status_text(status.version);
  result.capability_bits = status.capabilities.bits();
  result.last_error = status.last_error;
  if (result.last_error.has_value()) {
    result.last_error->message = bounded_status_text(result.last_error->message);
  }
  return result;
}

BackendRuntimeSnapshot runtime_snapshot(
    const integrations::filabridge::FilaBridgeStatus& status) {
  BackendRuntimeSnapshot result;
  result.connected = status.connected;
  result.healthy = status.healthy;
  result.version_formally_tested = status.version_formally_tested;
  result.version = bounded_status_text(status.version);
  result.capability_bits = status.capabilities.bits();
  result.last_error = status.last_error;
  if (result.last_error.has_value()) {
    result.last_error->message = bounded_status_text(result.last_error->message);
  }
  return result;
}

bool same_settings(
    const config::FilaBridgeSettings& left,
    const config::FilaBridgeSettings& right) {
  return left.url == right.url &&
      left.authentication_token == right.authentication_token &&
      left.selected_printer_id == right.selected_printer_id &&
      left.ca_certificate_pem == right.ca_certificate_pem;
}

}  // namespace

bool BackendWorker::start() {
  if (task_ != nullptr) return true;
  constexpr UBaseType_t queue_depth = 12U;
  queue_ = xQueueCreate(queue_depth, sizeof(Command*));
  if (queue_ == nullptr) return false;
  constexpr std::uint32_t stack_bytes = 12288U;
  constexpr UBaseType_t priority = 1U;
  constexpr BaseType_t core = 0;
  if (xTaskCreatePinnedToCore(
          task_entry,
          "opentag-backend",
          stack_bytes,
          this,
          priority,
          &task_,
          core) != pdPASS) {
    vQueueDelete(queue_);
    queue_ = nullptr;
    return false;
  }
  return true;
}

bool BackendWorker::enqueue(Command* command) {
  if (command == nullptr || queue_ == nullptr) {
    delete command;
    return false;
  }
  pending_.fetch_add(1U, std::memory_order_relaxed);
  if (xQueueSend(queue_, &command, 0U) != pdTRUE) {
    pending_.fetch_sub(1U, std::memory_order_relaxed);
    delete command;
    return false;
  }
  return true;
}

bool BackendWorker::submit_identified_spool(
    const nfc::openprinttag::MaterialRecord& material,
    const nfc::nfcv::Uid& uid,
    domain::WeightReading physical_weight,
    domain::EmptyWeightCandidates supplemental_empty_weights) {
  auto* command = new (std::nothrow) Command;
  if (command == nullptr) return false;
  command->type = CommandType::identified_spool;
  command->material = material;
  command->uid = uid;
  command->physical_weight = physical_weight;
  command->supplemental_empty_weights = supplemental_empty_weights;
  return enqueue(command);
}


CommandReceipt BackendWorker::submit_assignment_operation(
    std::string printer_id,
    int backend_toolhead_id,
    bool replace_occupied_confirmed,
    bool advanced_active_print_override,
    std::optional<std::uint64_t> expected_spool_generation,
    std::optional<domain::SpoolId> expected_spool_id,
    services::ToolheadMutationPrecondition precondition,
    std::optional<std::uint64_t> expected_printer_revision) {
  const auto now_ms = millis();
  const auto operation_id = operations_.begin(
      OperationKind::toolhead_assignment, now_ms, "Assignment queued");
  if (operation_id == 0U) return {false, 0U};
  if (printer_id.empty() || printer_id.size() > 128U ||
      backend_toolhead_id < 0 || backend_toolhead_id > 31) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Assignment target is invalid",
         false});
    return {false, operation_id};
  }
  auto* command = new (std::nothrow) Command;
  if (command == nullptr) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::backend_unavailable,
         "Backend command allocation failed",
         true});
    return {false, operation_id};
  }
  command->type = CommandType::assign;
  command->printer_id = std::move(printer_id);
  command->backend_toolhead_id = backend_toolhead_id;
  command->replace_occupied_confirmed = replace_occupied_confirmed;
  command->advanced_active_print_override = advanced_active_print_override;
  command->expected_spool_generation = expected_spool_generation;
  command->expected_spool_id = expected_spool_id;
  command->precondition = std::move(precondition);
  command->expected_printer_revision = expected_printer_revision;
  command->operation_id = operation_id;
  command->enqueued_at_ms = now_ms;
  if (!enqueue(command)) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::backend_unavailable,
         "Backend command queue is full",
         true});
    return {false, operation_id};
  }
  return {true, operation_id};
}

CommandReceipt BackendWorker::submit_unassignment_operation(
    std::string printer_id,
    int backend_toolhead_id,
    bool advanced_active_print_override,
    std::optional<std::uint64_t> expected_spool_generation,
    services::ToolheadMutationPrecondition precondition,
    std::optional<std::uint64_t> expected_printer_revision) {
  const auto now_ms = millis();
  const auto operation_id = operations_.begin(
      OperationKind::toolhead_unassignment, now_ms, "Unassignment queued");
  if (operation_id == 0U) return {false, 0U};
  if (printer_id.empty() || printer_id.size() > 128U ||
      backend_toolhead_id < 0 || backend_toolhead_id > 31) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Unassignment target is invalid",
         false});
    return {false, operation_id};
  }
  auto* command = new (std::nothrow) Command;
  if (command == nullptr) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::backend_unavailable,
         "Backend command allocation failed",
         true});
    return {false, operation_id};
  }
  command->type = CommandType::unassign;
  command->printer_id = std::move(printer_id);
  command->backend_toolhead_id = backend_toolhead_id;
  command->advanced_active_print_override = advanced_active_print_override;
  command->expected_spool_generation = expected_spool_generation;
  command->precondition = std::move(precondition);
  command->expected_printer_revision = expected_printer_revision;
  command->operation_id = operation_id;
  command->enqueued_at_ms = now_ms;
  if (!enqueue(command)) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::backend_unavailable,
         "Backend command queue is full",
         true});
    return {false, operation_id};
  }
  return {true, operation_id};
}

CommandReceipt BackendWorker::submit_refresh() {
  const auto now_ms = millis();
  const auto operation_id = operations_.begin(
      OperationKind::backend_probe, now_ms, "Backend probe queued");
  if (operation_id == 0U) return {false, 0U};
  auto* command = new (std::nothrow) Command;
  if (command == nullptr) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::backend_unavailable,
         "Backend command allocation failed",
         true});
    return {false, operation_id};
  }
  command->type = CommandType::refresh;
  command->operation_id = operation_id;
  command->enqueued_at_ms = now_ms;
  if (!enqueue(command)) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::backend_unavailable,
         "Backend command queue is full",
         true});
    return {false, operation_id};
  }
  return {true, operation_id};
}

BackendWorkerSnapshot BackendWorker::snapshot() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  auto result = status_;
  result.pending = pending();
  return result;
}

void BackendWorker::task_entry(void* context) {
  static_cast<BackendWorker*>(context)->run();
}

void BackendWorker::probe_backends(std::uint64_t operation_id) {
  const auto now_ms = millis();
  if (operation_id != 0U) {
    operations_.mark_running(operation_id, now_ms, "Probing backends");
  }
  if (WiFi.status() != WL_CONNECTED) {
    const auto error = wifi_offline_error();
    workflow_.set_spoolman_probe(false, error);
    workflow_.set_filabridge_probe(false, false, error);
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.spoolman = {};
      status_.filabridge = {};
      status_.spoolman.last_error = error;
      status_.filabridge.last_error = error;
      ++status_.revision;
    }
    if (operation_id != 0U) operations_.fail(operation_id, now_ms, error);
    return;
  }

  const auto configured = configuration_.snapshot();
  spoolman_.configure(configured.spoolman);
  filabridge_.configure(configured.filabridge);
  resolver_.configure(configured.spoolman);
  applied_spoolman_settings_ = configured.spoolman;
  applied_filabridge_settings_ = configured.filabridge;

  const auto spoolman_status = spoolman_.probe();
  workflow_.set_spoolman_probe(
      spoolman_status.ok() && spoolman_status.value().healthy,
      spoolman_status.ok()
          ? std::optional<core::Error>{}
          : std::optional<core::Error>{spoolman_status.error()});

  const auto filabridge_status = filabridge_.probe();
  std::optional<core::Error> printer_refresh_error;
  if (!filabridge_status.ok()) {
    workflow_.set_filabridge_probe(false, false, filabridge_status.error());
  } else {
    const auto capabilities = filabridge_.capabilities();
    const bool assignment_available =
        capabilities.has(integrations::BackendCapability::map_toolhead) &&
        capabilities.has(integrations::BackendCapability::unmap_toolhead);
    workflow_.set_filabridge_probe(
        true,
        assignment_available,
        filabridge_status.value().last_error);
    const auto refreshed = workflow_.refresh_printers();
    printer_refresh_error = refreshed.filabridge_error;
  }
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.spoolman = runtime_snapshot(spoolman_.status());
    status_.filabridge = runtime_snapshot(filabridge_.status());
    ++status_.revision;
  }
  if (operation_id != 0U) {
    if (!spoolman_status.ok()) {
      operations_.fail(operation_id, millis(), spoolman_status.error());
    } else if (!filabridge_status.ok()) {
      operations_.fail(operation_id, millis(), filabridge_status.error());
    } else if (printer_refresh_error.has_value()) {
      operations_.fail(operation_id, millis(), *printer_refresh_error);
    } else {
      operations_.succeed(operation_id, millis(), "Backend probes completed");
    }
  }
}

void BackendWorker::process(Command& command) {
  const auto configured = configuration_.snapshot();
  if (command.type == CommandType::identified_spool) {
    if (!applied_spoolman_settings_.has_value() ||
        !same_settings(*applied_spoolman_settings_, configured.spoolman)) {
      spoolman_.configure(configured.spoolman);
      resolver_.configure(configured.spoolman);
      applied_spoolman_settings_ = configured.spoolman;
    }
    const auto state = workflow_.accept_identified_spool(
        command.material,
        command.uid,
        command.physical_weight,
        command.supplemental_empty_weights,
        {configured.reconciliation.normal_tolerance_grams,
         configured.reconciliation.warning_tolerance_grams});
    (void)state;
    return;
  }
  if (command.type == CommandType::assign) {
    if (!applied_filabridge_settings_.has_value() ||
        !same_settings(*applied_filabridge_settings_, configured.filabridge)) {
      probe_backends();
    }
    if (static_cast<std::uint32_t>(millis() - command.enqueued_at_ms) >
        destructive_command_expiry_ms) {
      operations_.fail(
          command.operation_id,
          millis(),
          {core::ErrorCategory::invalid_response,
           "Assignment expired in the backend queue; refresh and retry",
           false});
      return;
    }
    operations_.mark_running(
        command.operation_id, millis(), "Verifying assignment preconditions");
    const auto result = workflow_.assign(
        command.printer_id,
        command.backend_toolhead_id,
        command.replace_occupied_confirmed,
        command.advanced_active_print_override,
        configured.toolheads,
        command.expected_spool_generation,
        command.expected_spool_id,
        command.precondition,
        command.expected_printer_revision);
    if (!result.ok()) {
      operations_.fail(command.operation_id, millis(), result.error());
    } else if (result.value().verified()) {
      operations_.succeed(
          command.operation_id, millis(), "Assignment verified by readback");
    } else {
      operations_.require_confirmation(
          command.operation_id,
          millis(),
          result.value().outcome ==
                  services::AssignmentOutcome::replacement_confirmation_required
              ? "Replacement confirmation required"
              : "Advanced printer-state override required");
    }
    return;
  }
  if (command.type == CommandType::unassign) {
    if (!applied_filabridge_settings_.has_value() ||
        !same_settings(*applied_filabridge_settings_, configured.filabridge)) {
      probe_backends();
    }
    if (static_cast<std::uint32_t>(millis() - command.enqueued_at_ms) >
        destructive_command_expiry_ms) {
      operations_.fail(
          command.operation_id,
          millis(),
          {core::ErrorCategory::invalid_response,
           "Unassignment expired in the backend queue; refresh and retry",
           false});
      return;
    }
    operations_.mark_running(
        command.operation_id, millis(), "Verifying unassignment preconditions");
    const auto result = workflow_.unassign(
        command.printer_id,
        command.backend_toolhead_id,
        command.advanced_active_print_override,
        command.expected_spool_generation,
        command.precondition,
        command.expected_printer_revision);
    if (!result.ok()) {
      operations_.fail(command.operation_id, millis(), result.error());
    } else if (result.value().verified()) {
      operations_.succeed(
          command.operation_id, millis(), "Unassignment verified by readback");
    } else {
      operations_.require_confirmation(
          command.operation_id,
          millis(),
          "Advanced printer-state override required");
    }
    return;
  }
  probe_backends(command.operation_id);
}

void BackendWorker::run() {
  last_probe_ms_ = millis() - probe_interval_ms;
  for (;;) {
    Command* command = nullptr;
    if (xQueueReceive(queue_, &command, pdMS_TO_TICKS(250U)) == pdTRUE &&
        command != nullptr) {
      process(*command);
      delete command;
      pending_.fetch_sub(1U, std::memory_order_relaxed);
    }
    const auto now_ms = millis();
    if (static_cast<std::uint32_t>(now_ms - last_probe_ms_) >=
        probe_interval_ms) {
      probe_backends();
      // Schedule from completion, not from the timestamp captured before a
      // potentially slow DNS/HTTP probe. Otherwise a probe cycle longer than
      // the interval immediately starts another cycle and can become a
      // permanent backend retry storm.
      last_probe_ms_ = millis();
    }
  }
}

}  // namespace opentag::application
