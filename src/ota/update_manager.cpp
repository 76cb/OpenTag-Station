#include "ota/update_manager.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace opentag::ota {
namespace {

core::Error update_error(std::string message, bool retryable = false) {
  return {core::ErrorCategory::firmware_update, std::move(message), retryable};
}

core::Error conflict_error(std::string message) {
  return {core::ErrorCategory::conflict, std::move(message), false};
}

core::Error bounded_error(core::Error error) {
  constexpr std::size_t maximum = 192U;
  if (error.message.size() > maximum) error.message.resize(maximum);
  for (auto& character : error.message) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20U || byte == 0x7FU) character = ' ';
  }
  return error;
}

void crc_byte(std::uint32_t& crc, std::uint8_t byte) {
  crc ^= byte;
  for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
    crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
}

template <typename Unsigned>
void crc_unsigned(std::uint32_t& crc, Unsigned value) {
  static_assert(std::is_unsigned_v<Unsigned>);
  for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    crc_byte(crc, static_cast<std::uint8_t>(value & 0xFFU));
    value >>= 8U;
  }
}

template <std::size_t Maximum>
void crc_text(std::uint32_t& crc, const BoundedText<Maximum>& value) {
  crc_unsigned(crc, value.length);
  for (const auto character : value.characters) {
    crc_byte(crc, static_cast<std::uint8_t>(character));
  }
}

void crc_partition(std::uint32_t& crc, const PartitionDescriptor& value) {
  crc_text(crc, value.label);
  crc_unsigned(crc, value.address);
  crc_unsigned(crc, value.size);
  crc_unsigned(crc, value.subtype);
}

void crc_firmware(std::uint32_t& crc, const FirmwareDescriptor& value) {
  crc_text(crc, value.project_name);
  crc_text(crc, value.version);
  crc_text(crc, value.git_sha);
  crc_text(crc, value.build_date);
  crc_text(crc, value.board_id);
  crc_text(crc, value.idf_version);
}

template <std::size_t Maximum>
bool valid_text(const BoundedText<Maximum>& value) {
  if (value.length > Maximum || value.characters[value.length] != '\0') {
    return false;
  }
  for (std::size_t index = 0U; index < value.length; ++index) {
    const auto byte = static_cast<unsigned char>(value.characters[index]);
    if (byte < 0x20U || byte == 0x7FU) return false;
  }
  return true;
}

bool valid_partition(const PartitionDescriptor& value, bool allow_empty) {
  return valid_text(value.label) &&
      ((allow_empty && !value.present()) ||
       (value.present() && !value.label.empty()));
}

bool valid_firmware(const FirmwareDescriptor& value) {
  return valid_text(value.project_name) && valid_text(value.version) &&
      valid_text(value.git_sha) && valid_text(value.build_date) &&
      valid_text(value.board_id) && valid_text(value.idf_version);
}

bool constant_time_equal(
    const Sha256Digest& left,
    const Sha256Digest& right) {
  std::uint8_t difference = 0U;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    difference |= static_cast<std::uint8_t>(left[index] ^ right[index]);
  }
  return difference == 0U;
}

bool upload_in_progress(UpdateState state) {
  return state == UpdateState::upload_receiving ||
      state == UpdateState::writing;
}

bool candidate_in_progress(UpdateState state) {
  return state == UpdateState::candidate_boot ||
      state == UpdateState::validating_candidate;
}

}  // namespace

const char* to_string(UpdateState state) {
  switch (state) {
    case UpdateState::idle: return "idle";
    case UpdateState::upload_receiving: return "upload_receiving";
    case UpdateState::writing: return "writing";
    case UpdateState::validating: return "validating";
    case UpdateState::ready_to_activate: return "ready_to_activate";
    case UpdateState::ready_to_reboot: return "ready_to_reboot";
    case UpdateState::reboot_pending: return "reboot_pending";
    case UpdateState::candidate_boot: return "candidate_boot";
    case UpdateState::validating_candidate: return "validating_candidate";
    case UpdateState::confirmed: return "confirmed";
    case UpdateState::rollback_pending: return "rollback_pending";
    case UpdateState::rolled_back: return "rolled_back";
    case UpdateState::failed: return "failed";
  }
  return "unknown";
}

const char* to_string(PartitionImageState state) {
  switch (state) {
    case PartitionImageState::unknown: return "unknown";
    case PartitionImageState::new_image: return "new";
    case PartitionImageState::pending_verify: return "pending_verify";
    case PartitionImageState::valid: return "valid";
    case PartitionImageState::invalid: return "invalid";
    case PartitionImageState::aborted: return "aborted";
    case PartitionImageState::undefined: return "undefined";
  }
  return "unknown";
}

bool same_partition(
    const PartitionDescriptor& left,
    const PartitionDescriptor& right) {
  return left.present() && right.present() &&
      left.address == right.address && left.size == right.size &&
      left.subtype == right.subtype;
}

