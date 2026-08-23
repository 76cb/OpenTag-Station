#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "hardware/nfc/st25r3916b/frontend_backend.hpp"
#include "hardware/nfc/st25r3916b/service.hpp"
#include "platform/rfal/rfal_platform_contract.hpp"

namespace {

using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::hardware::nfc::st25r3916b::BringUpState;
using opentag::hardware::nfc::st25r3916b::ChipIdentity;
using DirectFrontendBackend =
    opentag::hardware::nfc::st25r3916b::FrontendBackend;
using opentag::hardware::nfc::st25r3916b::FrontendBackendDiagnostics;
using opentag::hardware::nfc::st25r3916b::FrontendControl;
using opentag::hardware::nfc::st25r3916b::FrontendPowerMode;
using opentag::hardware::nfc::st25r3916b::FrontendResetMode;
using opentag::hardware::nfc::st25r3916b::FrontendTiming;
using opentag::hardware::nfc::st25r3916b::IFrontendBackend;
using opentag::hardware::nfc::st25r3916b::IRfalDriver;
using opentag::hardware::nfc::st25r3916b::Service;
using opentag::hardware::nfc::st25r3916b::decode_identity_register;
using opentag::platform::rfal::IRfalPlatform;

class FrontendBackend final : public IFrontendBackend {
 public:
  std::vector<std::string> calls;
  std::string fail_on;

  Result<void> set_power(bool enabled, std::uint32_t) override {
    return operation(enabled ? "power_on" : "power_off");
  }

  Result<void> reset_to_defaults(std::uint32_t) override {
    return operation("reset");
  }

  Result<ChipIdentity> read_and_validate_identity(std::uint32_t) override {
    calls.emplace_back("identity");
    if (fail_on == "identity") return Result<ChipIdentity>::failure(error());
    return Result<ChipIdentity>::success({0x06U, 0x01U});
  }

  Result<void> configure_interrupt(std::uint32_t) override {
    return operation("irq");
  }
  Result<void> initialize_rfal(std::uint32_t) override {
    return operation("rfal");
  }
  Result<void> set_rf_field(bool enabled, std::uint32_t) override {
    return operation(enabled ? "field_on" : "field_off");
  }

  FrontendBackendDiagnostics diagnostics() const override { return {}; }

 private:
  static opentag::core::Error error() {
    return {ErrorCategory::nfc_communication, "injected frontend failure", true};
  }

  Result<void> operation(const char* name) {
    calls.emplace_back(name);
    return fail_on == name
        ? Result<void>::failure(error())
        : Result<void>::success();
  }
};

class FakePlatform final : public IRfalPlatform {
 public:
  bool initialize() override { return initialize_ok; }

  bool transfer(
      const std::uint8_t* transmit,
      std::uint8_t* receive,
      std::size_t length) override {
    if (!transfer_ok || transmit == nullptr || receive == nullptr) return false;
    if (length == 1U && transmit[0] == 0xC1U) ++set_default_commands;
    for (std::size_t index = 0U; index < length; ++index) receive[index] = 0U;
    if (length == 2U && transmit[0] == 0x7FU) {
      receive[1] = identity_raw;
    } else if (length == 5U && transmit[0] == 0x5AU) {
      receive[1] = irq_status_main;
      line_active = false;
    } else if (length == 2U && transmit[0] == 0x02U) {
      operation_control = transmit[1];
      if (operation_control == 0x80U && generate_irq) {
        latched = true;
        line_active = true;
        ++irq_count_value;
        last_irq_ms = now_ms;
      }
      if (operation_control == 0U && irq_status_main == 0U) line_active = false;
    }
    return true;
  }

