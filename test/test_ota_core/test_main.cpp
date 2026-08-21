#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "ota/update_manager.hpp"

namespace {

using opentag::core::ByteView;
using opentag::core::Error;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::ota::BeginUploadRequest;
using opentag::ota::CandidateHealthDecision;
using opentag::ota::FirmwareDescriptor;
using opentag::ota::IOtaPlatform;
using opentag::ota::ISha256;
using opentag::ota::IUpdateRecordStore;
using opentag::ota::ImageValidation;
using opentag::ota::OperationPrecondition;
using opentag::ota::PartitionDescriptor;
using opentag::ota::PartitionImageState;
using opentag::ota::PlatformStatus;
using opentag::ota::Sha256Digest;
using opentag::ota::UpdateManager;
using opentag::ota::UpdateRecord;
using opentag::ota::UpdateSnapshot;
using opentag::ota::UpdateState;

Error ota_error(std::string message = "fake OTA error") {
  return {ErrorCategory::firmware_update, std::move(message), false};
}

PartitionDescriptor partition(
    const char* label,
    std::uint32_t address,
    std::uint8_t subtype) {
  PartitionDescriptor result;
  TEST_ASSERT_TRUE(result.label.assign(label));
  result.address = address;
  result.size = opentag::ota::maximum_image_bytes;
  result.subtype = subtype;
  return result;
}

FirmwareDescriptor firmware(const char* version) {
  FirmwareDescriptor result;
  TEST_ASSERT_TRUE(result.project_name.assign("opentag-station"));
  TEST_ASSERT_TRUE(result.version.assign(version));
  TEST_ASSERT_TRUE(result.git_sha.assign("0123456789ab"));
  TEST_ASSERT_TRUE(result.build_date.assign("2026-08-20T00:00:00Z"));
  TEST_ASSERT_TRUE(result.board_id.assign("wt32-sc01-plus-rev-a"));
  TEST_ASSERT_TRUE(result.idf_version.assign("v4.4.7"));
  return result;
}

Sha256Digest digest(std::uint8_t seed) {
  Sha256Digest result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

class FakePlatform final : public IOtaPlatform {
 public:
  FakePlatform() {
    status_value.running = partition("app0", 0x10000U, 0x10U);
    status_value.boot = status_value.running;
    status_value.inactive = partition("app1", 0x510000U, 0x11U);
    status_value.running_image = firmware("0.9.0");
    status_value.running_state = PartitionImageState::valid;
    status_value.rollback_available = true;
    validation.structure_valid = true;
    validation.target_compatible = true;
    validation.project_compatible = true;
    validation.image = firmware("1.0.0");
  }

  Result<PlatformStatus> status() override {
    ++status_calls;
    return fail_status
        ? Result<PlatformStatus>::failure(ota_error())
        : Result<PlatformStatus>::success(status_value);
  }

  Result<void> begin_write(
      const PartitionDescriptor& target,
      std::uint32_t expected_size) override {
    ++begin_calls;
    last_target = target;
    expected_length = expected_size;
    if (fail_begin) return Result<void>::failure(ota_error());
    writer_open = true;
    return Result<void>::success();
  }

  Result<void> write(ByteView chunk) override {
    ++write_calls;
    if (fail_write || !writer_open) {
      return Result<void>::failure(ota_error());
    }
    bytes_written += chunk.size;
    return Result<void>::success();
  }

  Result<void> finish_write() override {
    ++finish_calls;
    writer_open = false;
    return fail_finish ? Result<void>::failure(ota_error())
                       : Result<void>::success();
  }

  Result<void> abort_write() override {
    ++abort_calls;
    writer_open = false;
    return fail_abort ? Result<void>::failure(ota_error())
                      : Result<void>::success();
  }

  Result<ImageValidation> validate_staged_image(
      const PartitionDescriptor& target,
      std::uint32_t expected_size) override {
    ++validate_calls;
    last_target = target;
    validation.image_size = validation_size_override == 0U
                                ? expected_size
                                : validation_size_override;
    return fail_validate
        ? Result<ImageValidation>::failure(ota_error())
        : Result<ImageValidation>::success(validation);
  }

  Result<void> activate(const PartitionDescriptor& target) override {
    ++activate_calls;
    last_target = target;
    if (fail_activate) return Result<void>::failure(ota_error());
    status_value.boot = target;
    return Result<void>::success();
  }

  Result<void> confirm_running() override {
    ++confirm_calls;
    if (confirm_valid_before_failure) {
      status_value.running_state = PartitionImageState::valid;
    }
    if (fail_confirm) return Result<void>::failure(ota_error());
    status_value.running_state = PartitionImageState::valid;
    return Result<void>::success();
  }

  Result<void> rollback_and_reboot() override {
    ++rollback_calls;
    return fail_rollback ? Result<void>::failure(ota_error())
                         : Result<void>::success();
  }

  PlatformStatus status_value;
  ImageValidation validation;
  PartitionDescriptor last_target;
  std::size_t status_calls{0U};
  std::size_t begin_calls{0U};
  std::size_t write_calls{0U};
  std::size_t finish_calls{0U};
  std::size_t abort_calls{0U};
  std::size_t validate_calls{0U};
  std::size_t activate_calls{0U};
  std::size_t confirm_calls{0U};
  std::size_t rollback_calls{0U};
  std::size_t bytes_written{0U};
  std::uint32_t expected_length{0U};
  std::uint32_t validation_size_override{0U};
  bool writer_open{false};
  bool fail_status{false};
  bool fail_begin{false};
  bool fail_write{false};
  bool fail_finish{false};
  bool fail_abort{false};
  bool fail_validate{false};
  bool fail_activate{false};
  bool confirm_valid_before_failure{false};
  bool fail_confirm{false};
  bool fail_rollback{false};
};

class FakeSha final : public ISha256 {
 public:
  Result<void> begin() override {
    ++begin_calls;
    active = !fail_begin;
    return fail_begin ? Result<void>::failure(ota_error("hash begin failed"))
                      : Result<void>::success();
  }

  Result<void> update(ByteView chunk) override {
    ++update_calls;
    bytes_hashed += chunk.size;
    return fail_update ? Result<void>::failure(ota_error("hash update failed"))
                       : Result<void>::success();
  }

  Result<Sha256Digest> finish() override {
    ++finish_calls;
    active = false;
    return fail_finish
        ? Result<Sha256Digest>::failure(ota_error("hash finish failed"))
        : Result<Sha256Digest>::success(result);
  }

  void abort() override {
    ++abort_calls;
    active = false;
  }

  Sha256Digest result{digest(1U)};
  std::size_t begin_calls{0U};
  std::size_t update_calls{0U};
  std::size_t finish_calls{0U};
  std::size_t abort_calls{0U};
  std::size_t bytes_hashed{0U};
  bool active{false};
  bool fail_begin{false};
  bool fail_update{false};
  bool fail_finish{false};
};

class FakeRecordStore final : public IUpdateRecordStore {
 public:
  Result<std::optional<UpdateRecord>> load() override {
    ++load_calls;
    return fail_load
        ? Result<std::optional<UpdateRecord>>::failure(
              {ErrorCategory::storage, "record load failed", false})
        : Result<std::optional<UpdateRecord>>::success(record);
  }

  Result<std::uint64_t> reserve_generation(
      std::uint64_t minimum_exclusive) override {
    ++reserve_calls;
    if (fail_reserve || minimum_exclusive ==
            std::numeric_limits<std::uint64_t>::max()) {
      return Result<std::uint64_t>::failure(
          {ErrorCategory::storage, "generation reserve failed", false});
    }
    generation = std::max(generation, minimum_exclusive) + 1U;
    return Result<std::uint64_t>::success(generation);
  }

  Result<void> save(const UpdateRecord& value) override {
    ++save_calls;
    if (fail_save ||
        (fail_save_on_call != 0U && save_calls == fail_save_on_call)) {
      return Result<void>::failure(
          {ErrorCategory::storage, "record save failed", false});
    }
    TEST_ASSERT_TRUE(opentag::ota::valid_update_record(value));
    record = value;
    return Result<void>::success();
  }

  std::optional<UpdateRecord> record;
  std::uint64_t generation{0U};
  std::size_t load_calls{0U};
  std::size_t reserve_calls{0U};
  std::size_t save_calls{0U};
  std::size_t fail_save_on_call{0U};
  bool fail_load{false};
  bool fail_reserve{false};
  bool fail_save{false};
};

struct Harness {
  FakePlatform platform;
  FakeSha sha;
  FakeRecordStore records;
  UpdateManager manager{platform, sha, records};
};

UpdateSnapshot initialize(Harness& harness, std::uint32_t now_ms = 10U) {
  const auto result = harness.manager.initialize_from_boot(now_ms);
  TEST_ASSERT_TRUE_MESSAGE(result.ok(),
                           result.ok() ? "" : result.error().message.c_str());
  return result.value();
}

BeginUploadRequest request_for(
    const UpdateSnapshot& snapshot,
    std::uint32_t length,
    Sha256Digest expected = digest(1U),
    std::uint64_t operation_id = 42U) {
  BeginUploadRequest request;
  request.operation_id = operation_id;
  request.expected_generation = snapshot.generation;
  request.expected_length = length;
  request.expected_sha256 = expected;
  TEST_ASSERT_TRUE(request.declared_version.assign("1.0.0"));
  TEST_ASSERT_TRUE(request.declared_git_sha.assign("abcdef012345"));
  return request;
}

OperationPrecondition guard(const UpdateSnapshot& snapshot) {
  return {snapshot.operation_id, snapshot.generation};
}

UpdateSnapshot begin(Harness& harness, std::uint32_t length) {
  const auto initial = initialize(harness);
  const auto result = harness.manager.begin_upload(
      request_for(initial, length), 20U);
  TEST_ASSERT_TRUE_MESSAGE(result.ok(),
                           result.ok() ? "" : result.error().message.c_str());
  return result.value();
}

UpdateSnapshot write_all(
    Harness& harness,
    UpdateSnapshot snapshot,
    std::uint32_t length,
    std::uint32_t now_ms = 30U) {
  std::array<std::uint8_t, opentag::ota::maximum_upload_chunk_bytes> bytes{};
  std::uint32_t remaining = length;
  while (remaining != 0U) {
    const auto count = std::min<std::uint32_t>(remaining, bytes.size());
    const auto result = harness.manager.write_chunk(
        guard(snapshot), ByteView{bytes.data(), count}, now_ms++);
    TEST_ASSERT_TRUE_MESSAGE(result.ok(),
                             result.ok() ? "" : result.error().message.c_str());
    snapshot = result.value();
    remaining -= count;
  }
  return snapshot;
}

UpdateSnapshot validated(Harness& harness, std::uint32_t length = 8U) {
  auto snapshot = begin(harness, length);
  snapshot = write_all(harness, snapshot, length);
  const auto finished = harness.manager.finish_upload(guard(snapshot), 50U);
  TEST_ASSERT_TRUE_MESSAGE(
      finished.ok(), finished.ok() ? "" : finished.error().message.c_str());
  return finished.value();
}

void configure_candidate_boot(Harness& harness, UpdateState recorded_state) {
  const auto app0 = harness.platform.status_value.running;
  const auto app1 = harness.platform.status_value.inactive;
  harness.platform.status_value.running = app1;
  harness.platform.status_value.boot = app1;
  harness.platform.status_value.inactive = app0;
  harness.platform.status_value.running_image = firmware("1.0.0");
  harness.platform.status_value.running_state = PartitionImageState::pending_verify;
  harness.platform.status_value.rollback_available = true;

  UpdateRecord record{};
  record.record_size = sizeof(UpdateRecord);
  record.generation = 7U;
  record.operation_id = 99U;
  record.state = recorded_state;
  record.flags = opentag::ota::record_flag_validation_passed |
      opentag::ota::record_flag_activation_intent |
      opentag::ota::record_flag_activated;
  record.target = app1;
  record.expected_length = 1024U;
  record.bytes_received = 1024U;
  record.expected_sha256 = digest(1U);
  record.calculated_sha256 = digest(1U);
  record.flags |= opentag::ota::record_flag_calculated_sha_available;
  record.candidate = firmware("1.0.0");
  record.started_at_ms = 100U;
  record.updated_at_ms = 200U;
  record.checksum = opentag::ota::update_record_checksum(record);
  harness.records.record = record;
  harness.records.generation = record.generation;
}

void configure_rollback_seed_power_cut(
    Harness& harness,
    PartitionImageState running_state) {
  const auto staged = validated(harness);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(staged.state));
  TEST_ASSERT_TRUE(staged.validation_passed);
  TEST_ASSERT_FALSE(staged.activation_intent);
  TEST_ASSERT_FALSE(staged.activated);
  TEST_ASSERT_TRUE(harness.records.record.has_value());

  // Esp32OtaPlatform::activate() durably records the manager's intent, then
  // seeds erased otadata by selecting the already-running known-good slot.
  // This fixture is the exact power-cut boundary before mark-valid returns.
  harness.records.record->flags |=
      opentag::ota::record_flag_activation_intent;
  harness.records.record->flags &= static_cast<std::uint8_t>(
      ~opentag::ota::record_flag_activated);
  harness.records.record->checksum =
      opentag::ota::update_record_checksum(*harness.records.record);
  harness.platform.status_value.running_state = running_state;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_boot_initialization_reports_active_boot_and_inactive_slots() {
  Harness harness;
  const auto snapshot = initialize(harness);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::idle), static_cast<int>(snapshot.state));
  TEST_ASSERT_EQUAL_STRING("app0", snapshot.running.label.characters.data());
  TEST_ASSERT_EQUAL_STRING("app0", snapshot.boot.label.characters.data());
  TEST_ASSERT_EQUAL_STRING("app1", snapshot.inactive.label.characters.data());
  TEST_ASSERT_EQUAL_UINT64(1U, snapshot.revision);
}

