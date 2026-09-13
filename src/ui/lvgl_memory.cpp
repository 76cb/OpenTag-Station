#include "ui/lvgl_memory.h"
#include "network/backend_memory.hpp"

extern "C" void* opentag_lvgl_pool(size_t bytes) {
  static void* pool = nullptr;
  if (bytes != 64U * 1024U) return nullptr;
  if (!pool) {
    opentag::network::backend_memory_phase("lvgl_pool_before", bytes);
    pool = opentag::network::backend_reallocate(nullptr, bytes);
    opentag::network::backend_memory_phase(
        pool ? "lvgl_pool_psram" : "lvgl_pool_unavailable", bytes);
  }
  return pool;
}