  void select(bool active) override { selected = active; }
  bool external_power_control_available() const override {
    return external_power_available;
  }
  bool external_reset_available() const override {
    return external_reset_available_value;
  }
  void set_external_power(bool active) override {
    powered = active;
    ++power_control_calls;
  }
  void set_external_reset(bool active) override {
    reset_asserted = active;
    ++reset_control_calls;
  }
  bool interrupt_pending() const override { return line_active || latched; }
  bool interrupt_line_active() const override { return line_active; }
  bool interrupt_latched() const override { return latched; }
  std::uint32_t interrupt_count() const override { return irq_count_value; }
  std::uint32_t last_interrupt_at_ms() const override { return last_irq_ms; }
  void acknowledge_interrupt() override { latched = false; }
  std::uint32_t ticks_ms() const override { return now_ms; }
  void delay_ms(std::uint32_t milliseconds) override { now_ms += milliseconds; }
  bool lock_bus(std::uint32_t) override { return lock_ok; }
  void unlock_bus() override {}
  void enter_critical() override {}
  void leave_critical() override {}

  bool initialize_ok{true};
  bool external_power_available{true};
  bool external_reset_available_value{true};
  bool transfer_ok{true};
  bool lock_ok{true};
  bool generate_irq{true};
  bool selected{false};
  bool powered{false};
  bool reset_asserted{false};
  bool line_active{false};
  bool latched{false};
  std::uint8_t identity_raw{0x31U};
  std::uint8_t irq_status_main{0x80U};
  std::uint8_t operation_control{0U};
  std::uint32_t power_control_calls{0U};
  std::uint32_t reset_control_calls{0U};
  std::uint32_t set_default_commands{0U};
  std::uint32_t irq_count_value{0U};
  std::uint32_t last_irq_ms{0U};
  std::uint32_t now_ms{0U};
};

class FakeRfal final : public IRfalDriver {
 public:
  Result<void> initialize(std::uint32_t) override {
    ++initialize_calls;
    return initialize_ok
        ? Result<void>::success()
        : Result<void>::failure({
              ErrorCategory::nfc_communication,
              "injected RFAL initialization failure",
              false});
  }

  Result<void> set_rf_field(bool enabled, std::uint32_t) override {
    ++field_calls;
    if (!field_ok) {
      return Result<void>::failure({
          ErrorCategory::nfc_communication,
          "injected RF field failure",
          false});
    }
    field_enabled = enabled;
    return Result<void>::success();
  }

  bool initialize_ok{true};
  bool field_ok{true};
  bool field_enabled{false};
  std::uint32_t initialize_calls{0U};
  std::uint32_t field_calls{0U};
};

constexpr FrontendTiming test_timing{2U, 3U, 4U, 10U, 1U};
constexpr FrontendTiming module_timing{2U, 0U, 4U, 10U, 1U};
constexpr FrontendControl module_control{
    FrontendResetMode::set_default_command, FrontendPowerMode::always_on};

void prepare_direct_backend(DirectFrontendBackend& backend) {
  TEST_ASSERT_TRUE(backend.set_power(true, 100U).ok());
  TEST_ASSERT_TRUE(backend.reset_to_defaults(100U).ok());
  TEST_ASSERT_TRUE(backend.read_and_validate_identity(100U).ok());
}

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
  TEST_ASSERT_EQUAL_HEX8(0x06U, service.diagnostics().identity->product);
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

void test_each_bring_up_stage_failure_disables_field_and_power() {
  const std::vector<std::string> stages{
      "power_on", "reset", "identity", "irq", "rfal", "field_on"};
  for (const auto& stage : stages) {
    FrontendBackend backend;
    backend.fail_on = stage;
    Service service(backend);
    TEST_ASSERT_FALSE_MESSAGE(service.start(100U).ok(), stage.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "field_off", backend.calls[backend.calls.size() - 2U].c_str());
    TEST_ASSERT_EQUAL_STRING("power_off", backend.calls.back().c_str());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(BringUpState::fault),
        static_cast<int>(service.diagnostics().state));
  }
}

void test_stop_disables_field_then_power() {
  FrontendBackend backend;
  Service service(backend);
  TEST_ASSERT_TRUE(service.start(100U).ok());
  backend.calls.clear();
  TEST_ASSERT_TRUE(service.stop(100U).ok());
  assert_calls(backend, {"field_off", "power_off"});
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(BringUpState::off),
      static_cast<int>(service.diagnostics().state));
}

