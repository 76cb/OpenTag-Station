#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>

#include "core/byte_view.hpp"
#include "core/result.hpp"

namespace opentag::ota {

inline constexpr std::size_t sha256_digest_bytes = 32U;
inline constexpr std::size_t maximum_upload_chunk_bytes = 4096U;
inline constexpr std::uint32_t maximum_image_bytes = 5U * 1024U * 1024U;
inline constexpr std::uint32_t progress_persistence_interval_bytes =
    512U * 1024U;
inline constexpr std::uint32_t candidate_confirmation_window_ms = 30000U;

using Sha256Digest = std::array<std::uint8_t, sha256_digest_bytes>;

template <std::size_t Maximum>
struct BoundedText {
  static_assert(Maximum <= 255U);

  std::array<char, Maximum + 1U> characters{};
  std::uint8_t length{0U};

  [[nodiscard]] bool assign(std::string_view value) {
    if (value.size() > Maximum) return false;
    characters.fill('\0');
    for (std::size_t index = 0U; index < value.size(); ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if (byte == 0U || byte < 0x20U || byte == 0x7FU) return false;
      characters[index] = value[index];
    }
    length = static_cast<std::uint8_t>(value.size());
    return true;
  }

  [[nodiscard]] std::string_view view() const {
    return {characters.data(), length <= Maximum ? length : 0U};
  }

  [[nodiscard]] bool empty() const { return length == 0U; }
};

template <std::size_t Maximum>
[[nodiscard]] bool operator==(
    const BoundedText<Maximum>& left,
    const BoundedText<Maximum>& right) {
  return left.view() == right.view();
}

template <std::size_t Maximum>
[[nodiscard]] bool operator!=(
    const BoundedText<Maximum>& left,
    const BoundedText<Maximum>& right) {
  return !(left == right);
}

enum class UpdateState : std::uint8_t {
  idle,
  upload_receiving,
  writing,
  validating,
  ready_to_activate,
  ready_to_reboot,
  reboot_pending,
  candidate_boot,
  validating_candidate,
  confirmed,
  rollback_pending,
  rolled_back,
  failed,
};

enum class PartitionImageState : std::uint8_t {
  unknown,
  new_image,
  pending_verify,
  valid,
  invalid,
  aborted,
  undefined,
};

enum class CandidateHealthDecision : std::uint8_t {
  stabilizing,
  healthy,
  unhealthy,
  factory_reset_recovery,
};

[[nodiscard]] const char* to_string(UpdateState state);
[[nodiscard]] const char* to_string(PartitionImageState state);

struct PartitionDescriptor {
  BoundedText<16U> label;
  std::uint32_t address{0U};
  std::uint32_t size{0U};
  std::uint8_t subtype{0U};

  [[nodiscard]] bool present() const { return address != 0U && size != 0U; }
};

[[nodiscard]] bool same_partition(
    const PartitionDescriptor& left,
    const PartitionDescriptor& right);

struct FirmwareDescriptor {
  BoundedText<32U> project_name;
  BoundedText<32U> version;
  BoundedText<40U> git_sha;
  BoundedText<32U> build_date;
  BoundedText<32U> board_id;
  BoundedText<32U> idf_version;
};

struct PlatformStatus {
  PartitionDescriptor running;
  PartitionDescriptor boot;
  PartitionDescriptor inactive;
  FirmwareDescriptor running_image;
  PartitionImageState running_state{PartitionImageState::unknown};
  std::optional<PartitionDescriptor> last_invalid;
  bool rollback_available{false};
};

struct ImageValidation {
  bool structure_valid{false};
  bool target_compatible{false};
  bool project_compatible{false};
  std::uint32_t image_size{0U};
  FirmwareDescriptor image;
};

class IOtaPlatform {
 public:
  virtual ~IOtaPlatform() = default;

  [[nodiscard]] virtual core::Result<PlatformStatus> status() = 0;
  [[nodiscard]] virtual core::Result<void> begin_write(
      const PartitionDescriptor& target,
      std::uint32_t expected_size) = 0;
  [[nodiscard]] virtual core::Result<void> write(core::ByteView chunk) = 0;
  [[nodiscard]] virtual core::Result<void> finish_write() = 0;
  [[nodiscard]] virtual core::Result<void> abort_write() = 0;
  [[nodiscard]] virtual core::Result<ImageValidation> validate_staged_image(
      const PartitionDescriptor& target,
      std::uint32_t expected_size) = 0;
  [[nodiscard]] virtual core::Result<void> activate(
      const PartitionDescriptor& target) = 0;
  [[nodiscard]] virtual core::Result<void> confirm_running() = 0;
  [[nodiscard]] virtual core::Result<void> rollback_and_reboot() = 0;
};

class ISha256 {
 public:
  virtual ~ISha256() = default;

  [[nodiscard]] virtual core::Result<void> begin() = 0;
  [[nodiscard]] virtual core::Result<void> update(core::ByteView chunk) = 0;
  [[nodiscard]] virtual core::Result<Sha256Digest> finish() = 0;
  virtual void abort() = 0;
};

struct UpdateRecord {
  static constexpr std::uint32_t magic_value = 0x4F544155U;  // OTAU
  static constexpr std::uint16_t schema_value = 1U;

