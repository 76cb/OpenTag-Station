#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "core/result.hpp"
#include "network/backend_json.hpp"

namespace opentag::services {
struct CommunityCatalogStatus {
  enum class State { not_installed, ready, damaged } state{State::not_installed};
  std::string version;
  std::string source_revision;
  std::string source_sha256;
  std::uint32_t records{0};
  std::uint32_t size{0};
};
class ICommunityCatalogFile {
 public:
  virtual ~ICommunityCatalogFile()=default;
  virtual std::size_t size() const=0;
  virtual bool read(std::size_t offset,void* output,std::size_t count)=0;
};
class ICommunityCatalogStore {
 public:
  virtual ~ICommunityCatalogStore()=default;
  virtual std::unique_ptr<ICommunityCatalogFile> open_active()=0;
  virtual std::unique_ptr<ICommunityCatalogFile> open_staging(){return {};}
  virtual bool begin_staging(std::size_t){return false;}
  virtual bool append_staging(const std::uint8_t*,std::size_t){return false;}
  virtual void discard_staging(){}
  virtual bool install_staging(){return false;}
};
class CommunityCatalog {
 public:
  explicit CommunityCatalog(ICommunityCatalogStore& store):store_(store){}
  CommunityCatalogStatus status();
  core::Result<CommunityCatalogStatus> verify(ICommunityCatalogFile& file);
  core::Result<network::BackendDocument> search(const std::string& query,unsigned offset);
  core::Result<network::BackendDocument> detail(const std::string& id);
 private:
  ICommunityCatalogStore& store_;
};
} // namespace opentag::services