void test_identity_decoder_accepts_only_st25r3916b_product_code() {
  const auto valid = decode_identity_register(0x31U);
  TEST_ASSERT_TRUE(valid.ok());
  TEST_ASSERT_EQUAL_UINT8(6U, valid.value().product);
  TEST_ASSERT_EQUAL_UINT8(1U, valid.value().revision);

  TEST_ASSERT_FALSE(decode_identity_register(0x00U).ok());
  TEST_ASSERT_FALSE(decode_identity_register(0xFFU).ok());
  const auto wrong = decode_identity_register(0x29U);
  TEST_ASSERT_FALSE(wrong.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::nfc_communication),
      static_cast<int>(wrong.error().category));
}

void test_direct_backend_validates_spi_identity_and_real_irq_transition() {
  FakePlatform platform;
  FakeRfal rfal;
  DirectFrontendBackend backend(platform, test_timing, &rfal);
  prepare_direct_backend(backend);
  TEST_ASSERT_TRUE(backend.configure_interrupt(100U).ok());
  TEST_ASSERT_TRUE(backend.configure_interrupt(100U).ok());
  TEST_ASSERT_TRUE(backend.initialize_rfal(100U).ok());
  TEST_ASSERT_TRUE(backend.set_rf_field(true, 100U).ok());

  const auto diagnostics = backend.diagnostics();
  TEST_ASSERT_TRUE(diagnostics.power_enabled);
  TEST_ASSERT_TRUE(diagnostics.spi_ok);
  TEST_ASSERT_TRUE(diagnostics.irq_configured);
  TEST_ASSERT_EQUAL_UINT32(2U, diagnostics.irq_count);
  TEST_ASSERT_TRUE(diagnostics.rfal_initialized);
  TEST_ASSERT_TRUE(diagnostics.rf_field_enabled);
  TEST_ASSERT_FALSE(diagnostics.irq_line_state);
  TEST_ASSERT_FALSE(diagnostics.irq_latched);
  TEST_ASSERT_EQUAL_UINT8(0U, platform.operation_control);
}

void test_direct_backend_rejects_floating_bus_patterns_and_spi_failure() {
  for (const auto raw : {0x00U, 0xFFU, 0x29U}) {
    FakePlatform platform;
    platform.identity_raw = static_cast<std::uint8_t>(raw);
    DirectFrontendBackend backend(platform, test_timing);
    TEST_ASSERT_TRUE(backend.set_power(true, 100U).ok());
    TEST_ASSERT_TRUE(backend.reset_to_defaults(100U).ok());
    TEST_ASSERT_FALSE(backend.read_and_validate_identity(100U).ok());
    TEST_ASSERT_FALSE(backend.diagnostics().spi_ok);
  }
  FakePlatform platform;
  DirectFrontendBackend backend(platform, test_timing);
  TEST_ASSERT_TRUE(backend.set_power(true, 100U).ok());
  TEST_ASSERT_TRUE(backend.reset_to_defaults(100U).ok());
  platform.transfer_ok = false;
  TEST_ASSERT_FALSE(backend.read_and_validate_identity(100U).ok());
}

void test_direct_backend_distinguishes_broken_irq_and_missing_rfal() {
  FakePlatform platform;
  platform.generate_irq = false;
  platform.irq_status_main = 0U;
  DirectFrontendBackend backend(platform, test_timing);
  prepare_direct_backend(backend);
  const auto irq = backend.configure_interrupt(100U);
  TEST_ASSERT_FALSE(irq.ok());
  TEST_ASSERT_FALSE(backend.diagnostics().irq_configured);
  TEST_ASSERT_EQUAL_UINT8(0U, platform.operation_control);

  platform.generate_irq = true;
  platform.irq_status_main = 0x80U;
  TEST_ASSERT_TRUE(backend.configure_interrupt(100U).ok());
  const auto rfal = backend.initialize_rfal(100U);
  TEST_ASSERT_FALSE(rfal.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::configuration),
      static_cast<int>(rfal.error().category));
  TEST_ASSERT_FALSE(backend.set_rf_field(true, 100U).ok());
}

