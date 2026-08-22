#include <unity.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "web/idempotency_ledger.hpp"

namespace {
using opentag::web::IdempotencyLedger;
using opentag::web::IdempotencyLookupStatus;
}  // namespace

void setUp() {}
void tearDown() {}

void test_same_payload_reuses_operation_and_different_payload_conflicts() {
  IdempotencyLedger ledger;
  TEST_ASSERT_FALSE(ledger.insert("", 0x1234U, 42U, 100U));
  TEST_ASSERT_FALSE(ledger.insert("request-0", 0x1234U, 0U, 100U));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(ledger.lookup("", 0x1234U, 100U).status));
  TEST_ASSERT_TRUE(ledger.insert("request-1", 0x1234U, 42U, 100U));

  const auto same = ledger.lookup("request-1", 0x1234U, 110U);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(same.status));
  TEST_ASSERT_EQUAL_UINT64(42U, same.operation_id);
  TEST_ASSERT_EQUAL_UINT32(100U, same.accepted_at_ms);

  const auto conflict = ledger.lookup("request-1", 0x5678U, 110U);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::conflict),
      static_cast<int>(conflict.status));
  TEST_ASSERT_EQUAL_UINT64(42U, conflict.operation_id);
}

void test_ttl_expiry_is_wrap_safe() {
  IdempotencyLedger ledger;
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(
      10U * 60U * 1000U, IdempotencyLedger::ttl_ms);
  constexpr std::uint32_t accepted =
      std::numeric_limits<std::uint32_t>::max() - 100U;
  constexpr std::uint32_t before_expiry =
      static_cast<std::uint32_t>(
          accepted + IdempotencyLedger::ttl_ms - 1U);
  constexpr std::uint32_t at_expiry =
      static_cast<std::uint32_t>(accepted + IdempotencyLedger::ttl_ms);
  TEST_ASSERT_TRUE(ledger.insert("wrap", 7U, 9U, accepted));

  const auto before = ledger.lookup("wrap", 7U, before_expiry);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(before.status));

  const auto expired = ledger.lookup("wrap", 7U, at_expiry);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(expired.status));
  TEST_ASSERT_EQUAL_UINT(0U, ledger.size(at_expiry));
}

void test_full_capacity_never_overwrites_live_entries_and_reuses_expired_slot() {
  IdempotencyLedger ledger;
  for (std::size_t index = 0U; index < IdempotencyLedger::capacity; ++index) {
    TEST_ASSERT_TRUE(ledger.has_capacity(static_cast<std::uint32_t>(index)));
    TEST_ASSERT_TRUE(ledger.insert(
        "key-" + std::to_string(index),
        index + 100U,
        index + 1U,
        static_cast<std::uint32_t>(index)));
  }

  constexpr std::uint32_t before_first_expiry =
      IdempotencyLedger::ttl_ms - 1U;
  TEST_ASSERT_FALSE(ledger.has_capacity(before_first_expiry));
  TEST_ASSERT_FALSE(
      ledger.insert("overflow", 999U, 999U, before_first_expiry));
  TEST_ASSERT_EQUAL_UINT(
      IdempotencyLedger::capacity, ledger.size(before_first_expiry));
  for (std::size_t index = 0U; index < IdempotencyLedger::capacity; ++index) {
    const auto retained = ledger.lookup(
        "key-" + std::to_string(index),
        index + 100U,
        before_first_expiry);
    TEST_ASSERT_EQUAL(
        static_cast<int>(IdempotencyLookupStatus::same),
        static_cast<int>(retained.status));
    TEST_ASSERT_EQUAL_UINT64(index + 1U, retained.operation_id);
  }

  constexpr std::uint32_t first_expiry = IdempotencyLedger::ttl_ms;
  TEST_ASSERT_TRUE(ledger.has_capacity(first_expiry));
  TEST_ASSERT_TRUE(
      ledger.insert("replacement", 5000U, 6000U, first_expiry));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(
          ledger.lookup("key-0", 100U, first_expiry).status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(
          ledger.lookup("key-1", 101U, first_expiry).status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(
          ledger.lookup("replacement", 5000U, first_expiry).status));
  TEST_ASSERT_EQUAL_UINT(
      IdempotencyLedger::capacity, ledger.size(first_expiry));
}

void test_same_key_inserts_preserve_one_coherent_original_entry() {
  IdempotencyLedger ledger;
  TEST_ASSERT_TRUE(ledger.insert("one-key", 11U, 21U, 100U));
  TEST_ASSERT_TRUE(ledger.insert("one-key", 11U, 21U, 200U));
  TEST_ASSERT_FALSE(ledger.insert("one-key", 12U, 21U, 300U));
  TEST_ASSERT_FALSE(ledger.insert("one-key", 11U, 22U, 300U));

  const auto original = ledger.lookup("one-key", 11U, 400U);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(original.status));
  TEST_ASSERT_EQUAL_UINT64(21U, original.operation_id);
  TEST_ASSERT_EQUAL_UINT32(100U, original.accepted_at_ms);
  TEST_ASSERT_EQUAL_UINT(1U, ledger.size(400U));
}

