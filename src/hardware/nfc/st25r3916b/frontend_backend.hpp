#pragma once

#include <cstddef>
#include <cstdint>

#include "hardware/nfc/st25r3916b/service.hpp"
#include "platform/rfal/rfal_platform_contract.hpp"

namespace opentag::hardware::nfc::st25r3916b {

// Module-specific timings remain injected because the selected breakout,
// reset circuit, and power-enable circuit are not yet known.
struct FrontendTiming {
  std::uint32_t power_settle_ms{0U};
  std::uint32_t reset_assert_ms{0U};
  std::uint32_t reset_recovery_ms{0U};
  std::uint32_t irq_wait_ms{0U};
  std::uint32_t irq_poll_interval_ms{0U};

  [[nodiscard]] bool complete() const {
    return power_settle_ms != 0U && reset_assert_ms != 0U &&
        reset_recovery_ms != 0U && irq_wait_ms != 0U &&
        irq_poll_interval_ms != 0U && irq_poll_interval_ms <= irq_wait_ms;
  }
};

// The vendor RFAL adapter will implement this seam after an authoritative ST
// delivery and license record are committed. No substitute controller stack is
// accepted here.
class IRfalDriver {
 public:
  virtual ~IRfalDriver() = default;
  [[nodiscard]] virtual core::Result<void> initialize(
      std::uint32_t timeout_ms) = 0;
  [[nodiscard]] virtual core::Result<void> set_rf_field(
      bool enabled,
      std::uint32_t timeout_ms) = 0;
};

[[nodiscard]] core::Result<ChipIdentity> decode_identity_register(
    std::uint8_t raw_identity);

// Direct-register ST25R3916B backend. It performs the silicon identity and IRQ
// checkpoints over the injected ESP32 RFAL platform before delegating any RF
// operation to the separately gated ST RFAL adapter.
class FrontendBackend final : public IFrontendBackend {
 public:
  FrontendBackend(
      platform::rfal::IRfalPlatform& platform,
      FrontendTiming timing,
      IRfalDriver* rfal_driver = nullptr)
      : platform_(platform), timing_(timing), rfal_driver_(rfal_driver) {}

  [[nodiscard]] core::Result<void> set_power(
      bool enabled,
      std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<void> reset(std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<ChipIdentity> read_and_validate_identity(
      std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<void> configure_interrupt(
      std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<void> initialize_rfal(
      std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<void> set_rf_field(
      bool enabled,
      std::uint32_t timeout_ms) override;
  [[nodiscard]] FrontendBackendDiagnostics diagnostics() const override;

 private:
  [[nodiscard]] core::Result<void> transfer(
      const std::uint8_t* transmit,
      std::uint8_t* receive,
      std::size_t length,
      std::uint32_t timeout_ms);
  [[nodiscard]] core::Result<void> write_register(
      std::uint8_t address,
      std::uint8_t value,
      std::uint32_t timeout_ms);
  [[nodiscard]] core::Result<void> read_registers(
      std::uint8_t address,
      std::uint8_t* output,
      std::size_t count,
      std::uint32_t timeout_ms);

  platform::rfal::IRfalPlatform& platform_;
  FrontendTiming timing_;
  IRfalDriver* rfal_driver_{nullptr};
  FrontendBackendDiagnostics diagnostics_;
};

}  // namespace opentag::hardware::nfc::st25r3916b
