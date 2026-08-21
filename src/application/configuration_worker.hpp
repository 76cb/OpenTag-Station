#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "application/operation_registry.hpp"
#include "config/configuration_service.hpp"
#include "network/wifi_service.hpp"
#include "services/first_run_setup.hpp"

namespace opentag::application {

class ConfigurationWorker {
 public:
  ConfigurationWorker(
      config::ConfigurationService& configuration,
      network::WifiService& network,
      OperationRegistry& operations)
      : configuration_(configuration),
        network_(network),
        operations_(operations) {}

  [[nodiscard]] bool start();
  [[nodiscard]] CommandReceipt submit_replace(
      const config::Configuration& configuration,
      std::uint64_t expected_revision,
      std::uint32_t now_ms,
      OperationKind operation_kind = OperationKind::configuration);
  [[nodiscard]] bool submit_setup_completion(services::SetupStep step);
  [[nodiscard]] std::size_t pending() const {
    return pending_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool last_operation_succeeded() const {
    return last_operation_succeeded_.load(std::memory_order_relaxed);
  }

 private:
  enum class CommandType : std::uint8_t {
    replace,
    complete_setup_step,
  };

  struct Command {
    CommandType type{CommandType::replace};
    config::Configuration configuration;
    services::SetupStep setup_step{services::SetupStep::welcome};
    std::uint64_t expected_revision{0U};
    std::uint64_t operation_id{0U};
  };

  static void task_entry(void* context);
  void run();
  [[nodiscard]] bool enqueue(Command* command);

  config::ConfigurationService& configuration_;
  network::WifiService& network_;
  OperationRegistry& operations_;
  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic_size_t pending_{0U};
  std::atomic_bool last_operation_succeeded_{true};
};

}  // namespace opentag::application
