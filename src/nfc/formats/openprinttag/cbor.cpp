#include "nfc/formats/openprinttag/cbor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace opentag::nfc::openprinttag {
namespace {

using core::ByteView;

struct Head {
  std::uint8_t major{0};
  std::uint8_t additional{0};
  std::uint64_t argument{0};
  std::size_t size{0};
  bool indefinite{false};
};

core::Error malformed(const std::string& message) {
  return {core::ErrorCategory::invalid_openprinttag, message, false};
}

core::Result<Head> read_head(ByteView bytes, std::size_t offset) {
  if (!bytes.contains(offset, 1U)) {
    return core::Result<Head>::failure(malformed("truncated CBOR item header"));
  }
  const auto first = bytes[offset];
  Head head;
  head.major = first >> 5U;
  head.additional = first & 0x1FU;
  head.size = 1U;
  if (head.additional < 24U) {
    head.argument = head.additional;
    return core::Result<Head>::success(head);
  }
  if (head.additional == 31U) {
    head.indefinite = true;
    return core::Result<Head>::success(head);
  }

  std::size_t argument_bytes = 0U;
  switch (head.additional) {
    case 24U: argument_bytes = 1U; break;
    case 25U: argument_bytes = 2U; break;
    case 26U: argument_bytes = 4U; break;
    case 27U: argument_bytes = 8U; break;
    default:
      return core::Result<Head>::failure(malformed("reserved CBOR additional information"));
  }
  if (!bytes.contains(offset + 1U, argument_bytes)) {
    return core::Result<Head>::failure(malformed("truncated CBOR argument"));
  }
  for (std::size_t index = 0; index < argument_bytes; ++index) {
    head.argument = (head.argument << 8U) | bytes[offset + 1U + index];
  }
  head.size += argument_bytes;
  return core::Result<Head>::success(head);
}

core::Result<std::size_t> skip_item(
    ByteView bytes,
    std::size_t offset,
    std::size_t depth) {
  if (depth > CborMapView::maximum_nesting_depth) {
    return core::Result<std::size_t>::failure(malformed("CBOR nesting limit exceeded"));
  }
  const auto head_result = read_head(bytes, offset);
  if (!head_result.ok()) {
    return core::Result<std::size_t>::failure(head_result.error());
  }
  const auto head = head_result.value();
  std::size_t cursor = offset + head.size;

  if (head.major == 0U || head.major == 1U || head.major == 7U) {
    if (head.indefinite) {
      return core::Result<std::size_t>::failure(malformed("invalid indefinite scalar"));
    }
    return core::Result<std::size_t>::success(cursor);
  }

  if (head.major == 2U || head.major == 3U) {
    if (!head.indefinite) {
      if (head.argument > std::numeric_limits<std::size_t>::max() ||
          !bytes.contains(cursor, static_cast<std::size_t>(head.argument))) {
        return core::Result<std::size_t>::failure(malformed("CBOR byte/text string exceeds region"));
      }
      return core::Result<std::size_t>::success(
          cursor + static_cast<std::size_t>(head.argument));
    }
    for (;;) {
      if (!bytes.contains(cursor, 1U)) {
        return core::Result<std::size_t>::failure(malformed("unterminated CBOR string"));
      }
      if (bytes[cursor] == 0xFFU) {
        return core::Result<std::size_t>::success(cursor + 1U);
      }
      const auto chunk_head = read_head(bytes, cursor);
      if (!chunk_head.ok() || chunk_head.value().major != head.major ||
          chunk_head.value().indefinite) {
        return core::Result<std::size_t>::failure(malformed("invalid indefinite CBOR string chunk"));
      }
      const auto chunk_end = skip_item(bytes, cursor, depth + 1U);
      if (!chunk_end.ok()) {
        return chunk_end;
      }
      cursor = chunk_end.value();
    }
  }

  if (head.major == 4U || head.major == 5U) {
    if (!head.indefinite && head.major == 5U &&
        head.argument > std::numeric_limits<std::uint64_t>::max() / 2U) {
      return core::Result<std::size_t>::failure(malformed("CBOR map item count overflows"));
    }
    const std::uint64_t item_count = head.indefinite
                                         ? 0U
                                         : head.argument * (head.major == 5U ? 2U : 1U);
    std::uint64_t processed = 0U;
    for (;;) {
      if (head.indefinite) {
        if (!bytes.contains(cursor, 1U)) {
          return core::Result<std::size_t>::failure(malformed("unterminated CBOR container"));
        }
        if (bytes[cursor] == 0xFFU) {
          return core::Result<std::size_t>::success(cursor + 1U);
        }
      } else if (processed == item_count) {
        return core::Result<std::size_t>::success(cursor);
      }
      const auto item_end = skip_item(bytes, cursor, depth + 1U);
      if (!item_end.ok()) {
        return item_end;
      }
      cursor = item_end.value();
      ++processed;
    }
  }

  if (head.major == 6U) {
    if (head.indefinite) {
      return core::Result<std::size_t>::failure(malformed("invalid indefinite CBOR tag"));
    }
    return skip_item(bytes, cursor, depth + 1U);
  }
  return core::Result<std::size_t>::failure(malformed("unknown CBOR major type"));
}

core::Result<std::uint64_t> decode_unsigned(ByteView encoded) {
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().major != 0U || head.value().indefinite ||
      head.value().size != encoded.size) {
    return core::Result<std::uint64_t>::failure(malformed("CBOR value is not an unsigned integer"));
  }
  return core::Result<std::uint64_t>::success(head.value().argument);
}

