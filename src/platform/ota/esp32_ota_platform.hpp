#pragma once

#include <cstdint>

#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "ota/update_manager.hpp"

namespace opentag::platform::ota {

class MbedTlsSha256 final : public opentag::ota::ISha256 {
 public:
  MbedTlsSha256();
  ~MbedTlsSha256() override;

  MbedTlsSha256(const MbedTlsSha256&) = delete;
  MbedTlsSha256& operator=(const MbedTlsSha256&) = delete;

  [[nodiscard]] core::Result<void> begin() override;
  [[nodiscard]] core::Result<void> update(core::ByteView chunk) override;
  [[nodiscard]] core::Result<opentag::ota::Sha256Digest> finish() override;
  void abort() override;

 private:
  mbedtls_sha256_context context_{};
  bool active_{false};
};

class Esp32OtaPlatform final : public opentag::ota::IOtaPlatform {
 public:
  [[nodiscard]] core::Result<opentag::ota::PlatformStatus> status() override;
  [[nodiscard]] core::Result<void> begin_write(
      const opentag::ota::PartitionDescriptor& target,
      std::uint32_t expected_size) override;
  [[nodiscard]] core::Result<void> write(core::ByteView chunk) override;
  [[nodiscard]] core::Result<void> finish_write() override;
  [[nodiscard]] core::Result<void> abort_write() override;
  [[nodiscard]] core::Result<opentag::ota::ImageValidation>
  validate_staged_image(
      const opentag::ota::PartitionDescriptor& target,
      std::uint32_t expected_size) override;
  [[nodiscard]] core::Result<void> activate(
      const opentag::ota::PartitionDescriptor& target) override;
  [[nodiscard]] core::Result<void> confirm_running() override;
  [[nodiscard]] core::Result<void> rollback_and_reboot() override;

 private:
  const esp_partition_t* write_partition_{nullptr};
  esp_ota_handle_t write_handle_{0U};
  bool write_open_{false};
};

}  // namespace opentag::platform::ota
