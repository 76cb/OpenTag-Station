#include <unity.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/byte_view.hpp"
#include "core/error.hpp"
#include "core/result.hpp"
#include "nfc/formats/openprinttag/cbor.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/formats/openprinttag/initializer.hpp"
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "../fixtures/openprinttag_initializer_7e09cc3_312_aux32.hpp"

namespace {

using opentag::core::ByteView;
using opentag::core::Error;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::nfc::nfcv::TagGeometry;
using opentag::nfc::nfcv::Uid;
using opentag::nfc::openprinttag::CborMapView;
using opentag::nfc::openprinttag::Codec;
using opentag::nfc::openprinttag::DecodedTag;
using opentag::nfc::openprinttag::Initializer;
using opentag::nfc::openprinttag::NfcvInitializationConfig;

constexpr const char* official_fixture_01 =
    "e140270103ff012f910117550433647461672e6f72672f732f33333463353466303838521cf5"
    "6170706c69636174696f6e2f766e642e6f70656e7072696e74746167a10218d2bf041b000007"
    "d0fcab45f9056a33333463353466303838080009000a76504c412050727573612047616c617879"
    "20426c61636b0b6950727573616d656e740e1a68d3c7d7101903e8111903f41219011813443d"
    "3e3dff181c9f17ff181df93cf6182218cd182318e1182418aa182518281826183c182712182818"
    "28182914182a1840182b18c8182c1864182d183418389f0001ff183b831832fa4134cccdfa4301"
    "4ccd183c69323730203330203230ff000000000000000000000000000000000000000000000000"
    "000000000000a000000000000000000000000000000000000000000000000000000000000000"
    "000000fe";

constexpr const char* official_unknown_fixture =
    "e140270103ff012fc21c0000010d6170706c69636174696f6e2f766e642e6f70"
    "656e7072696e74746167a10218eabf08001926fd6d48656c6c6f2c20776f726c"
    "6421ff0000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000a00000000000000000000000"
    "0000000000000000000000000000000000000000000000fe";

std::uint8_t nibble(char value) {
  if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
  return static_cast<std::uint8_t>(value - 'A' + 10);
}

std::vector<std::uint8_t> from_hex(const char* hex) {
  const std::string text(hex);
  std::vector<std::uint8_t> result;
  result.reserve(text.size() / 2U);
  for (std::size_t index = 0U; index < text.size(); index += 2U) {
    result.push_back(static_cast<std::uint8_t>(
        (nibble(text[index]) << 4U) | nibble(text[index + 1U])));
  }
  return result;
}

Uid test_uid() {
  const std::array<std::uint8_t, 8> bytes{0xE0, 0x04, 0x01, 0x08, 0x66, 0x2F, 0x6F, 0xBC};
  return Uid::from_network_order(ByteView(bytes.data(), bytes.size())).value();
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_decodes_official_openprinttag_fixture() {
  const auto image = from_hex(official_fixture_01);
  TEST_ASSERT_EQUAL_UINT(312U, image.size());
  const auto decoded = Codec::decode(ByteView(image));
  TEST_ASSERT_TRUE(decoded.ok());
  TEST_ASSERT_EQUAL_UINT(312U, decoded.value().envelope.capability_capacity);
  TEST_ASSERT_EQUAL_UINT(4U, decoded.value().envelope.ndef_tlv_offset);
  TEST_ASSERT_EQUAL_UINT(8U, decoded.value().envelope.ndef_message_offset);
  TEST_ASSERT_EQUAL_UINT(66U, decoded.value().envelope.payload_offset);
  TEST_ASSERT_EQUAL_UINT(245U, decoded.value().envelope.payload_size);
  TEST_ASSERT_EQUAL_UINT(70U, decoded.value().envelope.main.absolute_offset);
  TEST_ASSERT_EQUAL_UINT(206U, decoded.value().envelope.main.size);
  TEST_ASSERT_TRUE(decoded.value().envelope.auxiliary.has_value());
  TEST_ASSERT_EQUAL_UINT(276U, decoded.value().envelope.auxiliary->absolute_offset);
  TEST_ASSERT_EQUAL_UINT(35U, decoded.value().envelope.auxiliary->size);
  TEST_ASSERT_EQUAL_UINT64(8594173675001ULL, *decoded.value().material.gtin);
  TEST_ASSERT_EQUAL_STRING("PLA Prusa Galaxy Black", decoded.value().material.material_name->c_str());
  TEST_ASSERT_EQUAL_STRING("Prusament", decoded.value().material.brand_name->c_str());
  TEST_ASSERT_EQUAL_INT64(1758709719, *decoded.value().material.manufactured_date);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1012.0F, static_cast<float>(*decoded.value().material.actual_netto_full_weight));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 1.24F, static_cast<float>(*decoded.value().material.density));
  TEST_ASSERT_TRUE(decoded.value().material.primary_color_lab.has_value());
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 129.3F,
      static_cast<float>((*decoded.value().material.primary_color_lab)[2]));
  TEST_ASSERT_EQUAL_STRING("270 30 20", decoded.value().material.primary_color_ral->c_str());
  TEST_ASSERT_EQUAL_INT(64, *decoded.value().material.container_width);
  TEST_ASSERT_EQUAL_INT(200, *decoded.value().material.container_outer_diameter);
  TEST_ASSERT_EQUAL_INT(100, *decoded.value().material.container_inner_diameter);
  TEST_ASSERT_EQUAL_INT(52, *decoded.value().material.container_hole_diameter);
  TEST_ASSERT_EQUAL_UINT(2U, decoded.value().material.certifications.size());
  TEST_ASSERT_EQUAL_INT(205, *decoded.value().material.min_print_temperature);
  TEST_ASSERT_EQUAL_INT(225, *decoded.value().material.max_print_temperature);
  TEST_ASSERT_TRUE(decoded.value().material.validation.valid());
}

