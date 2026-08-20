#include "hardware/scale/nau7802_device.hpp"

#include <Arduino.h>

#include <algorithm>
#include <string>

namespace opentag::hardware::scale {
namespace {

core::Error communication_error(const std::string& message, bool retryable = true) {
  return {core::ErrorCategory::scale_unavailable, message, retryable};
}

core::Error configuration_error(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

}  // namespace

Nau7802Device::Nau7802Device(
    TwoWire& wire,
    I2cPins pins,
    Nau7802Config config)
    : wire_(wire), pins_(pins), config_(config) {}

core::Result<std::uint8_t> Nau7802Device::read_register(std::uint8_t register_address) {
  wire_.beginTransmission(address);
  wire_.write(register_address);
  if (wire_.endTransmission(false) != 0U || wire_.requestFrom(address, 1U) != 1U) {
    return core::Result<std::uint8_t>::failure(
        communication_error("NAU7802 register read failed"));
  }
  return core::Result<std::uint8_t>::success(wire_.read());
}

core::Result<void> Nau7802Device::write_register(
    std::uint8_t register_address,
    std::uint8_t value) {
  wire_.beginTransmission(address);
  wire_.write(register_address);
  wire_.write(value);
  if (wire_.endTransmission() != 0U) {
    return core::Result<void>::failure(
        communication_error("NAU7802 register write failed"));
  }
  return core::Result<void>::success();
}

core::Result<void> Nau7802Device::update_register(
    std::uint8_t register_address,
    std::uint8_t clear_mask,
    std::uint8_t set_mask) {
  const auto current = read_register(register_address);
  if (!current.ok()) return core::Result<void>::failure(current.error());
  return write_register(
      register_address,
      static_cast<std::uint8_t>((current.value() & ~clear_mask) | set_mask));
}

core::Result<void> Nau7802Device::initialize(std::uint32_t timeout_ms) {
  if (initialized_) return core::Result<void>::success();
  if (!pins_.complete() || config_.i2c_frequency_hz == 0U ||
      config_.gain_code > 7U || config_.ldo_voltage_code > 7U ||
      timeout_ms < 650U) {
    return core::Result<void>::failure(
        configuration_error("NAU7802 pins/configuration or startup timeout is invalid"));
  }
  wire_.setTimeOut(static_cast<std::uint16_t>(std::min<std::uint32_t>(timeout_ms, 65535U)));
  if (!wire_.begin(pins_.sda, pins_.scl, config_.i2c_frequency_hz)) {
    return core::Result<void>::failure(communication_error("NAU7802 I2C bus initialization failed"));
  }
  const auto started_ms = millis();
  const auto timed_out = [&]() {
    return static_cast<std::uint32_t>(millis() - started_ms) >= timeout_ms;
  };

  auto power = read_register(register_power_control);
  if (!power.ok()) return core::Result<void>::failure(power.error());
  auto status = write_register(
      register_power_control,
      static_cast<std::uint8_t>(power.value() | 0x01U));
  if (!status.ok()) return status;
  delay(10U);
  power = read_register(register_power_control);
  if (!power.ok()) return core::Result<void>::failure(power.error());
  status = write_register(
      register_power_control,
      static_cast<std::uint8_t>((power.value() & ~0x01U) | 0x06U));
  if (!status.ok()) return status;

  // The analog block requires up to 600 ms after power-up. This runs only on
  // the scale worker and remains bounded by the caller's startup deadline.
  delay(600U);
  if (timed_out()) {
    return core::Result<void>::failure(communication_error("NAU7802 power-up timed out"));
  }
  power = read_register(register_power_control);
  if (!power.ok() || (power.value() & 0x08U) == 0U) {
    return core::Result<void>::failure(communication_error("NAU7802 analog power did not become ready"));
  }
  status = write_register(
      register_power_control,
      static_cast<std::uint8_t>(power.value() | 0x10U | 0x80U));
  if (!status.ok()) return status;

  const auto revision = read_register(register_revision);
  if (!revision.ok() || (revision.value() & 0x0FU) != 0x0FU) {
    return core::Result<void>::failure(communication_error("NAU7802 revision ID mismatch", false));
  }
  status = update_register(
      register_control_1,
      0x3FU,
      static_cast<std::uint8_t>(
          config_.gain_code | (config_.ldo_voltage_code << 3U)));
  if (!status.ok()) return status;
  status = update_register(
      register_control_2,
      0xF3U,
      static_cast<std::uint8_t>(config_.sample_rate) << 4U);
  if (!status.ok()) return status;
  status = update_register(register_adc_control, 0x30U, 0x30U);
  if (!status.ok()) return status;
  status = update_register(register_pga, 0x40U, 0x00U);
  if (!status.ok()) return status;

  initialized_ = true;
  return core::Result<void>::success();
}

core::Result<void> Nau7802Device::internal_calibrate(std::uint32_t timeout_ms) {
  if (!initialized_ || timeout_ms == 0U) {
    return core::Result<void>::failure(
        configuration_error("NAU7802 must be initialized before calibration"));
  }
  auto control = read_register(register_control_2);
  if (!control.ok()) return core::Result<void>::failure(control.error());
  auto status = write_register(
      register_control_2,
      static_cast<std::uint8_t>((control.value() & ~0x03U) | 0x04U));
  if (!status.ok()) return status;

  const auto started_ms = millis();
  for (;;) {
    control = read_register(register_control_2);
    if (!control.ok()) return core::Result<void>::failure(control.error());
    if ((control.value() & 0x04U) == 0U) break;
    if (static_cast<std::uint32_t>(millis() - started_ms) >= timeout_ms) {
      return core::Result<void>::failure(
          communication_error("NAU7802 internal calibration timed out"));
    }
    delay(5U);
  }
  if ((control.value() & 0x08U) != 0U) {
    return core::Result<void>::failure(
        communication_error("NAU7802 internal calibration reported an error", false));
  }
  return core::Result<void>::success();
}

core::Result<bool> Nau7802Device::sample_ready() {
  if (!initialized_) {
    return core::Result<bool>::failure(communication_error("NAU7802 is not initialized", false));
  }
  const auto power = read_register(register_power_control);
  if (!power.ok()) return core::Result<bool>::failure(power.error());
  return core::Result<bool>::success((power.value() & 0x20U) != 0U);
}

core::Result<std::int32_t> Nau7802Device::read_raw() {
  if (!initialized_) {
    return core::Result<std::int32_t>::failure(
        communication_error("NAU7802 is not initialized", false));
  }
  wire_.beginTransmission(address);
  wire_.write(register_adc_output);
  if (wire_.endTransmission(false) != 0U || wire_.requestFrom(address, 3U) != 3U) {
    return core::Result<std::int32_t>::failure(
        communication_error("NAU7802 ADC read failed"));
  }
  std::uint32_t value = static_cast<std::uint32_t>(wire_.read()) << 16U;
  value |= static_cast<std::uint32_t>(wire_.read()) << 8U;
  value |= static_cast<std::uint32_t>(wire_.read());
  if ((value & 0x800000U) != 0U) value |= 0xFF000000U;
  return core::Result<std::int32_t>::success(static_cast<std::int32_t>(value));
}

}  // namespace opentag::hardware::scale