std::uint32_t update_record_checksum(const UpdateRecord& record) {
  std::uint32_t crc = 0xFFFFFFFFU;
  crc_unsigned(crc, record.magic);
  crc_unsigned(crc, record.schema_version);
  crc_unsigned(crc, record.record_size);
  crc_unsigned(crc, record.generation);
  crc_unsigned(crc, record.operation_id);
  crc_unsigned(crc, static_cast<std::uint8_t>(record.state));
  crc_unsigned(crc, record.flags);
  crc_unsigned(crc, record.reserved);
  crc_partition(crc, record.target);
  crc_unsigned(crc, record.expected_length);
  crc_unsigned(crc, record.bytes_received);
  for (const auto byte : record.expected_sha256) crc_byte(crc, byte);
  for (const auto byte : record.calculated_sha256) crc_byte(crc, byte);
  crc_firmware(crc, record.candidate);
  crc_text(crc, record.last_error);
  crc_unsigned(crc, record.started_at_ms);
  crc_unsigned(crc, record.updated_at_ms);
  return ~crc;
}

bool valid_update_record(const UpdateRecord& record) {
  constexpr auto all_flags = record_flag_calculated_sha_available |
      record_flag_validation_passed | record_flag_activation_intent |
      record_flag_activated;
  return record.magic == UpdateRecord::magic_value &&
      record.schema_version == UpdateRecord::schema_value &&
      record.record_size == sizeof(UpdateRecord) &&
      static_cast<std::uint8_t>(record.state) <=
          static_cast<std::uint8_t>(UpdateState::failed) &&
      (record.flags & ~all_flags) == 0U && record.reserved == 0U &&
      valid_partition(record.target, true) &&
      valid_firmware(record.candidate) &&
      valid_text(record.last_error) &&
      record.expected_length <= maximum_image_bytes &&
      record.bytes_received <= record.expected_length &&
      record.checksum == update_record_checksum(record);
}

void UpdateManager::touch_locked(std::uint32_t now_ms) {
  if (state_.revision == std::numeric_limits<std::uint64_t>::max()) {
    state_.revision = 1U;
  } else {
    ++state_.revision;
  }
  state_.updated_at_ms = now_ms;
}

UpdateRecord UpdateManager::make_record_locked() const {
  UpdateRecord record{};
  record.record_size = static_cast<std::uint16_t>(sizeof(UpdateRecord));
  record.generation = state_.generation;
  record.operation_id = state_.operation_id;
  record.state = state_.state;
  record.target = state_.target;
  record.expected_length = state_.image_size;
  record.bytes_received = state_.bytes_received;
  record.expected_sha256 = state_.expected_sha256;
  record.calculated_sha256 = state_.calculated_sha256;
  record.candidate = state_.candidate;
  record.last_error = state_.last_error;
  record.started_at_ms = state_.started_at_ms;
  record.updated_at_ms = state_.updated_at_ms;
  if (state_.calculated_sha_available) {
    record.flags |= record_flag_calculated_sha_available;
  }
  if (state_.validation_passed) {
    record.flags |= record_flag_validation_passed;
  }
  if (state_.activation_intent) {
    record.flags |= record_flag_activation_intent;
  }
  if (state_.activated) record.flags |= record_flag_activated;
  record.checksum = update_record_checksum(record);
  return record;
}

core::Result<void> UpdateManager::persist_locked() {
  return records_.save(make_record_locked());
}

void UpdateManager::apply_record_locked(const UpdateRecord& record) {
  state_.generation = record.generation;
  state_.operation_id = record.operation_id;
  state_.state = record.state;
  state_.target = record.target;
  state_.image_size = record.expected_length;
  state_.bytes_received = record.bytes_received;
  state_.expected_sha256 = record.expected_sha256;
  state_.calculated_sha256 = record.calculated_sha256;
  state_.candidate = record.candidate;
  state_.last_error = record.last_error;
  state_.started_at_ms = record.started_at_ms;
  state_.updated_at_ms = record.updated_at_ms;
  state_.calculated_sha_available =
      (record.flags & record_flag_calculated_sha_available) != 0U;
  state_.validation_passed =
      (record.flags & record_flag_validation_passed) != 0U;
  state_.activation_intent =
      (record.flags & record_flag_activation_intent) != 0U;
  state_.activated = (record.flags & record_flag_activated) != 0U;
}

core::Result<void> UpdateManager::refresh_platform_locked() {
  const auto status = platform_.status();
  if (!status.ok()) return core::Result<void>::failure(status.error());
  const auto& value = status.value();
  if (!value.running.present() || !value.boot.present() ||
      !value.inactive.present() ||
      same_partition(value.running, value.inactive)) {
    return core::Result<void>::failure(update_error(
        "OTA partition layout is missing or does not identify an inactive slot"));
  }
  state_.running = value.running;
  state_.boot = value.boot;
  state_.inactive = value.inactive;
  state_.current = value.running_image;
  state_.running_image_state = value.running_state;
  state_.last_invalid = value.last_invalid;
  state_.rollback_available = value.rollback_available;
  return core::Result<void>::success();
}

