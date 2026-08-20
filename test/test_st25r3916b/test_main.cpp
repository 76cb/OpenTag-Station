#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "core/result.hpp"
#include "hardware/nfc/st25r3916b/service.hpp"

namespace {

using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::hardware::nfc::st25r3916b::BringUpState;
using opentag::hardware::nfc::st25r3916b::ChipIdentity;
using opentag::hardware::nfc::st25r3916b::IFrontendBackend;
using opentag::hardware::nfc::st25r3916b::Service;

class FrontendBackend final : public IFrontendBackend {
 public:
  std::vector<std::string> calls;
  std::string fail_on;

  Result<void> set_power(bool enabled, std::uint32_t) override {
    return operation(enabled ? "power_on" : "power_off");
  }

  Result<void> reset(std::uint32_t) override { return operation("reset"); }

  Result<ChipIdentity> read_and_validate_identity(std::uint32_t) override {
    calls.emplace_back("identity");
    if (fail_on == "identity") return Result<ChipIdentity>::failure(error());
    return Result<ChipIdentity>::success({0x05U, 0x01U});
  }

  Result<void> configure_interrupt(std::uint32_t) override { return operation("irq"); }
  Result<void> initialize_rfal(std::uint32_t) override { return operation("rfal"); }
  Result<void> set_rf_field(bool enabled, std::uint32_t) override {
    return operation(enabled ? "field_on" : "field_off");
  }

 private:
  static opentag::core::Error error() {
    return {ErrorCategory::nfc_communication, "injected frontend failure", true};
  }

  Result<void> operation(const char* name) {
    calls.emplace_back(name);
    return fail_on == name ? Result<void>::failure(error()) : Result<void>::success();
  }
};

void assert_calls(const FrontendBackend& backend, const std::vector<std::string>& expected) {
  TEST_ASSERT_EQUAL_UINT(expected.size(), backend.calls.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    TEST_ASSERT_EQUAL_STRING(expected[index].c_str(), backend.calls[index].c_str());
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_bring_up_follows_required_hardware_sequence() {
  FrontendBackend backend;
  Service service(backend);
  TEST_ASSERT_TRUE(service.start(100U).ok());
  assert_calls(backend, {"power_on", "reset", "identity", "irq", "rfal", "field_on"});
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BringUpState::ready),
      static_cast<int>(service.diagnostics().state));
  TEST_ASSERT_TRUE(service.diagnostics().identity.has_value());
  TEST_ASSERT_EQUAL_HEX8(0x05U, service.diagnostics().identity->product);
}

void test_bring_up_failure_leaves_frontend_safe_and_diagnostic() {
  FrontendBackend backend;
  backend.fail_on = "rfal";
  Service service(backend);
  const auto result = service.start(100U);
  TEST_ASSERT_FALSE(result.ok());
  assert_calls(
      backend,
      {"power_on", "reset", "identity", "irq", "rfal", "field_off", "power_off"});
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BringUpState::fault),
      static_cast<int>(service.diagnostics().state));
  TEST_ASSERT_TRUE(service.diagnostics().last_error.has_value());
}

void test_recovery_repeats_complete_sequence_without_reboot() {
  FrontendBackend backend;
  backend.fail_on = "irq";
  Service service(backend);
  TEST_ASSERT_FALSE(service.start(100U).ok());
  backend.fail_on.clear();
  backend.calls.clear();
  TEST_ASSERT_TRUE(service.recover(100U).ok());
  assert_calls(
      backend,
      {"field_off", "power_off", "power_on", "reset", "identity", "irq", "rfal", "field_on"});
  TEST_ASSERT_EQUAL_UINT(1U, service.diagnostics().recovery_count);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BringUpState::ready),
      static_cast<int>(service.diagnostics().state));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bring_up_follows_required_hardware_sequence);
  RUN_TEST(test_bring_up_failure_leaves_frontend_safe_and_diagnostic);
  RUN_TEST(test_recovery_repeats_complete_sequence_without_reboot);
  return UNITY_END();
}