bool valid_utf8(ByteView value) {
  std::size_t cursor = 0U;
  while (cursor < value.size) {
    const auto first = value[cursor++];
    if (first <= 0x7FU) continue;
    std::size_t continuation_count = 0U;
    std::uint8_t minimum_second = 0x80U;
    std::uint8_t maximum_second = 0xBFU;
    if (first >= 0xC2U && first <= 0xDFU) {
      continuation_count = 1U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuation_count = 2U;
      if (first == 0xE0U) minimum_second = 0xA0U;
      if (first == 0xEDU) maximum_second = 0x9FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuation_count = 3U;
      if (first == 0xF0U) minimum_second = 0x90U;
      if (first == 0xF4U) maximum_second = 0x8FU;
    } else {
      return false;
    }
    if (!value.contains(cursor, continuation_count) ||
        value[cursor] < minimum_second || value[cursor] > maximum_second) {
      return false;
    }
    for (std::size_t index = 1U; index < continuation_count; ++index) {
      if (value[cursor + index] < 0x80U || value[cursor + index] > 0xBFU) return false;
    }
    cursor += continuation_count;
  }
  return true;
}

core::Result<std::int64_t> decode_integer_value(ByteView encoded) {
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().indefinite || head.value().size != encoded.size ||
      (head.value().major != 0U && head.value().major != 1U)) {
    return core::Result<std::int64_t>::failure(malformed("CBOR value is not an integer"));
  }
  if (head.value().argument >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return core::Result<std::int64_t>::failure(malformed("CBOR integer exceeds signed range"));
  }
  const auto magnitude = static_cast<std::int64_t>(head.value().argument);
  return core::Result<std::int64_t>::success(
      head.value().major == 0U ? magnitude : -1 - magnitude);
}

double decode_half(std::uint16_t bits) {
  const int sign = (bits & 0x8000U) != 0U ? -1 : 1;
  const int exponent = (bits >> 10U) & 0x1FU;
  const int mantissa = bits & 0x03FFU;
  if (exponent == 0) {
    return sign * std::ldexp(static_cast<double>(mantissa), -24);
  }
  if (exponent == 31) {
    return mantissa == 0 ? sign * std::numeric_limits<double>::infinity()
                         : std::numeric_limits<double>::quiet_NaN();
  }
  return sign * std::ldexp(static_cast<double>(mantissa + 1024), exponent - 25);
}

core::Result<double> decode_number_value(ByteView encoded) {
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().indefinite || head.value().size != encoded.size) {
    return core::Result<double>::failure(malformed("CBOR value is not numeric"));
  }
  if (head.value().major == 0U) {
    return core::Result<double>::success(static_cast<double>(head.value().argument));
  }
  if (head.value().major == 1U) {
    return core::Result<double>::success(-1.0 - static_cast<double>(head.value().argument));
  }
  if (head.value().major != 7U) {
    return core::Result<double>::failure(malformed("CBOR value is not numeric"));
  }
  if (head.value().additional == 25U) {
    return core::Result<double>::success(
        decode_half(static_cast<std::uint16_t>(head.value().argument)));
  }
  if (head.value().additional == 26U) {
    const std::uint32_t bits = static_cast<std::uint32_t>(head.value().argument);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return core::Result<double>::success(value);
  }
  if (head.value().additional == 27U) {
    const std::uint64_t bits = head.value().argument;
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return core::Result<double>::success(value);
  }
  return core::Result<double>::failure(malformed("CBOR simple value is not a float"));
}

