#pragma once

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <new>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

namespace opentag::nfc {
// Task-context, non-DMA storage only. Never used for FreeRTOS stacks, Wire,
// ISR data or RFAL transport buffers. Small nested codec allocations retain
// their existing allocator; these large outer allocations prefer PSRAM.
inline void* allocate_read_storage(std::size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32)
  if (auto* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
    return p;
  return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
inline void free_read_storage(void* p) {
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(p);
#else
  std::free(p);
#endif
}
struct ReadStorageDeleter {
  template <class T> void operator()(T* p) const {
    if (p) { p->~T(); free_read_storage(p); }
  }
};
template <class T> std::shared_ptr<T> make_read_storage() {
  auto* p = allocate_read_storage(sizeof(T));
  if (!p) return {};
  return std::shared_ptr<T>(new (p) T(), ReadStorageDeleter{});
}
class ReadImage {
 public:
  explicit ReadImage(std::size_t size)
      : data_(static_cast<std::uint8_t*>(allocate_read_storage(size))), size_(size) {}
  std::uint8_t* data() { return data_.get(); }
  const std::uint8_t* data() const { return data_.get(); }
  std::size_t size() const { return size_; }
 private:
  struct Free { void operator()(std::uint8_t* p) const { free_read_storage(p); } };
  std::unique_ptr<std::uint8_t, Free> data_;
  std::size_t size_;
};
}  // namespace opentag::nfc
