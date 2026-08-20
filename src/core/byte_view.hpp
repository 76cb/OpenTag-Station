#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opentag::core {

struct ByteView {
  const std::uint8_t* data{nullptr};
  std::size_t size{0};

  ByteView() = default;
  ByteView(const std::uint8_t* bytes, std::size_t length)
      : data(bytes), size(length) {}
  explicit ByteView(const std::vector<std::uint8_t>& bytes)
      : data(bytes.data()), size(bytes.size()) {}

  [[nodiscard]] bool empty() const { return size == 0U; }
  [[nodiscard]] bool contains(std::size_t offset, std::size_t length) const {
    return offset <= size && length <= size - offset;
  }
  [[nodiscard]] ByteView subview(std::size_t offset, std::size_t length) const {
    return contains(offset, length) ? ByteView{data + offset, length} : ByteView{};
  }
  [[nodiscard]] std::uint8_t operator[](std::size_t index) const {
    return data[index];
  }
};

}  // namespace opentag::core