void append_argument(
    std::vector<std::uint8_t>& output,
    std::uint8_t major,
    std::uint64_t value) {
  if (value < 24U) {
    output.push_back(static_cast<std::uint8_t>((major << 5U) | value));
    return;
  }
  std::size_t bytes = value <= 0xFFU ? 1U : value <= 0xFFFFU ? 2U :
                      value <= 0xFFFFFFFFULL ? 4U : 8U;
  const std::uint8_t additional = bytes == 1U ? 24U : bytes == 2U ? 25U :
                                  bytes == 4U ? 26U : 27U;
  output.push_back(static_cast<std::uint8_t>((major << 5U) | additional));
  for (std::size_t index = bytes; index > 0U; --index) {
    output.push_back(static_cast<std::uint8_t>(value >> ((index - 1U) * 8U)));
  }
}

}  // namespace

core::Result<CborMapView> CborMapView::parse(core::ByteView region) {
  if (region.empty() || region.size > maximum_region_size) {
    return core::Result<CborMapView>::failure(malformed("CBOR region size is invalid"));
  }
  const auto root = read_head(region, 0U);
  if (!root.ok() || root.value().major != 5U) {
    return core::Result<CborMapView>::failure(malformed("OpenPrintTag region is not a CBOR map"));
  }

  CborMapView result;
  result.bytes_ = region;
  std::size_t cursor = root.value().size;
  std::uint64_t remaining = root.value().argument;
  for (;;) {
    if (root.value().indefinite) {
      if (!region.contains(cursor, 1U)) {
        return core::Result<CborMapView>::failure(malformed("unterminated CBOR map"));
      }
      if (region[cursor] == 0xFFU) {
        result.used_size_ = cursor + 1U;
        break;
      }
    } else if (remaining == 0U) {
      result.used_size_ = cursor;
      break;
    }
    if (result.entries_.size() >= maximum_entries) {
      return core::Result<CborMapView>::failure(malformed("CBOR map entry limit exceeded"));
    }

    const auto key_end = skip_item(region, cursor, 1U);
    if (!key_end.ok()) {
      return core::Result<CborMapView>::failure(key_end.error());
    }
    const CborSlice encoded_key{cursor, key_end.value() - cursor};
    const auto key = decode_unsigned(region.subview(encoded_key.offset, encoded_key.size));
    if (!key.ok()) {
      return core::Result<CborMapView>::failure(malformed("OpenPrintTag CBOR key is not unsigned"));
    }
    if (result.find(key.value()) != nullptr) {
      return core::Result<CborMapView>::failure(malformed("duplicate OpenPrintTag CBOR key"));
    }

    cursor = key_end.value();
    const auto value_end = skip_item(region, cursor, 1U);
    if (!value_end.ok()) {
      return core::Result<CborMapView>::failure(value_end.error());
    }
    result.entries_.push_back({key.value(), encoded_key, {cursor, value_end.value() - cursor}});
    cursor = value_end.value();
    if (!root.value().indefinite) {
      --remaining;
    }
  }
  return core::Result<CborMapView>::success(std::move(result));
}

const CborMapEntry* CborMapView::find(std::uint64_t key) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(), [key](const auto& entry) {
    return entry.key == key;
  });
  return found == entries_.end() ? nullptr : &*found;
}

core::Result<std::uint64_t> CborMapView::read_unsigned(std::uint64_t key) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<std::uint64_t>::failure(malformed("required CBOR key is absent"));
  }
  return decode_unsigned(bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size));
}

core::Result<std::int64_t> CborMapView::read_integer(std::uint64_t key) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<std::int64_t>::failure(malformed("required CBOR key is absent"));
  }
  return decode_integer_value(
      bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size));
}

core::Result<double> CborMapView::read_number(std::uint64_t key) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<double>::failure(malformed("required CBOR key is absent"));
  }
  return decode_number_value(
      bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size));
}

