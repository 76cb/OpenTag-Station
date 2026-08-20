#pragma once

#include <cstdint>

#include <Wire.h>

#include "services/scale_service.hpp"

namespace opentag::hardware::scale {

struct I2cPins {
  std::int8_t sda{-1};
  std::int8_t scl{-1};
  [[nodiscard]] bool complete() const { return sda >= 0 && scl >= 0; }
};

enum class Nau7802SampleRate : std::uint8_t {
  sps_10 = 0U,
  sps_20 = 1U,
  sps_40 = 2U,
  sps_80 = 3U,
  sps_320 = 7U,
};

struct Nau7802Config {
  std::uint32_t i2c_frequency_hz{400000U};
  Nau7802SampleRate sample_rate{Nau7802SampleRate::sps_10};
  std::uint8_t gain_code{7U};       // 128x
  std::uint8_t ldo_voltage_code{5U};  // 3.0 V
};

class Nau7802Device final : public services::IScaleAdc {
 public:
  Nau7802Device(
      TwoWire& wire,
      I2cPins pins,
      Nau7802Config config = {});

  [[nodiscard]] core::Result<void> initialize(std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<void> internal_calibrate(std::uint32_t timeout_ms) override;
  [[nodiscard]] core::Result<bool> sample_ready() override;
  [[nodiscard]] core::Result<std::int32_t> read_raw() override;

 private:
  static constexpr std::uint8_t address = 0x2AU;
  static constexpr std::uint8_t register_power_control = 0x00U;
  static constexpr std::uint8_t register_control_1 = 0x01U;
  static constexpr std::uint8_t register_control_2 = 0x02U;
  static constexpr std::uint8_t register_adc_output = 0x12U;
  static constexpr std::uint8_t register_adc_control = 0x15U;
  static constexpr std::uint8_t register_pga = 0x1BU;
  static constexpr std::uint8_t register_revision = 0x1FU;

  [[nodiscard]] core::Result<std::uint8_t> read_register(std::uint8_t address);
  [[nodiscard]] core::Result<void> write_register(
      std::uint8_t address,
      std::uint8_t value);
  [[nodiscard]] core::Result<void> update_register(
      std::uint8_t address,
      std::uint8_t clear_mask,
      std::uint8_t set_mask);

  TwoWire& wire_;
  I2cPins pins_;
  Nau7802Config config_;
  bool initialized_{false};
};

}  // namespace opentag::hardware::scale
