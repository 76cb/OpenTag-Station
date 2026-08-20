#include <unity.h>

#include <cstdint>
#include <string>

#include "logging/bounded_log.hpp"

using opentag::logging::BoundedLog;
using opentag::logging::LogComponent;
using opentag::logging::LogSeverity;
using opentag::logging::maximum_log_entries;
using opentag::logging::maximum_log_message_bytes;

void setUp() {}
void tearDown() {}

void test_capacity_drop_count_and_cursor_paging_are_bounded() {
  BoundedLog log;
  constexpr std::size_t overwritten = 3U;
  for (std::size_t index = 0U;
       index < maximum_log_entries + overwritten;
       ++index) {
    log.append(
        static_cast<std::uint32_t>(index * 10U),
        LogSeverity::info,
        LogComponent::application,
        "entry-" + std::to_string(index));
  }

  const auto all = log.snapshot();
  TEST_ASSERT_EQUAL_UINT(maximum_log_entries, all.entries.size());
  TEST_ASSERT_EQUAL_UINT64(overwritten, all.dropped_count);
  TEST_ASSERT_EQUAL_UINT64(overwritten + 1U, all.oldest_cursor);
  TEST_ASSERT_EQUAL_UINT64(
      maximum_log_entries + overwritten, all.latest_cursor);
  TEST_ASSERT_EQUAL_UINT64(all.oldest_cursor, all.entries.front().cursor);
  TEST_ASSERT_EQUAL_UINT64(all.latest_cursor, all.entries.back().cursor);

  const auto gap = log.snapshot(1U);
  TEST_ASSERT_TRUE(gap.history_gap);
  const auto page = log.snapshot(all.oldest_cursor, 2U);
  TEST_ASSERT_FALSE(page.history_gap);
  TEST_ASSERT_EQUAL_UINT(2U, page.entries.size());
  TEST_ASSERT_EQUAL_UINT64(all.oldest_cursor + 1U, page.entries[0].cursor);
  TEST_ASSERT_EQUAL_UINT64(all.oldest_cursor + 2U, page.entries[1].cursor);

  const auto current = log.snapshot(all.latest_cursor);
  TEST_ASSERT_TRUE(current.entries.empty());
  TEST_ASSERT_EQUAL_UINT(maximum_log_entries, log.size());
  TEST_ASSERT_EQUAL_UINT64(overwritten, log.dropped_count());
}

void test_long_message_is_truncated_at_the_fixed_byte_limit() {
  BoundedLog log;
  const std::string oversized(maximum_log_message_bytes + 37U, 'x');
  log.append(7U, LogSeverity::warning, LogComponent::storage, oversized);

  const auto snapshot = log.snapshot();
  TEST_ASSERT_EQUAL_UINT(1U, snapshot.entries.size());
  const auto& entry = snapshot.entries.front();
  TEST_ASSERT_TRUE(entry.truncated);
  TEST_ASSERT_FALSE(entry.redacted);
  TEST_ASSERT_EQUAL_UINT(maximum_log_message_bytes, entry.message_length);
  TEST_ASSERT_EQUAL_UINT(maximum_log_message_bytes, entry.text().size());
  TEST_ASSERT_EQUAL_CHAR('\0', entry.message[maximum_log_message_bytes]);
}

void assert_redacted(const std::string& message, const char* secret) {
  BoundedLog log;
  log.append(11U, LogSeverity::error, LogComponent::security, message);
  const auto snapshot = log.snapshot();
  TEST_ASSERT_EQUAL_UINT(1U, snapshot.entries.size());
  const auto& entry = snapshot.entries.front();
  const std::string stored(entry.text());
  TEST_ASSERT_TRUE(entry.redacted);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, stored.find("[REDACTED]"));
  TEST_ASSERT_EQUAL(std::string::npos, stored.find(secret));
}

void test_sensitive_markers_are_redacted_during_ingestion() {
  assert_redacted("Authorization: Bearer super-secret", "super-secret");
  assert_redacted("upstream Bearer bearer-secret", "bearer-secret");
  assert_redacted("proxy Basic Zm9vOmJhcg==", "Zm9vOmJhcg==");
  assert_redacted("password=hunter2", "hunter2");
  assert_redacted("api_token=abc123", "abc123");
  assert_redacted(
      "CA -----BEGIN CERTIFICATE----- private-material",
      "private-material");
  assert_redacted("ca_certificate_pem=private-ca", "private-ca");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_capacity_drop_count_and_cursor_paging_are_bounded);
  RUN_TEST(test_long_message_is_truncated_at_the_fixed_byte_limit);
  RUN_TEST(test_sensitive_markers_are_redacted_during_ingestion);
  return UNITY_END();
}
