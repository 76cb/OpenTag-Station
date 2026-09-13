#pragma once
#include <ArduinoJson.h>
#include "core/result.hpp"
#include "network/backend_memory.hpp"

namespace opentag::network {
class BackendJsonAllocator final : public ArduinoJson::Allocator {
 public:
  explicit BackendJsonAllocator(ResponseBody::Reallocate allocate = backend_reallocate,
      ResponseBody::Free free = backend_free) : allocate_(allocate), free_(free) {}
  void* allocate(std::size_t size) override { return reallocate(nullptr, size); }
  void deallocate(void* ptr) override {
    if (!ptr) return;
    auto* header = static_cast<Header*>(ptr) - 1;
    used_ -= header->size;
    free_(header);
  }
  void* reallocate(void* ptr, std::size_t size) override {
    if (!size) { deallocate(ptr); return nullptr; }
    auto* old = ptr ? static_cast<Header*>(ptr) - 1 : nullptr;
    const auto previous = old ? old->size : 0;
    if (size > maximum_bytes - (used_ - previous)) return nullptr;
    auto* next = static_cast<Header*>(allocate_(old, sizeof(Header) + size));
    if (!next) return nullptr;
    next->size = size;
    used_ = used_ - previous + size;
    return next + 1;
  }
  std::size_t used() const { return used_; }
  static constexpr std::size_t maximum_bytes = 196608U;
 private:
  struct alignas(std::max_align_t) Header { std::size_t size; };
  std::size_t used_{0};
  ResponseBody::Reallocate allocate_;
  ResponseBody::Free free_;
};
// Only the backend task uses this allocator. The per-task document aggregate
// is bounded even while nested extra-field scalars are being decoded.
inline BackendJsonAllocator backend_json_allocator;
template <BackendJsonAllocator& pool>
class AllocatedDocument : public JsonDocument {
 public:
  AllocatedDocument() : JsonDocument(&pool) {}
  AllocatedDocument(AllocatedDocument&&) = default;
  AllocatedDocument& operator=(AllocatedDocument&&) = default;
  AllocatedDocument(const AllocatedDocument&) = delete;
  ~AllocatedDocument() {
    clear();
    backend_memory_phase("document_released", 0, pool.used());
  }
};
using BackendDocument = AllocatedDocument<backend_json_allocator>;
inline core::Result<BackendDocument> parse_backend_json(ResponseBody& body, const char* context) {
  backend_memory_phase("before_parse", body.size(), backend_json_allocator.used());
  BackendDocument document;
  const auto parsed = deserializeJson(document, body.data(), body.size(),
                                      DeserializationOption::NestingLimit(12));
  backend_memory_phase("after_parse", body.size(), backend_json_allocator.used());
  body.release();
  if (parsed) {
    const bool resource = parsed == DeserializationError::NoMemory;
    return core::Result<BackendDocument>::failure({
        resource ? core::ErrorCategory::backend_unavailable : core::ErrorCategory::invalid_response,
        std::string(context) + (resource ? " JSON workspace unavailable; retry later" : " returned invalid or truncated JSON"), resource});
  }
  return core::Result<BackendDocument>::success(std::move(document));
}
}  // namespace opentag::network