core::Result<void> UpdateManager::validate_precondition_locked(
    OperationPrecondition precondition) const {
  if (precondition.operation_id == 0U || precondition.generation == 0U ||
      precondition.operation_id != state_.operation_id ||
      precondition.generation != state_.generation) {
    return core::Result<void>::failure(conflict_error(
        "OTA operation precondition is stale or does not own the active update"));
  }
  return core::Result<void>::success();
}

core::Result<UpdateSnapshot> UpdateManager::fail_locked(
    core::Error error,
    std::uint32_t now_ms,
    bool abort_writer) {
  error = bounded_error(std::move(error));
  if (abort_writer && writer_open_) {
    const auto ignored = platform_.abort_write();
    (void)ignored;
    writer_open_ = false;
  }
  sha256_.abort();
  state_.state = UpdateState::failed;
  state_.validation_passed = false;
  (void)state_.last_error.assign(error.message);
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) {
    return core::Result<UpdateSnapshot>::failure(
        bounded_error(saved.error()));
  }
  return core::Result<UpdateSnapshot>::failure(std::move(error));
}

core::Result<UpdateSnapshot>
UpdateManager::rollback_unreconciled_candidate_locked(
    core::Error error,
    std::uint32_t now_ms) {
  error = bounded_error(std::move(error));
  state_.target = state_.running;
  state_.candidate = state_.current;
  state_.state = UpdateState::rollback_pending;
  state_.validation_passed = false;
  state_.activation_intent = true;
  state_.activated = true;
  state_.candidate_started_at_ms = now_ms;
  (void)state_.last_error.assign(error.message);

  // Record the rollback intent when storage is still usable. A corrupt or
  // unavailable record must never prevent the bootloader rollback attempt.
  if (state_.generation == 0U) {
    const auto generation = records_.reserve_generation(0U);
    if (generation.ok() && generation.value() != 0U) {
      state_.generation = generation.value();
    }
  }
  touch_locked(now_ms);
  if (state_.generation != 0U) {
    const auto ignored = persist_locked();
    (void)ignored;
  }

  const auto rolled_back = platform_.rollback_and_reboot();
  if (!rolled_back.ok()) {
    // Keep a retryable candidate state if the platform rollback call returns
    // an error. This prevents a storage failure from trapping the manager in
    // a generic failed state that rejects rollback_candidate().
    const auto rollback_error = bounded_error(rolled_back.error());
    state_.state = UpdateState::candidate_boot;
    (void)state_.last_error.assign(rollback_error.message);
    touch_locked(now_ms);
    if (state_.generation != 0U) {
      const auto ignored = persist_locked();
      (void)ignored;
    }
    return core::Result<UpdateSnapshot>::failure(rollback_error);
  }

  // ESP-IDF reboots on successful rollback. Host doubles return, allowing the
  // terminal state and best-effort audit record to be verified.
  state_.state = UpdateState::rolled_back;
  state_.activated = false;
  touch_locked(now_ms);
  if (state_.generation != 0U) {
    const auto ignored = persist_locked();
    (void)ignored;
  }
  return core::Result<UpdateSnapshot>::failure(std::move(error));
}

bool UpdateManager::rollback_seed_recovery_required_locked() const {
  const bool recoverable_running =
      state_.running_image_state == PartitionImageState::pending_verify ||
      state_.running_image_state == PartitionImageState::new_image ||
      state_.running_image_state == PartitionImageState::valid;
  return state_.state == UpdateState::ready_to_reboot &&
      state_.generation != 0U && state_.operation_id != 0U &&
      state_.validation_passed && state_.calculated_sha_available &&
      constant_time_equal(state_.expected_sha256, state_.calculated_sha256) &&
      state_.image_size != 0U && state_.bytes_received == state_.image_size &&
      state_.activation_intent && !state_.activated &&
      recoverable_running && state_.target.present() &&
      same_partition(state_.boot, state_.running) &&
      same_partition(state_.inactive, state_.target) &&
      !same_partition(state_.running, state_.target);
}

core::Result<UpdateSnapshot>
UpdateManager::recover_rollback_seed_locked(std::uint32_t now_ms) {
  if (!rollback_seed_recovery_required_locked()) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Running OTA state does not match the rollback-seed recovery boundary"));
  }

  if (state_.running_image_state != PartitionImageState::valid) {
    const auto confirmed = platform_.confirm_running();
    if (!confirmed.ok()) {
      const auto error = bounded_error(confirmed.error());
      (void)state_.last_error.assign(error.message);
      touch_locked(now_ms);
      const auto ignored = persist_locked();
      (void)ignored;
      return core::Result<UpdateSnapshot>::failure(error);
    }
    state_.running_image_state = PartitionImageState::valid;
  }

  // This confirms only the pre-existing running image whose otadata entry was
  // being seeded. The staged target remains inactive, validated, and
  // unselected; candidate confirmation uses a separate state and health path.
  state_.activation_intent = false;
  state_.last_error = {};
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) {
    // The bootloader VALID state is authoritative, but the durable record still
    // contains activation intent. Retain the same in-memory intent and fail so
    // the owner keeps its lease and retries the metadata normalization.
    const auto error = bounded_error(saved.error());
    state_.activation_intent = true;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    return core::Result<UpdateSnapshot>::failure(error);
  }
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::recover_rollback_seed(
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  return recover_rollback_seed_locked(now_ms);
}

