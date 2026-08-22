#include "platform/storage/storage_service.hpp"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_partition.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <string>

#include "core/saturating_counter.hpp"

namespace opentag::platform::storage {
namespace {

constexpr char scale_calibration_key[] = "scaleCal";
constexpr char application_preferences_namespace[] = "opentag";
constexpr char control_preferences_namespace[] = "opentagCtl";
constexpr char factory_reset_pending_key[] = "resetPending";
constexpr char filesystem_provisioned_key[] = "fsProvisioned";
constexpr char filesystem_format_pending_key[] = "fsFormatPending";
constexpr char filesystem_base_path[] = "/littlefs";
constexpr char filesystem_partition_label[] = "littlefs";
constexpr std::uint8_t filesystem_max_open_files = 10U;
constexpr std::uint32_t scale_calibration_magic = 0x5343414CU;  // SCAL
constexpr char configuration_path[] = "/configuration.json";
constexpr char configuration_staging_path[] = "/configuration.new";
constexpr char configuration_backup_path[] = "/configuration.bak";
constexpr std::size_t maximum_configuration_bytes = 16384U;

struct StoredScaleCalibration {
  std::uint32_t magic;
  std::uint32_t schema_version;
  std::int32_t zero_offset_counts;
  float reference_grams;
  double counts_per_gram;
  float load_cell_capacity_grams;
  std::uint32_t checksum;
};

static_assert(sizeof(StoredScaleCalibration) == 32U);
static_assert(offsetof(StoredScaleCalibration, checksum) == 28U);

std::uint32_t crc32(const void* data, std::size_t size) {
  auto crc = 0xFFFFFFFFU;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t index = 0U; index < size; ++index) {
    crc ^= bytes[index];
    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

core::Error storage_error(const char* message) {
  return {core::ErrorCategory::storage, message, false};
}

bool filesystem_partition_is_fully_erased() {
  const auto* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      filesystem_partition_label);
  if (partition == nullptr || partition->size == 0U) return false;

  std::array<std::uint8_t, 512U> block{};
  for (std::size_t offset = 0U; offset < partition->size;
       offset += block.size()) {
    const auto length = std::min(block.size(), partition->size - offset);
    if (esp_partition_read(partition, offset, block.data(), length) != ESP_OK ||
        std::any_of(
            block.begin(),
            block.begin() + static_cast<std::ptrdiff_t>(length),
            [](std::uint8_t byte) { return byte != 0xFFU; })) {
      return false;
    }
  }
  return true;
}

core::Result<std::optional<std::string>> read_document_file(const char* path) {
  if (!LittleFS.exists(path)) {
    return core::Result<std::optional<std::string>>::success(std::nullopt);
  }
  auto file = LittleFS.open(path, FILE_READ);
  if (!file || file.isDirectory() || file.size() == 0U ||
      file.size() > maximum_configuration_bytes) {
    if (file) file.close();
    return core::Result<std::optional<std::string>>::failure(
        storage_error("configuration file has an invalid size or type"));
  }
  std::string document(file.size(), '\0');
  const auto bytes_read = file.read(
      reinterpret_cast<std::uint8_t*>(&document[0]), document.size());
  file.close();
  if (bytes_read != document.size()) {
    return core::Result<std::optional<std::string>>::failure(
        storage_error("configuration file could not be read completely"));
  }
  return core::Result<std::optional<std::string>>::success(std::move(document));
}

core::Result<void> remove_configuration_documents() {
  // Rollback sources are removed before the primary so a partial filesystem
  // failure retains the primary configuration.
  constexpr const char* reset_paths[] = {
      configuration_backup_path,
      configuration_staging_path,
      configuration_path,
  };
  for (const auto* path : reset_paths) {
    if (LittleFS.exists(path) && !LittleFS.remove(path)) {
      return core::Result<void>::failure(
          storage_error("factory reset could not remove device configuration"));
    }
  }
  return core::Result<void>::success();
}

}  // namespace

bool StorageService::initialize(std::uint32_t now_ms) {
  boot_started_ms_ = now_ms;
  boot_pending_.store(false, std::memory_order_release);
  const bool application_nvs_ready =
      preferences_.begin(application_preferences_namespace, false);
  const bool control_nvs_ready =
      control_preferences_.begin(control_preferences_namespace, false);
  status_.nvs_ready = application_nvs_ready && control_nvs_ready;
  const bool reset_pending =
      control_nvs_ready &&
      control_preferences_.getBool(factory_reset_pending_key, false);
  if (reset_pending) {
    reset_in_progress_.store(true, std::memory_order_release);
  }

  Serial.printf(
      "littlefs_mount=attempt label=%s format=disabled\n",
      filesystem_partition_label);
  status_.filesystem_ready = LittleFS.begin(
      false,
      filesystem_base_path,
      filesystem_max_open_files,
      filesystem_partition_label);
  const bool filesystem_was_provisioned =
      reset_pending ||
      (control_nvs_ready &&
       control_preferences_.getBool(filesystem_provisioned_key, false)) ||
      (application_nvs_ready &&
       preferences_.getBool(filesystem_provisioned_key, false));
  bool filesystem_format_authorized =
      control_nvs_ready &&
      control_preferences_.getBool(filesystem_format_pending_key, false);
  const bool filesystem_is_factory_blank =
      !status_.filesystem_ready && !filesystem_was_provisioned &&
      !filesystem_format_authorized &&
      filesystem_partition_is_fully_erased();
  if (filesystem_is_factory_blank && status_.nvs_ready) {
    filesystem_format_authorized = control_preferences_.putBool(
        filesystem_format_pending_key, true);
  }

  bool filesystem_format_attempted = false;
  if (!status_.filesystem_ready && status_.nvs_ready &&
      !filesystem_was_provisioned && filesystem_format_authorized) {
    // The durable intent is created only after the whole partition is proven
    // erased. If power is lost during the first format, the same intent safely
    // authorizes a retry without weakening protection for provisioned data.
    Serial.println(
        "littlefs_mount=unprovisioned performing=one-time-guarded-format");
    filesystem_format_attempted = true;
    status_.filesystem_ready = LittleFS.begin(
        true,
        filesystem_base_path,
        filesystem_max_open_files,
        filesystem_partition_label);
  }
  if (status_.filesystem_ready) {
    Serial.printf(
        "littlefs_mount=ready label=%s provisioned_before=%s\n",
        filesystem_partition_label,
        filesystem_was_provisioned ? "yes" : "no");
  } else {
    Serial.printf(
        "littlefs_mount=ERROR label=%s action=data-preserved format=%s\n",
        filesystem_partition_label,
        filesystem_was_provisioned
            ? "blocked-provisioned"
            : filesystem_format_attempted
                  ? "format-failed-retry-authorized"
                  : filesystem_is_factory_blank
                        ? "blocked-format-intent-not-durable"
                        : "blocked-not-confirmed-erased");
  }

  if (reset_pending) {
    if (!status_.filesystem_ready) return false;
    const std::lock_guard<std::mutex> lock(preferences_mutex_);
    const auto removed = remove_configuration_documents();
    if (!removed.ok() || !preferences_.clear()) return false;
    // Keep the external reset intent durable until the application namespace is
    // clear and the no-format guard has been restored.
    if (!control_preferences_.putBool(filesystem_provisioned_key, true)) return false;
    if (!control_preferences_.remove(factory_reset_pending_key)) return false;
    reset_in_progress_.store(false, std::memory_order_release);
  } else if (status_.filesystem_ready && status_.nvs_ready) {
    if (!control_preferences_.putBool(filesystem_provisioned_key, true)) return false;
  }
  if (status_.filesystem_ready && status_.nvs_ready &&
      filesystem_format_authorized &&
      control_preferences_.isKey(filesystem_format_pending_key) &&
      !control_preferences_.remove(filesystem_format_pending_key)) {
    return false;
  }

  if (status_.nvs_ready) {
    const bool previous_boot_pending = preferences_.getBool("bootPending", false);
    const auto previous_crash_streak = preferences_.getUChar("crashStreak", 0U);
    status_.boot_count = core::saturating_increment(
        preferences_.getULong("bootCount", 0U));
    status_.crash_streak = previous_boot_pending
                               ? core::saturating_increment(previous_crash_streak)
                               : 0U;
    preferences_.putULong("bootCount", status_.boot_count);
    preferences_.putUChar("crashStreak", status_.crash_streak);
    preferences_.putBool("bootPending", true);
    boot_pending_.store(true, std::memory_order_release);
  }

  if (status_.filesystem_ready) {
    status_.filesystem_total_bytes = LittleFS.totalBytes();
    status_.filesystem_used_bytes = LittleFS.usedBytes();
  }
  status_.coredump_partition_present =
      esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA,
          ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
          nullptr) != nullptr;
  return status_.nvs_ready && status_.filesystem_ready;
}

