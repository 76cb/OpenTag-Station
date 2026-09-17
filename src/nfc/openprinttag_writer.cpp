#include "nfc/openprinttag_writer.hpp"
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include <algorithm>
#include <cstring>

namespace opentag::nfc {
namespace {
struct WriterFieldScope {
  IWriterReader &reader;
  bool closed{false};
  ~WriterFieldScope() {
    if (!closed)
      (void)reader.field_off();
  }
  core::Result<void> close() {
    closed = true;
    auto off = reader.field_off();
    if (!off.ok())
      return off;
    auto health = reader.health();
    if (!health.ok())
      return health;
    if (reader.bus_errors())
      return core::Result<void>::failure(
          {core::ErrorCategory::nfc_communication,
           "NFC bus errors after field shutdown", false});
    return core::Result<void>::success();
  }
};
core::Result<void>
fail(const char *message,
     core::ErrorCategory category = core::ErrorCategory::conflict) {
  return core::Result<void>::failure({category, message, false});
}
} // namespace
core::Result<void> OpenPrintTagWriter::fence(const WriterPlan &p) {
  if (static_cast<std::uint32_t>(reader_.now_ms() - started_ms_) > 120000U)
    return fail("Physical NFC operation exceeded its bounded deadline");
  if (generation_() != p.generation)
    return fail("Tag generation changed; read and preview again");
  auto health = reader_.health();
  if (!health.ok())
    return health;
  if (reader_.bus_errors())
    return fail("NFC bus errors must be zero",
                core::ErrorCategory::nfc_communication);
  auto tags = reader_.inventory();
  if (!tags.ok())
    return core::Result<void>::failure(tags.error());
  if (tags.value().size() != 1 || tags.value()[0] != p.uid)
    return fail("Tag removed, replaced, or multiple tags present");
  auto geometry = reader_.geometry(p.uid);
  if (!geometry.ok())
    return core::Result<void>::failure(geometry.error());
  if (geometry.value().block_size != p.geometry.block_size ||
      geometry.value().block_count != p.geometry.block_count)
    return fail("Tag geometry changed");
  auto system = reader_.writer_system_information(p.uid);
  if (!system.ok())
    return core::Result<void>::failure(system.error());
  if (system.value() != p.system)
    return fail("Tag system information changed");
  return core::Result<void>::success();
}
core::Result<void> OpenPrintTagWriter::full_read(WriterPlan &p,
                                                 std::uint8_t *destination) {
  for (std::size_t block = 0; block < p.geometry.block_count; ++block) {
    auto check = fence(p);
    if (!check.ok())
      return check;
    std::uint8_t security = 0;
    auto read =
        reader_.security_read(p.uid, block, destination + block * 4, security);
    if (!read.ok())
      return read;
    if (security != p.security[block])
      return fail("Tag protection state changed");
    reader_.yield_between_chunks();
  }
  return fence(p);
}
core::Result<void> OpenPrintTagWriter::read(WriterPlan &p,
                                            const WriterPlan *interrupted) {
  started_ms_ = reader_.now_ms();
  WriterFieldScope field{reader_};
  p.attempted = p.verified = false;
  p.recovery = false;
  p.journal_state = WriterPlan::JournalState::none;
  p.count = p.completed = 0;
  p.generation = generation_();
  auto on = reader_.field_on();
  if (!on.ok())
    return on;
  auto tags = reader_.inventory();
  if (!tags.ok())
    return core::Result<void>::failure(tags.error());
  if (tags.value().size() != 1)
    return fail("Place exactly one stable NFC-V tag");
  p.uid = tags.value()[0];
  auto geometry = reader_.geometry(p.uid);
  if (!geometry.ok())
    return core::Result<void>::failure(geometry.error());
  p.geometry = geometry.value();
  if (p.uid.bytes[0] != 0xe0 || p.uid.bytes[1] != 0x04 ||
      p.geometry.block_size != 4 || p.geometry.block_count != 80)
    return fail(
        "Incompatible tag: this release approves NXP 80 x 4-byte SLIX2 only",
        core::ErrorCategory::unsupported_tag);
  auto system = reader_.writer_system_information(p.uid);
  if (!system.ok())
    return core::Result<void>::failure(system.error());
  p.system = system.value();
  for (std::size_t block = 0; block < 80; ++block) {
    auto check = fence(p);
    if (!check.ok())
      return check;
    auto result = reader_.security_read(
        p.uid, block, p.original.data() + block * 4, p.security[block]);
    if (!result.ok())
      return result;
    if (block < 78 && p.security[block] != 0)
      return fail("Tag has protected blocks",
                  core::ErrorCategory::tag_write_protected);
    reader_.yield_between_chunks();
  }
  auto repeated = full_read(p, p.scratch.data());
  if (!repeated.ok())
    return repeated;
  if (p.original != p.scratch)
    return fail("Unstable tag content; preview again");
  // A valid CBOR decode is not evidence that an interrupted transaction is
  // intact. Classify all physical blocks first, including the preserved tail.
  if (interrupted && interrupted->attempted && !interrupted->verified &&
      interrupted->uid == p.uid) {
    if (interrupted->geometry.block_count != p.geometry.block_count ||
        interrupted->geometry.block_size != p.geometry.block_size ||
        interrupted->system != p.system || interrupted->security != p.security)
      return fail(
          "Tag geometry/system/protection differs from recovery journal");
    for (std::size_t b = 0; b < p.geometry.block_count; ++b) {
      const auto *value = p.original.data() + 4 * b;
      if (std::memcmp(value, interrupted->original.data() + 4 * b, 4) &&
          std::memcmp(value, interrupted->target.data() + 4 * b, 4))
        return fail("Torn/unknown block differs from both journal images; "
                    "refusing rewrite");
    }
    p.journal_state = p.original == interrupted->target
                          ? WriterPlan::JournalState::target
                      : p.original == interrupted->original
                          ? WriterPlan::JournalState::original
                          : WriterPlan::JournalState::partial;
    p.recovery = p.journal_state == WriterPlan::JournalState::partial;
  }
  p.blank = std::all_of(p.original.begin(),
                        p.original.begin() + WriterPlan::usable_bytes,
                        [](auto byte) { return byte == 0; });
  if (!p.blank) {
    auto decoded = openprinttag::Codec::decode(
        {p.original.data(), WriterPlan::usable_bytes}, p.current);
    if (!decoded.ok()) {
      // An original image can itself be an authorized partial image if a
      // recovery was interrupted again before any additional block changed.
      p.recovery =
          p.recovery || p.journal_state == WriterPlan::JournalState::original;
      if (!p.recovery)
        return fail("Unsupported or incomplete OpenPrintTag; refusing unknown "
                    "data overwrite",
                    core::ErrorCategory::unsupported_tag);
    }
    // The accepted diagnostic initializer has an empty main map. It is a
    // recognized OpenPrintTag envelope, although not yet a populated spool.
    if (p.current.material.write_protection.value_or(0) != 0 ||
        (p.current.envelope.capability_access & 0x0f))
      return fail("OpenPrintTag declares write protection",
                  core::ErrorCategory::tag_write_protected);
  }
  p.target = p.original;
  p.current_checksum =
      nfcv::diagnostic_checksum(p.original.data(), p.original.size());
  return field.close();
}
core::Result<void> OpenPrintTagWriter::plan(WriterPlan &p) {
  p.count = p.completed = 0;
  if (!std::equal(p.original.begin() + WriterPlan::usable_bytes,
                  p.original.end(),
                  p.target.begin() + WriterPlan::usable_bytes))
    return fail("Target changes preserved blocks 78-79");
  auto decoded = openprinttag::Codec::decode(
      {p.target.data(), WriterPlan::usable_bytes}, p.proposed);
  if (!decoded.ok())
    return decoded;
  if (!p.proposed.material.validation.valid() ||
      !p.proposed.material.instance_uuid)
    return fail("Target metadata is not a valid identified OpenPrintTag");
  // Commit the block containing the instance UUID last. No extra writes of
  // unchanged blocks: this reduces, but cannot eliminate, partial-write risk.
  const auto identity_block = p.proposed.envelope.main.absolute_offset / 4;
  const auto identity_end = (p.proposed.envelope.main.absolute_offset + 18) / 4;
  for (std::size_t block = 0; block < 78; ++block) {
    if (std::memcmp(p.original.data() + block * 4, p.target.data() + block * 4,
                    4) &&
        block != 0 && (block < identity_block || block > identity_end))
      p.blocks[p.count++] = block;
  }
  for (std::size_t block = identity_block; block <= identity_end; ++block)
    if (std::memcmp(p.original.data() + block * 4, p.target.data() + block * 4,
                    4))
      p.blocks[p.count++] = block;
  if (std::memcmp(p.original.data(), p.target.data(), 4))
    p.blocks[p.count++] = 0;
  p.target_checksum =
      nfcv::diagnostic_checksum(p.target.data(), p.target.size());
  return core::Result<void>::success();
}
core::Result<void> OpenPrintTagWriter::execute(WriterPlan &p,
                                               const Progress &progress) {
  if (p.verified)
    return core::Result<void>::success();
  started_ms_ = reader_.now_ms();
  WriterFieldScope field{reader_};
  auto enabled = reader_.field_on();
  if (!enabled.ok())
    return enabled;
  if (p.attempted)
    return fail("Previous write was incomplete; reread actual tag before a new "
                "preview");
  progress("validating", 0, p.count);
  auto before = full_read(p, p.scratch.data());
  if (!before.ok())
    return before;
  if (p.scratch != p.original)
    return fail("Tag bytes changed after preview");
  p.attempted = true;
  for (std::size_t i = 0; i < p.count; ++i) {
    auto check = fence(p);
    if (!check.ok())
      return check;
    const auto block = p.blocks[i];
    std::uint8_t security = 0;
    auto current =
        reader_.security_read(p.uid, block, p.scratch.data(), security);
    if (!current.ok())
      return current;
    if (security != p.security[block] ||
        std::memcmp(p.scratch.data(), p.original.data() + block * 4, 4))
      return fail("Block content/protection changed before write");
    progress("writing", i + 1, p.count);
    auto write = reader_.commit_openprinttag_block(p.uid, block,
                                                   p.target.data() + block * 4);
    if (!write.ok())
      return write;
    auto readback =
        reader_.security_read(p.uid, block, p.scratch.data(), security);
    if (!readback.ok())
      return readback;
    if (security != p.security[block] ||
        std::memcmp(p.scratch.data(), p.target.data() + block * 4, 4))
      return fail("Block readback mismatch; write stopped",
                  core::ErrorCategory::nfc_crc);
    ++p.completed;
    reader_.yield_between_chunks();
  }
  progress("verifying", p.completed, p.count);
  auto final = full_read(p, p.scratch.data());
  if (!final.ok())
    return final;
  if (p.scratch != p.target)
    return fail("Complete physical readback mismatch",
                core::ErrorCategory::nfc_crc);
  progress("decoding", p.completed, p.count);
  auto decoded = openprinttag::Codec::decode(
      {p.scratch.data(), WriterPlan::usable_bytes}, p.verified_tag);
  if (!decoded.ok())
    return decoded;
  // Identical complete bytes through the same deterministic codec imply exact
  // semantics, with explicit checks on the association-critical values too.
  if (!p.verified_tag.material.validation.valid() ||
      p.verified_tag.material.instance_uuid !=
          p.proposed.material.instance_uuid ||
      p.verified_tag.material.consumed_weight !=
          p.proposed.material.consumed_weight)
    return fail("Final decoded semantic verification failed");
  auto check = fence(p);
  if (!check.ok())
    return check;
  auto off = field.close();
  if (!off.ok())
    return off;
  p.verified = true;
  return core::Result<void>::success();
}
} // namespace opentag::nfc
