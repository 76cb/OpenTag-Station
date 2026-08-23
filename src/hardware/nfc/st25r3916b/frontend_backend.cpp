#include "hardware/nfc/st25r3916b/frontend_backend.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace opentag::hardware::nfc::st25r3916b {
namespace {

constexpr std::uint8_t register_read_mode = 0x40U;
constexpr std::uint8_t identity_register = 0x3FU;
constexpr std::uint8_t operation_control_register = 0x02U;
constexpr std::uint8_t main_interrupt_register = 0x1AU;
constexpr std::uint8_t operation_enable_oscillator = 0x80U;
constexpr std::uint8_t oscillator_stable_interrupt = 0x80U;
constexpr std::uint8_t st25r3916b_product_code = 0x06U;
constexpr std::size_t interrupt_register_count = 4U;
constexpr std::size_t maximum_register_burst = interrupt_register_count;

core::Error communication_error(const std::string& message, bool retryable = true) {
  return {core::ErrorCategory::nfc_communication, message, retryable};
}

core::Error configuration_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

bool elapsed(
    std::uint32_t now,
    std::uint32_t started,
    std::uint32_t duration) {
  return static_cast<std::uint32_t>(now - started) >= duration;
}

}  // namespace

core::Result<ChipIdentity> decode_identity_register(std::uint8_t raw_identity) {
  if (raw_identity == 0x00U) {
    return core::Result<ChipIdentity>::failure(communication_error(
        "ST25R3916B identity read returned 0x00 (no driven SPI response)"));
  }
  if (raw_identity == 0xFFU) {
    return core::Result<ChipIdentity>::failure(communication_error(
        "ST25R3916B identity read returned 0xFF (floating or inactive SPI bus)"));
  }
  const ChipIdentity identity{
      static_cast<std::uint8_t>((raw_identity >> 3U) & 0x1FU),
      static_cast<std::uint8_t>(raw_identity & 0x07U),
  };
  if (identity.product != st25r3916b_product_code) {
    return core::Result<ChipIdentity>::failure(communication_error(
        "unexpected NFC frontend product code " +
        std::to_string(identity.product) + "; expected ST25R3916B code 6",
        false));
  }
  return core::Result<ChipIdentity>::success(identity);
}

core::Result<void> FrontendBackend::transfer(
    const std::uint8_t* transmit,
    std::uint8_t* receive,
    std::size_t length,
    std::uint32_t timeout_ms) {
  if (timeout_ms == 0U) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B SPI timeout must be non-zero"));
  }
  if (!platform_.lock_bus(timeout_ms)) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B SPI bus lock timed out"));
  }
  platform_.select(true);
  const bool transferred = platform_.transfer(transmit, receive, length);
  platform_.select(false);
  platform_.unlock_bus();
  if (!transferred) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B SPI transfer failed"));
  }
  return core::Result<void>::success();
}

core::Result<void> FrontendBackend::write_register(
    std::uint8_t address,
    std::uint8_t value,
    std::uint32_t timeout_ms) {
  std::array<std::uint8_t, 2U> transmit{
      static_cast<std::uint8_t>(address & 0x3FU), value};
  std::array<std::uint8_t, 2U> receive{};
  return transfer(
      transmit.data(), receive.data(), transmit.size(), timeout_ms);
}

core::Result<void> FrontendBackend::read_registers(
    std::uint8_t address,
    std::uint8_t* output,
    std::size_t count,
    std::uint32_t timeout_ms) {
  if (output == nullptr || count == 0U || count > maximum_register_burst) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B register read length is invalid"));
  }
  std::array<std::uint8_t, maximum_register_burst + 1U> transmit{};
  std::array<std::uint8_t, maximum_register_burst + 1U> receive{};
  transmit[0] = static_cast<std::uint8_t>(register_read_mode | (address & 0x3FU));
  const auto transferred = transfer(
      transmit.data(), receive.data(), count + 1U, timeout_ms);
  if (!transferred.ok()) return transferred;
  std::copy_n(receive.data() + 1U, count, output);
  return core::Result<void>::success();
}

core::Result<void> FrontendBackend::set_power(
    bool enabled,
    std::uint32_t timeout_ms) {
  if (timeout_ms == 0U || !timing_.complete()) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B module timing is incomplete"));
  }
  if (!enabled) {
    if (diagnostics_.rf_field_enabled && rfal_driver_ != nullptr) {
      (void)rfal_driver_->set_rf_field(false, timeout_ms);
    }
    platform_.power(false);
    platform_.acknowledge_interrupt();
    diagnostics_ = {};
    return core::Result<void>::success();
  }
  if (timing_.power_settle_ms > timeout_ms) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B power-settle timing exceeds the bounded step timeout"));
  }
  if (!platform_.initialize()) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B ESP32 SPI/GPIO platform initialization failed",
        false));
  }
  platform_.power(true);
  platform_.delay_ms(timing_.power_settle_ms);
  diagnostics_.power_enabled = true;
  return core::Result<void>::success();
}

