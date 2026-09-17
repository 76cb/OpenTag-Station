#pragma once
#include "nfc/openprinttag_writer.hpp"
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include <algorithm>
#include <cstring>

namespace opentag::nfc {
// V3 adds explicit operation and durable blank/owner cleanup checkpoints.
// V1/V2 migrate as WRITE, never as permission to clear unknown content.
using WriterJournalRecord = std::array<std::uint8_t, 836>;
inline void journal_put(std::uint8_t *r, std::size_t offset, std::uint32_t n) {
  for (unsigned i = 0; i < 4; ++i)
    r[offset + i] = n >> (i * 8);
}
inline std::uint32_t journal_get(const std::uint8_t *r, std::size_t offset) {
  std::uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i)
    n |= std::uint32_t(r[offset + i]) << (i * 8);
  return n;
}
inline void encode_writer_journal(WriterJournalRecord &r, const WriterPlan &p,
                                  std::int32_t spool, std::uint32_t backend) {
  r.fill(0);
  std::memcpy(r.data(), "OPTWR003", 8);
  std::copy(p.uid.bytes.begin(), p.uid.bytes.end(), r.begin() + 8);
  std::copy(p.system.bytes.begin(), p.system.bytes.end(), r.begin() + 16);
  r[80] = p.system.length;
  std::copy(p.security.begin(), p.security.end(), r.begin() + 81);
  std::copy(p.original.begin(), p.original.end(), r.begin() + 161);
  std::copy(p.target.begin(), p.target.end(), r.begin() + 481);
  journal_put(r.data(), 801, spool);
  journal_put(r.data(), 805, backend);
  journal_put(r.data(), 809, p.previous_spool_id);
  r[813] = static_cast<std::uint8_t>(p.operation);
  r[814] = (p.cleanup_pending ? 1 : 0) | (p.cleanup_owner_bound ? 2 : 0);
  r[815] = p.cleared_instance.has_value();
  if (p.cleared_instance)
    std::copy(p.cleared_instance->begin(), p.cleared_instance->end(),
              r.begin() + 816);
  journal_put(r.data(), 832, nfcv::diagnostic_checksum(r.data(), 832));
}
inline bool decode_writer_journal(core::ByteView bytes, WriterPlan &p,
                                  std::int32_t &spool, std::uint32_t &backend) {
  const auto *r = bytes.data;
  const auto n = bytes.size;
  if (!r || (n != 813 && n != 817 && n != 836))
    return false;
  const char *magic = n == 813   ? "OPTWR001"
                      : n == 817 ? "OPTWR002"
                                 : "OPTWR003";
  if (std::memcmp(r, magic, 8) || r[80] > 64 ||
      journal_get(r, n - 4) != nfcv::diagnostic_checksum(r, n - 4))
    return false;
  if (n == 836 && (r[813] > 1 || r[814] > 3 || r[815] > 1))
    return false;
  p.operation = n == 836 ? static_cast<WriterPlan::Operation>(r[813])
                         : WriterPlan::Operation::write;
  p.cleanup_pending = n == 836 && (r[814] & 1);
  p.cleanup_owner_bound = n == 836 && (r[814] & 2);
  p.cleared_instance.reset();
  if (n == 836 && r[815]) {
    p.cleared_instance.emplace();
    std::copy_n(r + 816, 16, p.cleared_instance->begin());
  }
  std::copy_n(r + 8, 8, p.uid.bytes.begin());
  std::copy_n(r + 16, 64, p.system.bytes.begin());
  p.system.length = r[80];
  std::copy_n(r + 81, 80, p.security.begin());
  std::copy_n(r + 161, 320, p.original.begin());
  std::copy_n(r + 481, 320, p.target.begin());
  p.geometry = {4, 80};
  p.attempted = true;
  p.verified = p.cleanup_pending;
  spool = journal_get(r, 801);
  backend = journal_get(r, 805);
  p.previous_spool_id = n == 813 ? 0 : journal_get(r, 809);
  if (p.operation == WriterPlan::Operation::write)
    return spool > 0 && p.previous_spool_id >= 0 &&
           p.previous_spool_id != spool && !p.cleanup_pending &&
           !p.cleanup_owner_bound && !p.cleared_instance;
  return spool >= 0 && p.previous_spool_id == 0 &&
         (!p.cleanup_owner_bound || p.cleanup_pending) &&
         std::all_of(p.target.begin(), p.target.begin() + 312,
                     [](auto b) { return b == 0; }) &&
         std::equal(p.target.begin() + 312, p.target.end(),
                    p.original.begin() + 312);
}
} // namespace opentag::nfc
