#include "diagnostics/shared_i2c_diagnostic.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

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
    case Phase::memory_read: return "NFC-V READ-ONLY MEMORY TEST";
    case Phase::initialization: return "BLANK TAG INITIALIZATION";
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
    case FailureStage::nfcv_system_information: return "NFC-V SYSTEM INFORMATION";
    case FailureStage::nfcv_invalid_geometry: return "NFC-V INVALID GEOMETRY";
    case FailureStage::nfcv_memory_read: return "NFC-V MEMORY READ";
    case FailureStage::nfcv_response_length: return "NFC-V RESPONSE LENGTH";
    case FailureStage::nfcv_uid_changed_during_read: return "NFC-V UID CHANGED DURING READ";
    case FailureStage::nfcv_read_consistency: return "NFC-V READ CONSISTENCY";
    case FailureStage::nfc_post_read_transport: return "NFC POST-READ TRANSPORT";
    case FailureStage::nfc_post_inventory_probe: return "NFC POST-INVENTORY PROBE";
    case FailureStage::nfc_post_inventory_identity: return "NFC POST-INVENTORY CHIP ID";
    case FailureStage::rf_field_off: return "RF FIELD DISABLE";
    case FailureStage::scale_sample: return "NAU7802 SCALE SAMPLE";
    case FailureStage::nfc_coexistence_probe: return "NFC PROBE DURING SCALE";
    case FailureStage::insufficient_scale_samples: return "SCALE READINGS DID NOT UPDATE";
    case FailureStage::image_generation: return "OPENPRINTTAG IMAGE GENERATION";
    case FailureStage::reference_vector_mismatch: return "OPENPRINTTAG REFERENCE VECTOR MISMATCH";
    case FailureStage::tag_not_blank: return "TAG NOT BLANK";
    case FailureStage::uid_changed_before_write: return "TAG UID CHANGED BEFORE WRITE";
    case FailureStage::geometry_changed: return "TAG GEOMETRY CHANGED";
    case FailureStage::tag_locked_write_protected: return "TAG LOCKED / WRITE PROTECTED";
    case FailureStage::write_authorization: return "WRITE AUTHORIZATION";
    case FailureStage::block_write: return "NFC-V BLOCK WRITE";
    case FailureStage::block_verify: return "NFC-V BLOCK VERIFY";
    case FailureStage::full_image_verify: return "NFC-V FULL IMAGE VERIFY";
    case FailureStage::post_write_decode: return "OPENPRINTTAG POST-WRITE DECODE";
    case FailureStage::post_write_transport: return "NFC POST-WRITE TRANSPORT";
  }
  return "UNKNOWN";
}


InitializationAuthorizationResult validate_initialization_authorization(
    const char* supplied_uid,
    const char* supplied_checksum,
    const char* supplied_confirmation,
    const char* expected_uid,
    const char* expected_checksum) {
  if (supplied_uid == nullptr || expected_uid == nullptr ||
      std::strcmp(supplied_uid, expected_uid) != 0) {
    return InitializationAuthorizationResult::uid_mismatch;
  }
  if (supplied_checksum == nullptr || expected_checksum == nullptr ||
      std::strcmp(supplied_checksum, expected_checksum) != 0) {
    return InitializationAuthorizationResult::checksum_mismatch;
  }
  if (supplied_confirmation == nullptr ||
      std::strcmp(supplied_confirmation, "INITIALIZE") != 0) {
    return InitializationAuthorizationResult::confirmation_mismatch;
  }
  return InitializationAuthorizationResult::pass;
}

bool is_zero_filled(core::ByteView bytes) {
  if (bytes.data == nullptr && bytes.size != 0U) return false;
  for (std::size_t index = 0U; index < bytes.size; ++index) {
    if (bytes[index] != 0U) return false;
  }
  return true;
}

bool matches_initialization_geometry(
    const NfcvSystemInformation& information,
    std::uint32_t expected_block_count,
    std::uint16_t expected_block_size) {
  return information.memory_size_present &&
         information.block_count == expected_block_count &&
         information.block_size == expected_block_size;
}

core::Result<std::vector<std::uint8_t>> build_initialization_target(
    core::ByteView current_image,
    core::ByteView initialization_image) {
  if ((current_image.data == nullptr && current_image.size != 0U) ||
      initialization_image.data == nullptr || initialization_image.size == 0U ||
      initialization_image.size > current_image.size) {
    return core::Result<std::vector<std::uint8_t>>::failure({
        core::ErrorCategory::invalid_openprinttag,
        "OpenPrintTag initialization image does not fit the detected tag",
        false,
    });
  }
  std::vector<std::uint8_t> result(
      current_image.data,
      current_image.data + current_image.size);
  std::copy(
      initialization_image.data,
      initialization_image.data + initialization_image.size,
      result.begin());
  return core::Result<std::vector<std::uint8_t>>::success(std::move(result));
}

}  // namespace opentag::diagnostics::shared_i2c