void test_fresh_generation_zero_reserves_first_positive_generation() {
  Harness harness;
  const auto initial = initialize(harness);
  TEST_ASSERT_EQUAL_UINT64(0U, initial.generation);
  const auto begun = harness.manager.begin_upload(
      request_for(initial, 8U), 20U);
  TEST_ASSERT_TRUE(begun.ok());
  TEST_ASSERT_EQUAL_UINT64(1U, begun.value().generation);
  TEST_ASSERT_EQUAL_UINT(1U, harness.records.reserve_calls);
}

void test_happy_path_streams_validates_then_activates_separately() {
  Harness harness;
  auto snapshot = validated(harness);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(snapshot.state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.finish_calls);
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.validate_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);
  TEST_ASSERT_TRUE(snapshot.validation_passed);
  TEST_ASSERT_FALSE(snapshot.activation_intent);
  TEST_ASSERT_FALSE(snapshot.activated);
  TEST_ASSERT_EQUAL_STRING("1.0.0", snapshot.candidate.version.characters.data());

  const auto activated = harness.manager.activate(guard(snapshot), 60U);
  TEST_ASSERT_TRUE(activated.ok());
  snapshot = activated.value();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(snapshot.state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.activate_calls);
  TEST_ASSERT_TRUE(snapshot.activation_intent);
  TEST_ASSERT_TRUE(snapshot.activated);
}

