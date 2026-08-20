#include "hardware/nfc/st25r3916b/service.hpp"

#include <utility>

namespace opentag::hardware::nfc::st25r3916b {
namespace {

core::Error invalid_timeout() {
  return {
      core::ErrorCategory::configuration,
      "ST25R3916B bring-up timeout must be non-zero",
      false,
  };
}

}  // namespace

core::Result<void> Service::fail(core::Error error, std::uint32_t timeout_ms) {
  (void)backend_.set_rf_field(false, timeout_ms);
  (void)backend_.set_power(false, timeout_ms);
  diagnostics_.state = BringUpState::fault;
  diagnostics_.last_error = error;
  return core::Result<void>::failure(std::move(error));
}

core::Result<void> Service::start(std::uint32_t step_timeout_ms) {
  if (step_timeout_ms == 0U) {
    diagnostics_.state = BringUpState::fault;
    diagnostics_.last_error = invalid_timeout();
    return core::Result<void>::failure(*diagnostics_.last_error);
  }
  diagnostics_.identity.reset();
  diagnostics_.last_error.reset();

  diagnostics_.state = BringUpState::powering;
  const auto powered = backend_.set_power(true, step_timeout_ms);
  if (!powered.ok()) return fail(powered.error(), step_timeout_ms);

  diagnostics_.state = BringUpState::resetting;
  const auto reset = backend_.reset(step_timeout_ms);
  if (!reset.ok()) return fail(reset.error(), step_timeout_ms);

  diagnostics_.state = BringUpState::identifying;
  const auto identity = backend_.read_and_validate_identity(step_timeout_ms);
  if (!identity.ok()) return fail(identity.error(), step_timeout_ms);
  diagnostics_.identity = identity.value();

  diagnostics_.state = BringUpState::configuring_irq;
  const auto irq = backend_.configure_interrupt(step_timeout_ms);
  if (!irq.ok()) return fail(irq.error(), step_timeout_ms);

  diagnostics_.state = BringUpState::initializing_rfal;
  const auto rfal = backend_.initialize_rfal(step_timeout_ms);
  if (!rfal.ok()) return fail(rfal.error(), step_timeout_ms);

  diagnostics_.state = BringUpState::enabling_field;
  const auto field = backend_.set_rf_field(true, step_timeout_ms);
  if (!field.ok()) return fail(field.error(), step_timeout_ms);

  diagnostics_.state = BringUpState::ready;
  return core::Result<void>::success();
}

core::Result<void> Service::recover(std::uint32_t step_timeout_ms) {
  if (step_timeout_ms == 0U) {
    return core::Result<void>::failure(invalid_timeout());
  }
  ++diagnostics_.recovery_count;
  (void)backend_.set_rf_field(false, step_timeout_ms);
  (void)backend_.set_power(false, step_timeout_ms);
  return start(step_timeout_ms);
}

core::Result<void> Service::stop(std::uint32_t step_timeout_ms) {
  if (step_timeout_ms == 0U) {
    return core::Result<void>::failure(invalid_timeout());
  }
  const auto field = backend_.set_rf_field(false, step_timeout_ms);
  const auto power = backend_.set_power(false, step_timeout_ms);
  diagnostics_.identity.reset();
  if (!field.ok()) {
    diagnostics_.state = BringUpState::fault;
    diagnostics_.last_error = field.error();
    return core::Result<void>::failure(field.error());
  }
  if (!power.ok()) {
    diagnostics_.state = BringUpState::fault;
    diagnostics_.last_error = power.error();
    return core::Result<void>::failure(power.error());
  }
  diagnostics_.state = BringUpState::off;
  diagnostics_.last_error.reset();
  return core::Result<void>::success();
}

}  // namespace opentag::hardware::nfc::st25r3916b
