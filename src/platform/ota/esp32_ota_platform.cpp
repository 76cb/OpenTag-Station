#include "platform/ota/esp32_ota_platform.hpp"

#include <esp_app_format.h>
#include <esp_err.h>
#include <esp_image_format.h>
#include <esp_partition.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>

#include "platform/ota/firmware_manifest.hpp"

#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) || \
    CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE != 1
#error "OpenTag Station OTA requires ESP-IDF bootloader rollback support"
#endif

namespace opentag::platform::ota {
namespace {

constexpr std::size_t manifest_scan_bytes = 4096U;
constexpr std::size_t manifest_scan_step =
    manifest_scan_bytes - sizeof(FirmwareManifest) + 1U;

core::Error update_error(
    std::string message,
    bool retryable = false) {
  return {core::ErrorCategory::firmware_update, std::move(message), retryable};
}

core::Error idf_error(const char* operation, esp_err_t error) {
  std::string message(operation);
  message += " failed: ";
  message += esp_err_to_name(error);
  return update_error(std::move(message));
}

std::string_view fixed_text(const char* value, std::size_t capacity) {
  const auto* end = static_cast<const char*>(std::memchr(value, '\0', capacity));
  return end == nullptr ? std::string_view{} :
                          std::string_view(value, static_cast<std::size_t>(end - value));
}

bool printable_text(std::string_view value) {
  if (value.empty()) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte >= 0x20U && byte < 0x7FU;
  });
}

opentag::ota::PartitionDescriptor describe_partition(
    const esp_partition_t* partition) {
  opentag::ota::PartitionDescriptor result;
  if (partition == nullptr) return result;
  (void)result.label.assign(
      fixed_text(partition->label, sizeof(partition->label)));
  result.address = partition->address;
  result.size = partition->size;
  result.subtype = static_cast<std::uint8_t>(partition->subtype);
  return result;
}

const esp_partition_t* resolve_partition(
    const opentag::ota::PartitionDescriptor& descriptor) {
  if (!descriptor.present() || descriptor.label.empty()) return nullptr;
  std::array<char, 17U> label{};
  const auto text = descriptor.label.view();
  std::copy(text.begin(), text.end(), label.begin());
  const auto* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(descriptor.subtype),
      label.data());
  if (partition == nullptr || partition->address != descriptor.address ||
      partition->size != descriptor.size) {
    return nullptr;
  }
  return partition;
}

opentag::ota::PartitionImageState image_state(esp_ota_img_states_t state) {
  switch (state) {
    case ESP_OTA_IMG_NEW:
      return opentag::ota::PartitionImageState::new_image;
    case ESP_OTA_IMG_PENDING_VERIFY:
      return opentag::ota::PartitionImageState::pending_verify;
    case ESP_OTA_IMG_VALID:
      return opentag::ota::PartitionImageState::valid;
    case ESP_OTA_IMG_INVALID:
      return opentag::ota::PartitionImageState::invalid;
    case ESP_OTA_IMG_ABORTED:
      return opentag::ota::PartitionImageState::aborted;
    case ESP_OTA_IMG_UNDEFINED:
      return opentag::ota::PartitionImageState::undefined;
  }
  return opentag::ota::PartitionImageState::unknown;
}

bool sane_manifest(const FirmwareManifest& manifest) {
  if (manifest.magic != firmware_manifest_magic ||
      manifest.trailer != firmware_manifest_trailer ||
      manifest.schema_version != firmware_manifest_schema ||
      manifest.structure_size != sizeof(FirmwareManifest)) {
    return false;
  }
  const auto project = fixed_text(manifest.project.data(), manifest.project.size());
  const auto hardware =
      fixed_text(manifest.hardware_id.data(), manifest.hardware_id.size());
  const auto version = fixed_text(manifest.version.data(), manifest.version.size());
  const auto git = fixed_text(manifest.git_sha.data(), manifest.git_sha.size());
  const auto date = fixed_text(manifest.build_date.data(), manifest.build_date.size());
  return printable_text(project) && printable_text(hardware) &&
      printable_text(version) && printable_text(git) && printable_text(date);
}