void test_pin_validation_preserves_required_control_lines() {
  opentag::boards::St25r3916bPins pins{
      1, 2, 3, 4, 5, -1, -1, false, false};
  TEST_ASSERT_TRUE(pins.complete());

  pins.external_reset_required = true;
  TEST_ASSERT_FALSE(pins.complete());
  pins.reset = 6;
  TEST_ASSERT_TRUE(pins.complete());

  pins.external_power_control_required = true;
  TEST_ASSERT_FALSE(pins.complete());
  pins.power_enable = 7;
  TEST_ASSERT_TRUE(pins.complete());

  pins.external_reset_required = false;
  TEST_ASSERT_FALSE(pins.complete());
}

void test_elehouse_module_uses_software_reset_without_fake_gpio() {
  FakePlatform platform;
  platform.external_power_available = false;
  platform.external_reset_available_value = false;
  DirectFrontendBackend backend(
      platform, module_timing, nullptr, module_control);

  TEST_ASSERT_TRUE(backend.set_power(true, 100U).ok());
  TEST_ASSERT_FALSE(backend.reset_to_defaults(4U).ok());
  TEST_ASSERT_EQUAL_UINT32(0U, platform.set_default_commands);
  TEST_ASSERT_TRUE(backend.reset_to_defaults(100U).ok());
  TEST_ASSERT_EQUAL_UINT32(1U, platform.set_default_commands);
  TEST_ASSERT_EQUAL_UINT32(0U, platform.power_control_calls);
  TEST_ASSERT_EQUAL_UINT32(0U, platform.reset_control_calls);
  TEST_ASSERT_TRUE(backend.diagnostics().power_enabled);
  TEST_ASSERT_FALSE(backend.diagnostics().external_power_control_available);
  TEST_ASSERT_FALSE(backend.diagnostics().external_reset_available);
  TEST_ASSERT_TRUE(backend.diagnostics().software_reset);

  TEST_ASSERT_FALSE(backend.set_power(false, 4U).ok());
  TEST_ASSERT_EQUAL_UINT32(1U, platform.set_default_commands);
  TEST_ASSERT_TRUE(backend.set_power(false, 100U).ok());
  TEST_ASSERT_EQUAL_UINT32(2U, platform.set_default_commands);
  TEST_ASSERT_TRUE(backend.diagnostics().power_enabled);
  TEST_ASSERT_FALSE(backend.read_and_validate_identity(100U).ok());
}

void test_control_mode_rejects_missing_or_invented_gpio() {
  FakePlatform platform;
  platform.external_power_available = false;
  platform.external_reset_available_value = false;
  DirectFrontendBackend external_backend(platform, test_timing);
  TEST_ASSERT_FALSE(external_backend.set_power(true, 100U).ok());

  FakePlatform invented;
  DirectFrontendBackend module_backend(
      invented, module_timing, nullptr, module_control);
  TEST_ASSERT_FALSE(module_backend.set_power(true, 100U).ok());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bring_up_follows_required_hardware_sequence);
  RUN_TEST(test_bring_up_failure_leaves_frontend_safe_and_diagnostic);
  RUN_TEST(test_recovery_repeats_complete_sequence_without_reboot);
  RUN_TEST(test_each_bring_up_stage_failure_disables_field_and_power);
  RUN_TEST(test_stop_disables_field_then_power);
  RUN_TEST(test_identity_decoder_accepts_only_st25r3916b_product_code);
  RUN_TEST(test_direct_backend_validates_spi_identity_and_real_irq_transition);
  RUN_TEST(test_direct_backend_rejects_floating_bus_patterns_and_spi_failure);
  RUN_TEST(test_direct_backend_distinguishes_broken_irq_and_missing_rfal);
  RUN_TEST(test_pin_validation_preserves_required_control_lines);
  RUN_TEST(test_elehouse_module_uses_software_reset_without_fake_gpio);
  RUN_TEST(test_control_mode_rejects_missing_or_invented_gpio);
  return UNITY_END();
}
