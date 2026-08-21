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
#include "nfc/protocols/nfcv/tag.hpp"

namespace {

using opentag::core::ByteView;
using opentag::core::Error;
using opentag::core::ErrorCategory;
using opentag::core::Result;
using opentag::nfc::nfcv::ITagTransport;
using opentag::nfc::nfcv::Reader;
using opentag::nfc::nfcv::TagGeometry;
using opentag::nfc::nfcv::Uid;
using opentag::nfc::nfcv::VerifiedWriter;
using opentag::nfc::nfcv::WritePlan;
using opentag::nfc::openprinttag::CborMapView;
using opentag::nfc::openprinttag::Codec;

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

class MemoryTransport final : public ITagTransport {
 public:
  std::vector<Uid> present_tags{test_uid()};
  TagGeometry geometry{4U, 4U};
  std::vector<bool> locks{false, false, false, false};
  std::vector<std::uint8_t> memory;
  bool corrupt_readback{false};
  bool inventory_fails{false};
  std::size_t inventory_calls{0U};
  std::size_t write_calls{0U};
  std::size_t field_resets{0U};

  Result<std::vector<Uid>> inventory(std::uint32_t) override {
    ++inventory_calls;
    if (inventory_fails) {
      return Result<std::vector<Uid>>::failure(
          {ErrorCategory::nfc_communication, "inventory timeout", true});
    }
    return Result<std::vector<Uid>>::success(present_tags);
  }

  Result<TagGeometry> read_geometry(const Uid&, std::uint32_t) override {
    return Result<TagGeometry>::success(geometry);
  }

  Result<std::vector<std::uint8_t>> read_single_block(
      const Uid&,
      std::uint16_t block_index,
      std::size_t expected_block_size,
      std::uint32_t) override {
    const auto offset = static_cast<std::size_t>(block_index) * expected_block_size;
    std::vector<std::uint8_t> result(
        memory.begin() + static_cast<std::ptrdiff_t>(offset),
        memory.begin() + static_cast<std::ptrdiff_t>(offset + expected_block_size));
    if (corrupt_readback && !result.empty()) result.front() ^= 0x01U;
    return Result<std::vector<std::uint8_t>>::success(std::move(result));
  }

  Result<std::vector<std::uint8_t>> read_multiple_blocks(
      const Uid&,
      std::uint16_t first_block,
      std::uint16_t block_count,
      std::size_t expected_block_size,
      std::uint32_t) override {
    const auto offset = static_cast<std::size_t>(first_block) * expected_block_size;
    const auto size = static_cast<std::size_t>(block_count) * expected_block_size;
    return Result<std::vector<std::uint8_t>>::success({
        memory.begin() + static_cast<std::ptrdiff_t>(offset),
        memory.begin() + static_cast<std::ptrdiff_t>(offset + size)});
  }

  Result<std::vector<bool>> read_block_locks(
      const Uid&,
      const TagGeometry&,
      std::uint32_t) override {
    return Result<std::vector<bool>>::success(locks);
  }

  Result<void> write_single_block(
      const Uid&,
      std::uint16_t block_index,
      ByteView data,
      std::uint32_t) override {
    ++write_calls;
    const auto offset = static_cast<std::size_t>(block_index) * data.size;
    std::copy(data.data, data.data + data.size, memory.begin() + static_cast<std::ptrdiff_t>(offset));
    return Result<void>::success();
  }