bool StorageService::health_window_due(std::uint32_t now_ms) const {
  return boot_pending_.load(std::memory_order_acquire) &&
      static_cast<std::uint32_t>(now_ms - boot_started_ms_) >=
          healthy_boot_after_ms;
}

core::Result<void> StorageService::confirm_healthy_boot() {
  if (!status().nvs_ready) {
    return core::Result<void>::failure(
        storage_error("NVS is unavailable for boot confirmation"));
  }
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<void>::failure(
        storage_error("factory reset blocks boot confirmation"));
  }
  if (!boot_pending_.load(std::memory_order_acquire)) {
    return core::Result<void>::success();
  }

  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<void>::failure(
        storage_error("factory reset blocks boot confirmation"));
  }
  if (!boot_pending_.load(std::memory_order_acquire)) {
    return core::Result<void>::success();
  }

  // Persist the diagnostic streak first and the authoritative pending marker
  // last. A power loss between the writes therefore remains fail-closed: the
  // next boot still sees an unconfirmed predecessor.
  if (preferences_.putUChar("crashStreak", 0U) != sizeof(std::uint8_t)) {
    return core::Result<void>::failure(
        storage_error("boot crash streak could not be cleared"));
  }
  if (preferences_.putBool("bootPending", false) != sizeof(bool)) {
    return core::Result<void>::failure(
        storage_error("healthy boot confirmation could not be persisted"));
  }
  {
    const std::lock_guard<std::mutex> status_lock(status_mutex_);
    status_.crash_streak = 0U;
  }
  boot_pending_.store(false, std::memory_order_release);
  return core::Result<void>::success();
}

