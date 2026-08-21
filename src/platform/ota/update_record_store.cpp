#include "platform/ota/update_record_store.hpp"

#include <algorithm>
#include <limits>

namespace opentag::platform::ota {
namespace {

constexpr char update_preferences_namespace[] = "opentagOta";
constexpr char update_record_key[] = "record";
constexpr char update_generation_key[] = "generation";

core::Error storage_error(const char* message) {
  return {core::ErrorCategory::storage, message, false};
}

}  // namespace

core::Result<void> Esp32UpdateRecordStore::initialize() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) return core::Result<void>::success();
  if (!preferences_.begin(update_preferences_namespace, false)) {
    return core::Result<void>::failure(
        storage_error("OTA metadata namespace is unavailable"));
  }
  initialized_ = true;
  return core::Result<void>::success();
}

core::Result<std::optional<opentag::ota::UpdateRecord>>
Esp32UpdateRecordStore::load() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    return core::Result<std::optional<opentag::ota::UpdateRecord>>::failure(
        storage_error("OTA metadata store is not initialized"));
  }
  const auto size = preferences_.getBytesLength(update_record_key);
  if (size == 0U) {
    return core::Result<std::optional<opentag::ota::UpdateRecord>>::success(
        std::nullopt);
  }
  if (size != sizeof(opentag::ota::UpdateRecord)) {
    return core::Result<std::optional<opentag::ota::UpdateRecord>>::failure(
        storage_error("OTA metadata record has an incompatible size"));
  }
  opentag::ota::UpdateRecord record{};
  if (preferences_.getBytes(update_record_key, &record, sizeof(record)) !=
      sizeof(record)) {
    return core::Result<std::optional<opentag::ota::UpdateRecord>>::failure(
        storage_error("OTA metadata record could not be read"));
  }
  if (!opentag::ota::valid_update_record(record)) {
    return core::Result<std::optional<opentag::ota::UpdateRecord>>::failure(
        storage_error("OTA metadata record failed integrity validation"));
  }
  return core::Result<std::optional<opentag::ota::UpdateRecord>>::success(
      record);
}

core::Result<std::uint64_t> Esp32UpdateRecordStore::reserve_generation(
    std::uint64_t minimum_exclusive) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    return core::Result<std::uint64_t>::failure(
        storage_error("OTA metadata store is not initialized"));
  }
  const auto current = preferences_.getULong64(update_generation_key, 0U);
  const auto floor = std::max(current, minimum_exclusive);
  if (floor == std::numeric_limits<std::uint64_t>::max()) {
    return core::Result<std::uint64_t>::failure(
        storage_error("OTA generation counter is exhausted"));
  }
  const auto next = floor + 1U;
  if (preferences_.putULong64(update_generation_key, next) != sizeof(next)) {
    return core::Result<std::uint64_t>::failure(
        storage_error("OTA generation reservation could not be persisted"));
  }
  return core::Result<std::uint64_t>::success(next);
}

core::Result<void> Esp32UpdateRecordStore::save(
    const opentag::ota::UpdateRecord& record) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    return core::Result<void>::failure(
        storage_error("OTA metadata store is not initialized"));
  }
  if (!opentag::ota::valid_update_record(record)) {
    return core::Result<void>::failure(
        storage_error("OTA metadata record is invalid"));
  }
  if (preferences_.putBytes(update_record_key, &record, sizeof(record)) !=
      sizeof(record)) {
    return core::Result<void>::failure(
        storage_error("OTA metadata record could not be persisted"));
  }
  return core::Result<void>::success();
}

}  // namespace opentag::platform::ota
