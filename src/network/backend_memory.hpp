#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <memory>
#include <new>
#include "network/memory_trace.hpp"
#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

namespace opentag::network {

struct BackendHeap {
  std::size_t free_internal, largest_internal;
};
inline bool backend_admitted(BackendHeap heap, bool secure = false) {
  // Preserve workspace for httpd, Wi-Fi and scale; TLS needs its own margin.
  return heap.free_internal >= (secure ? 48000U : 18000U) &&
         heap.largest_internal >= (secure ? 20000U : 6144U);
}
inline BackendHeap backend_heap() {
#ifdef ARDUINO
  return {heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)};
#else
  return {1000000U, 1000000U};
#endif
}
inline void backend_memory_phase(const char* phase, std::size_t bytes = 0,
                                 std::size_t parser_bytes = 0,
                                 std::uint32_t started = 0) {
#ifdef ARDUINO
  constexpr auto internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr auto external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  Serial.printf("BACKEND phase=%s ms=%lu task=%s internal=%u min=%u largest=%u psram=%u psram_min=%u psram_largest=%u response=%u parser=%u duration_ms=%lu\n",
      phase, static_cast<unsigned long>(millis()), pcTaskGetName(nullptr),
      heap_caps_get_free_size(internal), heap_caps_get_minimum_free_size(internal),
      heap_caps_get_largest_free_block(internal), heap_caps_get_free_size(external),
      heap_caps_get_minimum_free_size(external), heap_caps_get_largest_free_block(external),
      static_cast<unsigned>(bytes), static_cast<unsigned>(parser_bytes),
      static_cast<unsigned long>(started ? millis() - started : 0));
#endif
}
#ifndef ARDUINO
// Deterministic native fault injection; absent from firmware.
inline bool (*backend_allocation_admission)(std::size_t) = nullptr;
#endif
inline void* backend_reallocate(void* ptr, std::size_t size) {
#ifdef ARDUINO
  // Deliberately no internal fallback: backend payloads must not evict httpd.
  return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  if (backend_allocation_admission && !backend_allocation_admission(size)) return nullptr;
  return std::realloc(ptr, size);
#endif
}
inline void backend_free(void* ptr) {
#ifdef ARDUINO
  heap_caps_free(ptr);
#else
  std::free(ptr);
#endif
}
struct BackendPhaseGuard {
  const char* phase;
  std::uint32_t started;
  ~BackendPhaseGuard() { backend_memory_phase(phase, 0, 0, started); }
};
template <class T> struct ExternalDelete {
  void operator()(T* value) const {
    if (value) {
      value->~T(); backend_free(value);
      memory_trace("external_object", "released", sizeof(T));
    }
  }
};
template <class T, class Factory>
std::unique_ptr<T, ExternalDelete<T>> make_external(Factory factory,
    const char* owner = __builtin_FUNCTION()) {
  memory_trace(owner, "make_external_before", sizeof(T));
  void* memory = backend_reallocate(nullptr, sizeof(T));
  if (!memory) { backend_memory_phase("make_external_failed", sizeof(T)); return {}; }
  // C++17 guaranteed elision constructs the returned snapshot at its final
  // address, avoiding both a large automatic object and an intermediate copy.
  auto result = std::unique_ptr<T, ExternalDelete<T>>(new (memory) T(factory()));
  memory_trace(owner, "make_external_after", sizeof(T));
  return result;
}

// Move-only, fallible growth. No std::string copy or hidden internal allocation.
class ResponseBody {
 public:
  using Reallocate = void* (*)(void*, std::size_t);
  using Free = void (*)(void*);
  explicit ResponseBody(std::size_t maximum = 65536U,
      Reallocate allocate = backend_reallocate, Free free = backend_free)
      : maximum_(maximum), allocate_(allocate), free_(free) {}
  ResponseBody(const std::string& text) : ResponseBody() { append(text.data(), text.size()); }
  ResponseBody(const char* text) : ResponseBody() { append(text, std::strlen(text)); }
  ResponseBody(const ResponseBody&) = delete;
  ResponseBody& operator=(const ResponseBody&) = delete;
  ResponseBody(ResponseBody&& other) noexcept : ResponseBody() { swap(other); }
  ResponseBody& operator=(ResponseBody&& other) noexcept {
    if (this != &other) { release(); swap(other); }
    return *this;
  }
  ~ResponseBody() { release(); }
  bool reserve(std::size_t count) {
    if (count > maximum_) { overflowed_ = true; return false; }
    if (count + 1U <= capacity_) return true;
    auto* next = static_cast<char*>(allocate_(data_, count + 1U));
    if (!next) { failed_ = true; return false; }
    data_ = next; capacity_ = count + 1U; data_[size_] = '\0';
    return true;
  }
  bool resize_uninitialized(std::size_t count) {
    if (!reserve(count)) return false;
    size_ = count; data_[size_] = '\0'; return true;
  }
  char* mutable_data() { return data_; }
  // ArduinoJson Writer: failed writes are detected by the serializer owner.
  std::size_t write(std::uint8_t byte) { return write(&byte, 1U); }
  std::size_t write(const std::uint8_t* bytes, std::size_t count) {
    return append(reinterpret_cast<const char*>(bytes), count) ? count : 0U;
  }
  bool wrap(std::string_view prefix, std::string_view suffix) {
    if (prefix.size() + suffix.size() > maximum_ - size_) { overflowed_ = true; return false; }
    if (!reserve(size_ + prefix.size() + suffix.size())) return false;
    std::memmove(data_ + prefix.size(), data_, size_);
    std::memcpy(data_, prefix.data(), prefix.size());
    size_ += prefix.size();
    return append(suffix.data(), suffix.size());
  }
  bool append(const char* data, std::size_t count) {
    if (count > maximum_ - size_) { overflowed_ = true; return false; }
    if (size_ + count + 1U > capacity_) {
      const auto capacity = std::min(maximum_ + 1U,
          std::max(size_ + count + 1U, std::max<std::size_t>(512U, capacity_ * 2U)));
      auto* next = static_cast<char*>(allocate_(data_, capacity));
      if (!next) { failed_ = true; return false; }
      data_ = next;
      capacity_ = capacity;
    }
    if (count) std::memcpy(data_ + size_, data, count);
    size_ += count;
    data_[size_] = '\0';
    return true;
  }
  // Reuse a bounded workspace without reallocating or logging each record.
  void clear() { size_ = 0; if (data_) data_[0] = '\0'; }
  void release() {
    if (data_) {
      free_(data_);
      backend_memory_phase("body_released", size_);
    }
    data_ = nullptr; size_ = capacity_ = 0;
  }
  const char* data() const { return data_ ? data_ : ""; }
  const char* c_str() const { return data(); }
  const char* begin() const { return data(); }
  const char* end() const { return data() + size_; }
  operator std::string_view() const { return {data(), size_}; }
  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  bool failed() const { return failed_; }
  bool overflowed() const { return overflowed_; }
 private:
  void swap(ResponseBody& other) noexcept {
    using std::swap;
    swap(data_, other.data_); swap(size_, other.size_); swap(capacity_, other.capacity_);
    swap(maximum_, other.maximum_); swap(allocate_, other.allocate_); swap(free_, other.free_);
    swap(failed_, other.failed_); swap(overflowed_, other.overflowed_);
  }
  char* data_{nullptr};
  std::size_t size_{0}, capacity_{0}, maximum_;
  Reallocate allocate_;
  Free free_;
  bool failed_{false}, overflowed_{false};
};
}  // namespace opentag::network
