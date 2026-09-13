#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace opentag::network {
// Persistent callback storage survives a caller's deadline. A late DNS callback
// can never write to a dead stack frame or complete a newer lookup.
class DnsLookupState {
 public:
  bool begin(const char* name) {
    if (busy_ && !done_.load(std::memory_order_acquire)) return false;
    if (std::strlen(name) >= sizeof(host)) return false;
    std::strcpy(host, name);
    address = 0;
    done_.store(false, std::memory_order_relaxed);
    busy_ = true;
    return true;
  }
  void complete(std::uint32_t result) {
    address = result;
    done_.store(true, std::memory_order_release);
  }
  bool done() const { return done_.load(std::memory_order_acquire); }
  char host[254]{};
  std::uint32_t address{0};
 private:
  bool busy_{false};  // Only the backend owner starts lookups.
  std::atomic<bool> done_{false};
};
}  // namespace opentag::network

#ifdef ARDUINO
#include <Arduino.h>
#include <IPAddress.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include "network/operation_budget.hpp"

namespace opentag::network {
inline DnsLookupState backend_dns;
inline void backend_dns_complete(const char*, const ip_addr_t* address, void* argument) {
  static_cast<DnsLookupState*>(argument)->complete(
      address && IP_IS_V4(address) ? ip_2_ip4(address)->addr : 0);
}
inline void backend_dns_start(void* argument) {
  auto& state = *static_cast<DnsLookupState*>(argument);
  ip_addr_t address{};
  const auto result = dns_gethostbyname(state.host, &address, backend_dns_complete, argument);
  if (result == ERR_OK) backend_dns_complete(nullptr, &address, argument);
  else if (result != ERR_INPROGRESS) state.complete(0);
}
inline bool resolve_backend_host(const char* name, IPAddress& address,
                                 const OperationBudget& budget) {
  if (address.fromString(name)) return true;
  if (!backend_dns.begin(name)) return false;
  if (tcpip_try_callback(backend_dns_start, &backend_dns) != ERR_OK) {
    backend_dns.complete(0);
    return false;
  }
  const auto started = millis();
  while (!backend_dns.done()) {
    if (budget.expired(millis()) || static_cast<std::uint32_t>(millis() - started) >= 15000U)
      return false;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  address = backend_dns.address;
  return backend_dns.address != 0;
}
}  // namespace opentag::network
#endif