core::Result<std::optional<services::ScaleCalibration>>
StorageService::load_scale_calibration() {
  if (reset_in_progress_.load(std::memory_order_acquire) ||
      !status_.nvs_ready) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        storage_error("NVS is unavailable for scale calibration"));
  }
  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        storage_error("factory reset is in progress"));
  }
  const auto stored_size = preferences_.getBytesLength(scale_calibration_key);
  if (stored_size == 0U) {
    return core::Result<std::optional<services::ScaleCalibration>>::success(std::nullopt);
  }
  if (stored_size != sizeof(StoredScaleCalibration)) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        storage_error("stored scale calibration has an invalid size"));
  }

  StoredScaleCalibration stored{};
  if (preferences_.getBytes(scale_calibration_key, &stored, sizeof(stored)) !=
      sizeof(stored)) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        storage_error("stored scale calibration could not be read"));
  }
  if (stored.magic != scale_calibration_magic ||
      stored.checksum != crc32(&stored, offsetof(StoredScaleCalibration, checksum))) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        storage_error("stored scale calibration failed integrity validation"));
  }

  services::ScaleCalibration calibration;
  calibration.schema_version = stored.schema_version;
  calibration.zero_offset_counts = stored.zero_offset_counts;
  calibration.counts_per_gram = stored.counts_per_gram;
  calibration.reference_grams = stored.reference_grams;
  calibration.load_cell_capacity_grams = stored.load_cell_capacity_grams;
  const auto valid = calibration.validate();
  if (!valid.ok()) {
    return core::Result<std::optional<services::ScaleCalibration>>::failure(
        storage_error("stored scale calibration is invalid"));
  }
  return core::Result<std::optional<services::ScaleCalibration>>::success(calibration);
}

core::Result<void> StorageService::save_scale_calibration(
    const services::ScaleCalibration& calibration) {
  const auto valid = calibration.validate();
  if (!valid.ok()) return core::Result<void>::failure(valid.error());
  if (reset_in_progress_.load(std::memory_order_acquire) ||
      !status_.nvs_ready) {
    return core::Result<void>::failure(
        storage_error("NVS is unavailable for scale calibration"));
  }

  StoredScaleCalibration stored{};
  stored.magic = scale_calibration_magic;
  stored.schema_version = calibration.schema_version;
  stored.zero_offset_counts = calibration.zero_offset_counts;
  stored.reference_grams = calibration.reference_grams;
  stored.counts_per_gram = calibration.counts_per_gram;
  stored.load_cell_capacity_grams = calibration.load_cell_capacity_grams;
  stored.checksum = crc32(&stored, offsetof(StoredScaleCalibration, checksum));

  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<void>::failure(
        storage_error("factory reset is in progress"));
  }
  if (preferences_.putBytes(scale_calibration_key, &stored, sizeof(stored)) !=
      sizeof(stored)) {
    return core::Result<void>::failure(
        storage_error("scale calibration could not be persisted"));
  }
  return core::Result<void>::success();
}

core::Result<void> StorageService::clear_scale_calibration() {
  if (reset_in_progress_.load(std::memory_order_acquire) ||
      !status_.nvs_ready) {
    return core::Result<void>::failure(
        storage_error("NVS is unavailable for scale calibration"));
  }

  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<void>::failure(
        storage_error("factory reset is in progress"));
  }
  if (!preferences_.isKey(scale_calibration_key)) {
    return core::Result<void>::success();
  }
  if (!preferences_.remove(scale_calibration_key)) {
    return core::Result<void>::failure(
        storage_error("scale calibration could not be cleared"));
  }
  return core::Result<void>::success();
}

