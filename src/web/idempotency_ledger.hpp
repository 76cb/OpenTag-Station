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

  // Returns false for an oversized key, a conflicting duplicate key, or when
  // every slot holds a live entry. An exact duplicate is accepted without
  // changing the original operation or extending its retention window.
  [[nodiscard]] bool insert(
      std::string_view key,
      std::uint64_t payload_digest,
      std::uint64_t operation_id,
      std::uint32_t accepted_at_ms);

  [[nodiscard]] std::size_t size(std::uint32_t now_ms);

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
