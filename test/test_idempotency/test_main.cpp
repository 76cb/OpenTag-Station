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

void test_capacity_uses_deterministic_ring_overwrite() {
  IdempotencyLedger ledger;
  for (std::size_t index = 0U; index < IdempotencyLedger::capacity + 2U;
       ++index) {
    TEST_ASSERT_TRUE(ledger.insert(
        "key-" + std::to_string(index),
        index + 100U,
        index + 1U,
        static_cast<std::uint32_t>(index)));
  }

  TEST_ASSERT_EQUAL_UINT(IdempotencyLedger::capacity, ledger.size(100U));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(ledger.lookup("key-0", 100U, 100U).status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(ledger.lookup("key-1", 101U, 100U).status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(ledger.lookup("key-2", 102U, 100U).status));
  TEST_ASSERT_EQUAL_UINT64(
      IdempotencyLedger::capacity + 2U,
      ledger.lookup("key-17", 117U, 100U).operation_id);
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
  TEST_ASSERT_EQUAL_UINT(IdempotencyLedger::capacity, ledger.size(100U));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_same_payload_reuses_operation_and_different_payload_conflicts);
  RUN_TEST(test_ttl_expiry_is_wrap_safe);
  RUN_TEST(test_capacity_uses_deterministic_ring_overwrite);
  RUN_TEST(test_concurrent_lookup_and_insert_remain_coherent);
  return UNITY_END();
}
