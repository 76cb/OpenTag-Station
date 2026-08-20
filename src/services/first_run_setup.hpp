#pragma once

#include <cstdint>

#include "config/configuration_service.hpp"
#include "core/result.hpp"

namespace opentag::services {

enum class SetupStep : std::uint8_t {
  welcome,
  wifi,
  spoolman,
  filabridge,
  printer_selection,
  scale_calibration,
  nfc_status,
  ready,
};

[[nodiscard]] const char* to_string(SetupStep step);

class FirstRunSetup {
 public:
  explicit FirstRunSetup(config::ConfigurationService& configuration)
      : configuration_(configuration) {}

  [[nodiscard]] SetupStep current() const { return current_; }
  [[nodiscard]] bool complete() const;
  [[nodiscard]] bool step_complete(SetupStep step) const;
  [[nodiscard]] core::Result<void> mark_complete(SetupStep step);
  [[nodiscard]] core::Result<void> go_to(SetupStep step);
  [[nodiscard]] core::Result<void> next();
  [[nodiscard]] core::Result<void> previous();

 private:
  static constexpr std::uint32_t step_bit(SetupStep step) {
    return 1U << static_cast<std::uint8_t>(step);
  }

  config::ConfigurationService& configuration_;
  SetupStep current_{SetupStep::welcome};
};

}  // namespace opentag::services
