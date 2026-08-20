#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace opentag::logging {

inline constexpr std::size_t maximum_log_entries = 32U;
inline constexpr std::size_t maximum_log_message_bytes = 192U;

enum class LogSeverity : std::uint8_t {
  debug,
  info,
  warning,
  error,
};

enum class LogComponent : std::uint8_t {
  application,
  configuration,
  scale,
  nfc,
  network,
  backend,
  web,
  storage,
  security,
};

[[nodiscard]] constexpr const char* to_string(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::debug: return "debug";
    case LogSeverity::info: return "info";
    case LogSeverity::warning: return "warning";
    case LogSeverity::error: return "error";
  }
  return "unknown";
}

[[nodiscard]] constexpr const char* to_string(LogComponent component) {
  switch (component) {
    case LogComponent::application: return "application";
    case LogComponent::configuration: return "configuration";
    case LogComponent::scale: return "scale";
    case LogComponent::nfc: return "nfc";
    case LogComponent::network: return "network";
    case LogComponent::backend: return "backend";
    case LogComponent::web: return "web";
    case LogComponent::storage: return "storage";
    case LogComponent::security: return "security";
  }
  return "unknown";
}

struct LogEntry {
  std::uint64_t cursor{0U};
  std::uint32_t timestamp_ms{0U};
  LogSeverity severity{LogSeverity::info};
  LogComponent component{LogComponent::application};
  bool truncated{false};
  bool redacted{false};
  std::size_t message_length{0U};
  std::array<char, maximum_log_message_bytes + 1U> message{};

  [[nodiscard]] std::string_view text() const {
    return {message.data(), message_length};
  }
};

struct LogSnapshot {
  std::vector<LogEntry> entries;
  std::uint64_t oldest_cursor{0U};
  std::uint64_t latest_cursor{0U};
  std::uint64_t dropped_count{0U};
  bool history_gap{false};
};

class BoundedLog final {
 public:
  void append(
      std::uint32_t timestamp_ms,
      LogSeverity severity,
      LogComponent component,
      std::string_view message);

  [[nodiscard]] LogSnapshot snapshot(
      std::uint64_t after_cursor = 0U,
      std::size_t limit = maximum_log_entries) const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::uint64_t dropped_count() const;

 private:
  [[nodiscard]] static LogEntry make_entry(
      std::uint32_t timestamp_ms,
      LogSeverity severity,
      LogComponent component,
      std::string_view message);

  mutable std::mutex mutex_;
  std::array<LogEntry, maximum_log_entries> entries_{};
  std::size_t head_{0U};
  std::size_t count_{0U};
  std::uint64_t next_cursor_{1U};
  std::uint64_t dropped_count_{0U};
};

}  // namespace opentag::logging