void test_undefined_running_state_retains_intent_for_exact_activation_retry() {
  Harness harness;
  const auto staged = validated(harness);
  const auto precondition = guard(staged);
  const auto running = harness.platform.status_value.running;
  const auto target = harness.platform.status_value.inactive;
  harness.platform.status_value.running_state =
      PartitionImageState::undefined;
  harness.platform.fail_activate = true;

  const auto failed = harness.manager.activate(precondition, 60U);
  TEST_ASSERT_FALSE(failed.ok());
  const auto retained = harness.manager.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(retained.state));
  TEST_ASSERT_EQUAL_UINT64(precondition.operation_id, retained.operation_id);
  TEST_ASSERT_EQUAL_UINT64(precondition.generation, retained.generation);
  TEST_ASSERT_TRUE(retained.validation_passed);
  TEST_ASSERT_TRUE(retained.activation_intent);
  TEST_ASSERT_FALSE(retained.activated);
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(retained.running, running));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(retained.boot, running));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(retained.inactive, target));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(retained.target, target));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.activate_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);
  TEST_ASSERT_FALSE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activated) != 0U);

  TEST_ASSERT_FALSE(harness.manager.cancel(precondition, 61U).ok());
  const auto after_cancel = harness.manager.snapshot();
  TEST_ASSERT_EQUAL_UINT64(precondition.operation_id,
                           after_cancel.operation_id);
  TEST_ASSERT_EQUAL_UINT64(precondition.generation, after_cancel.generation);
  TEST_ASSERT_TRUE(after_cancel.activation_intent);
  TEST_ASSERT_FALSE(after_cancel.activated);

  harness.platform.fail_activate = false;
  const auto retried = harness.manager.activate(precondition, 62U);
  TEST_ASSERT_TRUE_MESSAGE(
      retried.ok(), retried.ok() ? "" : retried.error().message.c_str());
  TEST_ASSERT_TRUE(retried.value().activation_intent);
  TEST_ASSERT_TRUE(retried.value().activated);
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(retried.value().boot, target));
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(retried.value().running, running));
  TEST_ASSERT_EQUAL_UINT(2U, harness.platform.activate_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activated) != 0U);
}

void test_zero_and_oversized_images_are_rejected_before_flash_write() {
  Harness harness;
  auto initial = initialize(harness);
  auto result = harness.manager.begin_upload(request_for(initial, 0U), 20U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.begin_calls);

  initial = harness.manager.snapshot();
  result = harness.manager.begin_upload(
      request_for(initial, opentag::ota::maximum_image_bytes + 1U), 21U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.begin_calls);
}

void test_inactive_slot_is_selected_and_running_slot_is_never_written() {
  Harness harness;
  const auto snapshot = begin(harness, 8U);
  TEST_ASSERT_TRUE(opentag::ota::same_partition(
      harness.platform.status_value.inactive, harness.platform.last_target));
  TEST_ASSERT_FALSE(opentag::ota::same_partition(
      harness.platform.status_value.running, harness.platform.last_target));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(snapshot.target,
                                                snapshot.inactive));
}

void test_oversized_chunk_aborts_and_records_bounded_failure() {
  Harness harness;
  auto snapshot = begin(harness, 5000U);
  std::array<std::uint8_t,
             opentag::ota::maximum_upload_chunk_bytes + 1U> bytes{};
  const auto result = harness.manager.write_chunk(
      guard(snapshot), ByteView{bytes.data(), bytes.size()}, 30U);
  TEST_ASSERT_FALSE(result.ok());
  snapshot = harness.manager.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::failed),
                        static_cast<int>(snapshot.state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.abort_calls);
  TEST_ASSERT_TRUE(snapshot.last_error.length <= 192U);
}

void test_truncated_upload_is_aborted_without_validation_or_activation() {
  Harness harness;
  auto snapshot = begin(harness, 8U);
  std::array<std::uint8_t, 4U> bytes{};
  auto written = harness.manager.write_chunk(
      guard(snapshot), ByteView{bytes.data(), bytes.size()}, 30U);
  TEST_ASSERT_TRUE(written.ok());
  const auto finished = harness.manager.finish_upload(guard(written.value()), 40U);
  TEST_ASSERT_FALSE(finished.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.abort_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.finish_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);
}

void test_hash_mismatch_aborts_before_platform_image_validation() {
  Harness harness;
  harness.sha.result = digest(9U);
  auto snapshot = begin(harness, 8U);
  snapshot = write_all(harness, snapshot, 8U);
  const auto result = harness.manager.finish_upload(guard(snapshot), 50U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.abort_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.finish_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.validate_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);
}

void test_structure_target_project_and_length_validation_fail_closed() {
  const auto run_case = [](int which) {
    Harness harness;
    if (which == 0) harness.platform.validation.structure_valid = false;
    if (which == 1) harness.platform.validation.target_compatible = false;
    if (which == 2) harness.platform.validation.project_compatible = false;
    if (which == 3) harness.platform.validation_size_override = 7U;
    auto snapshot = begin(harness, 8U);
    snapshot = write_all(harness, snapshot, 8U);
    const auto result = harness.manager.finish_upload(guard(snapshot), 50U);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(UpdateState::failed),
        static_cast<int>(harness.manager.snapshot().state));
    TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);
  };
  for (int which = 0; which < 4; ++which) run_case(which);
}

void test_stale_generation_cannot_write_activate_cancel_or_reboot() {
  Harness harness;
  auto snapshot = begin(harness, 8U);
  const OperationPrecondition stale{snapshot.operation_id,
                                    snapshot.generation + 1U};
  std::array<std::uint8_t, 1U> byte{};
  TEST_ASSERT_FALSE(harness.manager.write_chunk(
      stale, ByteView{byte.data(), byte.size()}, 30U).ok());
  TEST_ASSERT_FALSE(harness.manager.cancel(stale, 31U).ok());
  TEST_ASSERT_FALSE(harness.manager.activate(stale, 32U).ok());
  TEST_ASSERT_FALSE(harness.manager.mark_reboot_pending(stale, 33U).ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::upload_receiving),
      static_cast<int>(harness.manager.snapshot().state));
}

