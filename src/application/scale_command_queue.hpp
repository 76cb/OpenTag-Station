#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "application/operation_registry.hpp"
#include "config/configuration_service.hpp"
#include "services/scale_service.hpp"

namespace opentag::application {

class ScaleCommandQueue final {
 public:
  ScaleCommandQueue(
      config::ConfigurationService& configuration,
      services::ScaleService& scale,
      OperationRegistry& operations)
      : configuration_(configuration),
        scale_(scale),
        operations_(operations) {}

  [[nodiscard]] bool initialize();
  [[nodiscard]] CommandReceipt submit_tare(std::uint32_t now_ms);
  [[nodiscard]] CommandReceipt submit_calibration(
      float reference_grams,
      std::uint32_t now_ms);
  void process_one(std::uint32_t now_ms);
  [[nodiscard]] std::size_t pending() const {
    return pending_.load(std::memory_order_relaxed);
  }

 private:
  enum class CommandType : std::uint8_t { tare, calibrate };
  struct Command {
    CommandType type{CommandType::tare};
    float reference_grams{0.0F};
    std::uint64_t operation_id{0U};
    std::uint32_t enqueued_at_ms{0U};
  };

  [[nodiscard]] CommandReceipt submit(Command command, OperationKind kind);

  static constexpr UBaseType_t queue_depth = 4U;
  static constexpr std::uint32_t command_expiry_ms = 10000U;
  config::ConfigurationService& configuration_;
  services::ScaleService& scale_;
  OperationRegistry& operations_;
  QueueHandle_t queue_{nullptr};
  std::atomic_size_t pending_{0U};
};

}  // namespace opentag::application
