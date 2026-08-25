#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentag::diagnostics::shared_i2c {

inline constexpr std::uint32_t diagnostic_clock_hz = 100000U;
inline constexpr std::uint8_t nau7802_address = 0x2AU;
inline constexpr std::uint8_t st25r3916b_address = 0x50U;
inline constexpr std::uint8_t first_legal_address = 0x08U;
inline constexpr std::uint8_t last_legal_address = 0x77U;
inline constexpr std::size_t maximum_scan_addresses = 32U;
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
  bus_recovery,
  targeted_probes,
  full_scan,
  nfc_register_test,
  nau_test,
  coexistence,
  complete,
};

enum class FailureStage : std::uint8_t {
  none,
  sda_stuck_low,
  scl_stuck_low,
  bus_recovery,
  wire_begin,
  nau_probe,
  nfc_probe,
  invalid_scan,
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

struct ScanSummary {
  std::uint8_t device_count{0U};
  std::uint8_t reported_count{0U};
  std::array<std::uint8_t, maximum_scan_addresses> addresses{};
  bool truncated{false};

  void record(std::uint8_t address);
};

struct ChipIdentity {
  std::uint8_t raw{0U};
  std::uint8_t product{0U};
  std::uint8_t revision{0U};

  [[nodiscard]] bool is_st25r3916b() const { return product == 0x06U; }
};

struct Snapshot {
  std::uint32_t clock_hz{diagnostic_clock_hz};
  LineState sda_idle{LineState::unknown};
  LineState scl_idle{LineState::unknown};
  bool bus_recovery_attempted{false};
  bool bus_recovery_success{false};
  ProbeResult nau_probe_result{ProbeResult::pending};
  ProbeResult nfc_probe_result{ProbeResult::pending};
  CheckResult nfc_identity_result{CheckResult::pending};
  CheckResult nfc_irq_result{CheckResult::pending};
  CheckResult nau_communication_result{CheckResult::pending};
  CheckResult scale_after_test{CheckResult::pending};
  CheckResult nfc_after_scale{CheckResult::pending};
  ScanSummary scan;
  bool invalid_scan_detected{false};
  ChipIdentity nfc_identity;
  std::uint32_t bus_error_count{0U};
  std::uint32_t scale_sample_count{0U};
  std::int32_t last_raw{0};
  std::uint32_t coexistence_elapsed_ms{0U};
  Phase phase{Phase::starting};
  FailureStage failure_stage{FailureStage::none};
};

[[nodiscard]] ProbeResult classify_wire_status(std::uint8_t status);
[[nodiscard]] bool scan_is_implausible(const ScanSummary& scan);
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
