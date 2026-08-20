#pragma once

#include <cstddef>
#include <cstdint>

namespace opentag::platform::rfal {

// Port seam for ST's RFAL package. The concrete ESP32 implementation will
// adapt these operations to SPIClass, GPIO interrupts, FreeRTOS locks, and
// monotonic timers without modifying vendor RFAL sources.
class IRfalPlatform {
 public:
  virtual ~IRfalPlatform() = default;

  virtual bool initialize() = 0;
  virtual bool transfer(
      const std::uint8_t* transmit,
      std::uint8_t* receive,
      std::size_t length) = 0;
  virtual void select(bool active) = 0;
  virtual void power(bool active) = 0;
  virtual void reset(bool active) = 0;
  [[nodiscard]] virtual bool interrupt_pending() const = 0;
  virtual void acknowledge_interrupt() = 0;
  [[nodiscard]] virtual std::uint32_t ticks_ms() const = 0;
  virtual void delay_ms(std::uint32_t milliseconds) = 0;
  [[nodiscard]] virtual bool lock_bus(std::uint32_t timeout_ms) = 0;
  virtual void unlock_bus() = 0;
  virtual void enter_critical() = 0;
  virtual void leave_critical() = 0;
};

}  // namespace opentag::platform::rfal
