#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

namespace opentag::network {
// No payloads, URIs, credentials or dynamic strings. Browser gauges are
// explicitly client-reported; httpd is synchronous and has no REST work queue.
struct MemoryCounters {
  std::atomic_uint sockets{0}, sockets_max{0}, websocket_clients{0};
  std::atomic_uint rest_active{0}, browser_active{0}, browser_queued{0}, browser_max{0};
};
inline MemoryCounters memory_counters;
inline void memory_trace(const char* owner, const char* phase,
    std::size_t bytes = 0, std::size_t json_bytes = 0) {
#ifdef ARDUINO
  constexpr auto internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr auto external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  Serial.printf("MEMORY ms=%lu task=%s owner=%s phase=%s internal=%u min=%u largest=%u psram=%u psram_min=%u psram_largest=%u bytes=%u json=%u sockets=%u sockets_max=%u ws=%u rest_active=%u rest_queued=0 browser_active=%u browser_queued=%u browser_max=%u\n",
      static_cast<unsigned long>(millis()), pcTaskGetName(nullptr), owner, phase,
      heap_caps_get_free_size(internal), heap_caps_get_minimum_free_size(internal),
      heap_caps_get_largest_free_block(internal), heap_caps_get_free_size(external),
      heap_caps_get_minimum_free_size(external), heap_caps_get_largest_free_block(external),
      static_cast<unsigned>(bytes), static_cast<unsigned>(json_bytes),
      memory_counters.sockets.load(), memory_counters.sockets_max.load(),
      memory_counters.websocket_clients.load(), memory_counters.rest_active.load(),
      memory_counters.browser_active.load(), memory_counters.browser_queued.load(),
      memory_counters.browser_max.load());
#endif
}
struct MemoryTraceScope {
  const char* owner;
  explicit MemoryTraceScope(const char* name) : owner(name) { memory_trace(owner, "before"); }
  ~MemoryTraceScope() { memory_trace(owner, "released"); }
};
}  // namespace opentag::network