core::Result<std::optional<std::string>>
StorageService::load_configuration_document() {
  if (reset_in_progress_.load(std::memory_order_acquire) ||
      !status_.filesystem_ready) {
    return core::Result<std::optional<std::string>>::failure(
        storage_error("LittleFS is unavailable for configuration"));
  }
  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<std::optional<std::string>>::failure(
        storage_error("factory reset is in progress"));
  }
  return read_document_file(configuration_path);
}

core::Result<std::optional<std::string>>
StorageService::load_configuration_backup_document() {
  if (reset_in_progress_.load(std::memory_order_acquire) ||
      !status_.filesystem_ready) {
    return core::Result<std::optional<std::string>>::failure(
        storage_error("LittleFS is unavailable for configuration backup"));
  }
  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<std::optional<std::string>>::failure(
        storage_error("factory reset is in progress"));
  }
  return read_document_file(configuration_backup_path);
}

core::Result<void> StorageService::save_configuration_document(
    const std::string& document) {
  if (reset_in_progress_.load(std::memory_order_acquire) ||
      !status_.filesystem_ready) {
    return core::Result<void>::failure(
        storage_error("LittleFS is unavailable for configuration"));
  }
  if (document.empty() || document.size() > maximum_configuration_bytes) {
    return core::Result<void>::failure(
        storage_error("configuration document size is invalid"));
  }

  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (reset_in_progress_.load(std::memory_order_acquire)) {
    return core::Result<void>::failure(
        storage_error("factory reset is in progress"));
  }
  auto staging = LittleFS.open(configuration_staging_path, FILE_WRITE);
  if (!staging || staging.write(
          reinterpret_cast<const std::uint8_t*>(document.data()),
          document.size()) != document.size()) {
    if (staging) staging.close();
    return core::Result<void>::failure(
        storage_error("configuration staging write failed"));
  }
  staging.flush();
  staging.close();
  const auto verified = read_document_file(configuration_staging_path);
  if (!verified.ok() || !verified.value().has_value() ||
      *verified.value() != document) {
    return core::Result<void>::failure(
        storage_error("configuration staging verification failed"));
  }

  if (LittleFS.exists(configuration_backup_path) &&
      !LittleFS.remove(configuration_backup_path)) {
    return core::Result<void>::failure(
        storage_error("previous configuration backup could not be replaced"));
  }
  const bool had_primary = LittleFS.exists(configuration_path);
  if (had_primary &&
      !LittleFS.rename(configuration_path, configuration_backup_path)) {
    return core::Result<void>::failure(
        storage_error("configuration backup could not be created"));
  }
  if (!LittleFS.rename(configuration_staging_path, configuration_path)) {
    if (had_primary) {
      LittleFS.rename(configuration_backup_path, configuration_path);
    }
    return core::Result<void>::failure(
        storage_error("configuration commit failed"));
  }
  return core::Result<void>::success();
}


core::Result<void> StorageService::factory_reset_device_data() {
  bool expected = false;
  if (!reset_in_progress_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return core::Result<void>::failure(
        storage_error("factory reset is already in progress"));
  }
  if (!status_.nvs_ready || !status_.filesystem_ready) {
    reset_in_progress_.store(false, std::memory_order_release);
    return core::Result<void>::failure(
        storage_error("factory reset requires NVS and LittleFS"));
  }

  const std::lock_guard<std::mutex> lock(preferences_mutex_);
  if (!control_preferences_.putBool(factory_reset_pending_key, true)) {
    reset_in_progress_.store(false, std::memory_order_release);
    return core::Result<void>::failure(
        storage_error("factory reset intent could not be persisted"));
  }
  const auto removed = remove_configuration_documents();
  if (!removed.ok()) return removed;
  if (!preferences_.clear()) {
    return core::Result<void>::failure(
        storage_error("factory reset could not clear the OpenTag NVS namespace"));
  }
  if (!control_preferences_.putBool(filesystem_provisioned_key, true)) {
    return core::Result<void>::failure(
        storage_error("factory reset could not restore the filesystem guard"));
  }
  if (!control_preferences_.remove(factory_reset_pending_key)) {
    return core::Result<void>::failure(
        storage_error("factory reset intent could not be cleared"));
  }
  // Keep reset_in_progress_ true until the scheduled reboot. Every writer
  // re-checks it after acquiring preferences_mutex_, preventing resurrection
  // of data between erase completion and restart.
  return core::Result<void>::success();
}

}  // namespace opentag::platform::storage