core::Result<UpdateSnapshot> UpdateManager::initialize_from_boot(
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  writer_open_ = false;
  state_ = {};

  const auto refreshed = refresh_platform_locked();
  if (!refreshed.ok()) {
    state_.state = UpdateState::failed;
    const auto error = bounded_error(refreshed.error());
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    return core::Result<UpdateSnapshot>::failure(error);
  }
  const bool pending_running =
      state_.running_image_state == PartitionImageState::pending_verify ||
      state_.running_image_state == PartitionImageState::new_image;

  const auto loaded = records_.load();
  if (!loaded.ok()) {
    if (pending_running) {
      return rollback_unreconciled_candidate_locked(
          loaded.error(), now_ms);
    }
    state_.state = UpdateState::failed;
    const auto error = bounded_error(loaded.error());
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    return core::Result<UpdateSnapshot>::failure(error);
  }
  const bool has_record = loaded.value().has_value();
  if (has_record) {
    if (!valid_update_record(*loaded.value())) {
      const auto error = update_error(
          "Persisted OTA record failed version or checksum validation");
      if (pending_running) {
        return rollback_unreconciled_candidate_locked(error, now_ms);
      }
      state_.state = UpdateState::failed;
      (void)state_.last_error.assign(error.message);
      touch_locked(now_ms);
      return core::Result<UpdateSnapshot>::failure(error);
    }
    apply_record_locked(*loaded.value());
  }

  if (rollback_seed_recovery_required_locked()) {
    return recover_rollback_seed_locked(now_ms);
  }

  if (pending_running) {
    const bool candidate_record_state =
        state_.state == UpdateState::ready_to_reboot ||
        state_.state == UpdateState::reboot_pending ||
        candidate_in_progress(state_.state);
    if (!has_record || state_.generation == 0U ||
        state_.operation_id == 0U || !candidate_record_state ||
        !state_.target.present() ||
        !same_partition(state_.target, state_.running) ||
        !state_.validation_passed || !state_.activation_intent ||
        !state_.activated) {
      return rollback_unreconciled_candidate_locked(
          update_error(
              "Running OTA candidate lacks a consistent durable activation record"),
          now_ms);
    }
    state_.target = state_.running;
    state_.candidate = state_.current;
    state_.state = UpdateState::candidate_boot;
    state_.activated = true;
    state_.activation_intent = true;
    state_.candidate_started_at_ms = now_ms;
    state_.last_error = {};
    touch_locked(now_ms);
    const auto saved = persist_locked();
    if (!saved.ok()) {
      return rollback_unreconciled_candidate_locked(
          saved.error(), now_ms);
    }
    return core::Result<UpdateSnapshot>::success(state_);
  }

  if (has_record && state_.state == UpdateState::ready_to_activate) {
    // Schema v1 briefly exposed this pre-activation state. Preserve the
    // validated image while normalizing it to the cancelable public state.
    state_.state = UpdateState::ready_to_reboot;
    state_.activation_intent = false;
    state_.activated = false;
    touch_locked(now_ms);
    const auto saved = persist_locked();
    if (!saved.ok()) {
      return core::Result<UpdateSnapshot>::failure(
          bounded_error(saved.error()));
    }
  }

  if (has_record &&
      (upload_in_progress(state_.state) ||
       state_.state == UpdateState::validating)) {
    return fail_locked(
        update_error("OTA upload was interrupted before image validation"),
        now_ms,
        false);
  }

  if (has_record && state_.last_invalid.has_value() &&
      state_.target.present() &&
      same_partition(*state_.last_invalid, state_.target) &&
      !same_partition(state_.running, state_.target)) {
    state_.state = UpdateState::rolled_back;
    state_.activated = false;
    state_.rollback_available = false;
    state_.last_error = {};
    touch_locked(now_ms);
    const auto saved = persist_locked();
    if (!saved.ok()) {
      return core::Result<UpdateSnapshot>::failure(saved.error());
    }
    return core::Result<UpdateSnapshot>::success(state_);
  }

  if (has_record && state_.target.present() && state_.activation_intent &&
      same_partition(state_.boot, state_.target) &&
      !same_partition(state_.running, state_.target)) {
    state_.state = state_.state == UpdateState::reboot_pending
                       ? UpdateState::reboot_pending
                       : UpdateState::ready_to_reboot;
    state_.activated = true;
  } else if (has_record && state_.target.present() &&
             same_partition(state_.running, state_.target) &&
             state_.running_image_state == PartitionImageState::valid &&
             (candidate_in_progress(state_.state) ||
              state_.state == UpdateState::rollback_pending)) {
    state_.state = UpdateState::confirmed;
    state_.activated = true;
  } else if (!has_record) {
    state_.state = UpdateState::idle;
  }

  state_.last_error = state_.state == UpdateState::failed
                          ? state_.last_error
                          : BoundedText<192U>{};
  touch_locked(now_ms);
  return core::Result<UpdateSnapshot>::success(state_);
}

