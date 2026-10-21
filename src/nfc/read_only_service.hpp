#pragma once

#include <memory>
#include <mutex>

#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/poll_schedule.hpp"
#include "nfc/protocols/nfca/identity.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "nfc/read_storage.hpp"
#include "nfc/technology.hpp"

namespace opentag::nfc {

// Intentionally has no tag mutation methods. Owned exclusively by the NFC task.
class IReadOnlyReader {
 public:
  virtual ~IReadOnlyReader() = default;
  virtual core::Result<void> initialize() = 0;
  virtual core::Result<void> field_on() = 0;
  virtual core::Result<void> field_off() = 0;
  virtual core::Result<void> health() = 0;
  virtual core::Result<std::vector<nfcv::Uid>> inventory() = 0;
  // ISO 14443A detection only: anticollision answers (NFCID1, SAK, ATQA),
  // never a memory read. A reader without NFC-A support reports no tags.
  virtual core::Result<std::vector<nfca::Identity>> inventory_nfca() {
    return core::Result<std::vector<nfca::Identity>>::success({});
  }
  virtual core::Result<nfcv::TagGeometry> geometry(const nfcv::Uid&) = 0;
  virtual core::Result<void> read_blocks(const nfcv::Uid&, std::size_t first,
                                         std::size_t count,
                                         std::size_t block_size,
                                         std::uint8_t* output) = 0;
  virtual std::uint32_t now_ms() const = 0;
  virtual std::uint32_t bus_errors() const = 0;
  virtual void stack_checkpoint(const char*) const {}
  virtual void yield_between_chunks() const {}
};

enum class ReadState {
  deferred,
  starting,
  idle,
  stabilizing,
  reading,
  openprinttag,
  blank,
  unsupported,
  // An NFC-A (ISO 14443A) tag: found and named, nothing read (#93).
  unsupported_technology,
  multiple,
  error
};
const char* to_string(ReadState state);

struct IdentifiedTag {
  nfcv::Uid uid;
  nfcv::TagGeometry geometry;
  std::uint32_t checksum{0};
  std::uint32_t identified_at_ms{0};
  std::uint64_t generation{0};
  openprinttag::DecodedTag decoded;
};

struct ReadSnapshot {
  ReadState state{ReadState::starting};
  bool initialized{false};
  bool present{false};
  bool blank_compatible{false};
  std::size_t tag_count{0};
  std::optional<nfcv::Uid> uid;
  nfcv::TagGeometry geometry;
  std::optional<std::uint32_t> checksum;
  std::uint64_t generation{0};
  std::uint32_t last_seen_ms{0};
  std::uint32_t bus_errors{0};
  std::optional<core::Error> error;
  std::shared_ptr<const IdentifiedTag> tag;
  // The technology of the present tag(s); none while the reader is empty.
  Technology technology{Technology::none};
  // Set with state unsupported_technology; uid stays empty for such a tag.
  std::optional<nfca::Identity> nfca;
};

// A present tag without OpenPrintTag data (blank, or other data) can still be
// identified by its UID alone, through Spoolman. Its read generation, else 0.
inline std::uint64_t uid_only_generation(const ReadSnapshot& s) {
  return !s.tag && s.present && s.uid &&
                 (s.state == ReadState::blank || s.state == ReadState::unsupported)
             ? s.generation
             : 0U;
}

class ReadOnlyService {
 public:
  explicit ReadOnlyService(IReadOnlyReader& reader) : reader_(reader) {}
  void poll();
  ReadSnapshot snapshot() const;
  // Sole backend owner, only after an explicit writer finishes or fails.
  void invalidate_after_write() { clear_active(); clear_nfca(); processed_.reset(); candidate_.reset(); consecutive_ = 0; missed_polls_ = 0; read_failures_ = 0; publish(); }
  static constexpr std::size_t maximum_memory_bytes = 4096U;
  static constexpr unsigned stable_polls = 3U;
  // Consecutive failed reads of one present tag before the error state.
  static constexpr unsigned read_attempts = 3U;

 private:
  core::Result<void> confirm_uid(const nfcv::Uid& uid);
  core::Result<void> read_image(const nfcv::Uid&, const nfcv::TagGeometry&,
                                ReadImage&,
                                std::uint32_t started);
  core::Result<void> read_tag(const nfcv::Uid& uid);
  // The NFC-A turn of a poll cycle (PollSchedule::nfca_turn).
  core::Result<void> poll_nfca(std::uint32_t now);
  void clear_active();
  void clear_nfca();
  void fail(const core::Error& error);
  void publish();
  IReadOnlyReader& reader_;
  ReadSnapshot live_;
  mutable std::mutex mutex_;
  ReadSnapshot published_;
  std::optional<nfcv::Uid> candidate_;
  std::optional<nfcv::Uid> processed_;
  unsigned consecutive_{0};
  unsigned missed_polls_{0};
  unsigned read_failures_{0};
  PollSchedule schedule_;
  std::optional<nfca::Identity> nfca_candidate_;
  unsigned nfca_missed_{0};
};
}  // namespace opentag::nfc
