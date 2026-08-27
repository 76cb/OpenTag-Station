#pragma once

#include <cstdint>

namespace opentag::diagnostics::shared_i2c {

inline constexpr std::uint32_t scale_clock_hz = 100000U;
inline constexpr std::uint32_t nfc_clock_hz = 100000U;
inline constexpr std::int8_t scale_sda_gpio = 10;
inline constexpr std::int8_t scale_scl_gpio = 11;
inline constexpr std::int8_t nfc_sda_gpio = 13;
inline constexpr std::int8_t nfc_scl_gpio = 14;
inline constexpr std::uint8_t nau7802_address = 0x2AU;
inline constexpr std::uint8_t st25r3916b_address = 0x50U;
inline constexpr std::uint8_t st25r3916b_identity_register = 0x3FU;
inline constexpr std::uint8_t st25r3916b_set_default_command = 0xC1U;

enum class LineState : std::uint8_t {
  unknown,
  low,
  high,
};

enum class ProbeResult : std::uint8_t {
  pending,
  ack,
  nack,
  bus_error,
};

enum class CheckResult : std::uint8_t {
  pending,
  pass,
  fail,
  skipped,
};

enum class Phase : std::uint8_t {
  starting,
  line_state,
  scale_probe,
  nfc_probe,
  nfc_register_test,
  nau_test,
  coexistence,
  complete,
};

enum class FailureStage : std::uint8_t {
  none,
  scale_sda_stuck_low,
  scale_scl_stuck_low,
  nfc_sda_stuck_low,
  nfc_scl_stuck_low,
  scale_wire_begin,
  nfc_wire_begin,
  nau_probe,
  nfc_probe,
  nfc_set_default,
  nfc_identity_read,
  nfc_identity_mismatch,
  nfc_irq_not_clear,
  nfc_irq_start_oscillator,
  nfc_irq_timeout,
  nfc_irq_status,
  nfc_irq_not_cleared,
  nau_initialize,
  scale_sample,
  nfc_coexistence_probe,
  insufficient_scale_samples,
};

struct ChipIdentity {
  std::uint8_t raw{0U};
  std::uint8_t product{0U};
  std::uint8_t revision{0U};

  [[nodiscard]] bool is_st25r3916b() const { return product == 0x06U; }
};

struct Snapshot {
  std::uint32_t scale_clock_hz{shared_i2c::scale_clock_hz};
  LineState scale_sda_idle{LineState::unknown};
  LineState scale_scl_idle{LineState::unknown};
  ProbeResult nau_probe_result{ProbeResult::pending};
  std::uint32_t scale_bus_error_count{0U};
  std::uint32_t nfc_clock_hz{shared_i2c::nfc_clock_hz};
  LineState nfc_sda_idle{LineState::unknown};
  LineState nfc_scl_idle{LineState::unknown};
  ProbeResult nfc_probe_result{ProbeResult::pending};
  CheckResult nfc_identity_result{CheckResult::pending};
  CheckResult nfc_irq_result{CheckResult::pending};
  CheckResult nau_communication_result{CheckResult::pending};
  CheckResult scale_after_test{CheckResult::pending};
  CheckResult nfc_after_scale{CheckResult::pending};
  ChipIdentity nfc_identity;
  std::uint32_t nfc_bus_error_count{0U};
  std::uint32_t scale_sample_count{0U};
  std::int32_t last_raw{0};
  std::uint32_t coexistence_elapsed_ms{0U};
  Phase phase{Phase::starting};
  FailureStage failure_stage{FailureStage::none};
};

[[nodiscard]] ProbeResult classify_wire_status(std::uint8_t status);
[[nodiscard]] constexpr std::uint8_t st25r3916b_read_mode(
    std::uint8_t register_address) {
  return static_cast<std::uint8_t>(0x40U | (register_address & 0x3FU));
}
[[nodiscard]] ChipIdentity decode_st25r3916b_identity(std::uint8_t raw);

[[nodiscard]] const char* to_string(LineState value);
[[nodiscard]] const char* to_string(ProbeResult value);
[[nodiscard]] const char* to_string(CheckResult value);
[[nodiscard]] const char* to_string(Phase value);
[[nodiscard]] const char* to_string(FailureStage value);

}  // namespace opentag::diagnostics::shared_i2c
