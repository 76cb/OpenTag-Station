#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace opentag::hardware::scale {

inline constexpr std::uint8_t first_valid_i2c_address = 0x08U;
inline constexpr std::uint8_t last_valid_i2c_address = 0x77U;
inline constexpr std::size_t maximum_reported_i2c_addresses = 16U;

struct I2cScanResult {
  bool bus_started{false};
  bool target_present{false};
  std::uint8_t device_count{0U};
  std::uint8_t reported_count{0U};
  std::array<std::uint8_t, maximum_reported_i2c_addresses> addresses{};

  void record(std::uint8_t address, std::uint8_t target_address) {
    if (device_count != std::numeric_limits<std::uint8_t>::max()) ++device_count;
    if (address == target_address) target_present = true;
    if (reported_count < addresses.size()) {
      addresses[reported_count++] = address;
    }
  }

  [[nodiscard]] bool found_any() const { return device_count != 0U; }
  [[nodiscard]] bool truncated() const {
    return device_count > reported_count;
  }
};

enum class ScaleI2cOutcome : std::uint8_t {
  present_on_expected_bus,
  present_on_reversed_bus,
  target_missing_with_other_devices,
  no_devices,
};

struct ScaleI2cDiagnosticResult {
  I2cScanResult expected;
  I2cScanResult reversed;
  bool reversed_scanned{false};
  bool expected_bus_restored{false};

  [[nodiscard]] ScaleI2cOutcome outcome() const {
    if (expected.target_present) {
      return ScaleI2cOutcome::present_on_expected_bus;
    }
    if (reversed_scanned && reversed.target_present) {
      return ScaleI2cOutcome::present_on_reversed_bus;
    }
    if (expected.found_any() ||
        (reversed_scanned && reversed.found_any())) {
      return ScaleI2cOutcome::target_missing_with_other_devices;
    }
    return ScaleI2cOutcome::no_devices;
  }
};

}  // namespace opentag::hardware::scale
