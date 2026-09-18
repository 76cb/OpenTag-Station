#pragma once
#include <functional>
#include "network/http_transport.hpp"
#include "ota/update_manager.hpp"
#include "services/community_catalog.hpp"
namespace opentag::services {
class CommunityCatalogUpdater {
 public:
  using Progress=std::function<void(std::size_t,std::size_t)>;
  CommunityCatalogUpdater(ICommunityCatalogStore& store,CommunityCatalog& catalog,
      network::IHttpTransport& transport,ota::ISha256& sha256,
      std::function<bool()> permitted={})
      :store_(store),catalog_(catalog),transport_(transport),sha256_(sha256),permitted_(std::move(permitted)){}
  core::Result<CommunityCatalogStatus> update(Progress progress={});
  static constexpr const char* manifest_url="https://76cb.github.io/OpenTag-Station/community-manifest.json";
  static constexpr const char* pack_url="https://76cb.github.io/OpenTag-Station/community.pack";
 private:
  ICommunityCatalogStore& store_;CommunityCatalog& catalog_;network::IHttpTransport& transport_;ota::ISha256& sha256_;std::function<bool()> permitted_;
};
}