  Result<void> reset_rf_field(std::uint32_t) override {
    ++field_resets;
    return Result<void>::success();
  }
};

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

void test_write_plan_uses_full_changed_blocks_and_rejects_locks() {
  const TagGeometry geometry{4U, 4U};
  std::vector<std::uint8_t> original(16U, 0U);
  auto updated = original;
  updated[5] = 0xA5U;
  updated[6] = 0x5AU;
  const auto plan = WritePlan::build(ByteView(original), ByteView(updated), geometry);
  TEST_ASSERT_TRUE(plan.ok());
  TEST_ASSERT_EQUAL_UINT(1U, plan.value().blocks().size());
  TEST_ASSERT_EQUAL_UINT(1U, plan.value().blocks().front().block_index);
  TEST_ASSERT_EQUAL_UINT(4U, plan.value().blocks().front().data.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(updated.data() + 4, plan.value().blocks().front().data.data(), 4U);

  std::vector<bool> locks(4U, false);
  locks[1] = true;
  const auto rejected = WritePlan::build(ByteView(original), ByteView(updated), geometry, locks);
  TEST_ASSERT_FALSE(rejected.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::tag_write_protected),
      static_cast<int>(rejected.error().category));
}

void test_verified_writer_checks_presence_and_readback() {
  const TagGeometry geometry{4U, 4U};
  std::vector<std::uint8_t> original(16U, 0U);
  auto updated = original;
  updated[1] = 1U;
  updated[9] = 2U;
  const auto plan = WritePlan::build(ByteView(original), ByteView(updated), geometry);
  TEST_ASSERT_TRUE(plan.ok());

  MemoryTransport transport;
  transport.memory = original;
  VerifiedWriter writer(transport);
  TEST_ASSERT_TRUE(writer.execute(test_uid(), plan.value(), 100U).ok());
  TEST_ASSERT_EQUAL_UINT(2U, transport.inventory_calls);
  TEST_ASSERT_EQUAL_UINT(2U, transport.write_calls);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(updated.data(), transport.memory.data(), updated.size());

  transport.memory = original;
  transport.corrupt_readback = true;
  const auto mismatch = writer.execute(test_uid(), plan.value(), 100U);
  TEST_ASSERT_FALSE(mismatch.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::nfc_communication),
      static_cast<int>(mismatch.error().category));
}

void test_verified_writer_aborts_if_tag_changes() {
  const TagGeometry geometry{4U, 1U};
  const std::vector<std::uint8_t> original(4U, 0U);
  const std::vector<std::uint8_t> updated(4U, 1U);
  const auto plan = WritePlan::build(ByteView(original), ByteView(updated), geometry);
  TEST_ASSERT_TRUE(plan.ok());

  MemoryTransport transport;
  transport.memory = original;
  transport.present_tags.front().bytes = {
      0xE0, 0x04, 0x02, 0x08, 0x66, 0x2F, 0x6F, 0xBC};
  VerifiedWriter writer(transport);
  const auto result = writer.execute(test_uid(), plan.value(), 100U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::tag_removed),
      static_cast<int>(result.error().category));
  TEST_ASSERT_EQUAL_UINT(0U, transport.write_calls);
}

void test_reader_discovers_geometry_locks_and_full_image() {
  MemoryTransport transport;
  transport.memory.resize(16U);
  for (std::size_t index = 0U; index < transport.memory.size(); ++index) {
    transport.memory[index] = static_cast<std::uint8_t>(index);
  }
  transport.locks[2] = true;
  Reader reader(transport);
  const auto detected = reader.detect_one(100U);
  TEST_ASSERT_TRUE(detected.ok());
  TEST_ASSERT_EQUAL_UINT(4U, detected.value().geometry.block_size);
  TEST_ASSERT_EQUAL_UINT(4U, detected.value().geometry.block_count);
  TEST_ASSERT_TRUE(detected.value().locked_blocks[2]);
  const auto image = reader.read_image(detected.value(), 100U);
  TEST_ASSERT_TRUE(image.ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(transport.memory.data(), image.value().data(), 16U);
}

void test_reader_rejects_multiple_tags() {
  MemoryTransport transport;
  transport.present_tags.push_back(test_uid());
  transport.present_tags.back().bytes[2] ^= 0x01U;
  Reader reader(transport);
  const auto result = reader.detect_one(100U);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ErrorCategory::multiple_tags),
      static_cast<int>(result.error().category));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_official_openprinttag_fixture);
  RUN_TEST(test_update_preserves_unknown_main_bytes_and_other_regions);
  RUN_TEST(test_fixture_truncations_and_byte_mutations_fail_safely);
  RUN_TEST(test_rejects_truncated_oversized_and_invalid_region_images);
  RUN_TEST(test_cbor_limits_nesting_and_duplicate_keys);
  RUN_TEST(test_normalizes_iso15693_wire_uid);
  RUN_TEST(test_write_plan_uses_full_changed_blocks_and_rejects_locks);
  RUN_TEST(test_verified_writer_checks_presence_and_readback);
  RUN_TEST(test_verified_writer_aborts_if_tag_changes);
  RUN_TEST(test_reader_discovers_geometry_locks_and_full_image);
  RUN_TEST(test_reader_rejects_multiple_tags);
  return UNITY_END();
}
