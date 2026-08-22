#include "application/configuration_worker.hpp"

#include <Arduino.h>

#include <new>

namespace opentag::application {
namespace {

bool wifi_changed(
    const config::Configuration& before,
    const config::Configuration& after) {
  return before.device.hostname != after.device.hostname ||
      before.wifi.ssid != after.wifi.ssid ||
      before.wifi.password != after.wifi.password ||
      before.wifi.auto_reconnect != after.wifi.auto_reconnect ||
      before.wifi.connect_timeout_ms != after.wifi.connect_timeout_ms ||
      before.wifi.reconnect_initial_ms != after.wifi.reconnect_initial_ms ||
      before.wifi.reconnect_max_ms != after.wifi.reconnect_max_ms;
}

}  // namespace

bool ConfigurationWorker::start() {
  if (task_ != nullptr) return true;
  constexpr UBaseType_t queue_depth = 8U;
  queue_ = xQueueCreate(queue_depth, sizeof(Command*));
  if (queue_ == nullptr) return false;
  constexpr std::uint32_t stack_bytes = 8192U;
  constexpr UBaseType_t priority = 1U;
  constexpr BaseType_t core = 0;
  if (xTaskCreatePinnedToCore(
          task_entry,
          "opentag-config",
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

bool ConfigurationWorker::enqueue(Command* command) {
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

CommandReceipt ConfigurationWorker::submit_replace(
    const config::Configuration& configuration,
    std::uint64_t expected_revision,
    std::uint32_t now_ms,
    OperationKind operation_kind) {
  const auto operation_id = operations_.begin(
      operation_kind, now_ms, "Configuration update queued");
  auto* command = new (std::nothrow) Command;
  if (command == nullptr) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Configuration command allocation failed",
         true});
    return {false, operation_id};
  }
  command->type = CommandType::replace;
  command->configuration = configuration;
  command->operation_kind = operation_kind;
  command->expected_revision = expected_revision;
  command->operation_id = operation_id;
  if (operation_kind == OperationKind::network_connect &&
      !network_connect_receipt_.expect(operation_id)) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::conflict,
         "Another Save & Connect receipt is still pending",
         true});
    delete command;
    return {false, operation_id};
  }
  if (!enqueue(command)) {
    if (operation_kind == OperationKind::network_connect) {
      network_connect_receipt_.clear(operation_id);
    }
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Configuration command queue is unavailable or full",
         true});
    return {false, operation_id};
  }
  return {true, operation_id};
}

bool ConfigurationWorker::submit_setup_completion(services::SetupStep step) {
  const auto now_ms = millis();
  const auto operation_id = operations_.begin(
      OperationKind::configuration, now_ms, "Setup progress update queued");
  if (static_cast<std::uint8_t>(step) >
      static_cast<std::uint8_t>(services::SetupStep::ready)) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Setup step is invalid",
         false});
    return false;
  }
  auto* command = new (std::nothrow) Command;
  if (command == nullptr) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Setup progress command allocation failed",
         true});
    return false;
  }
  command->type = CommandType::complete_setup_step;
  command->setup_step = step;
  command->operation_id = operation_id;
  if (!enqueue(command)) {
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::configuration,
         "Configuration command queue is unavailable or full",
         true});
    return false;
  }
  return true;
}

void ConfigurationWorker::task_entry(void* context) {
  static_cast<ConfigurationWorker*>(context)->run();
}

void ConfigurationWorker::run() {
  for (;;) {
    Command* command = nullptr;
    if (xQueueReceive(queue_, &command, portMAX_DELAY) != pdTRUE ||
        command == nullptr) {
      continue;
    }
    const auto started_at_ms = millis();
    operations_.mark_running(
        command->operation_id,
        started_at_ms,
        command->operation_kind == OperationKind::network_connect
            ? "Waiting for HTTP receipt delivery"
            : command->type == CommandType::replace
            ? "Validating configuration revision"
            : "Applying setup progress to latest configuration");

    if (command->operation_kind == OperationKind::network_connect) {
      constexpr std::uint32_t receipt_timeout_ms = 5000U;
      while (!network_connect_receipt_.delivered(command->operation_id) &&
             static_cast<std::uint32_t>(millis() - started_at_ms) <
                 receipt_timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10U));
      }
      if (!network_connect_receipt_.delivered(command->operation_id)) {
        operations_.fail(
            command->operation_id,
            millis(),
            {core::ErrorCategory::network,
             "Connect receipt was not delivered; settings and radio were unchanged",
             true});
        Serial.printf(
            "connect_receipt=failed operation=%llu ap=retained\n",
            static_cast<unsigned long long>(command->operation_id));
        network_connect_receipt_.clear(command->operation_id);
        delete command;
        pending_.fetch_sub(1U, std::memory_order_relaxed);
        continue;
      }
      Serial.printf(
          "connect_receipt=delivered operation=%llu\n",
          static_cast<unsigned long long>(command->operation_id));
    }

    auto before = configuration_.snapshot();
    auto proposed = before;
    auto saved = core::Result<void>::failure(
        {core::ErrorCategory::configuration,
         "Configuration command was not processed",
         false});
    if (command->type == CommandType::replace) {
      proposed = command->configuration;
      saved = configuration_.replace_if_revision(
          proposed, command->expected_revision);
    } else {
      constexpr std::uint8_t maximum_attempts = 3U;
      for (std::uint8_t attempt = 0U; attempt < maximum_attempts; ++attempt) {
        const auto latest = configuration_.versioned_snapshot();
        before = latest.configuration;
        proposed = before;
        proposed.setup.completed_steps |=
            1U << static_cast<std::uint8_t>(command->setup_step);
        if (command->setup_step == services::SetupStep::ready) {
          proposed.setup.ready_confirmed = true;
        }
        saved = configuration_.replace_if_revision(
            proposed, latest.revision);
        if (saved.ok() ||
            saved.error().category != core::ErrorCategory::conflict ||
            !saved.error().retryable) {
          break;
        }
      }
    }
    last_operation_succeeded_.store(saved.ok(), std::memory_order_relaxed);
    const bool should_reconfigure = saved.ok() &&
        (wifi_changed(before, proposed) ||
         command->operation_kind == OperationKind::network_connect);
    if (should_reconfigure) {
      Serial.printf(
          "configuration=persisted operation=%llu\n",
          static_cast<unsigned long long>(command->operation_id));
      network_.request_reconfigure(proposed.device, proposed.wifi);
    }
    const auto completed_at_ms = millis();
    if (saved.ok()) {
      operations_.succeed(
          command->operation_id,
          completed_at_ms,
          "Configuration persisted");
    } else {
      operations_.fail(command->operation_id, completed_at_ms, saved.error());
    }
    if (command->operation_kind == OperationKind::network_connect) {
      network_connect_receipt_.clear(command->operation_id);
    }
    delete command;
    pending_.fetch_sub(1U, std::memory_order_relaxed);
  }
}

}  // namespace opentag::application
