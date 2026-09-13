#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
// One application-lifetime TLSF pool; call before lv_init to handle OOM.
void* opentag_lvgl_pool(size_t bytes);
#ifdef __cplusplus
}
#endif
