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
enum class CommunityCatalogMemoryClass { unknown, internal, external, host };
struct CommunityCatalogMemorySnapshot {
  std::size_t stack_free_bytes{0};
  std::size_t internal_free_bytes{0};
  std::size_t internal_largest_bytes{0};
  std::size_t external_free_bytes{0};
  std::size_t external_largest_bytes{0};
};
class ICommunityCatalogMemory {
 public:
  virtual ~ICommunityCatalogMemory()=default;
  virtual void* allocate(CommunityCatalogMemoryClass memory_class,std::size_t bytes)=0;
  virtual void release(void* memory)=0;
  virtual CommunityCatalogMemoryClass classify(const void* memory) const=0;
  virtual bool requires_strict_classes() const=0;
  virtual bool check_heap(const void* memory) const=0;
  virtual CommunityCatalogMemorySnapshot snapshot() const=0;
  virtual std::uint32_t milliseconds() const=0;
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
  explicit CommunityCatalog(ICommunityCatalogStore& store,ICommunityCatalogMemory* memory=nullptr):store_(store),memory_(memory){}
  CommunityCatalogStatus status();
  core::Result<CommunityCatalogStatus> verify(ICommunityCatalogFile& file);
  core::Result<network::BackendDocument> search(const std::string& query,unsigned offset);
  core::Result<network::BackendDocument> detail(const std::string& id);
 private:
  ICommunityCatalogStore& store_;
  ICommunityCatalogMemory* memory_;
};
} // namespace opentag::services