core::Result<std::string> CborMapView::read_text(std::uint64_t key) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<std::string>::failure(malformed("required CBOR key is absent"));
  }
  const auto encoded = bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size);
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().major != 3U || head.value().indefinite ||
      head.value().argument > encoded.size - head.value().size) {
    return core::Result<std::string>::failure(malformed("CBOR value is not definite text"));
  }
  const auto text = encoded.subview(
      head.value().size, static_cast<std::size_t>(head.value().argument));
  if (!valid_utf8(text)) {
    return core::Result<std::string>::failure(malformed("CBOR text is not valid UTF-8"));
  }
  return core::Result<std::string>::success(std::string(
      reinterpret_cast<const char*>(text.data), text.size));
}

core::Result<std::vector<std::uint8_t>> CborMapView::read_bytes(std::uint64_t key) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<std::vector<std::uint8_t>>::failure(malformed("required CBOR key is absent"));
  }
  const auto encoded = bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size);
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().major != 2U || head.value().indefinite ||
      head.value().argument > encoded.size - head.value().size) {
    return core::Result<std::vector<std::uint8_t>>::failure(malformed("CBOR value is not definite bytes"));
  }
  const auto start = encoded.data + head.value().size;
  return core::Result<std::vector<std::uint8_t>>::success(
      {start, start + static_cast<std::size_t>(head.value().argument)});
}

core::Result<std::vector<std::uint64_t>> CborMapView::read_unsigned_array(
    std::uint64_t key,
    std::size_t maximum_items) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<std::vector<std::uint64_t>>::failure(malformed("required CBOR key is absent"));
  }
  const auto encoded = bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size);
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().major != 4U) {
    return core::Result<std::vector<std::uint64_t>>::failure(malformed("CBOR value is not an array"));
  }
  std::vector<std::uint64_t> output;
  std::size_t cursor = head.value().size;
  std::uint64_t remaining = head.value().argument;
  for (;;) {
    if (head.value().indefinite) {
      if (!encoded.contains(cursor, 1U)) {
        return core::Result<std::vector<std::uint64_t>>::failure(malformed("unterminated CBOR array"));
      }
      if (encoded[cursor] == 0xFFU) {
        break;
      }
    } else if (remaining == 0U) {
      break;
    }
    if (output.size() >= maximum_items) {
      return core::Result<std::vector<std::uint64_t>>::failure(malformed("CBOR array item limit exceeded"));
    }
    const auto end = skip_item(encoded, cursor, 1U);
    if (!end.ok()) {
      return core::Result<std::vector<std::uint64_t>>::failure(end.error());
    }
    const auto value = decode_unsigned(encoded.subview(cursor, end.value() - cursor));
    if (!value.ok()) {
      return core::Result<std::vector<std::uint64_t>>::failure(value.error());
    }
    output.push_back(value.value());
    cursor = end.value();
    if (!head.value().indefinite) {
      --remaining;
    }
  }
  return core::Result<std::vector<std::uint64_t>>::success(std::move(output));
}

core::Result<std::vector<double>> CborMapView::read_number_array(
    std::uint64_t key,
    std::size_t maximum_items) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<std::vector<double>>::failure(malformed("required CBOR key is absent"));
  }
  const auto encoded = bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size);
  const auto head = read_head(encoded, 0U);
  if (!head.ok() || head.value().major != 4U) {
    return core::Result<std::vector<double>>::failure(malformed("CBOR value is not an array"));
  }
  std::vector<double> output;
  std::size_t cursor = head.value().size;
  std::uint64_t remaining = head.value().argument;
  for (;;) {
    if (head.value().indefinite) {
      if (!encoded.contains(cursor, 1U)) {
        return core::Result<std::vector<double>>::failure(malformed("unterminated CBOR array"));
      }
      if (encoded[cursor] == 0xFFU) break;
    } else if (remaining == 0U) {
      break;
    }
    if (output.size() >= maximum_items) {
      return core::Result<std::vector<double>>::failure(malformed("CBOR array item limit exceeded"));
    }
    const auto end = skip_item(encoded, cursor, 1U);
    if (!end.ok()) return core::Result<std::vector<double>>::failure(end.error());
    const auto value = decode_number_value(encoded.subview(cursor, end.value() - cursor));
    if (!value.ok()) return core::Result<std::vector<double>>::failure(value.error());
    output.push_back(value.value());
    cursor = end.value();
    if (!head.value().indefinite) --remaining;
  }
  return core::Result<std::vector<double>>::success(std::move(output));
}