UpdateSnapshot UpdateManager::snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

core::Result<UpdateSnapshot> UpdateManager::begin_upload(
    const BeginUploadRequest& request,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (request.expected_generation != state_.generation) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "OTA generation changed before the upload began"));
  }
  if (request.operation_id == 0U) {
    return core::Result<UpdateSnapshot>::failure(update_error(
        "OTA upload requires a nonzero operation ID"));
  }
  if (state_.state != UpdateState::idle && state_.state != UpdateState::failed &&
      state_.state != UpdateState::confirmed &&
      state_.state != UpdateState::rolled_back) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Another OTA lifecycle operation is already active"));
  }

  const auto refreshed = refresh_platform_locked();
  if (!refreshed.ok()) {
    return core::Result<UpdateSnapshot>::failure(refreshed.error());
  }
  if (state_.running_image_state == PartitionImageState::pending_verify ||
      state_.running_image_state == PartitionImageState::new_image) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "A candidate image must be confirmed or rolled back before another upload"));
  }
  if (request.expected_length == 0U ||
      request.expected_length > maximum_image_bytes ||
      request.expected_length > state_.inactive.size) {
    return core::Result<UpdateSnapshot>::failure(update_error(
        "Firmware image size is zero or exceeds the inactive OTA slot"));
  }

  const auto generation = records_.reserve_generation(state_.generation);
  if (!generation.ok()) {
    return core::Result<UpdateSnapshot>::failure(generation.error());
  }
  if (generation.value() == 0U || generation.value() <= state_.generation) {
    return core::Result<UpdateSnapshot>::failure(update_error(
        "OTA generation store reused or wrapped a durable generation"));
  }

  const auto revision = state_.revision;
  const auto running = state_.running;
  const auto boot = state_.boot;
  const auto inactive = state_.inactive;
  const auto current = state_.current;
  const auto running_image_state = state_.running_image_state;
  const auto last_invalid = state_.last_invalid;
  const auto rollback_available = state_.rollback_available;
  state_ = {};
  state_.revision = revision;
  state_.generation = generation.value();
  state_.operation_id = request.operation_id;
  state_.state = UpdateState::upload_receiving;
  state_.running = running;
  state_.boot = boot;
  state_.inactive = inactive;
  state_.target = inactive;
  state_.current = current;
  state_.candidate.version = request.declared_version;
  state_.candidate.git_sha = request.declared_git_sha;
  state_.image_size = request.expected_length;
  state_.expected_sha256 = request.expected_sha256;
  state_.rollback_available = rollback_available;
  state_.running_image_state = running_image_state;
  state_.last_invalid = last_invalid;
  state_.started_at_ms = now_ms;
  touch_locked(now_ms);
  next_progress_persistence_bytes_ = progress_persistence_interval_bytes;

  const auto hash_started = sha256_.begin();
  if (!hash_started.ok()) {
    return fail_locked(hash_started.error(), now_ms, false);
  }
  const auto write_started =
      platform_.begin_write(state_.target, state_.image_size);
  if (!write_started.ok()) {
    sha256_.abort();
    return fail_locked(write_started.error(), now_ms, false);
  }
  writer_open_ = true;
  const auto saved = persist_locked();
  if (!saved.ok()) return fail_locked(saved.error(), now_ms, true);
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::write_chunk(
    OperationPrecondition precondition,
    core::ByteView chunk,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto owned = validate_precondition_locked(precondition);
  if (!owned.ok()) {
    return core::Result<UpdateSnapshot>::failure(owned.error());
  }
  if (!upload_in_progress(state_.state) || !writer_open_) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "OTA upload is not accepting firmware chunks"));
  }
  if (chunk.data == nullptr || chunk.empty() ||
      chunk.size > maximum_upload_chunk_bytes ||
      chunk.size > state_.image_size - state_.bytes_received) {
    return fail_locked(
        update_error("Firmware chunk is empty, oversized, or exceeds declared length"),
        now_ms,
        true);
  }

  const auto written = platform_.write(chunk);
  if (!written.ok()) return fail_locked(written.error(), now_ms, true);
  const auto hashed = sha256_.update(chunk);
  if (!hashed.ok()) return fail_locked(hashed.error(), now_ms, true);

  state_.bytes_received += static_cast<std::uint32_t>(chunk.size);
  state_.state = UpdateState::writing;
  touch_locked(now_ms);
  if (state_.bytes_received >= next_progress_persistence_bytes_ ||
      state_.bytes_received == state_.image_size) {
    const auto saved = persist_locked();
    if (!saved.ok()) return fail_locked(saved.error(), now_ms, true);
    const auto completed_intervals =
        state_.bytes_received / progress_persistence_interval_bytes;
    if (completed_intervals >=
        std::numeric_limits<std::uint32_t>::max() /
            progress_persistence_interval_bytes) {
      next_progress_persistence_bytes_ = state_.image_size;
    } else {
      next_progress_persistence_bytes_ =
          (completed_intervals + 1U) * progress_persistence_interval_bytes;
    }
  }
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::finish_upload(
    OperationPrecondition precondition,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto owned = validate_precondition_locked(precondition);
  if (!owned.ok()) {
    return core::Result<UpdateSnapshot>::failure(owned.error());
  }
  if (!upload_in_progress(state_.state) || !writer_open_) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "OTA upload is not ready to finish"));
  }
  if (state_.bytes_received != state_.image_size) {
    return fail_locked(
        update_error("Firmware upload ended before the declared byte count"),
        now_ms,
        true);
  }

  state_.state = UpdateState::validating;
  touch_locked(now_ms);
  const auto validating_saved = persist_locked();
  if (!validating_saved.ok()) {
    return fail_locked(validating_saved.error(), now_ms, true);
  }

  const auto digest = sha256_.finish();
  if (!digest.ok()) return fail_locked(digest.error(), now_ms, true);
  state_.calculated_sha256 = digest.value();
  state_.calculated_sha_available = true;
  if (!constant_time_equal(state_.expected_sha256, state_.calculated_sha256)) {
    return fail_locked(
        update_error("Firmware SHA-256 does not match the declared digest"),
        now_ms,
        true);
  }

  const auto finished = platform_.finish_write();
  writer_open_ = false;
  sha256_.abort();
  if (!finished.ok()) return fail_locked(finished.error(), now_ms, false);

  const auto validation =
      platform_.validate_staged_image(state_.target, state_.image_size);
  if (!validation.ok()) {
    return fail_locked(validation.error(), now_ms, false);
  }
  if (!validation.value().structure_valid) {
    return fail_locked(
        update_error("Firmware image structure validation failed"),
        now_ms,
        false);
  }
  if (!validation.value().target_compatible) {
    return fail_locked(
        update_error("Firmware image target is incompatible with this station"),
        now_ms,
        false);
  }
  if (!validation.value().project_compatible) {
    return fail_locked(
        update_error("Firmware image project identity is incompatible"),
        now_ms,
        false);
  }
  if (validation.value().image_size != state_.image_size) {
    return fail_locked(
        update_error("Validated firmware length differs from the upload length"),
        now_ms,
        false);
  }

  state_.candidate = validation.value().image;
  state_.validation_passed = true;
  // Validation is a durable, externally visible staging boundary. Do not
  // change the boot partition until an explicit activation request arrives.
  state_.state = UpdateState::ready_to_reboot;
  state_.last_error = {};
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) return fail_locked(saved.error(), now_ms, false);
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::activate(
    OperationPrecondition precondition,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto owned = validate_precondition_locked(precondition);
  if (!owned.ok()) {
    return core::Result<UpdateSnapshot>::failure(owned.error());
  }
  if (state_.state != UpdateState::ready_to_reboot ||
      !state_.validation_passed || !state_.target.present()) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "OTA image has not completed validation and cannot be activated"));
  }

  const auto target = state_.target;
  const auto refreshed = refresh_platform_locked();
  if (!refreshed.ok()) {
    return core::Result<UpdateSnapshot>::failure(refreshed.error());
  }
  if (!state_.activated && state_.activation_intent &&
      rollback_seed_recovery_required_locked()) {
    const auto recovered = recover_rollback_seed_locked(now_ms);
    if (!recovered.ok()) return recovered;
  }
  if (state_.activated) {
    if (!state_.activation_intent ||
        !same_partition(state_.boot, target) ||
        same_partition(state_.running, target)) {
      return core::Result<UpdateSnapshot>::failure(conflict_error(
          "Activated OTA metadata no longer matches the boot partition"));
    }
    return core::Result<UpdateSnapshot>::success(state_);
  }
  if (same_partition(state_.running, target) ||
      !same_partition(state_.inactive, target)) {
    return fail_locked(
        update_error("Validated OTA target is no longer the inactive slot"),
        now_ms,
        false);
  }

  state_.target = target;
  state_.activation_intent = true;
  touch_locked(now_ms);
  const auto intent_saved = persist_locked();
  if (!intent_saved.ok()) {
    const auto error = bounded_error(intent_saved.error());
    state_.activation_intent = false;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    return core::Result<UpdateSnapshot>::failure(error);
  }

  const auto activated = platform_.activate(target);
  if (!activated.ok()) {
    const auto error = bounded_error(activated.error());
    const auto reconciled = refresh_platform_locked();
    state_.target = target;
    if (reconciled.ok() && same_partition(state_.boot, target) &&
        !same_partition(state_.running, target)) {
      // Boot selection is authoritative even if the platform reported an
      // ambiguous post-write error. Preserve the durable intent and let the
      // exact operation retry persist reboot_pending.
      state_.activation_intent = true;
      state_.activated = true;
      (void)state_.last_error.assign(error.message);
      touch_locked(now_ms);
      const auto ignored = persist_locked();
      (void)ignored;
      return core::Result<UpdateSnapshot>::success(state_);
    }

    const bool proven_safe_unselected = reconciled.ok() &&
        state_.running_image_state == PartitionImageState::valid &&
        same_partition(state_.boot, state_.running) &&
        same_partition(state_.inactive, target);
    // A NEW/PENDING running slot may be the interrupted rollback-seed write.
    // Retain intent so cancel cannot erase its only durable recovery evidence.
    state_.activation_intent = !proven_safe_unselected;
    state_.activated = false;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    const auto ignored = persist_locked();
    (void)ignored;
    return core::Result<UpdateSnapshot>::failure(error);
  }
  state_.boot = target;
  state_.activated = true;
  state_.state = UpdateState::ready_to_reboot;
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) {
    // The intent record was durable before the boot partition changed.
    // Continue so the owner can persist reboot_pending and reboot; boot
    // reconciliation also recognizes this exact cut point.
    const auto error = bounded_error(saved.error());
    (void)state_.last_error.assign(error.message);
  }
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::mark_reboot_pending(
    OperationPrecondition precondition,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto owned = validate_precondition_locked(precondition);
  if (!owned.ok()) {
    return core::Result<UpdateSnapshot>::failure(owned.error());
  }
  if (state_.state != UpdateState::ready_to_reboot || !state_.activated) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "OTA reboot is not allowed before inactive-slot activation"));
  }
  state_.state = UpdateState::reboot_pending;
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) {
    const auto error = bounded_error(saved.error());
    state_.state = UpdateState::ready_to_reboot;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    return core::Result<UpdateSnapshot>::failure(error);
  }
  state_.last_error = {};
  return core::Result<UpdateSnapshot>::success(state_);
}

