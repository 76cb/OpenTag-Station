#include "logging/bounded_log.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace opentag::logging {
namespace {

constexpr std::string_view redaction_text = "[REDACTED]";
constexpr std::array<std::string_view, 10U> sensitive_markers{{
    "authorization",
    "bearer",
    "basic",
    "password",
    "token",
    "ca_certificate_pem",
    "-----begin certificate-----",
    "-----begin private key-----",
    "-----begin rsa private key-----",
    "-----begin ec private key-----",
}};

constexpr char ascii_lower(char value) {
  return value >= 'A' && value <= 'Z'
             ? static_cast<char>(value - 'A' + 'a')
             : value;
}

bool starts_with_case_insensitive(
    std::string_view input,
    std::size_t offset,
    std::string_view expected) {
  if (offset > input.size() || expected.size() > input.size() - offset) {
    return false;
  }
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (ascii_lower(input[offset + index]) != expected[index]) return false;
  }
  return true;
}

bool starts_sensitive_marker(std::string_view input, std::size_t offset) {
  return std::any_of(
      sensitive_markers.begin(), sensitive_markers.end(),
      [&](std::string_view marker) {
        return starts_with_case_insensitive(input, offset, marker);
      });
}

char safe_log_character(char value) {
  const auto byte = static_cast<unsigned char>(value);
  return byte < 0x20U || byte == 0x7FU ? ' ' : value;
}

}  // namespace

LogEntry BoundedLog::make_entry(
    std::uint32_t timestamp_ms,
    LogSeverity severity,
    LogComponent component,
    std::string_view input) {
  LogEntry entry;
  entry.timestamp_ms = timestamp_ms;
  entry.severity = severity;
  entry.component = component;

  std::size_t input_offset = 0U;
  while (input_offset < input.size()) {
    if (starts_sensitive_marker(input, input_offset)) {
      entry.redacted = true;
      if (entry.message_length + redaction_text.size() >
          maximum_log_message_bytes) {
        entry.message_length = maximum_log_message_bytes - redaction_text.size();
        entry.truncated = true;
      }
      for (const auto character : redaction_text) {
        entry.message[entry.message_length++] = character;
      }
      break;
    }
    if (entry.message_length == maximum_log_message_bytes) {
      entry.truncated = true;
      break;
    }
    entry.message[entry.message_length++] =
        safe_log_character(input[input_offset++]);
  }
  entry.message[entry.message_length] = '\0';
  return entry;
}

void BoundedLog::append(
    std::uint32_t timestamp_ms,
    LogSeverity severity,
    LogComponent component,
    std::string_view message) {
  auto entry = make_entry(timestamp_ms, severity, component, message);
  const std::lock_guard<std::mutex> lock(mutex_);
  entry.cursor = next_cursor_++;

  if (count_ < maximum_log_entries) {
    const auto index = (head_ + count_) % maximum_log_entries;
    entries_[index] = entry;
    ++count_;
    return;
  }

  entries_[head_] = entry;
  head_ = (head_ + 1U) % maximum_log_entries;
  ++dropped_count_;
}

LogSnapshot BoundedLog::snapshot(
    std::uint64_t after_cursor,
    std::size_t limit) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  LogSnapshot result;
  result.dropped_count = dropped_count_;
  if (count_ == 0U) return result;

  result.oldest_cursor = entries_[head_].cursor;
  result.latest_cursor =
      entries_[(head_ + count_ - 1U) % maximum_log_entries].cursor;
  result.history_gap = after_cursor != 0U &&
      result.oldest_cursor > after_cursor &&
      result.oldest_cursor - after_cursor > 1U;

  const auto bounded_limit = std::min(limit, maximum_log_entries);
  result.entries.reserve(std::min(bounded_limit, count_));
  for (std::size_t offset = 0U;
       offset < count_ && result.entries.size() < bounded_limit;
       ++offset) {
    const auto& entry = entries_[(head_ + offset) % maximum_log_entries];
    if (entry.cursor > after_cursor) result.entries.push_back(entry);
  }
  return result;
}

std::size_t BoundedLog::size() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

std::uint64_t BoundedLog::dropped_count() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return dropped_count_;
}

}  // namespace opentag::logging