core::Result<FirmwareManifest> find_manifest(
    const esp_partition_t* partition,
    std::uint32_t image_size) {
  if (partition == nullptr || image_size < sizeof(FirmwareManifest) ||
      image_size > partition->size) {
    return core::Result<FirmwareManifest>::failure(
        update_error("Firmware manifest search range is invalid"));
  }

  std::array<std::uint8_t, manifest_scan_bytes> buffer{};
  for (std::uint32_t offset = 0U; offset < image_size;) {
    const auto count = std::min<std::uint32_t>(
        buffer.size(), image_size - offset);
    const auto read = esp_partition_read(partition, offset, buffer.data(), count);
    if (read != ESP_OK) {
      return core::Result<FirmwareManifest>::failure(
          idf_error("Reading the staged firmware manifest", read));
    }
    if (count >= sizeof(FirmwareManifest)) {
      const auto last = count - sizeof(FirmwareManifest);
      for (std::size_t index = 0U; index <= last; ++index) {
        if (std::memcmp(
                buffer.data() + index,
                firmware_manifest_magic.data(),
                firmware_manifest_magic.size()) != 0) {
          continue;
        }
        FirmwareManifest manifest{};
        std::memcpy(&manifest, buffer.data() + index, sizeof(manifest));
        if (sane_manifest(manifest)) {
          return core::Result<FirmwareManifest>::success(manifest);
        }
      }
    }
    if (image_size - offset <= count) break;
    offset += static_cast<std::uint32_t>(manifest_scan_step);
  }
  return core::Result<FirmwareManifest>::failure(
      update_error("Staged image does not contain a valid OpenTag manifest"));
}

core::Result<opentag::ota::FirmwareDescriptor> describe_firmware(
    const esp_partition_t* partition,
    std::uint32_t image_size) {
  const auto manifest = find_manifest(partition, image_size);
  if (!manifest.ok()) {
    return core::Result<opentag::ota::FirmwareDescriptor>::failure(
        manifest.error());
  }
  esp_app_desc_t app{};
  const auto described = esp_ota_get_partition_description(partition, &app);
  if (described != ESP_OK) {
    return core::Result<opentag::ota::FirmwareDescriptor>::failure(
        idf_error("Reading the staged application descriptor", described));
  }

  opentag::ota::FirmwareDescriptor result;
  const auto& value = manifest.value();
  const bool assigned = result.project_name.assign(
                            fixed_text(value.project.data(), value.project.size())) &&
      result.version.assign(
          fixed_text(value.version.data(), value.version.size())) &&
      result.git_sha.assign(
          fixed_text(value.git_sha.data(), value.git_sha.size())) &&
      result.build_date.assign(
          fixed_text(value.build_date.data(), value.build_date.size())) &&
      result.board_id.assign(
          fixed_text(value.hardware_id.data(), value.hardware_id.size())) &&
      result.idf_version.assign(fixed_text(app.idf_ver, sizeof(app.idf_ver)));
  if (!assigned) {
    return core::Result<opentag::ota::FirmwareDescriptor>::failure(
        update_error("Firmware manifest fields exceed OTA metadata bounds"));
  }
  return core::Result<opentag::ota::FirmwareDescriptor>::success(result);
}

}  // namespace

MbedTlsSha256::MbedTlsSha256() {
  mbedtls_sha256_init(&context_);
}

MbedTlsSha256::~MbedTlsSha256() {
  mbedtls_sha256_free(&context_);
}

core::Result<void> MbedTlsSha256::begin() {
  if (active_) {
    return core::Result<void>::failure(
        update_error("SHA-256 calculation is already active"));
  }
  if (mbedtls_sha256_starts_ret(&context_, 0) != 0) {
    return core::Result<void>::failure(
        update_error("SHA-256 calculation could not be initialized"));
  }
  active_ = true;
  return core::Result<void>::success();
}

