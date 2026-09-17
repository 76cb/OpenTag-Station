#pragma once
#include <cstdint>

// Shared with tools/render_touch_review.py. Coordinates are the real 480 x 320
// LVGL layout; fixtures approximate font rasterization, not physical hardware.
namespace opentag::ui::layout {
struct Box { std::int16_t x, y, w, h; };
inline constexpr Box heading{16, 12, 448, 28};
inline constexpr Box home_state{16, 12, 448, 22};
inline constexpr Box home_material{16, 40, 448, 30};
inline constexpr Box home_identity{16, 76, 448, 24};
inline constexpr Box home_weight{16, 102, 448, 48};
inline constexpr Box home_art{374, 84, 70, 70};
inline constexpr Box empty_art{216, 106, 48, 48};
inline constexpr Box home_weigh{16, 162, 216, 46};
inline constexpr Box home_assign{248, 162, 216, 46};
inline constexpr Box home_tag{16, 218, 216, 46};
inline constexpr Box home_more{248, 218, 216, 46};
inline constexpr Box gross{16, 52, 208, 48};
inline constexpr Box gross_unit{16, 104, 208, 22};
inline constexpr Box quality{16, 136, 208, 24};
inline constexpr Box receipt{248, 52, 216, 112};
inline constexpr Box weigh{16, 180, 216, 48};
inline constexpr Box update{248, 180, 216, 48};
inline constexpr Box feedback{16, 238, 448, 28};
inline constexpr Box printer_name{16, 12, 448, 28};
inline constexpr Box printer_spool{16, 46, 448, 24};
inline constexpr Box tool1{16, 82, 144, 68};
inline constexpr Box tool2{168, 82, 144, 68};
inline constexpr Box tool3{320, 82, 144, 68};
inline constexpr Box tool4{92, 160, 144, 68};
inline constexpr Box tool5{244, 160, 144, 68};
inline constexpr Box tag_detail{16, 52, 448, 98};
inline constexpr Box tag_update{16, 162, 216, 46};
inline constexpr Box tag_clear{248, 162, 216, 46};
inline constexpr Box tag_confirm{16, 218, 448, 46};
inline constexpr Box brightness{24, 60, 432, 44};
inline constexpr Box policy{16, 118, 216, 48};
inline constexpr Box calibration{248, 118, 216, 48};
inline constexpr Box about{248, 180, 216, 48};
inline constexpr Box wifi{16, 180, 216, 80};
inline constexpr Box nav{0, 272, 480, 48};
}
