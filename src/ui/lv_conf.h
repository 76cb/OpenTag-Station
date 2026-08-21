#ifndef LV_CONF_H
#define LV_CONF_H

// LVGL 8.3 project configuration. Unspecified options intentionally retain
// LVGL's version-pinned defaults until the display service is implemented.
#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)

#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_USER_DATA 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_THEME_DEFAULT 1

#endif  // LV_CONF_H
