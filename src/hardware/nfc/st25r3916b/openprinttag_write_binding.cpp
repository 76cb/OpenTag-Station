#include "hardware/nfc/st25r3916b/i2c_reader.hpp"
#if defined(ARDUINO_ARCH_ESP32)
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include <algorithm>
#include <cstring>
namespace opentag::hardware::nfc::st25r3916b {
using opentag::nfc::nfcv::Uid;
namespace {
std::array<std::uint8_t, 8> wire(const Uid &uid) {
  std::array<std::uint8_t, 8> bytes{};
  std::reverse_copy(uid.bytes.begin(), uid.bytes.end(), bytes.begin());
  return bytes;
}
} // namespace
core::Result<opentag::nfc::WriterSystemInformation>
I2cReader::writer_system_information(const Uid &uid) {
  const auto id = wire(uid);
  std::array<std::uint8_t, 64> bytes{};
  std::uint16_t length = 0;
  auto rc = nfc_.rfalNfcvPollerGetSystemInformation(RFAL_NFCV_REQ_FLAG_DEFAULT,
                                                    id.data(), bytes.data(),
                                                    bytes.size(), &length);
  if (rc != ERR_NONE || length < 10 || length > bytes.size() || (bytes[0] & 1))
    return core::Result<opentag::nfc::WriterSystemInformation>::failure(
        error("Writer system information", rc));
  return core::Result<opentag::nfc::WriterSystemInformation>::success(
      {bytes, length});
}
core::Result<void> I2cReader::security_read(const Uid &uid, std::size_t block,
                                            std::uint8_t *data,
                                            std::uint8_t &security) {
  if (block >= 80)
    return core::Result<void>::failure(error("Writer read bounds", ERR_PARAM));
  const auto id = wire(uid);
  std::array<std::uint8_t, 34> response{};
  std::uint16_t received = 0;
  // Exact accepted diagnostic OPTION read: flags, security, four data bytes.
  const auto rc = nfc_.rfalNfcvPollerReadSingleBlock(
      RFAL_NFCV_REQ_FLAG_DEFAULT | RFAL_NFCV_REQ_FLAG_OPTION, id.data(), block,
      response.data(), response.size(), &received);
  if (rc != ERR_NONE || received != 6 || (response[0] & 1))
    return core::Result<void>::failure(error("Writer security/readback", rc));
  security = response[1];
  std::memcpy(data, response.data() + 2, 4);
  return health();
}
core::Result<void>
I2cReader::commit_openprinttag_block(const Uid &uid, std::size_t block,
                                     const std::uint8_t *data) {
  if (block >= 78 || !data)
    return core::Result<void>::failure(
        error("Writer preserved range", ERR_PARAM));
  const auto id = wire(uid);
  // Same addressed DEFAULT write, length and status handling as the physically
  // validated blank diagnostic. No retry that might hide an uncertain write.
  const auto rc = nfc_.rfalNfcvPollerWriteSingleBlock(
      RFAL_NFCV_REQ_FLAG_DEFAULT, id.data(), block, data, 4);
  if (rc != ERR_NONE)
    return core::Result<void>::failure(error("OpenPrintTag block write", rc));
  return health();
}
} // namespace opentag::hardware::nfc::st25r3916b
#endif
