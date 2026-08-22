#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "application/device_lifecycle_gate.hpp"
#include "application/operation_registry.hpp"
#include "platform/storage/storage_service.hpp"

namespace opentag::application {

class DeviceControlWorker final {
 public:
  DeviceControlWorker(
      platform::storage::StorageService& storage,
      OperationRegistry& operations,
      DeviceLifecycleGate& lifecycle)
      : storage_(storage), operations_(operations), lifecycle_(lifecycle) {}

  [[nodiscard]] bool start();
  [[nodiscard]] CommandReceipt submit_reboot(std::uint32_t now_ms);
  [[nodiscard]] CommandReceipt submit_factory_reset(std::uint32_t now_ms);
  [[nodiscard]] TaskHandle_t task_handle() const { return task_; }
  [[nodiscard]] std::size_t pending() const {
    return pending_.load(std::memory_order_relaxed);
  }

 private:
  enum class Action : std::uint8_t { reboot, factory_reset };
  struct Command {
    Action action{Action::reboot};
    std::uint64_t operation_id{0U};
  };

  [[nodiscard]] CommandReceipt submit(Action action, std::uint32_t now_ms);
  static void task_entry(void* context);
  void run();

  platform::storage::StorageService& storage_;
  OperationRegistry& operations_;
  DeviceLifecycleGate& lifecycle_;
  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic_size_t pending_{0U};
  std::mutex active_mutex_;
  std::uint64_t active_operation_id_{0U};
  Action active_action_{Action::reboot};
  DeviceLifecycleLease active_lease_;
};

}  // namespace opentag::application
