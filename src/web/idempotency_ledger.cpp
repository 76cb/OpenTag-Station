#include "web/idempotency_ledger.hpp"

#include <algorithm>

namespace opentag::web {

bool IdempotencyLedger::expired(
    std::uint32_t now_ms,
    std::uint32_t accepted_at_ms) {
  static_assert(ttl_ms < (std::uint32_t{1U} << 31U));
  return static_cast<std::uint32_t>(now_ms - accepted_at_ms) >= ttl_ms;
}

bool IdempotencyLedger::matches_key(
    const Entry& entry,
    std::string_view key) {
  return entry.key_size == key.size() &&
         std::equal(key.begin(), key.end(), entry.key.begin());
}

void IdempotencyLedger::expire_entries(std::uint32_t now_ms) {
  for (auto& entry : entries_) {
    if (entry.occupied && expired(now_ms, entry.accepted_at_ms)) {
      entry = {};
    }
  }
}

IdempotencyLookupResult IdempotencyLedger::lookup(
    std::string_view key,
    std::uint64_t payload_digest,
    std::uint32_t now_ms) {
  if (key.size() > maximum_key_bytes) return {};

  const std::lock_guard<std::mutex> lock(mutex_);
  expire_entries(now_ms);

  // Search newest-to-oldest so a later insert wins deterministically even if
  // a caller inserts the same key more than once without first looking it up.
  for (std::size_t offset = 0U; offset < capacity; ++offset) {
    const auto index = (next_slot_ + capacity - 1U - offset) % capacity;
    const auto& entry = entries_[index];
    if (!entry.occupied || !matches_key(entry, key)) continue;
    return {
        entry.payload_digest == payload_digest
            ? IdempotencyLookupStatus::same
            : IdempotencyLookupStatus::conflict,
        entry.operation_id,
        entry.accepted_at_ms,
    };
  }
  return {};
}

bool IdempotencyLedger::insert(
    std::string_view key,
    std::uint64_t payload_digest,
    std::uint64_t operation_id,
    std::uint32_t accepted_at_ms) {
  if (key.size() > maximum_key_bytes) return false;

  const std::lock_guard<std::mutex> lock(mutex_);
  auto& entry = entries_[next_slot_];
  entry = {};
  entry.occupied = true;
  std::copy(key.begin(), key.end(), entry.key.begin());
  entry.key[key.size()] = '\0';
  entry.key_size = key.size();
  entry.payload_digest = payload_digest;
  entry.operation_id = operation_id;
  entry.accepted_at_ms = accepted_at_ms;
  next_slot_ = (next_slot_ + 1U) % capacity;
  return true;
}

std::size_t IdempotencyLedger::size(std::uint32_t now_ms) {
  const std::lock_guard<std::mutex> lock(mutex_);
  expire_entries(now_ms);
  return static_cast<std::size_t>(std::count_if(
      entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.occupied;
      }));
}

}  // namespace opentag::web
