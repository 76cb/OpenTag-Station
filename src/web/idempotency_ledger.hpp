#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace opentag::web {

enum class IdempotencyLookupStatus : std::uint8_t {
  miss,
  same,
  conflict,
};

struct IdempotencyLookupResult {
  IdempotencyLookupStatus status{IdempotencyLookupStatus::miss};
  std::uint64_t operation_id{0U};
  std::uint32_t accepted_at_ms{0U};
};

enum class IdempotencyAdmission : std::uint8_t {
  // Start the operation, then insert() its key.
  proceed,
  // Start the operation but do not insert the key (read-only request).
  proceed_unretained,
  // Exact duplicate of a retained key: return operation_id, start nothing.
  replay,
  // Retained key reused with a different payload.
  conflict,
  // Every slot belongs to a live (queued/running) operation.
  full,
};

struct IdempotencyAdmissionResult {
  IdempotencyAdmission decision{IdempotencyAdmission::proceed};
  std::uint64_t operation_id{0U};
  std::uint32_t accepted_at_ms{0U};
};

class IdempotencyLedger final {
 public:
  static constexpr std::size_t capacity = 32U;
  static constexpr std::size_t maximum_key_bytes = 64U;
  static constexpr std::uint32_t ttl_ms = 10U * 60U * 1000U;

  [[nodiscard]] IdempotencyLookupResult lookup(
      std::string_view key,
      std::uint64_t payload_digest,
      std::uint32_t now_ms);

  // Call after a miss and before starting an operation. The application
  // context serializes this preflight and insert under its transaction mutex,
  // preventing capacity from changing while side effects are created.
  [[nodiscard]] bool has_capacity(std::uint32_t now_ms);

  // Like has_capacity(), but when every slot is occupied it evicts the oldest
  // entry whose operation has finished (is_finished(operation_id) == true),
  // so a busy browser session is never refused because of completed work.
  // Returns false only while all retained entries belong to live operations.
  // is_finished is called with the ledger mutex held; it must not call back
  // into the ledger.
  template <typename IsFinished>
  [[nodiscard]] bool reserve_capacity(
      std::uint32_t now_ms,
      IsFinished&& is_finished) {
    const std::lock_guard<std::mutex> lock(mutex_);
    expire_entries(now_ms);
    std::size_t oldest = capacity;
    std::uint32_t oldest_age = 0U;
    for (std::size_t index = 0U; index < capacity; ++index) {
      const auto& entry = entries_[index];
      if (!entry.occupied) return true;
      const auto age =
          static_cast<std::uint32_t>(now_ms - entry.accepted_at_ms);
      if ((oldest == capacity || age > oldest_age) &&
          is_finished(entry.operation_id)) {
        oldest = index;
        oldest_age = age;
      }
    }
    if (oldest == capacity) return false;
    entries_[oldest] = {};
    return true;
  }

  // Returns false for an oversized key, a conflicting duplicate key, or when
  // every slot holds a live entry. An exact duplicate is accepted without
  // changing the original operation or extending its retention window.
  [[nodiscard]] bool insert(
      std::string_view key,
      std::uint64_t payload_digest,
      std::uint64_t operation_id,
      std::uint32_t accepted_at_ms);

  [[nodiscard]] std::size_t size(std::uint32_t now_ms);

  // One admission decision for a mutation request. An existing retained key
  // is honoured for every request (replay or conflict). A read-only request
  // (retain == false) never takes a slot. Otherwise room is made with
  // reserve_capacity(). Callers serialize admit() and the following insert().
  template <typename IsFinished>
  [[nodiscard]] IdempotencyAdmissionResult admit(
      std::string_view key,
      std::uint64_t payload_digest,
      std::uint32_t now_ms,
      bool retain,
      IsFinished&& is_finished) {
    const auto existing = lookup(key, payload_digest, now_ms);
    if (existing.status == IdempotencyLookupStatus::same) {
      return {IdempotencyAdmission::replay, existing.operation_id,
              existing.accepted_at_ms};
    }
    if (existing.status == IdempotencyLookupStatus::conflict) {
      return {IdempotencyAdmission::conflict, existing.operation_id,
              existing.accepted_at_ms};
    }
    if (!retain) return {IdempotencyAdmission::proceed_unretained};
    if (!reserve_capacity(now_ms, is_finished)) {
      return {IdempotencyAdmission::full};
    }
    return {IdempotencyAdmission::proceed};
  }

 private:
  struct Entry {
    bool occupied{false};
    std::array<char, maximum_key_bytes + 1U> key{};
    std::size_t key_size{0U};
    std::uint64_t payload_digest{0U};
    std::uint64_t operation_id{0U};
    std::uint32_t accepted_at_ms{0U};
  };

  [[nodiscard]] static bool expired(
      std::uint32_t now_ms,
      std::uint32_t accepted_at_ms);
  [[nodiscard]] static bool matches_key(
      const Entry& entry,
      std::string_view key);
  void expire_entries(std::uint32_t now_ms);

  std::mutex mutex_;
  std::array<Entry, capacity> entries_{};
  std::size_t next_slot_{0U};
};

}  // namespace opentag::web
