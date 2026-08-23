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

struct FrontendBackendDiagnostics {
  bool power_enabled{false};
  bool external_power_control_available{false};
  bool external_reset_available{false};
  bool software_reset{false};
  bool spi_ok{false};
  bool irq_configured{false};
  bool irq_line_state{false};
  bool irq_latched{false};
  std::uint32_t irq_count{0U};
  std::uint32_t last_irq_at_ms{0U};
  bool rfal_initialized{false};
  bool rf_field_enabled{false};
};

class IFrontendBackend {
 public:
  virtual ~IFrontendBackend() = default;

  [[nodiscard]] virtual core::Result<void> set_power(
      bool enabled,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> reset_to_defaults(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<ChipIdentity> read_and_validate_identity(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> configure_interrupt(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> initialize_rfal(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> set_rf_field(
      bool enabled,
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual FrontendBackendDiagnostics diagnostics() const = 0;
};

struct BringUpDiagnostics {
  BringUpState state{BringUpState::off};
  std::optional<ChipIdentity> identity;
  std::optional<core::Error> last_error;
  FrontendBackendDiagnostics frontend;
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
  void refresh_frontend_diagnostics();

  IFrontendBackend& backend_;
  BringUpDiagnostics diagnostics_;
};

}  // namespace opentag::hardware::nfc::st25r3916b
