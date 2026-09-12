#include "diagnostics/shared_i2c_diagnostic.hpp"

namespace opentag::diagnostics::shared_i2c {

ProbeResult classify_wire_status(std::uint8_t status) {
  switch (status) {
    case 0U: return ProbeResult::ack;
    case 2U:
    case 3U: return ProbeResult::nack;
    default: return ProbeResult::bus_error;
  }
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

const char* to_string(TagDetected value) {
  switch (value) {
    case TagDetected::pending: return "PENDING";
    case TagDetected::no: return "NO";
    case TagDetected::yes: return "YES";
    case TagDetected::multiple: return "MULTIPLE";
  }
  return "PENDING";
}

const char* to_string(Phase value) {
  switch (value) {
    case Phase::starting: return "STARTING";
    case Phase::line_state: return "RAW LINE STATE";
    case Phase::scale_probe: return "SCALE TARGET PROBE";
    case Phase::nfc_probe: return "NFC TARGET PROBE";
    case Phase::nfc_register_test: return "NFC REGISTER TEST";
    case Phase::nau_test: return "NAU7802 TEST";
    case Phase::rfal_initialize: return "RFAL INITIALIZE";
    case Phase::nfcv_poller: return "NFC-V POLLER";
    case Phase::coexistence: return "30 SECOND RF / SCALE TEST";
    case Phase::complete: return "COMPLETE";
  }
  return "UNKNOWN";
}

const char* to_string(FailureStage value) {
  switch (value) {
    case FailureStage::none: return "NONE";
    case FailureStage::scale_sda_stuck_low: return "SCALE SDA HELD LOW";
    case FailureStage::scale_scl_stuck_low: return "SCALE SCL HELD LOW";
    case FailureStage::nfc_sda_stuck_low: return "NFC SDA HELD LOW";
    case FailureStage::nfc_scl_stuck_low: return "NFC SCL HELD LOW";
    case FailureStage::scale_wire_begin: return "SCALE WIRE BEGIN AT 100 KHZ";
    case FailureStage::nfc_wire_begin: return "NFC WIRE1 BEGIN AT 100 KHZ";
    case FailureStage::nau_probe: return "NAU7802 TARGET PROBE";
    case FailureStage::nfc_probe: return "ST25R3916B TARGET PROBE";
    case FailureStage::nfc_set_default: return "NFC SET DEFAULT";
    case FailureStage::nfc_identity_read: return "NFC IDENTITY REGISTER READ";
    case FailureStage::nfc_identity_mismatch: return "NFC IDENTITY MISMATCH";
    case FailureStage::nfc_irq_not_clear: return "NFC IRQ HIGH AFTER RESET";
    case FailureStage::nfc_irq_start_oscillator: return "NFC OSCILLATOR START";
    case FailureStage::nfc_irq_timeout: return "NFC IRQ ASSERT TIMEOUT";
    case FailureStage::nfc_irq_status: return "NFC I_OSC STATUS READ";
    case FailureStage::nfc_irq_not_cleared: return "NFC IRQ CLEAR";
    case FailureStage::nau_initialize: return "NAU7802 INITIALIZATION";
    case FailureStage::rfal_initialize: return "RFAL INITIALIZATION";
    case FailureStage::nfcv_poller_initialize: return "NFC-V POLLER INITIALIZATION";
    case FailureStage::rf_field_on: return "RF FIELD ENABLE";
    case FailureStage::nfcv_presence: return "NFC-V PRESENCE CHECK";
    case FailureStage::nfcv_inventory: return "ISO15693 INVENTORY";
    case FailureStage::nfcv_uid: return "NFC-V UID NORMALIZATION";
    case FailureStage::nfc_uid_changed: return "NFC-V UID CHANGED";
    case FailureStage::nfc_post_inventory_probe: return "NFC POST-INVENTORY PROBE";
    case FailureStage::nfc_post_inventory_identity: return "NFC POST-INVENTORY CHIP ID";
    case FailureStage::rf_field_off: return "RF FIELD DISABLE";
    case FailureStage::scale_sample: return "NAU7802 SCALE SAMPLE";
    case FailureStage::nfc_coexistence_probe: return "NFC PROBE DURING SCALE";
    case FailureStage::insufficient_scale_samples: return "SCALE READINGS DID NOT UPDATE";
  }
  return "UNKNOWN";
}

std::array<char, 24U> format_diagnostic_uid(
    const std::array<std::uint8_t, 8U>& canonical_uid) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::array<char, 24U> output{};
  std::size_t offset = 0U;
  for (std::size_t index = 0U; index < canonical_uid.size(); ++index) {
    const auto byte = canonical_uid[index];
    output[offset++] = hex[(byte >> 4U) & 0x0FU];
    output[offset++] = hex[byte & 0x0FU];
    if (index + 1U < canonical_uid.size()) output[offset++] = ':';
  }
  output[offset] = '\0';
  return output;
}

}  // namespace opentag::diagnostics::shared_i2c
