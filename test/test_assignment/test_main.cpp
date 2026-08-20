#include <unity.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "core/result.hpp"
#include "domain/printer.hpp"
#include "integrations/backend_capabilities.hpp"
#include "integrations/printer_assignment.hpp"
#include "services/toolhead_assignment_service.hpp"

namespace {

using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::domain::Printer;
using opentag::domain::PrinterState;
using opentag::domain::SpoolId;
using opentag::domain::Toolhead;
using opentag::integrations::BackendCapabilities;
using opentag::integrations::IPrinterAssignmentService;
using opentag::services::AssignmentOutcome;
using opentag::services::AssignmentRequest;
using opentag::services::ToolheadAssignmentService;
using opentag::services::UnassignmentRequest;

Printer printer(
    PrinterState state,
    std::optional<SpoolId> assigned = std::nullopt) {
  Printer result;
  result.id = "xl-stable-id";
  result.display_name = "Workshop XL";
  result.state = state;
  auto toolhead = Toolhead::from_zero_based_backend(result.id, 2);
  toolhead.assigned_spool = assigned;
  result.toolheads.push_back(toolhead);
  return result;
}

class FakeBackend final : public IPrinterAssignmentService {
 public:
  std::vector<Printer> current_printers{printer(PrinterState::idle)};
  std::optional<SpoolId> readback_spool;
  int list_calls{0};
  int readback_calls{0};
  int assign_calls{0};
  int unassign_calls{0};
  std::string mutated_printer;
  int mutated_toolhead{-1};
  SpoolId mutated_spool{0};

  Result<std::vector<Printer>> list_printers() override {
    ++list_calls;
    return Result<std::vector<Printer>>::success(current_printers);
  }

  Result<std::vector<Toolhead>> get_toolheads(const std::string&) override {
    ++readback_calls;
    auto result = current_printers.front().toolheads;
    result.front().assigned_spool = readback_spool;
    return Result<std::vector<Toolhead>>::success(std::move(result));
  }

  Result<void> assign_spool(
      const std::string& printer_id,
      int backend_toolhead_id,
      SpoolId spool_id) override {
    ++assign_calls;
    mutated_printer = printer_id;
    mutated_toolhead = backend_toolhead_id;
    mutated_spool = spool_id;
    return Result<void>::success();
  }

  Result<void> unassign_spool(
      const std::string& printer_id,
      int backend_toolhead_id) override {
    ++unassign_calls;
    mutated_printer = printer_id;
    mutated_toolhead = backend_toolhead_id;
    return Result<void>::success();
  }

  BackendCapabilities capabilities() const override { return {}; }
};

AssignmentRequest request() {
  AssignmentRequest value;
  value.printer_id = "xl-stable-id";
  value.backend_toolhead_id = 2;
  value.spool_id = 147;
  return value;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_occupied_toolhead_requires_explicit_replacement_confirmation() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::idle, 104)};
  ToolheadAssignmentService service(backend);

  const auto result = service.assign(request());

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(AssignmentOutcome::replacement_confirmation_required),
      static_cast<int>(result.value().outcome));
  TEST_ASSERT_EQUAL_INT32(104, *result.value().previous_spool_id);
  TEST_ASSERT_EQUAL_INT(0, backend.assign_calls);
  TEST_ASSERT_EQUAL_INT(0, backend.readback_calls);
}

void test_active_print_blocks_mapping_without_advanced_override() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::printing)};
  ToolheadAssignmentService service(backend);

  const auto result = service.assign(request());

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(AssignmentOutcome::active_print_override_required),
      static_cast<int>(result.value().outcome));
  TEST_ASSERT_EQUAL_INT(0, backend.assign_calls);
}

void test_unknown_printer_state_requires_advanced_override() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::unknown)};
  ToolheadAssignmentService service(backend);

  const auto result = service.assign(request());

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(AssignmentOutcome::printer_state_override_required),
      static_cast<int>(result.value().outcome));
  TEST_ASSERT_EQUAL_INT(0, backend.assign_calls);
  TEST_ASSERT_EQUAL_INT(0, backend.readback_calls);
}