void test_update_preserves_unknown_main_bytes_and_other_regions() {
  const auto image = from_hex(official_unknown_fixture);
  const auto before = Codec::decode(ByteView(image));
  TEST_ASSERT_TRUE(before.ok());
  TEST_ASSERT_EQUAL_UINT(1U, before.value().material.unknown_main_fields);
  const auto updated = Codec::update_consumed_weight(ByteView(image), 42.5);
  TEST_ASSERT_TRUE(updated.ok());
  const auto after = Codec::decode(ByteView(updated.value()));
  TEST_ASSERT_TRUE(after.ok());
  TEST_ASSERT_FLOAT_WITHIN(
      0.001F, 42.5F, static_cast<float>(*after.value().material.consumed_weight));
  TEST_ASSERT_EQUAL_UINT(1U, after.value().material.unknown_main_fields);

  const auto aux = *before.value().envelope.auxiliary;
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      image.data() + before.value().envelope.main.absolute_offset,
      updated.value().data() + before.value().envelope.main.absolute_offset,
      before.value().envelope.main.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(image.data(), updated.value().data(), aux.absolute_offset);
  const auto tail = aux.absolute_offset + aux.size;
  TEST_ASSERT_EQUAL_UINT8_ARRAY(image.data() + tail, updated.value().data() + tail, image.size() - tail);
}

void test_rejects_truncated_oversized_and_invalid_region_images() {
  const auto fixture = from_hex(official_fixture_01);
  std::vector<std::uint8_t> truncated(fixture.begin(), fixture.begin() + 100);
  TEST_ASSERT_FALSE(Codec::decode(ByteView(truncated)).ok());

  std::vector<std::uint8_t> oversized(Codec::maximum_tag_image_size + 1U, 0U);
  TEST_ASSERT_FALSE(Codec::decode(ByteView(oversized)).ok());

  auto invalid_offset = fixture;
  invalid_offset[69] = 0xFFU;
  TEST_ASSERT_FALSE(Codec::decode(ByteView(invalid_offset)).ok());

  auto write_protected = fixture;
  write_protected[1] = static_cast<std::uint8_t>(write_protected[1] | 0x0FU);
  const auto refused = Codec::update_consumed_weight(ByteView(write_protected), 12.0);
  TEST_ASSERT_FALSE(refused.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::tag_write_protected),
      static_cast<int>(refused.error().category));
}

void test_fixture_truncations_and_byte_mutations_fail_safely() {
  const auto fixture = from_hex(official_fixture_01);
  for (std::size_t length = 0U; length < fixture.size(); ++length) {
    const auto decoded = Codec::decode(ByteView(fixture.data(), length));
    TEST_ASSERT_FALSE_MESSAGE(
        decoded.ok(), "a truncated declared-capacity image was accepted");
  }

  for (std::size_t index = 0U; index < fixture.size(); ++index) {
    auto mutated = fixture;
    mutated[index] ^= static_cast<std::uint8_t>(0xA5U + (index & 0x0FU));
    const auto decoded = Codec::decode(ByteView(mutated));
    if (decoded.ok()) {
      const auto& envelope = decoded.value().envelope;
      TEST_ASSERT_TRUE(envelope.capability_capacity <= mutated.size());
      TEST_ASSERT_TRUE(envelope.payload_offset <= envelope.capability_capacity);
      TEST_ASSERT_TRUE(
          envelope.payload_size <= envelope.capability_capacity - envelope.payload_offset);
    }
  }
}