core::Result<void> MbedTlsSha256::update(core::ByteView chunk) {
  if (!active_ || chunk.data == nullptr || chunk.empty()) {
    return core::Result<void>::failure(
        update_error("SHA-256 update state or chunk is invalid"));
  }
  if (mbedtls_sha256_update_ret(&context_, chunk.data, chunk.size) != 0) {
    return core::Result<void>::failure(
        update_error("SHA-256 calculation failed while streaming"));
  }
  return core::Result<void>::success();
}

core::Result<opentag::ota::Sha256Digest> MbedTlsSha256::finish() {
  if (!active_) {
    return core::Result<opentag::ota::Sha256Digest>::failure(
        update_error("SHA-256 calculation is not active"));
  }
  opentag::ota::Sha256Digest digest{};
  const auto result = mbedtls_sha256_finish_ret(&context_, digest.data());
  active_ = false;
  if (result != 0) {
    return core::Result<opentag::ota::Sha256Digest>::failure(
        update_error("SHA-256 calculation could not be finalized"));
  }
  return core::Result<opentag::ota::Sha256Digest>::success(digest);
}

void MbedTlsSha256::abort() {
  if (!active_) return;
  mbedtls_sha256_free(&context_);
  mbedtls_sha256_init(&context_);
  active_ = false;
}

core::Result<opentag::ota::PlatformStatus> Esp32OtaPlatform::status() {
  // Keep the application-owned identity record reachable from live code.
  // Compiler-level `used` alone does not prevent linker section collection.
  if (!sane_manifest(opentag_firmware_manifest)) {
    return core::Result<opentag::ota::PlatformStatus>::failure(
        update_error("Linked firmware manifest is invalid"));
  }
  const auto* running = esp_ota_get_running_partition();
  const auto* boot = esp_ota_get_boot_partition();
  const auto* inactive = esp_ota_get_next_update_partition(running);
  if (running == nullptr || boot == nullptr || inactive == nullptr ||
      running == inactive) {
    return core::Result<opentag::ota::PlatformStatus>::failure(
        update_error("A/B application partitions are unavailable"));
  }

  opentag::ota::PlatformStatus result;
  result.running = describe_partition(running);
  result.boot = describe_partition(boot);
  result.inactive = describe_partition(inactive);

  esp_image_metadata_t metadata{};
  metadata.start_addr = running->address;
  const esp_partition_pos_t position{running->address, running->size};
  const auto verified = esp_image_verify(
      ESP_IMAGE_VERIFY_SILENT, &position, &metadata);
  if (verified != ESP_OK) {
    return core::Result<opentag::ota::PlatformStatus>::failure(
        idf_error("Validating the running firmware", verified));
  }
  const auto described = describe_firmware(running, metadata.image_len);
  if (!described.ok()) {
    return core::Result<opentag::ota::PlatformStatus>::failure(
        described.error());
  }
  result.running_image = described.value();

  esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
  const auto state_result = esp_ota_get_state_partition(running, &running_state);
  if (state_result == ESP_OK) {
    result.running_state = image_state(running_state);
  } else if (state_result == ESP_ERR_NOT_FOUND) {
    result.running_state = opentag::ota::PartitionImageState::undefined;
  } else {
    return core::Result<opentag::ota::PlatformStatus>::failure(
        idf_error("Reading the running OTA state", state_result));
  }

  if (const auto* last_invalid = esp_ota_get_last_invalid_partition();
      last_invalid != nullptr) {
    result.last_invalid = describe_partition(last_invalid);
  }
  result.rollback_available = esp_ota_check_rollback_is_possible();
  return core::Result<opentag::ota::PlatformStatus>::success(result);
}