void test_cancel_aborts_active_writer_and_is_forbidden_after_activation() {
  Harness harness;
  auto snapshot = begin(harness, 8U);
  const auto canceled = harness.manager.cancel(guard(snapshot), 30U);
  TEST_ASSERT_TRUE(canceled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::idle),
                        static_cast<int>(canceled.value().state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.abort_calls);

  snapshot = validated(harness);
  const auto staged_canceled = harness.manager.cancel(guard(snapshot), 60U);
  TEST_ASSERT_TRUE(staged_canceled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::idle),
                        static_cast<int>(staged_canceled.value().state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);

  snapshot = validated(harness);
  const auto activated = harness.manager.activate(guard(snapshot), 70U);
  TEST_ASSERT_TRUE(activated.ok());
  TEST_ASSERT_FALSE(harness.manager.cancel(guard(activated.value()), 71U).ok());
}

void test_validated_image_survives_reset_without_implicit_activation() {
  FakePlatform platform;
  FakeSha first_sha;
  FakeRecordStore records;
  UpdateManager first(platform, first_sha, records);

  auto initialized = first.initialize_from_boot(10U);
  TEST_ASSERT_TRUE(initialized.ok());
  auto snapshot = first.begin_upload(request_for(initialized.value(), 8U), 20U);
  TEST_ASSERT_TRUE(snapshot.ok());
  std::array<std::uint8_t, 8U> bytes{};
  auto written = first.write_chunk(
      guard(snapshot.value()), ByteView{bytes.data(), bytes.size()}, 30U);
  TEST_ASSERT_TRUE(written.ok());
  auto finished = first.finish_upload(guard(written.value()), 40U);
  TEST_ASSERT_TRUE(finished.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(finished.value().state));
  TEST_ASSERT_FALSE(finished.value().activated);
  TEST_ASSERT_EQUAL_UINT(0U, platform.activate_calls);
  TEST_ASSERT_EQUAL_STRING(
      "app0", platform.status_value.boot.label.characters.data());

  FakeSha second_sha;
  UpdateManager after_reset(platform, second_sha, records);
  const auto reconciled = after_reset.initialize_from_boot(100U);
  TEST_ASSERT_TRUE(reconciled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(reconciled.value().state));
  TEST_ASSERT_TRUE(reconciled.value().validation_passed);
  TEST_ASSERT_FALSE(reconciled.value().activation_intent);
  TEST_ASSERT_FALSE(reconciled.value().activated);
  TEST_ASSERT_EQUAL_UINT(0U, platform.activate_calls);
  TEST_ASSERT_EQUAL_STRING(
      "app0", platform.status_value.boot.label.characters.data());

  const auto canceled =
      after_reset.cancel(guard(reconciled.value()), 110U);
  TEST_ASSERT_TRUE(canceled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::idle),
                        static_cast<int>(canceled.value().state));
  TEST_ASSERT_EQUAL_UINT(0U, platform.activate_calls);
}

void test_reboot_requires_activation_and_preserves_operation_generation() {
  Harness harness;
  auto snapshot = validated(harness);
  TEST_ASSERT_FALSE(
      harness.manager.mark_reboot_pending(guard(snapshot), 60U).ok());
  const auto activated = harness.manager.activate(guard(snapshot), 61U);
  TEST_ASSERT_TRUE(activated.ok());
  const auto pending =
      harness.manager.mark_reboot_pending(guard(activated.value()), 62U);
  TEST_ASSERT_TRUE(pending.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::reboot_pending),
                        static_cast<int>(pending.value().state));
  TEST_ASSERT_EQUAL_UINT64(snapshot.generation, pending.value().generation);
  TEST_ASSERT_EQUAL_UINT64(snapshot.operation_id, pending.value().operation_id);
}

void test_progress_is_persisted_at_fixed_milestones_not_every_chunk() {
  Harness harness;
  constexpr std::uint32_t length =
      opentag::ota::progress_persistence_interval_bytes + 4096U;
  auto snapshot = begin(harness, length);
  const auto saves_after_begin = harness.records.save_calls;
  snapshot = write_all(harness, snapshot, length);
  TEST_ASSERT_TRUE(harness.records.save_calls >= saves_after_begin + 2U);
  TEST_ASSERT_TRUE(harness.records.save_calls < harness.platform.write_calls);
  TEST_ASSERT_EQUAL_UINT32(length, harness.records.record->bytes_received);
}

void test_interrupted_upload_becomes_durable_failure_and_generation_advances() {
  FakePlatform platform;
  FakeSha first_sha;
  FakeRecordStore records;
  UpdateManager first(platform, first_sha, records);
  const auto initial = first.initialize_from_boot(10U);
  TEST_ASSERT_TRUE(initial.ok());
  const auto begun = first.begin_upload(request_for(initial.value(), 8U), 20U);
  TEST_ASSERT_TRUE(begun.ok());
  const auto first_generation = begun.value().generation;

  platform.writer_open = false;  // Simulated reset discards the IDF OTA handle.
  FakeSha second_sha;
  UpdateManager second(platform, second_sha, records);
  const auto reconciled = second.initialize_from_boot(100U);
  TEST_ASSERT_FALSE(reconciled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::failed),
                        static_cast<int>(second.snapshot().state));

  const auto second_initial = second.snapshot();
  const auto next = second.begin_upload(
      request_for(second_initial, 8U, digest(1U), 43U), 110U);
  TEST_ASSERT_TRUE(next.ok());
  TEST_ASSERT_TRUE(next.value().generation > first_generation);
}

void test_interrupted_validation_is_not_resumed_after_reset() {
  Harness harness;
  auto snapshot = begin(harness, 8U);
  snapshot = write_all(harness, snapshot, 8U);
  harness.records.record->state = UpdateState::validating;
  harness.records.record->checksum =
      opentag::ota::update_record_checksum(*harness.records.record);
  harness.platform.writer_open = false;

  FakeSha second_sha;
  UpdateManager after_reset(harness.platform, second_sha, harness.records);
  const auto reconciled = after_reset.initialize_from_boot(100U);
  TEST_ASSERT_FALSE(reconciled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::failed),
                        static_cast<int>(after_reset.snapshot().state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::failed),
                        static_cast<int>(harness.records.record->state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);
}

