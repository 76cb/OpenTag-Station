#include <unity.h>

#include <algorithm>
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
using opentag::web::IdempotencyAdmission;

// Simulates the operation registry: operations listed here are still live.
struct LiveOperations {
  std::vector<std::uint64_t> live;
  bool operator()(std::uint64_t id) const {
    for (const auto value : live) if (value == id) return false;
    return true;
  }
};
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

void test_read_only_catalog_requests_never_consume_slots_then_write_accepted() {
  IdempotencyLedger ledger;
  LiveOperations ops;
  std::uint64_t next_op = 1U;
  // A previous write is still running and retained.
  TEST_ASSERT_TRUE(ledger.insert("write-0", 1U, next_op, 0U));
  ops.live.push_back(next_op++);
  for (std::uint32_t index = 0U; index < 40U; ++index) {
    const auto admission = ledger.admit(
        "catalog-" + std::to_string(index), 500U + index, index, false, ops);
    TEST_ASSERT_EQUAL(
        static_cast<int>(IdempotencyAdmission::proceed_unretained),
        static_cast<int>(admission.decision));
    // Catalog operations stay live and are never inserted.
    ops.live.push_back(next_op++);
  }
  TEST_ASSERT_EQUAL_UINT(1U, ledger.size(40U));
  const auto write = ledger.admit("write-1", 2U, 41U, true, ops);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::proceed),
      static_cast<int>(write.decision));
  TEST_ASSERT_TRUE(ledger.insert("write-1", 2U, next_op, 41U));
  // A read-only request reusing a retained key still replays/conflicts.
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::replay),
      static_cast<int>(ledger.admit("write-1", 2U, 42U, false, ops).decision));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::conflict),
      static_cast<int>(ledger.admit("write-1", 3U, 42U, false, ops).decision));
}

void test_full_ledger_evicts_oldest_finished_entry_and_keeps_live_replay() {
  IdempotencyLedger ledger;
  LiveOperations ops;
  for (std::size_t index = 0U; index < IdempotencyLedger::capacity; ++index) {
    TEST_ASSERT_TRUE(ledger.insert(
        "key-" + std::to_string(index), index + 100U, index + 1U,
        static_cast<std::uint32_t>(index * 10U)));
    ops.live.push_back(index + 1U);
  }
  constexpr std::uint32_t now = 1000U;
  // Operations for key-3 and key-7 have finished; key-3 is older.
  ops.live.erase(std::remove(ops.live.begin(), ops.live.end(), 4U), ops.live.end());
  ops.live.erase(std::remove(ops.live.begin(), ops.live.end(), 8U), ops.live.end());
  TEST_ASSERT_FALSE(ledger.has_capacity(now));
  const auto first = ledger.admit("new-1", 1U, now, true, ops);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::proceed),
      static_cast<int>(first.decision));
  TEST_ASSERT_TRUE(ledger.insert("new-1", 1U, 900U, now));
  ops.live.push_back(900U);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(ledger.lookup("key-3", 103U, now).status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::same),
      static_cast<int>(ledger.lookup("key-7", 107U, now).status));
  // Live duplicates still replay their original operation.
  const auto replay = ledger.admit("key-0", 100U, now, true, ops);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::replay),
      static_cast<int>(replay.decision));
  TEST_ASSERT_EQUAL_UINT64(1U, replay.operation_id);
  // Next eviction takes key-7, then nothing finished remains.
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::proceed),
      static_cast<int>(ledger.admit("new-2", 2U, now, true, ops).decision));
  TEST_ASSERT_TRUE(ledger.insert("new-2", 2U, 901U, now));
  ops.live.push_back(901U);
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyLookupStatus::miss),
      static_cast<int>(ledger.lookup("key-7", 107U, now).status));
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::full),
      static_cast<int>(ledger.admit("new-3", 3U, now, true, ops).decision));
  TEST_ASSERT_EQUAL_UINT(IdempotencyLedger::capacity, ledger.size(now));
}

void test_full_ledger_refuses_while_every_operation_is_live() {
  IdempotencyLedger ledger;
  LiveOperations ops;
  for (std::size_t index = 0U; index < IdempotencyLedger::capacity; ++index) {
    TEST_ASSERT_TRUE(ledger.insert(
        "live-" + std::to_string(index), index, index + 1U, 0U));
    ops.live.push_back(index + 1U);
  }
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::full),
      static_cast<int>(ledger.admit("extra", 1U, 5U, true, ops).decision));
  // Read-only requests are still admitted without a slot.
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::proceed_unretained),
      static_cast<int>(ledger.admit("extra", 1U, 5U, false, ops).decision));
  // Expiry still frees slots as before.
  TEST_ASSERT_EQUAL(
      static_cast<int>(IdempotencyAdmission::proceed),
      static_cast<int>(ledger.admit(
          "extra", 1U, IdempotencyLedger::ttl_ms, true, ops).decision));
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
  RUN_TEST(
      test_read_only_catalog_requests_never_consume_slots_then_write_accepted);
  RUN_TEST(
      test_full_ledger_evicts_oldest_finished_entry_and_keeps_live_replay);
  RUN_TEST(test_full_ledger_refuses_while_every_operation_is_live);
  return UNITY_END();
}
