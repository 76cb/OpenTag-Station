#include "hardware/nfc/st25r3916b/i2c_reader.hpp"
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>

#include <algorithm>

#include "nfc/protocols/nfcv/read_protocol.hpp"

namespace opentag::hardware::nfc::st25r3916b {
using Board = boards::Wt32Sc01PlusRevA;
using opentag::nfc::nfcv::TagGeometry;
using opentag::nfc::nfcv::Uid;
namespace {
auto wire_uid(const Uid& uid) {
  std::array<std::uint8_t, 8> result;
  std::reverse_copy(uid.bytes.begin(), uid.bytes.end(), result.begin());
  return result;
}
}  // namespace
std::uint32_t I2cReader::now_ms() const { return millis(); }
void I2cReader::yield_between_chunks() const {
  // Let the idle task run even for a maximum-sized single-block fallback read.
  vTaskDelay(1U);
}
void I2cReader::stack_checkpoint(const char* stage) const {
  Serial.printf(
      "NFC checkpoint=%s stack_free=%lu bytes\n", stage,
      static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
}
core::Error I2cReader::error(const char* stage, ReturnCode code) {
  const bool bus = (code & 0xFF00U) == ERR_I2C_GRP;
  if (bus) ++errors_;
  return {core::ErrorCategory::nfc_communication,
          std::string(stage) + " (RFAL " + std::to_string(code) + ")", !bus};
}
core::Result<void> I2cReader::health() {
  Wire1.beginTransmission(Board::nfc_i2c_address);
  if (Wire1.endTransmission() != 0U) {
    ++errors_;
    return core::Result<void>::failure({core::ErrorCategory::nfc_communication,
                                        "NFC I2C probe failed", false});
  }
  Wire1.beginTransmission(Board::nfc_i2c_address);
  Wire1.write(
      0x7FU);  // validated register-read framing, identity register 0x3F
  if (Wire1.endTransmission(false) != 0U ||
      Wire1.requestFrom(Board::nfc_i2c_address,
                        static_cast<std::uint8_t>(1U)) != 1U) {
    ++errors_;
    return core::Result<void>::failure({core::ErrorCategory::nfc_communication,
                                        "NFC chip ID transport failed", false});
  }
  if ((Wire1.read() >> 3U) != 0x06U)
    return core::Result<void>::failure({core::ErrorCategory::nfc_communication,
                                        "NFC chip ID mismatch", false});
  return core::Result<void>::success();
}
core::Result<void> I2cReader::initialize() {
  if (!Wire1.begin(Board::nfc_sda, Board::nfc_scl, Board::nfc_clock_hz))
    return core::Result<void>::failure(
        {core::ErrorCategory::nfc_communication, "Wire1 begin failed", false});
  Wire1.setTimeOut(50U);
  auto result = health();
  if (!result.ok()) return result;
  auto rc = nfc_.rfalNfcInitialize();
  if (rc == ERR_NONE) rc = nfc_.rfalNfcvPollerInitialize();
  const auto off = reader_.rfalFieldOff();
  if (rc == ERR_NONE) rc = off;
  Serial.printf(
      "NFC initialize stack_free=%lu bytes RFAL=%u\n",
      static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)), rc);
  if (rc != ERR_NONE)
    return core::Result<void>::failure(error("RFAL initialize", rc));
  return health();
}
core::Result<void> I2cReader::field_on() {
  const auto rc = reader_.rfalFieldOnAndStartGT();
  if (rc != ERR_NONE)
    return core::Result<void>::failure(error("RF field on", rc));
  return core::Result<void>::success();
}
core::Result<void> I2cReader::field_off() {
  const auto rc = reader_.rfalFieldOff();
  if (rc != ERR_NONE)
    return core::Result<void>::failure(error("RF field off", rc));
  return core::Result<void>::success();
}
core::Result<std::vector<Uid>> I2cReader::inventory() {
  std::array<rfalNfcvListenDevice, RFAL_NFC_MAX_DEVICES> devices{};
  std::uint8_t count = 0;
  const auto rc = nfc_.rfalNfcvPollerCollisionResolution(
      RFAL_COMPLIANCE_MODE_NFC, static_cast<std::uint8_t>(devices.size()),
      devices.data(), &count);
  if (rc != ERR_NONE)
    return core::Result<std::vector<Uid>>::failure(
        error("NFC-V inventory", rc));
  std::vector<Uid> result;
  for (unsigned i = 0; i < count; ++i) {
    const auto uid = Uid::from_wire_lsb_first(
        core::ByteView(devices[i].InvRes.UID, RFAL_NFCV_UID_LEN));
    if (!uid.ok()) return core::Result<std::vector<Uid>>::failure(uid.error());
    result.push_back(uid.value());
  }
  return core::Result<std::vector<Uid>>::success(std::move(result));
}
core::Result<TagGeometry> I2cReader::geometry(const Uid& uid) {
  // Same standard/extended fallback and parser used in the accepted diagnostic.
  const auto wire = wire_uid(uid);
  std::array<std::uint8_t, 64> response{};
  std::uint16_t received = 0;
  opentag::nfc::nfcv::NfcvSystemInformation info;
  auto rc = nfc_.rfalNfcvPollerGetSystemInformation(
      RFAL_NFCV_REQ_FLAG_DEFAULT, wire.data(), response.data(), response.size(),
      &received);
  bool valid = rc == ERR_NONE &&
               opentag::nfc::nfcv::parse_nfcv_system_information(
                   response.data(), received, false, info) &&
               info.memory_size_present;
  if (!valid) {
    if ((rc & 0xFF00U) == ERR_I2C_GRP)
      return core::Result<TagGeometry>::failure(
          error("System information I2C", rc));
    received = 0;
    rc = nfc_.rfalNfcvPollerExtendedGetSystemInformation(
        RFAL_NFCV_REQ_FLAG_DEFAULT, wire.data(), RFAL_NFCV_SYSINFO_REQ_ALL,
        response.data(), response.size(), &received);
    valid = rc == ERR_NONE &&
            opentag::nfc::nfcv::parse_nfcv_system_information(
                response.data(), received, true, info) &&
            info.memory_size_present;
  }
  if (!valid || info.wire_uid != wire)
    return core::Result<TagGeometry>::failure(
        error("System information/UID", rc));
  return core::Result<TagGeometry>::success(
      {info.block_size, info.block_count});
}
core::Result<void> I2cReader::read_blocks(const Uid& uid, std::size_t first,
                                          std::size_t count, std::size_t size,
                                          std::uint8_t* output) {
  const auto wire = wire_uid(uid);
  std::array<std::uint8_t, 257> response{};
  std::uint16_t received = 0;
  ReturnCode rc;
  if (count == 0 || count > 8 || size == 0 || size > 32 || first > 65535)
    return core::Result<void>::failure(
        {core::ErrorCategory::nfc_communication, "Invalid read bounds", false});
  if (count == 1) {
    rc = first <= 255U ? nfc_.rfalNfcvPollerReadSingleBlock(
                             RFAL_NFCV_REQ_FLAG_DEFAULT, wire.data(), first,
                             response.data(), response.size(), &received)
                       : nfc_.rfalNfcvPollerExtendedReadSingleBlock(
                             RFAL_NFCV_REQ_FLAG_DEFAULT, wire.data(), first,
                             response.data(), response.size(), &received);
  } else {
    // ISO15693 encodes count minus one. Never cross the 8-bit address boundary.
    rc = first + count <= 256U
             ? nfc_.rfalNfcvPollerReadMultipleBlocks(
                   RFAL_NFCV_REQ_FLAG_DEFAULT, wire.data(), first, count - 1U,
                   response.data(), response.size(), &received)
             : nfc_.rfalNfcvPollerExtendedReadMultipleBlocks(
                   RFAL_NFCV_REQ_FLAG_DEFAULT, wire.data(), first, count - 1U,
                   response.data(), response.size(), &received);
  }
  if (rc != ERR_NONE)
    return core::Result<void>::failure(error("NFC-V read", rc));
  const auto copied = opentag::nfc::nfcv::copy_nfcv_read_response(
      response.data(), received, output, count * size);
  if (copied != opentag::nfc::nfcv::ReadResponseResult::pass)
    return core::Result<void>::failure({core::ErrorCategory::nfc_communication,
                                        "Invalid read status/length", false});
  return core::Result<void>::success();
}
}  // namespace opentag::hardware::nfc::st25r3916b
#endif