void test_cbor_limits_nesting_and_duplicate_keys() {
  std::vector<std::uint8_t> nested{0xBFU, 0x00U};
  nested.insert(nested.end(), 14U, 0x9FU);
  nested.push_back(0x00U);
  nested.insert(nested.end(), 14U, 0xFFU);
  nested.push_back(0xFFU);
  TEST_ASSERT_FALSE(CborMapView::parse(ByteView(nested)).ok());

  const std::vector<std::uint8_t> duplicate{0xBFU, 0x01U, 0x01U, 0x01U, 0x02U, 0xFFU};
  TEST_ASSERT_FALSE(CborMapView::parse(ByteView(duplicate)).ok());

  const std::vector<std::uint8_t> invalid_utf8{0xBFU, 0x01U, 0x62U, 0xC0U, 0xAFU, 0xFFU};
  const auto invalid_text = CborMapView::parse(ByteView(invalid_utf8));
  TEST_ASSERT_TRUE(invalid_text.ok());
  TEST_ASSERT_FALSE(invalid_text.value().read_text(1U).ok());
}

void test_normalizes_iso15693_wire_uid() {
  const std::array<std::uint8_t, 8> wire{0xBC, 0x6F, 0x2F, 0x66, 0x08, 0x01, 0x04, 0xE0};
  const auto uid = Uid::from_wire_lsb_first(ByteView(wire.data(), wire.size()));
  TEST_ASSERT_TRUE(uid.ok());
  TEST_ASSERT_EQUAL_STRING("E0040108662F6FBC", uid.value().hex().c_str());
}