void test_capacity_churn_stress_remains_bounded_across_clock_wrap() {
  IdempotencyLedger ledger;
  std::uint32_t now =
      std::numeric_limits<std::uint32_t>::max() -
      IdempotencyLedger::ttl_ms / 2U;
  for (std::uint64_t generation = 1U; generation <= 200U; ++generation) {
    for (std::size_t slot = 0U; slot < IdempotencyLedger::capacity; ++slot) {
      TEST_ASSERT_TRUE(ledger.has_capacity(now));
      TEST_ASSERT_TRUE(ledger.insert(
          "stress-" + std::to_string(slot),
          generation * 1000U + slot,
          generation * IdempotencyLedger::capacity + slot,
          now));
    }
    TEST_ASSERT_FALSE(ledger.has_capacity(now));
    TEST_ASSERT_FALSE(ledger.insert("stress-overflow", 1U, 1U, now));
    TEST_ASSERT_EQUAL_UINT(IdempotencyLedger::capacity, ledger.size(now));
    now = static_cast<std::uint32_t>(now + IdempotencyLedger::ttl_ms);
    TEST_ASSERT_EQUAL_UINT(0U, ledger.size(now));
  }
}

void test_concurrent_lookup_and_insert_remain_coherent() {
  IdempotencyLedger ledger;
  std::atomic<bool> start{false};
  std::atomic<bool> coherent{true};
  std::vector<std::thread> threads;

  for (std::uint64_t worker = 1U; worker <= 3U; ++worker) {
    threads.emplace_back([&ledger, &start, worker]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (std::uint64_t index = 1U; index <= 500U; ++index) {
        (void)ledger.insert(
            "shared", 0xA5A5U, worker * 1000U + index, 50U);
      }
    });
  }
  for (std::size_t reader = 0U; reader < 2U; ++reader) {
    threads.emplace_back([&ledger, &start, &coherent]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (std::size_t index = 0U; index < 1000U; ++index) {
        const auto result = ledger.lookup("shared", 0xA5A5U, 100U);
        if (result.status == IdempotencyLookupStatus::conflict ||
            (result.status == IdempotencyLookupStatus::same &&
             result.operation_id == 0U)) {
          coherent.store(false, std::memory_order_relaxed);
        }
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& thread : threads) thread.join();

  TEST_ASSERT_TRUE(coherent.load(std::memory_order_relaxed));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(ledger.lookup("shared", 0xA5A5U, 100U).status));
  TEST_ASSERT_EQUAL_UINT(1U, ledger.size(100U));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_same_payload_reuses_operation_and_different_payload_conflicts);
  RUN_TEST(test_ttl_expiry_is_wrap_safe);
  RUN_TEST(
      test_full_capacity_never_overwrites_live_entries_and_reuses_expired_slot);
  RUN_TEST(test_same_key_inserts_preserve_one_coherent_original_entry);
  RUN_TEST(
      test_capacity_churn_stress_remains_bounded_across_clock_wrap);
  RUN_TEST(test_concurrent_lookup_and_insert_remain_coherent);
  return UNITY_END();
}