void UpdateManager::clear_operation_locked(std::uint32_t now_ms) {
  const auto revision = state_.revision;
  const auto generation = state_.generation;
  const auto running = state_.running;
  const auto boot = state_.boot;
  const auto inactive = state_.inactive;
  const auto current = state_.current;
  const auto running_image_state = state_.running_image_state;
  const auto last_invalid = state_.last_invalid;
  const auto rollback_available = state_.rollback_available;
  state_ = {};
  state_.revision = revision;
  state_.generation = generation;
  state_.running = running;
  state_.boot = boot;
  state_.inactive = inactive;
  state_.current = current;
  state_.running_image_state = running_image_state;
  state_.last_invalid = last_invalid;
  state_.rollback_available = rollback_available;
  state_.state = UpdateState::idle;
  touch_locked(now_ms);
}

core::Result<UpdateSnapshot> UpdateManager::cancel(
    OperationPrecondition precondition,
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto owned = validate_precondition_locked(precondition);
  if (!owned.ok()) {
    return core::Result<UpdateSnapshot>::failure(owned.error());
  }
  const bool validated_but_not_activated =
      state_.state == UpdateState::ready_to_reboot && !state_.activated &&
      !state_.activation_intent;
  if (!upload_in_progress(state_.state) && !validated_but_not_activated) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "OTA can only be canceled before activation"));
  }
  if (writer_open_) {
    const auto aborted = platform_.abort_write();
    writer_open_ = false;
    sha256_.abort();
    if (!aborted.ok()) return fail_locked(aborted.error(), now_ms, false);
  } else {
    sha256_.abort();
  }
  clear_operation_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) return fail_locked(saved.error(), now_ms, false);
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::handle_candidate_health(
    CandidateHealthDecision decision,
    std::uint32_t now_ms) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!candidate_in_progress(state_.state)) {
      return core::Result<UpdateSnapshot>::failure(conflict_error(
          "Running firmware is not awaiting candidate validation"));
    }
    if (decision == CandidateHealthDecision::factory_reset_recovery) {
      return core::Result<UpdateSnapshot>::success(state_);
    }
    if (decision == CandidateHealthDecision::stabilizing) {
      if (state_.state == UpdateState::candidate_boot) {
        state_.state = UpdateState::validating_candidate;
        touch_locked(now_ms);
        const auto saved = persist_locked();
        if (!saved.ok()) {
          return core::Result<UpdateSnapshot>::failure(saved.error());
        }
      }
      return core::Result<UpdateSnapshot>::success(state_);
    }
  }
  return decision == CandidateHealthDecision::healthy
             ? confirm_candidate(now_ms)
             : rollback_candidate(now_ms);
}

