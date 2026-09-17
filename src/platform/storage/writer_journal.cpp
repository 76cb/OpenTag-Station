#if defined(ARDUINO_ARCH_ESP32)
#include "platform/storage/writer_journal.hpp"
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include <LittleFS.h>
#include <cstring>
namespace opentag::platform::storage {
namespace {
constexpr const char *path = "/writer-recovery.bin";
constexpr const char *staging = "/writer-recovery.new";
using Record = std::array<std::uint8_t, 817>;
void put(Record &r, std::size_t offset, std::uint32_t n) {
  for (unsigned i = 0; i < 4; ++i)
    r[offset + i] = n >> (i * 8);
}
std::uint32_t get(const Record &r, std::size_t offset) {
  std::uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i)
    n |= std::uint32_t(r[offset + i]) << (i * 8);
  return n;
}
class Journal final : public nfc::WriterJournal {
public:
  bool load(nfc::WriterPlan &p, std::int32_t &spool,
            std::uint32_t &backend) override {
    auto record = network::make_external<Record>([] { return Record{}; });
    if (!record)
      return false;
    auto file = LittleFS.open(path, "r");
    if (!file || (file.size() != 813 && file.size() != record->size()))
      return false;
    const auto size = file.size();
    if (file.read(record->data(), size) != size)
      return false;
    const auto &r = *record;
    const bool legacy = size == 813;
    const auto checksum_offset = legacy ? 809U : 813U;
    if (std::memcmp(r.data(), legacy ? "OPTWR001" : "OPTWR002", 8) ||
        get(r, checksum_offset) !=
            nfc::nfcv::diagnostic_checksum(r.data(), checksum_offset) ||
        r[80] > 64)
      return false;
    std::copy_n(r.data() + 8, 8, p.uid.bytes.begin());
    std::copy_n(r.data() + 16, 64, p.system.bytes.begin());
    p.system.length = r[80];
    std::copy_n(r.data() + 81, 80, p.security.begin());
    std::copy_n(r.data() + 161, 320, p.original.begin());
    std::copy_n(r.data() + 481, 320, p.target.begin());
    p.geometry = {4, 80};
    p.attempted = true;
    p.verified = false;
    spool = get(r, 801);
    backend = get(r, 805);
    p.previous_spool_id = legacy ? 0 : get(r, 809);
    return spool > 0 && p.previous_spool_id >= 0 &&
           p.previous_spool_id != spool;
  }
  bool save(const nfc::WriterPlan &p, std::int32_t spool,
            std::uint32_t backend) override {
    auto record = network::make_external<Record>([] { return Record{}; });
    if (!record)
      return false;
    auto &r = *record;
    std::memcpy(r.data(), "OPTWR002", 8);
    std::copy(p.uid.bytes.begin(), p.uid.bytes.end(), r.data() + 8);
    std::copy(p.system.bytes.begin(), p.system.bytes.end(), r.data() + 16);
    r[80] = p.system.length;
    std::copy(p.security.begin(), p.security.end(), r.data() + 81);
    std::copy(p.original.begin(), p.original.end(), r.data() + 161);
    std::copy(p.target.begin(), p.target.end(), r.data() + 481);
    put(r, 801, spool);
    put(r, 805, backend);
    put(r, 809, p.previous_spool_id);
    put(r, 813, nfc::nfcv::diagnostic_checksum(r.data(), 813));
    auto file = LittleFS.open(staging, "w");
    if (!file)
      return false;
    const bool written = file.write(r.data(), r.size()) == r.size();
    file.flush();
    file.close();
    if (!written)
      return false;
    auto verify = LittleFS.open(staging, "r");
    if (!verify || verify.size() != r.size())
      return false;
    for (auto expected : r)
      if (verify.read() != expected)
        return false;
    verify.close();
    return LittleFS.rename(staging, path);
  }
  void clear() override {
    LittleFS.remove(path);
    LittleFS.remove(staging);
  }
};
} // namespace
nfc::WriterJournal &writer_journal() {
  static Journal instance;
  return instance;
}
} // namespace opentag::platform::storage
#endif
