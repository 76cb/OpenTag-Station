#pragma once

#include <cstdint>

#include "platform/rfal/rfal_platform_contract.hpp"

#if defined(ARDUINO_ARCH_ESP32)

#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace opentag::platform::rfal {

struct Esp32RfalPins {
  std::int8_t sck{-1};
  std::int8_t mosi{-1};
  std::int8_t miso{-1};
  std::int8_t chip_select{-1};
  std::int8_t interrupt{-1};
  std::int8_t reset{-1};
  std::int8_t power_enable{-1};

  [[nodiscard]] bool complete() const {
    return sck >= 0 && mosi >= 0 && miso >= 0 && chip_select >= 0 &&
           interrupt >= 0 && reset >= 0;
  }
};

struct Esp32RfalElectricalConfig {
  std::uint32_t spi_frequency_hz{0};
  std::uint8_t spi_mode{0};
  bool chip_select_active_low{true};
  bool interrupt_active_high{true};
  bool reset_active_high{true};
  bool power_enable_active_high{true};
};

class Esp32RfalPlatform final : public IRfalPlatform {
 public:
  Esp32RfalPlatform(
      SPIClass& spi,
      Esp32RfalPins pins,
      Esp32RfalElectricalConfig electrical);
  ~Esp32RfalPlatform() override;

  bool initialize() override;
  bool transfer(
      const std::uint8_t* transmit,
      std::uint8_t* receive,
      std::size_t length) override;
  void select(bool active) override;
  void power(bool active) override;
  void reset(bool active) override;
  [[nodiscard]] bool interrupt_pending() const override;
  void acknowledge_interrupt() override;
  [[nodiscard]] std::uint32_t ticks_ms() const override;
  void delay_ms(std::uint32_t milliseconds) override;
  [[nodiscard]] bool lock_bus(std::uint32_t timeout_ms) override;
  void unlock_bus() override;
  void enter_critical() override;
  void leave_critical() override;

 private:
  static void IRAM_ATTR interrupt_trampoline(void* context);
  [[nodiscard]] std::uint8_t level(bool active, bool active_high) const;

  SPIClass& spi_;
  Esp32RfalPins pins_;
  Esp32RfalElectricalConfig electrical_;
  SemaphoreHandle_t bus_mutex_{nullptr};
  portMUX_TYPE critical_mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile bool interrupt_latched_{false};
  bool initialized_{false};
};

}  // namespace opentag::platform::rfal

#endif
