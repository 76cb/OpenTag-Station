#pragma once
#include "services/community_catalog.hpp"
namespace opentag::platform::storage {
class CommunityCatalogStore final:public services::ICommunityCatalogStore {
 public:
  CommunityCatalogStore();
  ~CommunityCatalogStore();
  std::unique_ptr<services::ICommunityCatalogFile> open_active() override;
  std::unique_ptr<services::ICommunityCatalogFile> open_staging() override;
  bool begin_staging(std::size_t expected_size) override;
  bool append_staging(const std::uint8_t*,std::size_t) override;
  void discard_staging() override;
  bool install_staging() override;
 private:
  class Writer;
  std::unique_ptr<Writer> writer_;
};
}
