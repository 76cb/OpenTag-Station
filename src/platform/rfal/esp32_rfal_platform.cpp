#include "platform/rfal/esp32_rfal_platform.hpp"

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>

namespace opentag::platform::rfal {

Esp32RfalPlatform::Esp32RfalPlatform(
    SPIClass& spi,
    Esp32RfalPins pins,
    Esp32RfalElectricalConfig electrical)
    : spi_(spi), pins_(pins), electrical_(electrical) {}

Esp32RfalPlatform::~Esp32RfalPlatform() {
  if (initialized_) detachInterrupt(static_cast<std::uint8_t>(pins_.interrupt));
  if (bus_mutex_ != nullptr) vSemaphoreDelete(bus_mutex_);
}

std::uint8_t Esp32RfalPlatform::level(bool active, bool active_high) const {
  return active == active_high ? HIGH : LOW;
}

bool Esp32RfalPlatform::initialize() {
  if (initialized_) return true;
  if (!pins_.complete() || electrical_.spi_frequency_hz == 0U ||
      electrical_.spi_mode > 3U) {
    return false;
  }
  bus_mutex_ = xSemaphoreCreateRecursiveMutex();
  if (bus_mutex_ == nullptr) return false;

  pinMode(pins_.chip_select, OUTPUT);
  pinMode(pins_.reset, OUTPUT);
  pinMode(pins_.interrupt, INPUT);
  if (pins_.power_enable >= 0) pinMode(pins_.power_enable, OUTPUT);
  digitalWrite(
      pins_.chip_select,
      level(false, !electrical_.chip_select_active_low));
  reset(false);
  power(false);

  spi_.begin(pins_.sck, pins_.miso, pins_.mosi, pins_.chip_select);
  attachInterruptArg(
      pins_.interrupt,
      &Esp32RfalPlatform::interrupt_trampoline,
      this,
      electrical_.interrupt_active_high ? RISING : FALLING);
  initialized_ = true;
  return true;
}

bool Esp32RfalPlatform::transfer(
    const std::uint8_t* transmit,
    std::uint8_t* receive,
    std::size_t length) {
  if (!initialized_ || transmit == nullptr || receive == nullptr || length == 0U) {
    return false;
  }
  spi_.transferBytes(transmit, receive, static_cast<std::uint32_t>(length));
  return true;
}

void Esp32RfalPlatform::select(bool active) {
  if (!initialized_) return;
  if (active) {
    spi_.beginTransaction(SPISettings(
        electrical_.spi_frequency_hz, MSBFIRST, electrical_.spi_mode));
  }
  digitalWrite(
      pins_.chip_select,
      level(active, !electrical_.chip_select_active_low));
  if (!active) spi_.endTransaction();
}

void Esp32RfalPlatform::power(bool active) {
  if (pins_.power_enable >= 0) {
    digitalWrite(
        pins_.power_enable,
        level(active, electrical_.power_enable_active_high));
  }
}

void Esp32RfalPlatform::reset(bool active) {
  if (pins_.reset >= 0) {
    digitalWrite(pins_.reset, level(active, electrical_.reset_active_high));
  }
}

bool Esp32RfalPlatform::interrupt_pending() const {
  if (!initialized_) return false;
  const bool line_active = digitalRead(pins_.interrupt) ==
      level(true, electrical_.interrupt_active_high);
  return interrupt_latched_ || line_active;
}

void Esp32RfalPlatform::acknowledge_interrupt() {
  interrupt_latched_ = false;
}

std::uint32_t Esp32RfalPlatform::ticks_ms() const {
  return millis();
}

void Esp32RfalPlatform::delay_ms(std::uint32_t milliseconds) {
  delay(milliseconds);
}

bool Esp32RfalPlatform::lock_bus(std::uint32_t timeout_ms) {
  return bus_mutex_ != nullptr &&
      xSemaphoreTakeRecursive(bus_mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void Esp32RfalPlatform::unlock_bus() {
  if (bus_mutex_ != nullptr) (void)xSemaphoreGiveRecursive(bus_mutex_);
}

void Esp32RfalPlatform::enter_critical() {
  portENTER_CRITICAL(&critical_mux_);
}

void Esp32RfalPlatform::leave_critical() {
  portEXIT_CRITICAL(&critical_mux_);
}

void IRAM_ATTR Esp32RfalPlatform::interrupt_trampoline(void* context) {
  auto* self = static_cast<Esp32RfalPlatform*>(context);
  self->interrupt_latched_ = true;
}

}  // namespace opentag::platform::rfal

#endif
