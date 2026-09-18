#include "platform/storage/community_catalog_store.hpp"
#ifdef ARDUINO
#include <LittleFS.h>
namespace opentag::platform::storage { namespace {
constexpr const char* active="/community.pack";constexpr const char* staging="/community.new";constexpr const char* backup="/community.old";
class CatalogFile final:public services::ICommunityCatalogFile {
 public:
  explicit CatalogFile(fs::File file):file_(std::move(file)),size_(file_.size()){}
  std::size_t size() const override{return size_;}
  bool read(std::size_t offset,void* output,std::size_t count) override {
    return file_.seek(offset,SeekSet)&&file_.read(static_cast<std::uint8_t*>(output),count)==count;
  }
 private:fs::File file_;std::size_t size_;
};
}
class CommunityCatalogStore::Writer {public:fs::File file;std::size_t expected{},written{};};
CommunityCatalogStore::CommunityCatalogStore()=default;
CommunityCatalogStore::~CommunityCatalogStore()=default;
std::unique_ptr<services::ICommunityCatalogFile> CommunityCatalogStore::open_active(){
  if(!LittleFS.exists(active)&&LittleFS.exists(backup))LittleFS.rename(backup,active);
  else if(LittleFS.exists(active)&&LittleFS.exists(backup))LittleFS.remove(backup);
  auto file=LittleFS.open(active,"r");if(!file)return {};return std::unique_ptr<services::ICommunityCatalogFile>(new(std::nothrow) CatalogFile(std::move(file)));
}
std::unique_ptr<services::ICommunityCatalogFile> CommunityCatalogStore::open_staging(){auto file=LittleFS.open(staging,"r");if(!file)return {};return std::unique_ptr<services::ICommunityCatalogFile>(new(std::nothrow) CatalogFile(std::move(file)));}
bool CommunityCatalogStore::begin_staging(std::size_t expected){discard_staging();if(LittleFS.totalBytes()-LittleFS.usedBytes()<expected+131072)return false;writer_.reset(new(std::nothrow) Writer);if(!writer_)return false;writer_->expected=expected;writer_->file=LittleFS.open(staging,"w");return bool(writer_->file);}
bool CommunityCatalogStore::append_staging(const std::uint8_t* data,std::size_t size){if(!writer_||size>writer_->expected-writer_->written)return false;const auto written=writer_->file.write(data,size);writer_->written+=written;return written==size;}
void CommunityCatalogStore::discard_staging(){if(writer_){writer_->file.close();writer_.reset();}LittleFS.remove(staging);}
bool CommunityCatalogStore::install_staging(){if(!writer_||writer_->written!=writer_->expected)return false;writer_->file.flush();writer_->file.close();writer_.reset();LittleFS.remove(backup);const bool had=LittleFS.exists(active);if(had&&!LittleFS.rename(active,backup))return false;if(!LittleFS.rename(staging,active)){if(had)LittleFS.rename(backup,active);return false;}LittleFS.remove(backup);return true;}
}
#endif