core::Result<void> Esp32OtaPlatform::begin_write(
    const opentag::ota::PartitionDescriptor& target,
    std::uint32_t expected_size) {
  if (write_open_) {
    return core::Result<void>::failure(
        update_error("An OTA partition writer is already open"));
  }
  const auto* partition = resolve_partition(target);
  const auto* running = esp_ota_get_running_partition();
  const auto* expected = esp_ota_get_next_update_partition(running);
  if (partition == nullptr || running == nullptr || expected == nullptr ||
      partition != expected || partition == running || expected_size == 0U ||
      expected_size > partition->size) {
    return core::Result<void>::failure(
        update_error("OTA target is not the current inactive application slot"));
  }
  const auto result = esp_ota_begin(partition, expected_size, &write_handle_);
  if (result != ESP_OK) {
    write_handle_ = 0U;
    return core::Result<void>::failure(
        idf_error("Opening the inactive OTA partition", result));
  }
  write_partition_ = partition;
  write_open_ = true;
  return core::Result<void>::success();
}

core::Result<void> Esp32OtaPlatform::write(core::ByteView chunk) {
  if (!write_open_ || write_partition_ == nullptr || chunk.data == nullptr ||
      chunk.empty() || chunk.size > opentag::ota::maximum_upload_chunk_bytes) {
    return core::Result<void>::failure(
        update_error("OTA write state or chunk is invalid"));
  }
  const auto result = esp_ota_write(write_handle_, chunk.data, chunk.size);
  if (result != ESP_OK) {
    return core::Result<void>::failure(
        idf_error("Writing the inactive OTA partition", result));
  }
  return core::Result<void>::success();
}

core::Result<void> Esp32OtaPlatform::finish_write() {
  if (!write_open_) {
    return core::Result<void>::failure(
        update_error("OTA partition writer is not open"));
  }
  const auto handle = write_handle_;
  write_handle_ = 0U;
  write_partition_ = nullptr;
  write_open_ = false;
  const auto result = esp_ota_end(handle);
  if (result != ESP_OK) {
    return core::Result<void>::failure(
        idf_error("Finalizing and validating the OTA image", result));
  }
  return core::Result<void>::success();
}

core::Result<void> Esp32OtaPlatform::abort_write() {
  if (!write_open_) return core::Result<void>::success();
  const auto handle = write_handle_;
  write_handle_ = 0U;
  write_partition_ = nullptr;
  write_open_ = false;
  const auto result = esp_ota_abort(handle);
  if (result != ESP_OK) {
    return core::Result<void>::failure(
        idf_error("Aborting the OTA partition writer", result));
  }
  return core::Result<void>::success();
}

core::Result<opentag::ota::ImageValidation>
Esp32OtaPlatform::validate_staged_image(
    const opentag::ota::PartitionDescriptor& target,
    std::uint32_t expected_size) {
  const auto* partition = resolve_partition(target);
  const auto* running = esp_ota_get_running_partition();
  const auto* inactive = esp_ota_get_next_update_partition(running);
  if (partition == nullptr || partition != inactive || partition == running ||
      expected_size == 0U || expected_size > partition->size) {
    return core::Result<opentag::ota::ImageValidation>::failure(
        update_error("Staged image target is not the inactive application slot"));
  }

  esp_image_metadata_t metadata{};
  metadata.start_addr = partition->address;
  const esp_partition_pos_t position{partition->address, partition->size};
  const auto verified = esp_image_verify(
      ESP_IMAGE_VERIFY_SILENT, &position, &metadata);
  if (verified != ESP_OK) {
    return core::Result<opentag::ota::ImageValidation>::failure(
        idf_error("Verifying the staged firmware image", verified));
  }
  if (metadata.image_len != expected_size ||
      metadata.image.magic != ESP_IMAGE_HEADER_MAGIC ||
      metadata.image.chip_id != ESP_CHIP_ID_ESP32S3 ||
      metadata.image.hash_appended != 1U) {
    return core::Result<opentag::ota::ImageValidation>::failure(
        update_error("Staged image size, chip header, or appended hash is invalid"));
  }

  const auto described = describe_firmware(partition, expected_size);
  if (!described.ok()) {
    return core::Result<opentag::ota::ImageValidation>::failure(
        described.error());
  }
  opentag::ota::ImageValidation validation;
  validation.structure_valid = true;
  validation.image_size = metadata.image_len;
  validation.image = described.value();
  validation.target_compatible =
      validation.image.board_id.view() == firmware_hardware_id;
  validation.project_compatible =
      validation.image.project_name.view() == firmware_project_name;
  return core::Result<opentag::ota::ImageValidation>::success(validation);
}