void test_initializer_matches_pinned_python_golden_vector_and_decodes() {
  const auto expected = from_hex(openprinttag_initializer_7e09cc3_312_aux32_hex);
  const NfcvInitializationConfig config{312U, 4U, 32U, std::nullopt};
  const auto first = Initializer::generate(config);
  const auto second = Initializer::generate(config);
  TEST_ASSERT_TRUE(first.ok());
  TEST_ASSERT_TRUE(second.ok());
  TEST_ASSERT_EQUAL_UINT(312U, first.value().bytes.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      expected.data(), first.value().bytes.data(), expected.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      first.value().bytes.data(), second.value().bytes.data(), expected.size());
  TEST_ASSERT_EQUAL_HEX32(
      0x6B6EABF1U,
      opentag::nfc::nfcv::diagnostic_checksum(
          first.value().bytes.data(), first.value().bytes.size()));
  TEST_ASSERT_EQUAL_UINT8(0xE1U, first.value().bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0x40U, first.value().bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(0x27U, first.value().bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(0x01U, first.value().bytes[3]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, first.value().bytes[4]);
  TEST_ASSERT_EQUAL_UINT8(0xFFU, first.value().bytes[5]);
  TEST_ASSERT_EQUAL_UINT8(0xFEU, first.value().bytes.back());

  const auto decoded = Codec::decode(ByteView(first.value().bytes));
  TEST_ASSERT_TRUE(decoded.ok());
  DecodedTag decoded_into;
  const auto decoded_into_status =
      Codec::decode(ByteView(first.value().bytes), decoded_into);
  TEST_ASSERT_TRUE(decoded_into_status.ok());
  TEST_ASSERT_EQUAL_UINT(
      decoded.value().envelope.payload_offset,
      decoded_into.envelope.payload_offset);
  TEST_ASSERT_EQUAL_UINT(
      decoded.value().envelope.auxiliary->absolute_offset,
      decoded_into.envelope.auxiliary->absolute_offset);
  TEST_ASSERT_EQUAL_UINT(312U, decoded.value().envelope.capability_capacity);
  TEST_ASSERT_EQUAL_UINT(42U, decoded.value().envelope.payload_offset);
  TEST_ASSERT_EQUAL_UINT(269U, decoded.value().envelope.payload_size);
  TEST_ASSERT_EQUAL_UINT(4U, decoded.value().envelope.meta.used_size);
  TEST_ASSERT_EQUAL_UINT(1U, decoded.value().envelope.main.used_size);
  TEST_ASSERT_TRUE(decoded.value().envelope.auxiliary.has_value());
  TEST_ASSERT_EQUAL_UINT(234U, first.value().auxiliary_region_offset.value());
  TEST_ASSERT_EQUAL_UINT(276U, decoded.value().envelope.auxiliary->absolute_offset);
  TEST_ASSERT_EQUAL_UINT(35U, decoded.value().envelope.auxiliary->size);
  TEST_ASSERT_EQUAL_UINT(1U, decoded.value().envelope.auxiliary->used_size);
  TEST_ASSERT_EQUAL_UINT(0U, decoded.value().material.unknown_main_fields);
  TEST_ASSERT_EQUAL_UINT(0U, decoded.value().material.unknown_auxiliary_fields);
}

void test_initializer_handles_short_tlv_aux_alignment_and_bounds() {
  const auto short_image = Initializer::generate({136U, 1U, std::nullopt, std::nullopt});
  TEST_ASSERT_TRUE(short_image.ok());
  TEST_ASSERT_EQUAL_UINT(136U, short_image.value().bytes.size());
  TEST_ASSERT_EQUAL_UINT8(0x03U, short_image.value().bytes[4]);
  TEST_ASSERT_NOT_EQUAL(0xFFU, short_image.value().bytes[5]);
  TEST_ASSERT_EQUAL_UINT8(0xD2U, short_image.value().bytes[6]);

  const auto with_aux = Initializer::generate({312U, 4U, 32U, std::nullopt});
  TEST_ASSERT_TRUE(with_aux.ok());
  TEST_ASSERT_TRUE(with_aux.value().auxiliary_region_offset.has_value());
  TEST_ASSERT_EQUAL_UINT(
      0U,
      (with_aux.value().payload_offset +
       *with_aux.value().auxiliary_region_offset) % 4U);
  const auto decoded = Codec::decode(ByteView(with_aux.value().bytes));
  TEST_ASSERT_TRUE(decoded.ok());
  TEST_ASSERT_TRUE(decoded.value().envelope.auxiliary.has_value());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(32U, decoded.value().envelope.auxiliary->size);

  TEST_ASSERT_FALSE(Initializer::generate({310U, 4U, std::nullopt, std::nullopt}).ok());
  TEST_ASSERT_FALSE(Initializer::generate({2048U, 4U, std::nullopt, std::nullopt}).ok());
  TEST_ASSERT_FALSE(Initializer::generate({312U, 0U, std::nullopt, std::nullopt}).ok());
  TEST_ASSERT_FALSE(Initializer::generate({312U, 4U, 4U, std::nullopt}).ok());
  TEST_ASSERT_FALSE(Initializer::generate({312U, 4U, 269U, std::nullopt}).ok());
  TEST_ASSERT_FALSE(Initializer::generate({312U, 4U, std::nullopt, 0U}).ok());
}

void test_initializer_image_decodes_with_auxiliary_region() {
  const auto generated = Initializer::generate({312U, 4U, 32U, std::nullopt});
  TEST_ASSERT_TRUE(generated.ok());
  TEST_ASSERT_EQUAL_UINT(312U, generated.value().bytes.size());
  // The tag has 320 bytes; the last 8 belong to the blocks the writer never touches.
  std::vector<std::uint8_t> image(320U, 0U);
  std::copy(generated.value().bytes.begin(), generated.value().bytes.end(), image.begin());
  const auto decoded = Codec::decode(ByteView(image));
  TEST_ASSERT_TRUE(decoded.ok());
  TEST_ASSERT_EQUAL_UINT(312U, decoded.value().envelope.capability_capacity);
  TEST_ASSERT_TRUE(decoded.value().envelope.auxiliary.has_value());
  TEST_ASSERT_EQUAL_UINT(1U, decoded.value().envelope.auxiliary->used_size);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_official_openprinttag_fixture);
  RUN_TEST(test_update_preserves_unknown_main_bytes_and_other_regions);
  RUN_TEST(test_fixture_truncations_and_byte_mutations_fail_safely);
  RUN_TEST(test_rejects_truncated_oversized_and_invalid_region_images);
  RUN_TEST(test_cbor_limits_nesting_and_duplicate_keys);
  RUN_TEST(test_normalizes_iso15693_wire_uid);
  RUN_TEST(test_initializer_matches_pinned_python_golden_vector_and_decodes);
  RUN_TEST(test_initializer_handles_short_tlv_aux_alignment_and_bounds);
  RUN_TEST(test_initializer_image_decodes_with_auxiliary_region);
  return UNITY_END();
}