core::Result<bool> CborMapView::is_null(std::uint64_t key) const {
  const auto* entry = find(key);
  if (entry == nullptr) {
    return core::Result<bool>::failure(malformed("required CBOR key is absent"));
  }
  const auto encoded = bytes_.subview(entry->encoded_value.offset, entry->encoded_value.size);
  return core::Result<bool>::success(encoded.size == 1U && encoded[0] == 0xF6U);
}

core::Result<std::vector<std::uint8_t>> CborMapView::replace(
    std::uint64_t key,
    core::ByteView encoded_value,
    std::size_t region_capacity) const {
  if (encoded_value.empty() || encoded_value.size > maximum_region_size ||
      region_capacity > maximum_region_size) {
    return core::Result<std::vector<std::uint8_t>>::failure(malformed("invalid CBOR replacement bounds"));
  }
  const auto replacement_end = skip_item(encoded_value, 0U, 0U);
  if (!replacement_end.ok() || replacement_end.value() != encoded_value.size) {
    return core::Result<std::vector<std::uint8_t>>::failure(malformed("replacement is not one valid CBOR item"));
  }

  std::vector<std::uint8_t> output;
  output.reserve(region_capacity);
  output.push_back(0xBFU);
  bool replaced = false;
  for (const auto& entry : entries_) {
    const auto key_bytes = bytes_.subview(entry.encoded_key.offset, entry.encoded_key.size);
    output.insert(output.end(), key_bytes.data, key_bytes.data + key_bytes.size);
    if (entry.key == key) {
      output.insert(output.end(), encoded_value.data, encoded_value.data + encoded_value.size);
      replaced = true;
    } else {
      const auto value_bytes = bytes_.subview(entry.encoded_value.offset, entry.encoded_value.size);
      output.insert(output.end(), value_bytes.data, value_bytes.data + value_bytes.size);
    }
  }
  if (!replaced) {
    const auto encoded_key = encode_unsigned(key);
    output.insert(output.end(), encoded_key.begin(), encoded_key.end());
    output.insert(output.end(), encoded_value.data, encoded_value.data + encoded_value.size);
  }
  output.push_back(0xFFU);
  if (output.size() > region_capacity) {
    return core::Result<std::vector<std::uint8_t>>::failure({
        core::ErrorCategory::invalid_openprinttag,
        "updated CBOR map does not fit its allocated region",
        false,
    });
  }
  output.resize(region_capacity, 0U);
  return core::Result<std::vector<std::uint8_t>>::success(std::move(output));
}

std::vector<std::uint8_t> CborMapView::encode_unsigned(std::uint64_t value) {
  std::vector<std::uint8_t> output;
  append_argument(output, 0U, value);
  return output;
}

std::vector<std::uint8_t> CborMapView::encode_number(double value) {
  if (std::isfinite(value) && std::floor(value) == value) {
    std::vector<std::uint8_t> output;
    if (value >= 0.0 && value < std::ldexp(1.0, 64)) {
      append_argument(output, 0U, static_cast<std::uint64_t>(value));
      return output;
    }
    if (value < 0.0 && value >= -std::ldexp(1.0, 63)) {
      const auto argument = value == -std::ldexp(1.0, 63)
                                ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                                : static_cast<std::uint64_t>(-1.0 - value);
      append_argument(output, 1U, argument);
      return output;
    }
  }
  const float compact = static_cast<float>(value);
  if (std::isfinite(compact) &&
      std::fabs(static_cast<double>(compact) - value) <= 0.001) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &compact, sizeof(bits));
    std::vector<std::uint8_t> output{0xFAU};
    for (std::size_t index = 4U; index > 0U; --index) {
      output.push_back(static_cast<std::uint8_t>(bits >> ((index - 1U) * 8U)));
    }
    return output;
  }
  std::uint64_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  std::vector<std::uint8_t> output{0xFBU};
  for (std::size_t index = 8U; index > 0U; --index) {
    output.push_back(static_cast<std::uint8_t>(bits >> ((index - 1U) * 8U)));
  }
  return output;
}

}  // namespace opentag::nfc::openprinttag
