#pragma once
#include "nfc/openprinttag_writer.hpp"
namespace opentag::nfc {
class WriterJournal {
public:
  virtual ~WriterJournal() = default;
  virtual bool load(WriterPlan &, std::int32_t &spool_id,
                    std::uint32_t &backend) = 0;
  virtual bool save(const WriterPlan &, std::int32_t spool_id,
                    std::uint32_t backend) = 0;
  virtual bool clear() = 0;
};
} // namespace opentag::nfc