void test_confirmed_active_replacement_executes_once_and_verifies_exact_spool() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::paused, 104)};
  backend.readback_spool = 147;
  ToolheadAssignmentService service(backend);
  auto input = request();
  input.replace_occupied_confirmed = true;
  input.advanced_active_print_override = true;

  const auto result = service.assign(input);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_TRUE(result.value().verified());
  TEST_ASSERT_EQUAL_INT(1, backend.list_calls);
  TEST_ASSERT_EQUAL_INT(1, backend.assign_calls);
  TEST_ASSERT_EQUAL_INT(1, backend.readback_calls);
  TEST_ASSERT_EQUAL_STRING("xl-stable-id", backend.mutated_printer.c_str());
  TEST_ASSERT_EQUAL_INT(2, backend.mutated_toolhead);
  TEST_ASSERT_EQUAL_INT32(147, backend.mutated_spool);
}

void test_http_success_with_wrong_readback_is_not_success() {
  FakeBackend backend;
  backend.readback_spool = 99;
  ToolheadAssignmentService service(backend);

  const auto result = service.assign(request());

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::invalid_response),
      static_cast<int>(result.error().category));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, result.error().message.find("verification failed"));
  TEST_ASSERT_EQUAL_INT(1, backend.assign_calls);
  TEST_ASSERT_EQUAL_INT(1, backend.readback_calls);
}

void test_same_spool_is_idempotent_without_a_write() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::printing, 147)};
  ToolheadAssignmentService service(backend);

  const auto result = service.assign(request());

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(
      static_cast<int>(AssignmentOutcome::already_in_expected_state),
      static_cast<int>(result.value().outcome));
  TEST_ASSERT_TRUE(result.value().verified());
  TEST_ASSERT_EQUAL_INT(0, backend.assign_calls);
}

void test_unassignment_is_re_read_and_verified_empty() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::idle, 204)};
  backend.readback_spool.reset();
  ToolheadAssignmentService service(backend);
  UnassignmentRequest input;
  input.printer_id = "xl-stable-id";
  input.backend_toolhead_id = 2;

  const auto result = service.unassign(input);

  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_TRUE(result.value().verified());
  TEST_ASSERT_EQUAL_INT32(204, *result.value().previous_spool_id);
  TEST_ASSERT_EQUAL_INT(1, backend.unassign_calls);
  TEST_ASSERT_EQUAL_INT(1, backend.readback_calls);
}

void test_missing_stable_printer_id_aborts_before_mutation() {
  FakeBackend backend;
  backend.current_printers.clear();
  ToolheadAssignmentService service(backend);

  const auto result = service.assign(request());

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(0, backend.assign_calls);
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::conflict),
      static_cast<int>(result.error().category));
}

void test_stale_assignment_confirmation_aborts_before_mutation() {
  FakeBackend backend;
  backend.current_printers = {printer(PrinterState::idle, 204)};
  ToolheadAssignmentService service(backend);
  auto input = request();
  input.replace_occupied_confirmed = true;
  input.precondition.supplied = true;
  input.precondition.expected_previous_spool_id = 104;
  input.precondition.expected_printer_state = PrinterState::idle;

  const auto result = service.assign(input);

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(0, backend.assign_calls);
  TEST_ASSERT_EQUAL(
      static_cast<int>(ErrorCategory::conflict),
      static_cast<int>(result.error().category));
  TEST_ASSERT_NOT_EQUAL(
      std::string::npos, result.error().message.find("stale"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_occupied_toolhead_requires_explicit_replacement_confirmation);
  RUN_TEST(test_active_print_blocks_mapping_without_advanced_override);
  RUN_TEST(test_unknown_printer_state_requires_advanced_override);
  RUN_TEST(test_confirmed_active_replacement_executes_once_and_verifies_exact_spool);
  RUN_TEST(test_http_success_with_wrong_readback_is_not_success);
  RUN_TEST(test_same_spool_is_idempotent_without_a_write);
  RUN_TEST(test_unassignment_is_re_read_and_verified_empty);
  RUN_TEST(test_missing_stable_printer_id_aborts_before_mutation);
  RUN_TEST(test_stale_assignment_confirmation_aborts_before_mutation);
  return UNITY_END();
}
