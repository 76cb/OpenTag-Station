#pragma once

#include <cstddef>
#include <cstdint>

namespace opentag::web::assets {

inline constexpr std::size_t maximum_index_html_bytes = 28U * 1024U;
inline constexpr std::size_t maximum_stylesheet_bytes = 28U * 1024U;
inline constexpr std::size_t maximum_javascript_bytes = 136U * 1024U;
inline constexpr std::size_t maximum_total_source_bytes = 188U * 1024U;

extern const char index_html[];
extern const std::size_t index_html_size;
extern const char application_css[];
extern const std::size_t application_css_size;
extern const char application_javascript[];
extern const std::size_t application_javascript_size;
#if defined(ARDUINO_ARCH_ESP32)
extern const std::uint8_t application_css_gzip[];
extern const std::size_t application_css_gzip_size;
extern const std::uint8_t application_javascript_gzip[];
extern const std::size_t application_javascript_gzip_size;
#endif

}  // namespace opentag::web::assets