  std::uint32_t magic{magic_value};
  std::uint16_t schema_version{schema_value};
  std::uint16_t record_size{0U};
  std::uint64_t generation{0U};
  std::uint64_t operation_id{0U};
  UpdateState state{UpdateState::idle};
  std::uint8_t flags{0U};
  std::uint16_t reserved{0U};
  PartitionDescriptor target;
  std::uint32_t expected_length{0U};
  std::uint32_t bytes_received{0U};
  Sha256Digest expected_sha256{};
  Sha256Digest calculated_sha256{};
  FirmwareDescriptor candidate;
  BoundedText<192U> last_error;
  std::uint32_t started_at_ms{0U};
  std::uint32_t updated_at_ms{0U};
  std::uint32_t checksum{0U};
};

static_assert(std::is_standard_layout_v<UpdateRecord>);
static_assert(std::is_trivially_copyable_v<UpdateRecord>);

inline constexpr std::uint8_t record_flag_calculated_sha_available = 1U << 0U;
inline constexpr std::uint8_t record_flag_validation_passed = 1U << 1U;
inline constexpr std::uint8_t record_flag_activation_intent = 1U << 2U;
inline constexpr std::uint8_t record_flag_activated = 1U << 3U;

[[nodiscard]] std::uint32_t update_record_checksum(const UpdateRecord& record);
[[nodiscard]] bool valid_update_record(const UpdateRecord& record);

class IUpdateRecordStore {
 public:
  virtual ~IUpdateRecordStore() = default;

  [[nodiscard]] virtual core::Result<std::optional<UpdateRecord>> load() = 0;

  // This is a durable monotonic reservation, independent of the record save.
  // The returned generation must be nonzero and strictly greater than the
  // supplied floor. A store must fail instead of wrapping or reusing a value.
  [[nodiscard]] virtual core::Result<std::uint64_t> reserve_generation(
      std::uint64_t minimum_exclusive) = 0;

  [[nodiscard]] virtual core::Result<void> save(
      const UpdateRecord& record) = 0;
};

struct BeginUploadRequest {
  std::uint64_t operation_id{0U};
  std::uint64_t expected_generation{0U};
  std::uint32_t expected_length{0U};
  Sha256Digest expected_sha256{};
  BoundedText<32U> declared_version;
  BoundedText<40U> declared_git_sha;
};

struct OperationPrecondition {
  std::uint64_t operation_id{0U};
  std::uint64_t generation{0U};
};

struct UpdateSnapshot {
  std::uint64_t revision{0U};
  std::uint64_t generation{0U};
  std::uint64_t operation_id{0U};
  UpdateState state{UpdateState::idle};
  PartitionDescriptor running;
  PartitionDescriptor boot;
  PartitionDescriptor inactive;
  PartitionDescriptor target;
  FirmwareDescriptor current;
  FirmwareDescriptor candidate;
  std::uint32_t image_size{0U};
  std::uint32_t bytes_received{0U};
  Sha256Digest expected_sha256{};
  Sha256Digest calculated_sha256{};
  bool calculated_sha_available{false};
  bool validation_passed{false};
  bool activation_intent{false};
  bool activated{false};
  bool rollback_available{false};
  PartitionImageState running_image_state{PartitionImageState::unknown};
  std::optional<PartitionDescriptor> last_invalid;
  BoundedText<192U> last_error;
  std::uint32_t started_at_ms{0U};
  std::uint32_t updated_at_ms{0U};
  std::uint32_t candidate_started_at_ms{0U};
};

class UpdateManager final {
 public:
  UpdateManager(
      IOtaPlatform& platform,
      ISha256& sha256,
      IUpdateRecordStore& records)
      : platform_(platform), sha256_(sha256), records_(records) {}

  [[nodiscard]] core::Result<UpdateSnapshot> initialize_from_boot(
      std::uint32_t now_ms);
  [[nodiscard]] UpdateSnapshot snapshot() const;

  [[nodiscard]] core::Result<UpdateSnapshot> begin_upload(
      const BeginUploadRequest& request,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> write_chunk(
      OperationPrecondition precondition,
      core::ByteView chunk,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> finish_upload(
      OperationPrecondition precondition,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> activate(
      OperationPrecondition precondition,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> mark_reboot_pending(
      OperationPrecondition precondition,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> cancel(
      OperationPrecondition precondition,
      std::uint32_t now_ms);

  [[nodiscard]] core::Result<UpdateSnapshot> handle_candidate_health(
      CandidateHealthDecision decision,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> confirm_candidate(
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> rollback_candidate(
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot> recover_rollback_seed(
      std::uint32_t now_ms);

 private:
  [[nodiscard]] core::Result<UpdateSnapshot> fail_locked(
      core::Error error,
      std::uint32_t now_ms,
      bool abort_writer);
  [[nodiscard]] core::Result<UpdateSnapshot>
  rollback_unreconciled_candidate_locked(
      core::Error error,
      std::uint32_t now_ms);
  [[nodiscard]] core::Result<UpdateSnapshot>
  recover_rollback_seed_locked(std::uint32_t now_ms);
  [[nodiscard]] bool rollback_seed_recovery_required_locked() const;
  [[nodiscard]] core::Result<void> persist_locked();
  [[nodiscard]] core::Result<void> validate_precondition_locked(
      OperationPrecondition precondition) const;
  [[nodiscard]] core::Result<void> refresh_platform_locked();
  void apply_record_locked(const UpdateRecord& record);
  [[nodiscard]] UpdateRecord make_record_locked() const;
  void clear_operation_locked(std::uint32_t now_ms);
  void touch_locked(std::uint32_t now_ms);

  IOtaPlatform& platform_;
  ISha256& sha256_;
  IUpdateRecordStore& records_;
  mutable std::mutex mutex_;
  UpdateSnapshot state_;
  bool writer_open_{false};
  std::uint32_t next_progress_persistence_bytes_{
      progress_persistence_interval_bytes};
};

}  // namespace opentag::ota