core::Result<void> Esp32OtaPlatform::activate(
    const opentag::ota::PartitionDescriptor& target) {
  auto* partition = resolve_partition(target);
  auto* running = esp_ota_get_running_partition();
  auto* inactive = esp_ota_get_next_update_partition(running);
  if (partition == nullptr || partition != inactive || partition == running) {
    return core::Result<void>::failure(
        update_error("Only the current inactive OTA slot can be activated"));
  }

  esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
  const auto state_result =
      esp_ota_get_state_partition(running, &running_state);
  const bool ota_metadata_uninitialized =
      state_result == ESP_ERR_NOT_FOUND ||
      (state_result == ESP_OK && running_state == ESP_OTA_IMG_UNDEFINED);
  if (ota_metadata_uninitialized) {
    // A serial-flashed ota_0 image can boot with erased otadata. Seed the
    // running image as the known-good rollback target before selecting the
    // first candidate. The order matters: mark-valid applies to the active
    // otadata entry, so it must happen before the inactive slot is selected.
    const auto selected_running = esp_ota_set_boot_partition(running);
    if (selected_running != ESP_OK) {
      return core::Result<void>::failure(idf_error(
          "Seeding the running OTA partition", selected_running));
    }
    const auto marked_valid = esp_ota_mark_app_valid_cancel_rollback();
    if (marked_valid != ESP_OK) {
      return core::Result<void>::failure(idf_error(
          "Marking the running OTA partition valid", marked_valid));
    }

    // Re-resolve every pointer after mutating otadata. Never rely on a stale
    // target selection when choosing the partition that will boot next.
    partition = resolve_partition(target);
    running = esp_ota_get_running_partition();
    inactive = esp_ota_get_next_update_partition(running);
    if (partition == nullptr || partition != inactive ||
        partition == running) {
      return core::Result<void>::failure(update_error(
          "Inactive OTA topology changed while seeding rollback metadata"));
    }
  } else if (state_result != ESP_OK) {
    return core::Result<void>::failure(idf_error(
        "Reading the running OTA state before activation", state_result));
  } else if (running_state != ESP_OTA_IMG_VALID) {
    return core::Result<void>::failure(update_error(
        "Running OTA image is not a confirmed rollback target"));
  }

  const auto result = esp_ota_set_boot_partition(partition);
  if (result != ESP_OK) {
    return core::Result<void>::failure(
        idf_error("Selecting the validated OTA boot partition", result));
  }
  return core::Result<void>::success();
}

core::Result<void> Esp32OtaPlatform::confirm_running() {
  const auto* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    return core::Result<void>::failure(
        update_error("Running OTA partition is unavailable"));
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const auto read = esp_ota_get_state_partition(running, &state);
  if (read != ESP_OK) {
    return core::Result<void>::failure(
        idf_error("Reading candidate OTA state", read));
  }
  if (state == ESP_OTA_IMG_VALID) return core::Result<void>::success();
  if (state != ESP_OTA_IMG_NEW && state != ESP_OTA_IMG_PENDING_VERIFY) {
    return core::Result<void>::failure(
        update_error("Running image is not awaiting OTA confirmation"));
  }
  const auto result = esp_ota_mark_app_valid_cancel_rollback();
  if (result != ESP_OK) {
    return core::Result<void>::failure(
        idf_error("Confirming the candidate OTA image", result));
  }
  return core::Result<void>::success();
}

core::Result<void> Esp32OtaPlatform::rollback_and_reboot() {
  if (!esp_ota_check_rollback_is_possible()) {
    return core::Result<void>::failure(
        update_error("No known-good OTA partition is available for rollback"));
  }
  const auto result = esp_ota_mark_app_invalid_rollback_and_reboot();
  return result == ESP_OK
      ? core::Result<void>::success()
      : core::Result<void>::failure(
            idf_error("Requesting OTA rollback", result));
}

}  // namespace opentag::platform::ota