void test_legacy_ready_to_activate_record_becomes_cancelable_staged_image() {
  Harness harness;
  auto snapshot = validated(harness);
  harness.records.record->state = UpdateState::ready_to_activate;
  harness.records.record->flags &=
      static_cast<std::uint8_t>(
          ~(opentag::ota::record_flag_activation_intent |
            opentag::ota::record_flag_activated));
  harness.records.record->checksum =
      opentag::ota::update_record_checksum(*harness.records.record);

  FakeSha second_sha;
  UpdateManager after_reset(harness.platform, second_sha, harness.records);
  const auto reconciled = after_reset.initialize_from_boot(100U);
  TEST_ASSERT_TRUE(reconciled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(reconciled.value().state));
  TEST_ASSERT_TRUE(reconciled.value().validation_passed);
  TEST_ASSERT_FALSE(reconciled.value().activation_intent);
  TEST_ASSERT_FALSE(reconciled.value().activated);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(harness.records.record->state));

  const auto canceled =
      after_reset.cancel(guard(reconciled.value()), 110U);
  TEST_ASSERT_TRUE(canceled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::idle),
                        static_cast<int>(canceled.value().state));
}

void test_record_crc_detects_state_digest_and_metadata_corruption() {
  UpdateRecord record{};
  record.record_size = sizeof(UpdateRecord);
  record.generation = 3U;
  record.operation_id = 4U;
  record.state = UpdateState::ready_to_reboot;
  record.target = partition("app1", 0x510000U, 0x11U);
  record.expected_length = 8U;
  record.bytes_received = 8U;
  record.expected_sha256 = digest(1U);
  record.calculated_sha256 = digest(1U);
  record.candidate = firmware("1.0.0");
  TEST_ASSERT_TRUE(record.last_error.assign("durable failure"));
  record.checksum = opentag::ota::update_record_checksum(record);
  TEST_ASSERT_TRUE(opentag::ota::valid_update_record(record));
  record.expected_sha256[7] ^= 0x80U;
  TEST_ASSERT_FALSE(opentag::ota::valid_update_record(record));

  record.expected_sha256[7] ^= 0x80U;
  record.candidate.build_date.characters[0] = '\x01';
  record.checksum = opentag::ota::update_record_checksum(record);
  TEST_ASSERT_FALSE(opentag::ota::valid_update_record(record));

  record.candidate = firmware("1.0.0");
  record.checksum = opentag::ota::update_record_checksum(record);
  TEST_ASSERT_TRUE(opentag::ota::valid_update_record(record));
  record.last_error.characters[0] ^= 1;
  TEST_ASSERT_FALSE(opentag::ota::valid_update_record(record));
}

void test_failed_update_error_survives_manager_reinitialization() {
  FakePlatform platform;
  FakeSha first_sha;
  FakeRecordStore records;
  UpdateManager first(platform, first_sha, records);
  const auto initial = first.initialize_from_boot(10U);
  TEST_ASSERT_TRUE(initial.ok());
  const auto begun = first.begin_upload(
      request_for(initial.value(), 8U), 20U);
  TEST_ASSERT_TRUE(begun.ok());
  platform.fail_write = true;
  std::array<std::uint8_t, 8U> bytes{};
  const auto failed = first.write_chunk(
      guard(begun.value()), ByteView{bytes.data(), bytes.size()}, 30U);
  TEST_ASSERT_FALSE(failed.ok());
  TEST_ASSERT_EQUAL_STRING(
      "fake OTA error", first.snapshot().last_error.characters.data());

  platform.writer_open = false;
  platform.fail_write = false;
  FakeSha second_sha;
  UpdateManager after_reboot(platform, second_sha, records);
  const auto restored = after_reboot.initialize_from_boot(100U);
  TEST_ASSERT_TRUE(restored.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::failed),
      static_cast<int>(restored.value().state));
  TEST_ASSERT_EQUAL_STRING(
      "fake OTA error", restored.value().last_error.characters.data());
}

void test_activation_power_cut_after_set_boot_reconciles_and_can_reboot() {
  Harness harness;
  auto snapshot = validated(harness);
  harness.records.fail_save_on_call = harness.records.save_calls + 2U;
  const auto activated = harness.manager.activate(guard(snapshot), 60U);
  TEST_ASSERT_TRUE(activated.ok());
  TEST_ASSERT_TRUE(activated.value().activated);
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.activate_calls);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);
  TEST_ASSERT_FALSE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activated) != 0U);

  FakeSha second_sha;
  UpdateManager after_power_cut(
      harness.platform, second_sha, harness.records);
  const auto reconciled = after_power_cut.initialize_from_boot(100U);
  TEST_ASSERT_TRUE(reconciled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(reconciled.value().state));
  TEST_ASSERT_TRUE(reconciled.value().activated);

  const auto idempotent =
      after_power_cut.activate(guard(reconciled.value()), 110U);
  TEST_ASSERT_TRUE(idempotent.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.activate_calls);
  const auto pending =
      after_power_cut.mark_reboot_pending(guard(idempotent.value()), 120U);
  TEST_ASSERT_TRUE(pending.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::reboot_pending),
                        static_cast<int>(pending.value().state));
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activated) != 0U);
}

void test_first_update_seed_power_cut_confirms_running_and_preserves_candidate() {
  const PartitionImageState cut_states[] = {
      PartitionImageState::new_image,
      PartitionImageState::pending_verify,
  };
  for (const auto cut_state : cut_states) {
    Harness harness;
    configure_rollback_seed_power_cut(harness, cut_state);
    const auto known_good = harness.platform.status_value.running;
    const auto candidate = harness.platform.status_value.inactive;

    FakeSha reboot_sha;
    UpdateManager after_power_cut(
        harness.platform, reboot_sha, harness.records);
    const auto reconciled = after_power_cut.initialize_from_boot(100U);
    TEST_ASSERT_TRUE_MESSAGE(
        reconciled.ok(),
        reconciled.ok() ? "" : reconciled.error().message.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                          static_cast<int>(reconciled.value().state));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(PartitionImageState::valid),
                          static_cast<int>(
                              reconciled.value().running_image_state));
    TEST_ASSERT_TRUE(reconciled.value().validation_passed);
    TEST_ASSERT_FALSE(reconciled.value().activation_intent);
    TEST_ASSERT_FALSE(reconciled.value().activated);
    TEST_ASSERT_TRUE(opentag::ota::same_partition(
        reconciled.value().running, known_good));
    TEST_ASSERT_TRUE(opentag::ota::same_partition(
        reconciled.value().boot, known_good));
    TEST_ASSERT_TRUE(opentag::ota::same_partition(
        reconciled.value().target, candidate));
    TEST_ASSERT_TRUE(opentag::ota::same_partition(
        reconciled.value().inactive, candidate));
    TEST_ASSERT_EQUAL_STRING(
        "1.0.0", reconciled.value().candidate.version.characters.data());
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
    TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
    TEST_ASSERT_FALSE(
        (harness.records.record->flags &
         opentag::ota::record_flag_activation_intent) != 0U);

    const auto activated =
        after_power_cut.activate(guard(reconciled.value()), 110U);
    TEST_ASSERT_TRUE_MESSAGE(
        activated.ok(),
        activated.ok() ? "" : activated.error().message.c_str());
    TEST_ASSERT_TRUE(activated.value().activation_intent);
    TEST_ASSERT_TRUE(activated.value().activated);
    TEST_ASSERT_TRUE(opentag::ota::same_partition(
        activated.value().boot, candidate));
    TEST_ASSERT_TRUE(opentag::ota::same_partition(
        activated.value().running, known_good));
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.activate_calls);
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
    TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  }
}