core::Result<UpdateSnapshot> UpdateManager::confirm_candidate(
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!candidate_in_progress(state_.state)) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Running firmware is not awaiting candidate confirmation"));
  }
  if (!state_.validation_passed || !state_.activation_intent ||
      !state_.activated || !state_.target.present()) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Candidate lacks durable validation and activation evidence"));
  }
  if (static_cast<std::uint32_t>(
          now_ms - state_.candidate_started_at_ms) <
      candidate_confirmation_window_ms) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Candidate health confirmation window has not elapsed"));
  }

  const auto target = state_.target;
  const auto refreshed = refresh_platform_locked();
  if (!refreshed.ok()) {
    return core::Result<UpdateSnapshot>::failure(refreshed.error());
  }
  state_.target = target;
  if (same_partition(state_.running, state_.target) &&
      state_.running_image_state == PartitionImageState::valid) {
    // Confirmation may have physically succeeded even if its API call reported
    // an ambiguous error. The exact durable candidate identity plus the
    // bootloader's VALID state is authoritative on retry.
    state_.state = UpdateState::confirmed;
    state_.rollback_available = true;
    state_.last_error = {};
    touch_locked(now_ms);
    const auto ignored = persist_locked();
    (void)ignored;
    return core::Result<UpdateSnapshot>::success(state_);
  }
  if (!same_partition(state_.running, state_.target) ||
      (state_.running_image_state != PartitionImageState::pending_verify &&
       state_.running_image_state != PartitionImageState::new_image)) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Boot partition state changed before candidate confirmation"));
  }

  const auto confirmed = platform_.confirm_running();
  if (!confirmed.ok()) {
    const auto error = bounded_error(confirmed.error());
    state_.state = UpdateState::validating_candidate;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    const auto ignored = persist_locked();
    (void)ignored;
    return core::Result<UpdateSnapshot>::failure(error);
  }
  state_.running_image_state = PartitionImageState::valid;
  state_.state = UpdateState::confirmed;
  state_.rollback_available = true;
  state_.last_error = {};
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) {
    // The bootloader's VALID state is authoritative. Report success so the
    // lifecycle lease can be released; a later boot reconciles the stale
    // candidate record against the valid running partition.
    const auto error = bounded_error(saved.error());
    (void)state_.last_error.assign(error.message);
  }
  return core::Result<UpdateSnapshot>::success(state_);
}

