#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace opentag::network {
// COMPLETE is intentional success: the owner no longer needs the remainder.
// ERROR is never accepted as a short successful response.
enum class StreamDisposition { next, complete, error };
using StreamConsumer = std::function<StreamDisposition(const std::uint8_t*, std::size_t)>;
}