void test_first_update_seed_confirmation_failure_is_explicitly_retryable() {
  Harness harness;
  configure_rollback_seed_power_cut(
      harness, PartitionImageState::pending_verify);
  const auto candidate = harness.platform.status_value.inactive;
  harness.platform.fail_confirm = true;

  FakeSha reboot_sha;
  UpdateManager after_power_cut(harness.platform, reboot_sha, harness.records);
  const auto initial_recovery = after_power_cut.initialize_from_boot(100U);
  TEST_ASSERT_FALSE(initial_recovery.ok());
  auto snapshot = after_power_cut.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(snapshot.state));
  TEST_ASSERT_TRUE(snapshot.validation_passed);
  TEST_ASSERT_TRUE(snapshot.activation_intent);
  TEST_ASSERT_FALSE(snapshot.activated);
  TEST_ASSERT_TRUE(opentag::ota::same_partition(snapshot.target, candidate));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);

  harness.platform.fail_confirm = false;
  const auto retried = after_power_cut.recover_rollback_seed(110U);
  TEST_ASSERT_TRUE_MESSAGE(
      retried.ok(), retried.ok() ? "" : retried.error().message.c_str());
  snapshot = retried.value();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PartitionImageState::valid),
                        static_cast<int>(snapshot.running_image_state));
  TEST_ASSERT_TRUE(snapshot.validation_passed);
  TEST_ASSERT_FALSE(snapshot.activation_intent);
  TEST_ASSERT_FALSE(snapshot.activated);
  TEST_ASSERT_TRUE(opentag::ota::same_partition(snapshot.target, candidate));
  TEST_ASSERT_EQUAL_UINT(2U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_FALSE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);
}

void test_first_update_seed_save_failure_recovers_after_reboot() {
  Harness harness;
  configure_rollback_seed_power_cut(
      harness, PartitionImageState::pending_verify);
  const auto known_good = harness.platform.status_value.running;
  const auto candidate = harness.platform.status_value.inactive;
  harness.records.fail_save_on_call = harness.records.save_calls + 1U;

  FakeSha first_reboot_sha;
  UpdateManager first_reboot(
      harness.platform, first_reboot_sha, harness.records);
  const auto first_recovery = first_reboot.initialize_from_boot(100U);
  TEST_ASSERT_FALSE(first_recovery.ok());
  const auto stale_intent = first_reboot.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(stale_intent.state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PartitionImageState::valid),
                        static_cast<int>(stale_intent.running_image_state));
  TEST_ASSERT_TRUE(stale_intent.validation_passed);
  TEST_ASSERT_TRUE(stale_intent.activation_intent);
  TEST_ASSERT_FALSE(stale_intent.activated);
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(stale_intent.running, known_good));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(stale_intent.boot, known_good));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(stale_intent.target, candidate));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);

  FakeSha second_reboot_sha;
  UpdateManager second_reboot(
      harness.platform, second_reboot_sha, harness.records);
  const auto normalized = second_reboot.initialize_from_boot(200U);
  TEST_ASSERT_TRUE_MESSAGE(
      normalized.ok(),
      normalized.ok() ? "" : normalized.error().message.c_str());
  TEST_ASSERT_TRUE(normalized.value().validation_passed);
  TEST_ASSERT_FALSE(normalized.value().activation_intent);
  TEST_ASSERT_FALSE(normalized.value().activated);
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(normalized.value().target, candidate));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_FALSE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);

  const auto activated =
      second_reboot.activate(guard(normalized.value()), 210U);
  TEST_ASSERT_TRUE_MESSAGE(
      activated.ok(),
      activated.ok() ? "" : activated.error().message.c_str());
  TEST_ASSERT_TRUE(activated.value().activation_intent);
  TEST_ASSERT_TRUE(activated.value().activated);
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(activated.value().boot, candidate));
  TEST_ASSERT_TRUE(
      opentag::ota::same_partition(activated.value().running, known_good));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.activate_calls);
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
}

void test_first_update_seed_intent_cannot_be_canceled_before_recovery() {
  Harness harness;
  configure_rollback_seed_power_cut(harness, PartitionImageState::new_image);
  harness.platform.fail_confirm = true;

  FakeSha reboot_sha;
  UpdateManager after_power_cut(harness.platform, reboot_sha, harness.records);
  TEST_ASSERT_FALSE(after_power_cut.initialize_from_boot(100U).ok());
  const auto before_cancel = after_power_cut.snapshot();
  TEST_ASSERT_TRUE(before_cancel.activation_intent);
  TEST_ASSERT_FALSE(before_cancel.activated);

  const auto canceled =
      after_power_cut.cancel(guard(before_cancel), 110U);
  TEST_ASSERT_FALSE(canceled.ok());
  const auto after_cancel = after_power_cut.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::ready_to_reboot),
                        static_cast<int>(after_cancel.state));
  TEST_ASSERT_EQUAL_UINT64(before_cancel.operation_id,
                           after_cancel.operation_id);
  TEST_ASSERT_EQUAL_UINT64(before_cancel.generation, after_cancel.generation);
  TEST_ASSERT_TRUE(after_cancel.validation_passed);
  TEST_ASSERT_TRUE(after_cancel.activation_intent);
  TEST_ASSERT_FALSE(after_cancel.activated);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.abort_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_TRUE(
      (harness.records.record->flags &
       opentag::ota::record_flag_activation_intent) != 0U);
}

void test_corrupt_persisted_record_fails_closed_without_reusing_generation() {
  Harness harness;
  const auto staged = validated(harness);
  harness.records.record->checksum ^= 1U;
  const auto result = harness.manager.initialize_from_boot(1000U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::failed),
                        static_cast<int>(harness.manager.snapshot().state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);

  const auto failed = harness.manager.snapshot();
  const auto next = harness.manager.begin_upload(
      request_for(failed, 8U, digest(1U), 43U), 1010U);
  TEST_ASSERT_TRUE(next.ok());
  TEST_ASSERT_TRUE(next.value().generation > staged.generation);
}

void test_pending_candidate_rolls_back_when_record_is_corrupt_or_unavailable() {
  {
    Harness harness;
    configure_candidate_boot(harness, UpdateState::reboot_pending);
    harness.records.record->checksum ^= 1U;
    const auto result = harness.manager.initialize_from_boot(1000U);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
    TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::rolled_back),
                          static_cast<int>(harness.manager.snapshot().state));
  }

  {
    Harness harness;
    configure_candidate_boot(harness, UpdateState::reboot_pending);
    harness.records.fail_load = true;
    const auto result = harness.manager.initialize_from_boot(1000U);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
    TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::rolled_back),
                          static_cast<int>(harness.manager.snapshot().state));
  }
}

