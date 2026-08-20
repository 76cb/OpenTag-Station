#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/error.hpp"

namespace opentag::application {

enum class OperationKind : std::uint8_t {
  configuration,
  scale_tare,
  scale_calibration,
  backend_probe,
  toolhead_assignment,
  toolhead_unassignment,
  nfc_read,
  reboot,
  factory_reset,
};

enum class OperationState : std::uint8_t {
  queued,
  running,
  succeeded,
  failed,
  confirmation_required,
};

struct OperationRecord {
  std::uint64_t id{0U};
  OperationKind kind{OperationKind::configuration};
  OperationState state{OperationState::queued};
  std::uint32_t created_at_ms{0U};
  std::uint32_t updated_at_ms{0U};
  std::string message;
  std::optional<core::Error> error;
};

struct CommandReceipt {
  bool accepted{false};
  std::uint64_t operation_id{0U};
};

[[nodiscard]] const char* to_string(OperationKind kind);
[[nodiscard]] const char* to_string(OperationState state);

class OperationRegistry final {
 public:
  static constexpr std::size_t capacity = 24U;
  static constexpr std::size_t maximum_message_bytes = 192U;

  [[nodiscard]] std::uint64_t begin(
      OperationKind kind,
      std::uint32_t now_ms,
      std::string message = {});
  void mark_running(
      std::uint64_t id,
      std::uint32_t now_ms,
      std::string message = {});
  void succeed(
      std::uint64_t id,
      std::uint32_t now_ms,
      std::string message = {});
  void fail(
      std::uint64_t id,
      std::uint32_t now_ms,
      core::Error error);
  void require_confirmation(
      std::uint64_t id,
      std::uint32_t now_ms,
      std::string message);
  [[nodiscard]] std::optional<OperationRecord> get(std::uint64_t id) const;
  [[nodiscard]] std::vector<OperationRecord> snapshot(
      std::size_t limit = capacity) const;
  [[nodiscard]] std::uint64_t revision() const;

 private:
  void update(
      std::uint64_t id,
      OperationState state,
      std::uint32_t now_ms,
      std::string message,
      std::optional<core::Error> error = std::nullopt);
  static std::string bounded(std::string value);

  mutable std::mutex mutex_;
  std::array<OperationRecord, capacity> records_{};
  std::size_t next_slot_{0U};
  std::size_t count_{0U};
  std::uint64_t next_id_{1U};
  std::uint64_t revision_{0U};
};

}  // namespace opentag::application