core::Result<UpdateSnapshot> UpdateManager::rollback_candidate(
    std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!candidate_in_progress(state_.state)) {
    return core::Result<UpdateSnapshot>::failure(conflict_error(
        "Running firmware is not an unconfirmed candidate"));
  }

  const auto target = state_.target;
  const auto refreshed = refresh_platform_locked();
  if (!refreshed.ok()) {
    return core::Result<UpdateSnapshot>::failure(refreshed.error());
  }
  state_.target = target.present() ? target : state_.running;
  if (!same_partition(state_.running, state_.target) ||
      !state_.rollback_available) {
    const auto error = bounded_error(update_error(
        "Bootloader reports that candidate rollback is unavailable"));
    // The running image is still unconfirmed. Never collapse this into the
    // generic failed terminal state, which would reject every later rollback
    // retry while leaving the bootloader candidate pending.
    state_.state = UpdateState::validating_candidate;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    const auto ignored = persist_locked();
    (void)ignored;
    return core::Result<UpdateSnapshot>::failure(error);
  }

  state_.state = UpdateState::rollback_pending;
  touch_locked(now_ms);
  const auto pending_saved = persist_locked();
  std::optional<core::Error> audit_error;
  if (!pending_saved.ok()) {
    audit_error = bounded_error(pending_saved.error());
    (void)state_.last_error.assign(audit_error->message);
    touch_locked(now_ms);
  }

  // Once the platform has proven that the running image is an unconfirmed
  // candidate and rollback is available, failure to update the audit record
  // must never prevent the bootloader rollback. The bootloader state is the
  // fail-safe authority; persistence remains best-effort at this cut point.
  const auto rolled_back = platform_.rollback_and_reboot();
  if (!rolled_back.ok()) {
    const auto error = bounded_error(rolled_back.error());
    state_.state = UpdateState::validating_candidate;
    (void)state_.last_error.assign(error.message);
    touch_locked(now_ms);
    const auto ignored = persist_locked();
    (void)ignored;
    return core::Result<UpdateSnapshot>::failure(error);
  }
  // The ESP32 implementation reboots on success and normally never reaches
  // this line. Host fakes return so the terminal transition is testable.
  state_.state = UpdateState::rolled_back;
  state_.activated = false;
  if (!audit_error.has_value()) state_.last_error = {};
  touch_locked(now_ms);
  const auto saved = persist_locked();
  if (!saved.ok()) {
    const auto error = bounded_error(saved.error());
    (void)state_.last_error.assign(error.message);
  }
  return core::Result<UpdateSnapshot>::success(state_);
}

}  // namespace opentag::ota
