#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/result.hpp"

namespace opentag::hardware::nfc::st25r3916b {

struct ChipIdentity {
  std::uint8_t product{0};
  std::uint8_t revision{0};
};

enum class BringUpState : std::uint8_t {
  off,
  powering,
  resetting,
  identifying,
  configuring_irq,
  initializing_rfal,
  enabling_field,
  ready,
  fault,
};

class IFrontendBackend {
 public:
  virtual ~IFrontendBackend() = default;

  [[nodiscard]] virtual core::Result<void> set_power(
      bool enabled,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> reset(std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<ChipIdentity> read_and_validate_identity(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> configure_interrupt(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> initialize_rfal(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> set_rf_field(
      bool enabled,
      std::uint32_t timeout_ms) = 0;
};

struct BringUpDiagnostics {
  BringUpState state{BringUpState::off};
  std::optional<ChipIdentity> identity;
  std::optional<core::Error> last_error;
  std::uint32_t recovery_count{0};
};

class Service {
 public:
  explicit Service(IFrontendBackend& backend) : backend_(backend) {}

  [[nodiscard]] core::Result<void> start(std::uint32_t step_timeout_ms);
  [[nodiscard]] core::Result<void> recover(std::uint32_t step_timeout_ms);
  [[nodiscard]] core::Result<void> stop(std::uint32_t step_timeout_ms);
  [[nodiscard]] const BringUpDiagnostics& diagnostics() const { return diagnostics_; }

 private:
  [[nodiscard]] core::Result<void> fail(core::Error error, std::uint32_t timeout_ms);

  IFrontendBackend& backend_;
  BringUpDiagnostics diagnostics_;
};

}  // namespace opentag::hardware::nfc::st25r3916b
