#include "application/device_control_worker.hpp"

#include <Arduino.h>
#include <esp_system.h>

namespace opentag::application {

bool DeviceControlWorker::start() {
  if (task_ != nullptr) return true;
  queue_ = xQueueCreate(1U, sizeof(Command));
  if (queue_ == nullptr) return false;
  if (xTaskCreatePinnedToCore(
          task_entry,
          "opentag-control",
          4096U,
          this,
          1U,
          &task_,
          0) != pdPASS) {
    vQueueDelete(queue_);
    queue_ = nullptr;
    return false;
  }
  return true;
}

CommandReceipt DeviceControlWorker::submit_reboot(std::uint32_t now_ms) {
  return submit(Action::reboot, now_ms);
}

CommandReceipt DeviceControlWorker::submit_factory_reset(std::uint32_t now_ms) {
  return submit(Action::factory_reset, now_ms);
}

CommandReceipt DeviceControlWorker::submit(Action action, std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(active_mutex_);
  const auto kind = action == Action::reboot ? OperationKind::reboot
                                              : OperationKind::factory_reset;
  if (active_operation_id_ != 0U) {
    if (active_action_ == action) return {true, active_operation_id_};
    const auto rejected_id = operations_.begin(
        kind, now_ms, "Device control request rejected");
    operations_.fail(
        rejected_id,
        now_ms,
        {core::ErrorCategory::conflict,
         "A different device control action is already in progress",
         false});
    return {false, rejected_id};
  }

  const auto operation_id = operations_.begin(
      kind,
      now_ms,
      action == Action::reboot ? "Device reboot queued"
                               : "Factory reset queued");
  Command command{action, operation_id};
  active_operation_id_ = operation_id;
  active_action_ = action;
  pending_.store(1U, std::memory_order_relaxed);
  if (queue_ == nullptr || xQueueSend(queue_, &command, 0U) != pdTRUE) {
    active_operation_id_ = 0U;
    pending_.store(0U, std::memory_order_relaxed);
    operations_.fail(
        operation_id,
        now_ms,
        {core::ErrorCategory::storage,
         "Device control queue is unavailable or busy",
         true});
    return {false, operation_id};
  }
  return {true, operation_id};
}

void DeviceControlWorker::task_entry(void* context) {
  static_cast<DeviceControlWorker*>(context)->run();
}

void DeviceControlWorker::run() {
  for (;;) {
    Command command;
    if (xQueueReceive(queue_, &command, portMAX_DELAY) != pdTRUE) continue;
    operations_.mark_running(
        command.operation_id,
        millis(),
        command.action == Action::reboot ? "Rebooting device"
                                         : "Erasing device-owned configuration");
    if (command.action == Action::factory_reset) {
      const auto erased = storage_.factory_reset_device_data();
      if (!erased.ok()) {
        if (storage_.factory_reset_recovery_pending()) {
          // The durable reset marker is authoritative. Reboot into the
          // idempotent early-boot recovery path instead of leaving storage
          // fail-closed until a manual power cycle.
          operations_.mark_running(
              command.operation_id,
              millis(),
              "Factory reset recovery pending; reboot scheduled");
        } else {
          const std::lock_guard<std::mutex> lock(active_mutex_);
          operations_.fail(
              command.operation_id, millis(), erased.error());
          active_operation_id_ = 0U;
          pending_.store(0U, std::memory_order_relaxed);
          continue;
        }
      } else {
        operations_.mark_running(
            command.operation_id,
            millis(),
            "Factory reset erased device data; reboot scheduled");
      }
    }

    // The HTTP task has already returned the accepted receipt. Give the TCP
    // stack a bounded interval to flush it before restarting.
    vTaskDelay(pdMS_TO_TICKS(750U));
    esp_restart();
  }
}

}  // namespace opentag::application
