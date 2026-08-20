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
  static constexpr std::size_t capacity = 16U;
  static constexpr std::size_t maximum_key_bytes = 64U;
  static constexpr std::uint32_t ttl_ms = 60000U;

  [[nodiscard]] IdempotencyLookupResult lookup(
      std::string_view key,
      std::uint64_t payload_digest,
      std::uint32_t now_ms);

  // Returns false instead of truncating when the caller violates the key-size
  // contract. Each successful insert advances the deterministic ring cursor.
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
