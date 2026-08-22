#pragma once

#include <cstddef>

namespace opentag::web::assets {

inline constexpr std::size_t maximum_index_html_bytes = 24U * 1024U;
inline constexpr std::size_t maximum_stylesheet_bytes = 24U * 1024U;
inline constexpr std::size_t maximum_javascript_bytes = 120U * 1024U;
inline constexpr std::size_t maximum_total_source_bytes = 152U * 1024U;

extern const char index_html[];
extern const std::size_t index_html_size;
extern const char application_css[];
extern const std::size_t application_css_size;
extern const char application_javascript[];
extern const std::size_t application_javascript_size;

}  // namespace opentag::web::assets
