#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include "network/operation_budget.hpp"

namespace opentag::network {
// HTTPClient calls these virtuals even from Stream::readStringUntil/readBytes.
// Enforce the total operation deadline against continuously trickling headers
// or chunked bodies, which inactivity timeouts alone cannot bound. The real
// TCP/TLS client retains TLS hostname verification and socket timeouts.
template <class Base, class Address, class Clock>
class DeadlineClient final : public Base {
 public:
  DeadlineClient(Base& client, const OperationBudget& budget, Clock clock)
      : client_(client), budget_(budget), clock_(clock) {}
  int connect(Address ip, std::uint16_t port) override { return connect(ip, port, 5000); }
  int connect(const char* host, std::uint16_t port) override { return connect(host, port, 5000); }
  int connect(Address ip, std::uint16_t port, std::int32_t timeout) override {
    if (expired()) return 0;
    const auto result = client_.connect(ip, port,
        std::min<std::int32_t>(timeout, budget_.remaining(clock_())));
    connection_errno_ = result ? 0 : errno;
    return expired() ? 0 : result;
  }
  int connect(const char* host, std::uint16_t port, std::int32_t timeout) override {
    if (expired()) return 0;
    const auto result = client_.connect(host, port,
        std::min<std::int32_t>(timeout, budget_.remaining(clock_())));
    connection_errno_ = result ? 0 : errno;
    return expired() ? 0 : result;
  }
  std::size_t write(std::uint8_t byte) override { return write(&byte, 1U); }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    std::size_t sent = 0;
    while (sent < size && !expired()) {
      const auto count = client_.write(data + sent, std::min<std::size_t>(128U, size - sent));
      if (!count) break;
      sent += count;
    }
    return sent;
  }
  int available() override { return expired() ? 0 : client_.available(); }
  int read() override { return expired() ? -1 : client_.read(); }
  int read(std::uint8_t* data, std::size_t size) override { return expired() ? -1 : client_.read(data, size); }
  int peek() override { return expired() ? -1 : client_.peek(); }
  void flush() override {
    std::uint8_t discarded[64];
    while (!expired() && client_.available() > 0) {
      if (client_.read(discarded, sizeof(discarded)) <= 0) break;
    }
  }
  void stop() override { client_.stop(); }
  std::uint8_t connected() override { return expired() ? 0 : client_.connected(); }
  int setTimeout(std::uint32_t seconds) override {
    // Stream's timeout is milliseconds, unlike the WiFiClient socket API.
    this->Base::setTimeout(seconds);
    return client_.setTimeout(seconds);
  }
  int fd() const override { return client_.fd(); }
  int connection_errno() const { return connection_errno_; }
 private:
  bool expired() {
    if (!budget_.expired(clock_())) return false;
    client_.stop();
    return true;
  }
  Base& client_;
  const OperationBudget& budget_;
  Clock clock_;
  int connection_errno_{0};
};
}  // namespace opentag::network
