#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include <Wire.h>
#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "nfc/read_only_service.hpp"

namespace opentag::hardware::nfc::st25r3916b {
class I2cReader final : public opentag::nfc::IReadOnlyReader {
 public:
  I2cReader()
      : reader_(&Wire1, boards::Wt32Sc01PlusRevA::nfc_interrupt),
        nfc_(&reader_) {}
  core::Result<void> initialize() override;
  core::Result<void> field_on() override;
  core::Result<void> field_off() override;
  core::Result<void> health() override;
  core::Result<std::vector<opentag::nfc::nfcv::Uid>> inventory() override;
  core::Result<opentag::nfc::nfcv::TagGeometry> geometry(
      const opentag::nfc::nfcv::Uid&) override;
  core::Result<void> read_blocks(const opentag::nfc::nfcv::Uid&, std::size_t,
                                 std::size_t, std::size_t,
                                 std::uint8_t*) override;
  std::uint32_t now_ms() const override;
  std::uint32_t bus_errors() const override { return errors_; }
  void stack_checkpoint(const char*) const override;
  void yield_between_chunks() const override;

 private:
  core::Error error(const char* stage, ReturnCode code);
  RfalRfST25R3916Class reader_;
  RfalNfcClass nfc_;
  std::uint32_t errors_{0};
};
}  // namespace opentag::hardware::nfc::st25r3916b
#endif
