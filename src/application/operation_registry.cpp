#include "application/operation_registry.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace opentag::application {

const char* to_string(OperationKind kind) {
  switch (kind) {
    case OperationKind::configuration: return "configuration";
    case OperationKind::scale_tare: return "scale_tare";
    case OperationKind::scale_calibration: return "scale_calibration";
    case OperationKind::backend_probe: return "backend_probe";
    case OperationKind::toolhead_assignment: return "toolhead_assignment";
    case OperationKind::toolhead_unassignment: return "toolhead_unassignment";
    case OperationKind::nfc_read: return "nfc_read";
    case OperationKind::firmware_upload: return "firmware_upload";
    case OperationKind::firmware_reboot: return "firmware_reboot";
    case OperationKind::firmware_cancel: return "firmware_cancel";
    case OperationKind::reboot: return "reboot";
    case OperationKind::factory_reset: return "factory_reset";
  }
  return "unknown";
}

const char* to_string(OperationState state) {
  switch (state) {
    case OperationState::queued: return "queued";
    case OperationState::running: return "running";
    case OperationState::succeeded: return "succeeded";
    case OperationState::failed: return "failed";
    case OperationState::confirmation_required:
      return "confirmation_required";
  }
  return "unknown";
}

std::string OperationRegistry::bounded(std::string value) {
  if (value.size() > maximum_message_bytes) {
    value.resize(maximum_message_bytes);
  }
  for (auto& character : value) {
    if (static_cast<unsigned char>(character) < 0x20U && character != '\t') {
      character = ' ';
    }
  }
  return value;
}

std::uint64_t OperationRegistry::begin(
    OperationKind kind,
    std::uint32_t now_ms,
    std::string message) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto id = next_id_++;
  auto& record = records_[next_slot_];
  record = {};
  record.id = id;
  record.kind = kind;
  record.state = OperationState::queued;
  record.created_at_ms = now_ms;
  record.updated_at_ms = now_ms;
  record.message = bounded(std::move(message));
  next_slot_ = (next_slot_ + 1U) % capacity;
  count_ = std::min(count_ + 1U, capacity);
  ++revision_;
  return id;
}

void OperationRegistry::reserve_ids_above(std::uint64_t highest_used) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (highest_used >= next_id_ &&
      highest_used != std::numeric_limits<std::uint64_t>::max()) {
    next_id_ = highest_used + 1U;
  }
}

void OperationRegistry::update(
    std::uint64_t id,
    OperationState state,
    std::uint32_t now_ms,
    std::string message,
    std::optional<core::Error> error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& record : records_) {
    if (record.id != id) continue;
    record.state = state;
    record.updated_at_ms = now_ms;
    record.message = bounded(std::move(message));
    if (error.has_value()) {
      error->message = bounded(std::move(error->message));
    }
    record.error = std::move(error);
    ++revision_;
    return;
  }
}

void OperationRegistry::mark_running(
    std::uint64_t id,
    std::uint32_t now_ms,
    std::string message) {
  update(id, OperationState::running, now_ms, std::move(message));
}

void OperationRegistry::succeed(
    std::uint64_t id,
    std::uint32_t now_ms,
    std::string message) {
  update(id, OperationState::succeeded, now_ms, std::move(message));
}

void OperationRegistry::fail(
    std::uint64_t id,
    std::uint32_t now_ms,
    core::Error error) {
  const auto message = error.message;
  update(
      id,
      OperationState::failed,
      now_ms,
      message,
      std::move(error));
}

void OperationRegistry::require_confirmation(
    std::uint64_t id,
    std::uint32_t now_ms,
    std::string message) {
  update(
      id,
      OperationState::confirmation_required,
      now_ms,
      std::move(message));
}

std::optional<OperationRecord> OperationRegistry::get(
    std::uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& record : records_) {
    if (record.id == id) return record;
  }
  return std::nullopt;
}

std::vector<OperationRecord> OperationRegistry::snapshot(
    std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  limit = std::min({limit, count_, capacity});
  std::vector<OperationRecord> result;
  result.reserve(limit);
  for (std::size_t offset = 0U; offset < limit; ++offset) {
    const auto index =
        (next_slot_ + capacity - 1U - offset) % capacity;
    if (records_[index].id != 0U) result.push_back(records_[index]);
  }
  return result;
}

std::uint64_t OperationRegistry::revision() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return revision_;
}

}  // namespace opentag::application