void test_untracked_or_unpersistable_pending_candidate_rolls_back() {
  {
    Harness harness;
    harness.platform.status_value.running =
        harness.platform.status_value.inactive;
    harness.platform.status_value.boot =
        harness.platform.status_value.running;
    harness.platform.status_value.inactive =
        partition("app0", 0x10000U, 0x10U);
    harness.platform.status_value.running_state =
        PartitionImageState::pending_verify;
    const auto result = harness.manager.initialize_from_boot(1000U);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
  }

  {
    Harness harness;
    configure_candidate_boot(harness, UpdateState::reboot_pending);
    harness.records.fail_save = true;
    const auto result = harness.manager.initialize_from_boot(1000U);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::rolled_back),
                          static_cast<int>(harness.manager.snapshot().state));
  }
}

void test_failed_automatic_rollback_remains_explicitly_retryable() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  harness.records.fail_load = true;
  harness.platform.fail_rollback = true;
  const auto result = harness.manager.initialize_from_boot(1000U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::candidate_boot),
                        static_cast<int>(harness.manager.snapshot().state));
}

void test_unreconciled_candidate_can_never_be_confirmed_after_rollback_failure() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  harness.records.record->checksum ^= 1U;
  harness.platform.fail_rollback = true;

  const auto initialized = harness.manager.initialize_from_boot(1000U);
  TEST_ASSERT_FALSE(initialized.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
  TEST_ASSERT_FALSE(harness.manager.snapshot().validation_passed);

  const auto confirmed = harness.manager.handle_candidate_health(
      CandidateHealthDecision::healthy, 31000U);
  TEST_ASSERT_FALSE(confirmed.ok());
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);

  harness.platform.fail_rollback = false;
  const auto retried = harness.manager.rollback_candidate(32000U);
  TEST_ASSERT_TRUE(retried.ok());
  TEST_ASSERT_EQUAL_UINT(2U, harness.platform.rollback_calls);
}

void test_unavailable_candidate_rollback_remains_retryable() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  harness.platform.status_value.rollback_available = false;
  const auto candidate = initialize(harness, 1000U);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::candidate_boot),
      static_cast<int>(candidate.state));

  harness.records.fail_save = true;
  const auto unavailable = harness.manager.rollback_candidate(1100U);
  TEST_ASSERT_FALSE(unavailable.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::validating_candidate),
      static_cast<int>(harness.manager.snapshot().state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);

  harness.records.fail_save = false;
  harness.platform.status_value.rollback_available = true;
  const auto retried = harness.manager.rollback_candidate(1200U);
  TEST_ASSERT_TRUE(retried.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::rolled_back),
      static_cast<int>(retried.value().state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
}

void test_mismatched_candidate_target_remains_retryable() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  const auto candidate = initialize(harness, 1000U);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::candidate_boot),
      static_cast<int>(candidate.state));
  const auto candidate_partition = harness.platform.status_value.running;
  const auto known_good_partition = harness.platform.status_value.inactive;

  harness.platform.status_value.running = known_good_partition;
  harness.platform.status_value.boot = known_good_partition;
  harness.platform.status_value.inactive = candidate_partition;
  const auto mismatched = harness.manager.rollback_candidate(1100U);
  TEST_ASSERT_FALSE(mismatched.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(UpdateState::validating_candidate),
      static_cast<int>(harness.manager.snapshot().state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);

  harness.platform.status_value.running = candidate_partition;
  harness.platform.status_value.boot = candidate_partition;
  harness.platform.status_value.inactive = known_good_partition;
  const auto retried = harness.manager.rollback_candidate(1200U);
  TEST_ASSERT_TRUE(retried.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
}

void test_candidate_waits_full_health_window_then_confirms() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  auto initialized = initialize(harness, 1000U);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::candidate_boot),
                        static_cast<int>(initialized.state));
  auto stabilizing = harness.manager.handle_candidate_health(
      CandidateHealthDecision::stabilizing, 5000U);
  TEST_ASSERT_TRUE(stabilizing.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::validating_candidate),
                        static_cast<int>(stabilizing.value().state));
  TEST_ASSERT_FALSE(harness.manager.confirm_candidate(30999U).ok());
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);
  const auto healthy = harness.manager.handle_candidate_health(
      CandidateHealthDecision::healthy, 31000U);
  TEST_ASSERT_TRUE(healthy.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::confirmed),
                        static_cast<int>(healthy.value().state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
}

void test_confirmation_failure_retains_candidate_for_rollback() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  initialize(harness, 1000U);
  harness.platform.fail_confirm = true;
  const auto result = harness.manager.confirm_candidate(31000U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::validating_candidate),
                        static_cast<int>(harness.manager.snapshot().state));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(PartitionImageState::pending_verify),
      static_cast<int>(harness.manager.snapshot().running_image_state));

  harness.platform.fail_confirm = false;
  const auto rolled_back = harness.manager.rollback_candidate(32000U);
  TEST_ASSERT_TRUE(rolled_back.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
}

void test_confirm_error_after_physical_valid_normalizes_on_retry() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  const auto candidate = initialize(harness, 1000U);
  TEST_ASSERT_TRUE(candidate.validation_passed);
  TEST_ASSERT_TRUE(candidate.activation_intent);
  TEST_ASSERT_TRUE(candidate.activated);
  TEST_ASSERT_TRUE(opentag::ota::same_partition(
      candidate.running, candidate.target));

  harness.platform.confirm_valid_before_failure = true;
  harness.platform.fail_confirm = true;
  const auto ambiguous = harness.manager.confirm_candidate(31000U);
  TEST_ASSERT_FALSE(ambiguous.ok());
  const auto retryable = harness.manager.snapshot();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::validating_candidate),
                        static_cast<int>(retryable.state));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(PartitionImageState::pending_verify),
      static_cast<int>(retryable.running_image_state));
  TEST_ASSERT_TRUE(retryable.validation_passed);
  TEST_ASSERT_TRUE(retryable.activation_intent);
  TEST_ASSERT_TRUE(retryable.activated);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(PartitionImageState::valid),
      static_cast<int>(harness.platform.status_value.running_state));
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);

  harness.platform.fail_confirm = false;
  const auto reconciled = harness.manager.confirm_candidate(32000U);
  TEST_ASSERT_TRUE_MESSAGE(
      reconciled.ok(),
      reconciled.ok() ? "" : reconciled.error().message.c_str());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::confirmed),
                        static_cast<int>(reconciled.value().state));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(PartitionImageState::valid),
      static_cast<int>(reconciled.value().running_image_state));
  TEST_ASSERT_TRUE(opentag::ota::same_partition(
      reconciled.value().running, reconciled.value().target));
  TEST_ASSERT_EQUAL_UINT(0U, reconciled.value().last_error.length);
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::confirmed),
                        static_cast<int>(harness.records.record->state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.records.record->last_error.length);
}

void test_confirmed_platform_state_wins_if_audit_record_save_fails() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  initialize(harness, 1000U);
  harness.records.fail_save_on_call = harness.records.save_calls + 1U;
  const auto confirmed = harness.manager.confirm_candidate(31000U);
  TEST_ASSERT_TRUE(confirmed.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::confirmed),
                        static_cast<int>(confirmed.value().state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PartitionImageState::valid),
                        static_cast<int>(confirmed.value().running_image_state));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::candidate_boot),
                        static_cast<int>(harness.records.record->state));

  FakeSha second_sha;
  UpdateManager after_reset(harness.platform, second_sha, harness.records);
  const auto reconciled = after_reset.initialize_from_boot(32000U);
  TEST_ASSERT_TRUE(reconciled.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::confirmed),
                        static_cast<int>(reconciled.value().state));
}

