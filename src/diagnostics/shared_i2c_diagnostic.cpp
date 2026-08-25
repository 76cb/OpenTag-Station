#include "diagnostics/shared_i2c_diagnostic.hpp"

#include <limits>

namespace opentag::diagnostics::shared_i2c {

void ScanSummary::record(std::uint8_t address) {
  if (device_count != std::numeric_limits<std::uint8_t>::max()) {
    ++device_count;
  }
  if (reported_count < addresses.size()) {
    addresses[reported_count++] = address;
  } else {
    truncated = true;
  }
}

ProbeResult classify_wire_status(std::uint8_t status) {
  switch (status) {
    case 0U: return ProbeResult::ack;
    case 2U:
    case 3U: return ProbeResult::nack;
    default: return ProbeResult::bus_error;
  }
}

bool scan_is_implausible(const ScanSummary& scan) {
  if (scan.truncated) return true;
  std::uint8_t unexpected_count = 0U;
  for (std::uint8_t index = 0U; index < scan.reported_count; ++index) {
    const auto address = scan.addresses[index];
    if (address != nau7802_address && address != st25r3916b_address) {
      ++unexpected_count;
    }
  }
  // This diagnostic bus has exactly two intentional targets. A small number
  // of stray ACKs is displayed verbatim, but three or more matches the
  // multi-address aliasing pattern produced by electrical contention.
  return unexpected_count >= 3U;
}

ChipIdentity decode_st25r3916b_identity(std::uint8_t raw) {
  return {
      raw,
      static_cast<std::uint8_t>((raw >> 3U) & 0x1FU),
      static_cast<std::uint8_t>(raw & 0x07U),
  };
}

const char* to_string(LineState value) {
  switch (value) {
    case LineState::unknown: return "UNKNOWN";
    case LineState::low: return "LOW";
    case LineState::high: return "HIGH";
  }
  return "UNKNOWN";
}

const char* to_string(ProbeResult value) {
  switch (value) {
    case ProbeResult::pending: return "PENDING";
    case ProbeResult::ack: return "ACK";
    case ProbeResult::nack: return "NACK";
    case ProbeResult::bus_error: return "BUS ERROR";
  }
  return "BUS ERROR";
}

const char* to_string(CheckResult value) {
  switch (value) {
    case CheckResult::pending: return "PENDING";
    case CheckResult::pass: return "PASS";
    case CheckResult::fail: return "FAIL";
    case CheckResult::skipped: return "SKIPPED";
  }
  return "FAIL";
}

const char* to_string(Phase value) {
  switch (value) {
    case Phase::starting: return "STARTING";
    case Phase::line_state: return "RAW LINE STATE";
    case Phase::bus_recovery: return "BUS RECOVERY";
    case Phase::targeted_probes: return "TARGETED PROBES";
    case Phase::full_scan: return "FULL SCAN";
    case Phase::nfc_register_test: return "NFC REGISTER TEST";
    case Phase::nau_test: return "NAU7802 TEST";
    case Phase::coexistence: return "30 SECOND COEXISTENCE";
    case Phase::complete: return "COMPLETE";
  }
  return "UNKNOWN";
}

const char* to_string(FailureStage value) {
  switch (value) {
    case FailureStage::none: return "NONE";
    case FailureStage::sda_stuck_low: return "SDA HELD LOW";
    case FailureStage::scl_stuck_low: return "SCL HELD LOW";
    case FailureStage::bus_recovery: return "BUS RECOVERY FAILED";
    case FailureStage::wire_begin: return "WIRE BEGIN AT 100 KHZ";
    case FailureStage::nau_probe: return "NAU7802 TARGET PROBE";
    case FailureStage::nfc_probe: return "ST25R3916B TARGET PROBE";
    case FailureStage::invalid_scan: return "BUS CONTENTION / INVALID SCAN";
    case FailureStage::nfc_set_default: return "NFC SET DEFAULT";
    case FailureStage::nfc_identity_read: return "NFC IDENTITY REGISTER READ";
    case FailureStage::nfc_identity_mismatch: return "NFC IDENTITY MISMATCH";
    case FailureStage::nfc_irq_not_clear: return "NFC IRQ HIGH AFTER RESET";
    case FailureStage::nfc_irq_start_oscillator: return "NFC OSCILLATOR START";
    case FailureStage::nfc_irq_timeout: return "NFC IRQ ASSERT TIMEOUT";
    case FailureStage::nfc_irq_status: return "NFC I_OSC STATUS READ";
    case FailureStage::nfc_irq_not_cleared: return "NFC IRQ CLEAR";
    case FailureStage::nau_initialize: return "NAU7802 INITIALIZATION";
    case FailureStage::scale_sample: return "NAU7802 SCALE SAMPLE";
    case FailureStage::nfc_coexistence_probe: return "NFC PROBE DURING SCALE";
    case FailureStage::insufficient_scale_samples: return "SCALE READINGS DID NOT UPDATE";
  }
  return "UNKNOWN";
}

}  // namespace opentag::diagnostics::shared_i2c
