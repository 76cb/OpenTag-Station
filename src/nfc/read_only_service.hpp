#pragma once

#include <memory>
#include <mutex>

#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "nfc/read_storage.hpp"

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
  unsupported,
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
  std::size_t tag_count{0};
  std::optional<nfcv::Uid> uid;
  nfcv::TagGeometry geometry;
  std::optional<std::uint32_t> checksum;
  std::uint64_t generation{0};
  std::uint32_t last_seen_ms{0};
  std::uint32_t bus_errors{0};
  std::optional<core::Error> error;
  std::shared_ptr<const IdentifiedTag> tag;
};

class ReadOnlyService {
 public:
  explicit ReadOnlyService(IReadOnlyReader& reader) : reader_(reader) {}
  void poll();
  ReadSnapshot snapshot() const;
  // Sole backend owner, only after an explicit writer finishes or fails.
  void invalidate_after_write() { clear_active(); processed_.reset(); candidate_.reset(); consecutive_ = 0; publish(); }
  static constexpr std::size_t maximum_memory_bytes = 4096U;
  static constexpr unsigned stable_polls = 3U;

 private:
  core::Result<void> confirm_uid(const nfcv::Uid& uid);
  core::Result<void> read_image(const nfcv::Uid&, const nfcv::TagGeometry&,
                                ReadImage&,
                                std::uint32_t started);
  core::Result<void> read_tag(const nfcv::Uid& uid);
  void clear_active();
  void fail(const core::Error& error);
  void publish();
  IReadOnlyReader& reader_;
  ReadSnapshot live_;
  mutable std::mutex mutex_;
  ReadSnapshot published_;
  std::optional<nfcv::Uid> candidate_;
  std::optional<nfcv::Uid> processed_;
  unsigned consecutive_{0};
  std::uint32_t last_poll_{0};
  bool polled_{false};
};
}  // namespace opentag::nfc