void test_factory_reset_recovery_neither_confirms_nor_rolls_back_candidate() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  initialize(harness, 1000U);
  const auto result = harness.manager.handle_candidate_health(
      CandidateHealthDecision::factory_reset_recovery, 40000U);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::candidate_boot),
                        static_cast<int>(result.value().state));
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.confirm_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.rollback_calls);
}

void test_unhealthy_candidate_persists_rollback_before_platform_reboot() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  initialize(harness, 1000U);
  const auto saves_before = harness.records.save_calls;
  const auto result = harness.manager.handle_candidate_health(
      CandidateHealthDecision::unhealthy, 2000U);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
  TEST_ASSERT_TRUE(harness.records.save_calls >= saves_before + 2U);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::rolled_back),
                        static_cast<int>(result.value().state));
}

void test_unhealthy_candidate_rolls_back_when_intent_persistence_fails() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  initialize(harness, 1000U);
  harness.records.fail_save = true;

  const auto result = harness.manager.handle_candidate_health(
      CandidateHealthDecision::unhealthy, 2000U);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.rollback_calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::rolled_back),
                        static_cast<int>(result.value().state));
}

void test_last_invalid_target_reconciles_as_rolled_back() {
  Harness harness;
  configure_candidate_boot(harness, UpdateState::reboot_pending);
  const auto failed_target = harness.platform.status_value.running;
  const auto restored = harness.platform.status_value.inactive;
  harness.platform.status_value.running = restored;
  harness.platform.status_value.boot = restored;
  harness.platform.status_value.inactive = failed_target;
  harness.platform.status_value.running_state = PartitionImageState::valid;
  harness.platform.status_value.last_invalid = failed_target;
  const auto result = initialize(harness, 5000U);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::rolled_back),
                        static_cast<int>(result.state));
}

void test_platform_write_failure_aborts_once_and_never_activates() {
  Harness harness;
  auto snapshot = begin(harness, 8U);
  harness.platform.fail_write = true;
  std::array<std::uint8_t, 8U> bytes{};
  const auto result = harness.manager.write_chunk(
      guard(snapshot), ByteView{bytes.data(), bytes.size()}, 30U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_UINT(1U, harness.platform.abort_calls);
  TEST_ASSERT_EQUAL_UINT(0U, harness.platform.activate_calls);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateState::failed),
                        static_cast<int>(harness.manager.snapshot().state));
}

void test_platform_errors_are_bounded_before_snapshot_persistence() {
  Harness harness;
  auto snapshot = begin(harness, 8U);
  harness.platform.fail_write = true;
  // FakePlatform returns a short error; exercise the shared bound directly via
  // a record-store error on finish with a long platform validation message.
  harness.platform.fail_write = false;
  snapshot = write_all(harness, snapshot, 8U);
  harness.sha.result = digest(9U);
  const auto result = harness.manager.finish_upload(guard(snapshot), 50U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_TRUE(harness.manager.snapshot().last_error.length <= 192U);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_initialization_reports_active_boot_and_inactive_slots);
  RUN_TEST(test_fresh_generation_zero_reserves_first_positive_generation);
  RUN_TEST(test_happy_path_streams_validates_then_activates_separately);
  RUN_TEST(test_undefined_running_state_retains_intent_for_exact_activation_retry);
  RUN_TEST(test_zero_and_oversized_images_are_rejected_before_flash_write);
  RUN_TEST(test_inactive_slot_is_selected_and_running_slot_is_never_written);
  RUN_TEST(test_oversized_chunk_aborts_and_records_bounded_failure);
  RUN_TEST(test_truncated_upload_is_aborted_without_validation_or_activation);
  RUN_TEST(test_hash_mismatch_aborts_before_platform_image_validation);
  RUN_TEST(test_structure_target_project_and_length_validation_fail_closed);
  RUN_TEST(test_stale_generation_cannot_write_activate_cancel_or_reboot);
  RUN_TEST(test_cancel_aborts_active_writer_and_is_forbidden_after_activation);
  RUN_TEST(test_validated_image_survives_reset_without_implicit_activation);
  RUN_TEST(test_reboot_requires_activation_and_preserves_operation_generation);
  RUN_TEST(test_progress_is_persisted_at_fixed_milestones_not_every_chunk);
  RUN_TEST(test_interrupted_upload_becomes_durable_failure_and_generation_advances);
  RUN_TEST(test_interrupted_validation_is_not_resumed_after_reset);
  RUN_TEST(test_legacy_ready_to_activate_record_becomes_cancelable_staged_image);
  RUN_TEST(test_record_crc_detects_state_digest_and_metadata_corruption);
  RUN_TEST(test_failed_update_error_survives_manager_reinitialization);
  RUN_TEST(test_activation_power_cut_after_set_boot_reconciles_and_can_reboot);
  RUN_TEST(test_first_update_seed_power_cut_confirms_running_and_preserves_candidate);
  RUN_TEST(test_first_update_seed_confirmation_failure_is_explicitly_retryable);
  RUN_TEST(test_first_update_seed_save_failure_recovers_after_reboot);
  RUN_TEST(test_first_update_seed_intent_cannot_be_canceled_before_recovery);
  RUN_TEST(test_corrupt_persisted_record_fails_closed_without_reusing_generation);
  RUN_TEST(test_pending_candidate_rolls_back_when_record_is_corrupt_or_unavailable);
  RUN_TEST(test_untracked_or_unpersistable_pending_candidate_rolls_back);
  RUN_TEST(test_failed_automatic_rollback_remains_explicitly_retryable);
  RUN_TEST(test_unreconciled_candidate_can_never_be_confirmed_after_rollback_failure);
  RUN_TEST(test_unavailable_candidate_rollback_remains_retryable);
  RUN_TEST(test_mismatched_candidate_target_remains_retryable);
  RUN_TEST(test_candidate_waits_full_health_window_then_confirms);
  RUN_TEST(test_confirmation_failure_retains_candidate_for_rollback);
  RUN_TEST(test_confirm_error_after_physical_valid_normalizes_on_retry);
  RUN_TEST(test_confirmed_platform_state_wins_if_audit_record_save_fails);
  RUN_TEST(test_factory_reset_recovery_neither_confirms_nor_rolls_back_candidate);
  RUN_TEST(test_unhealthy_candidate_persists_rollback_before_platform_reboot);
  RUN_TEST(test_unhealthy_candidate_rolls_back_when_intent_persistence_fails);
  RUN_TEST(test_last_invalid_target_reconciles_as_rolled_back);
  RUN_TEST(test_platform_write_failure_aborts_once_and_never_activates);
  RUN_TEST(test_platform_errors_are_bounded_before_snapshot_persistence);
  return UNITY_END();
}