core::Result<void> FrontendBackend::reset(std::uint32_t timeout_ms) {
  const auto reset_duration = timing_.reset_assert_ms + timing_.reset_recovery_ms;
  if (!diagnostics_.power_enabled) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B reset requested while frontend power is off",
        false));
  }
  if (timeout_ms == 0U || reset_duration < timing_.reset_assert_ms ||
      reset_duration > timeout_ms) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B reset timing exceeds the bounded step timeout"));
  }
  platform_.reset(true);
  platform_.delay_ms(timing_.reset_assert_ms);
  platform_.reset(false);
  platform_.delay_ms(timing_.reset_recovery_ms);
  platform_.acknowledge_interrupt();
  diagnostics_.spi_ok = false;
  diagnostics_.irq_configured = false;
  diagnostics_.rfal_initialized = false;
  diagnostics_.rf_field_enabled = false;
  return core::Result<void>::success();
}

core::Result<ChipIdentity> FrontendBackend::read_and_validate_identity(
    std::uint32_t timeout_ms) {
  if (!diagnostics_.power_enabled) {
    return core::Result<ChipIdentity>::failure(communication_error(
        "ST25R3916B identity requested while frontend power is off",
        false));
  }
  std::uint8_t raw_identity = 0U;
  const auto read = read_registers(
      identity_register, &raw_identity, 1U, timeout_ms);
  if (!read.ok()) return core::Result<ChipIdentity>::failure(read.error());
  const auto decoded = decode_identity_register(raw_identity);
  diagnostics_.spi_ok = decoded.ok();
  return decoded;
}

core::Result<void> FrontendBackend::configure_interrupt(
    std::uint32_t timeout_ms) {
  if (!diagnostics_.spi_ok) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B IRQ validation requires a valid chip identity",
        false));
  }
  const std::uint32_t wait_ms = std::min(timeout_ms, timing_.irq_wait_ms);
  if (wait_ms == 0U) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B IRQ timeout must be non-zero"));
  }

  std::array<std::uint8_t, interrupt_register_count> pending{};
  const auto cleared = read_registers(
      main_interrupt_register,
      pending.data(),
      pending.size(),
      timeout_ms);
  if (!cleared.ok()) return cleared;
  platform_.acknowledge_interrupt();
  if (platform_.interrupt_line_active()) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B IRQ line remains active after pending status was cleared",
        false));
  }

  const auto irq_count_before = platform_.interrupt_count();
  const auto enabled = write_register(
      operation_control_register,
      operation_enable_oscillator,
      timeout_ms);
  if (!enabled.ok()) return enabled;

  const auto started_at = platform_.ticks_ms();
  while (!platform_.interrupt_pending() &&
         !elapsed(platform_.ticks_ms(), started_at, wait_ms)) {
    platform_.delay_ms(timing_.irq_poll_interval_ms);
  }
  const bool observed = platform_.interrupt_pending();
  std::array<std::uint8_t, interrupt_register_count> status{};
  const auto status_read = read_registers(
      main_interrupt_register,
      status.data(),
      status.size(),
      timeout_ms);
  const auto disabled = write_register(
      operation_control_register, 0U, timeout_ms);
  platform_.acknowledge_interrupt();
  if (!status_read.ok()) return status_read;
  if (!disabled.ok()) return disabled;
  if (!observed || platform_.interrupt_count() == irq_count_before ||
      (status[0] & oscillator_stable_interrupt) == 0U) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B oscillator-ready IRQ was not observed",
        false));
  }
  if (platform_.interrupt_line_active()) {
    return core::Result<void>::failure(communication_error(
        "ST25R3916B IRQ line did not clear after status acknowledgement",
        false));
  }
  diagnostics_.irq_configured = true;
  return core::Result<void>::success();
}

core::Result<void> FrontendBackend::initialize_rfal(
    std::uint32_t timeout_ms) {
  if (!diagnostics_.irq_configured) {
    return core::Result<void>::failure(communication_error(
        "ST RFAL initialization requires a validated IRQ path",
        false));
  }
  if (rfal_driver_ == nullptr) {
    return core::Result<void>::failure(configuration_error(
        "ST RFAL is not vendored; authoritative STSW-ST25RFAL002 delivery is required"));
  }
  const auto initialized = rfal_driver_->initialize(timeout_ms);
  diagnostics_.rfal_initialized = initialized.ok();
  return initialized;
}

core::Result<void> FrontendBackend::set_rf_field(
    bool enabled,
    std::uint32_t timeout_ms) {
  if (!enabled && rfal_driver_ == nullptr) {
    diagnostics_.rf_field_enabled = false;
    return core::Result<void>::success();
  }
  if (rfal_driver_ == nullptr || (enabled && !diagnostics_.rfal_initialized)) {
    return core::Result<void>::failure(configuration_error(
        "ST25R3916B RF field is gated until ST RFAL initializes"));
  }
  const auto field = rfal_driver_->set_rf_field(enabled, timeout_ms);
  if (field.ok()) diagnostics_.rf_field_enabled = enabled;
  return field;
}

FrontendBackendDiagnostics FrontendBackend::diagnostics() const {
  auto result = diagnostics_;
  result.irq_line_state = platform_.interrupt_line_active();
  result.irq_latched = platform_.interrupt_latched();
  result.irq_count = platform_.interrupt_count();
  result.last_irq_at_ms = platform_.last_interrupt_at_ms();
  return result;
}

}  // namespace opentag::hardware::nfc::st25r3916b
