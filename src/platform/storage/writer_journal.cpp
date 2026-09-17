#if defined(ARDUINO_ARCH_ESP32)
#include "platform/storage/writer_journal.hpp"
#include "nfc/writer_journal_codec.hpp"
#include <LittleFS.h>
#include <cstring>
namespace opentag::platform::storage {
namespace {
constexpr const char *path = "/writer-recovery.bin";
constexpr const char *staging = "/writer-recovery.new";
using Record = nfc::WriterJournalRecord;
class Journal final : public nfc::WriterJournal {
public:
  bool load(nfc::WriterPlan &p, std::int32_t &spool,
            std::uint32_t &backend) override {
    auto record = network::make_external<Record>([] { return Record{}; });
    if (!record)
      return false;
    auto file = LittleFS.open(path, "r");
    if (!file || (file.size() != 813 && file.size() != 817 &&
                  file.size() != record->size()))
      return false;
    const auto size = file.size();
    if (file.read(record->data(), size) != size)
      return false;
    return nfc::decode_writer_journal({record->data(), size}, p, spool,
                                      backend);
  }
  bool save(const nfc::WriterPlan &p, std::int32_t spool,
            std::uint32_t backend) override {
    auto record = network::make_external<Record>([] { return Record{}; });
    if (!record)
      return false;
    auto &r = *record;
    nfc::encode_writer_journal(r, p, spool, backend);
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
  bool clear() override {
    LittleFS.remove(path);
    LittleFS.remove(staging);
    return !LittleFS.exists(path) && !LittleFS.exists(staging);
  }
};
} // namespace
nfc::WriterJournal &writer_journal() {
  static Journal instance;
  return instance;
}
} // namespace opentag::platform::storage
#endif
