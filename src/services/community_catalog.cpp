#include "services/community_catalog.hpp"
#include "network/miniz/miniz_tinfl.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_types.h>
#endif

namespace opentag::services { namespace {
constexpr std::size_t header_size=120,directory_size=32,block_limit=65536;
constexpr std::size_t canary_size=16;
constexpr std::uint8_t leading_canary=0xa5,trailing_canary=0x5a;
constexpr std::uint8_t magic[]{'O','T','C','P','A','C','K',0};
struct Header {std::uint16_t format{},bytes{};std::uint32_t records{},indexes{},details{},directory{},data{},size{},crc{},block{};std::array<std::uint8_t,32> source{};std::string version,revision;};
struct Directory {std::uint8_t kind{};std::uint32_t first{},count{},offset{},compressed{},expanded{},crc{},reserved{};};
std::uint16_t u16(const std::uint8_t* p){return p[0]|std::uint16_t(p[1])<<8;}
std::uint32_t u32(const std::uint8_t* p){return p[0]|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;}
float f32(const std::uint8_t* p){const auto n=u32(p);float value;std::memcpy(&value,&n,4);return value;}
std::uint32_t crc32(const std::uint8_t* p,std::size_t n){std::uint32_t crc=0xffffffffU;while(n--){crc^=*p++;for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320U&-(crc&1));}return crc^0xffffffffU;}
core::Error invalid(const char* text){return {core::ErrorCategory::invalid_response,text,false};}
class DefaultCommunityCatalogMemory final:public ICommunityCatalogMemory {
 public:
  void* allocate(CommunityCatalogMemoryClass memory_class,std::size_t bytes) override {
#ifdef ARDUINO
    const auto caps=memory_class==CommunityCatalogMemoryClass::internal
        ? MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT:MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    return heap_caps_malloc(bytes,caps);
#else
    (void)memory_class;return std::malloc(bytes);
#endif
  }
  void release(void* memory) override {
#ifdef ARDUINO
    heap_caps_free(memory);
#else
    std::free(memory);
#endif
  }
  CommunityCatalogMemoryClass classify(const void* memory) const override {
#ifdef ARDUINO
    if(esp_ptr_internal(memory))return CommunityCatalogMemoryClass::internal;
    if(esp_ptr_external_ram(memory))return CommunityCatalogMemoryClass::external;
    return CommunityCatalogMemoryClass::unknown;
#else
    return memory?CommunityCatalogMemoryClass::host:CommunityCatalogMemoryClass::unknown;
#endif
  }
  bool requires_strict_classes() const override {
#ifdef ARDUINO
    return true;
#else
    return false;
#endif
  }
  bool check_heap(const void* memory) const override {
#ifdef ARDUINO
    return memory&&heap_caps_check_integrity_addr(reinterpret_cast<intptr_t>(memory),false);
#else
    return memory;
#endif
  }
  CommunityCatalogMemorySnapshot snapshot() const override {
#ifdef ARDUINO
    constexpr auto internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    constexpr auto external=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    return {uxTaskGetStackHighWaterMark(nullptr),heap_caps_get_free_size(internal),
        heap_caps_get_largest_free_block(internal),heap_caps_get_free_size(external),
        heap_caps_get_largest_free_block(external)};
#else
    return {};
#endif
  }
  std::uint32_t milliseconds() const override {
#ifdef ARDUINO
    return millis();
#else
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
  }
};
ICommunityCatalogMemory& default_memory(){static DefaultCommunityCatalogMemory memory;return memory;}
const char* memory_class_name(CommunityCatalogMemoryClass value){
  switch(value){case CommunityCatalogMemoryClass::internal:return "internal";case CommunityCatalogMemoryClass::external:return "spiram";case CommunityCatalogMemoryClass::host:return "host";default:return "unavailable";}
}
const char* block_kind_name(std::uint8_t kind){return kind==1?"index":kind==2?"detail":"unknown";}
class GuardedBuffer {
 public:
  GuardedBuffer(ICommunityCatalogMemory& memory,std::size_t size):memory_(&memory),size_(size){
    raw_=static_cast<std::uint8_t*>(memory.allocate(CommunityCatalogMemoryClass::external,size+2*canary_size));
    if(raw_){std::memset(raw_,leading_canary,canary_size);std::memset(raw_+canary_size+size_,trailing_canary,canary_size);}
  }
  GuardedBuffer(const GuardedBuffer&)=delete;GuardedBuffer& operator=(const GuardedBuffer&)=delete;
  GuardedBuffer(GuardedBuffer&& other) noexcept:memory_(other.memory_),raw_(other.raw_),size_(other.size_){other.raw_=nullptr;other.size_=0;}
  GuardedBuffer& operator=(GuardedBuffer&&)=delete;
  ~GuardedBuffer(){if(raw_)memory_->release(raw_);}
  explicit operator bool() const{return raw_;}
  std::uint8_t* data(){return raw_?raw_+canary_size:nullptr;}
  const std::uint8_t* data() const{return raw_?raw_+canary_size:nullptr;}
  std::size_t size() const{return size_;}
  bool canaries_valid() const{
    if(!raw_)return false;
    for(std::size_t n=0;n<canary_size;++n)if(raw_[n]!=leading_canary||raw_[canary_size+size_+n]!=trailing_canary)return false;
    return true;
  }
 private:ICommunityCatalogMemory* memory_;std::uint8_t* raw_{nullptr};std::size_t size_{};
};
class InternalInflater {
 public:
  explicit InternalInflater(ICommunityCatalogMemory& memory):memory_(&memory){
    raw_=memory.allocate(CommunityCatalogMemoryClass::internal,sizeof(tinfl_decompressor));
    if(raw_)value_=new(raw_) tinfl_decompressor{};
  }
  InternalInflater(const InternalInflater&)=delete;InternalInflater& operator=(const InternalInflater&)=delete;
  ~InternalInflater(){if(value_){value_->~tinfl_decompressor();memory_->release(raw_);}}
  tinfl_decompressor* get() const{return value_;}
 private:ICommunityCatalogMemory* memory_;void* raw_{nullptr};tinfl_decompressor* value_{nullptr};
};
void log_inflate_begin(ICommunityCatalogMemory& memory,const Directory& d,std::size_t block_index,
    const void* input,const void* output,const void* inflater){
#ifdef ARDUINO
  const auto heap=memory.snapshot();
  Serial.printf("COMMUNITY_INFLATE phase=begin kind=%s block_index=%u ordinal=%u compressed=%u expanded=%u input=%s output=%s inflater=%s stack_free=%u internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
      block_kind_name(d.kind),static_cast<unsigned>(block_index),static_cast<unsigned>(d.first),
      static_cast<unsigned>(d.compressed),static_cast<unsigned>(d.expanded),memory_class_name(memory.classify(input)),
      memory_class_name(memory.classify(output)),memory_class_name(memory.classify(inflater)),
      static_cast<unsigned>(heap.stack_free_bytes),static_cast<unsigned>(heap.internal_free_bytes),
      static_cast<unsigned>(heap.internal_largest_bytes),static_cast<unsigned>(heap.external_free_bytes),
      static_cast<unsigned>(heap.external_largest_bytes));
#else
  (void)memory;(void)d;(void)block_index;(void)input;(void)output;(void)inflater;
#endif
}
void log_inflate_complete(ICommunityCatalogMemory& memory,const Directory& d,std::size_t block_index,
    const char* status,int tinfl_status,std::size_t consumed,std::size_t produced,bool crc_ok,bool guards_ok,bool heap_ok,std::uint32_t started){
#ifdef ARDUINO
  Serial.printf("COMMUNITY_INFLATE phase=complete kind=%s block_index=%u ordinal=%u status=%s tinfl_status=%d input_consumed=%u output_produced=%u crc=%s canaries=%s heap=%s duration_ms=%lu\n",
      block_kind_name(d.kind),static_cast<unsigned>(block_index),static_cast<unsigned>(d.first),status,tinfl_status,
      static_cast<unsigned>(consumed),static_cast<unsigned>(produced),crc_ok?"ok":"failed",guards_ok?"ok":"failed",
      heap_ok?"ok":"failed",static_cast<unsigned long>(memory.milliseconds()-started));
#else
  (void)memory;(void)d;(void)block_index;(void)status;(void)tinfl_status;(void)consumed;(void)produced;(void)crc_ok;(void)guards_ok;(void)heap_ok;(void)started;
#endif
}
bool read_header(ICommunityCatalogFile& file,Header& h) {
  std::array<std::uint8_t,header_size> p{};if(file.size()<p.size()||!file.read(0,p.data(),p.size())||std::memcmp(p.data(),magic,8))return false;
  h.format=u16(p.data()+8);h.bytes=u16(p.data()+10);h.records=u32(p.data()+12);h.indexes=u32(p.data()+16);h.details=u32(p.data()+20);h.directory=u32(p.data()+24);h.data=u32(p.data()+28);h.size=u32(p.data()+32);h.crc=u32(p.data()+36);h.block=u32(p.data()+40);std::copy_n(p.data()+44,32,h.source.begin());h.version=std::string(reinterpret_cast<char*>(p.data()+76),strnlen(reinterpret_cast<char*>(p.data()+76),24));h.revision=std::string(reinterpret_cast<char*>(p.data()+100),strnlen(reinterpret_cast<char*>(p.data()+100),20));
  const auto directories=std::size_t(h.indexes)+h.details;
  return h.format==1&&h.bytes==header_size&&h.records&&h.records<=100000&&h.indexes&&h.details&&directories<=81920&&h.directory==header_size&&h.data==header_size+directories*directory_size&&h.size==file.size()&&h.size<=2621440&&h.block==block_limit&&h.data<=h.size;
}
bool read_directory(ICommunityCatalogFile& file,const Header& h,std::size_t n,Directory& d){
  if(n>=h.indexes+h.details)return false;std::array<std::uint8_t,directory_size> p{};if(!file.read(h.directory+n*directory_size,p.data(),p.size()))return false;
  d.kind=p[0];d.first=u32(p.data()+4);d.count=u32(p.data()+8);d.offset=u32(p.data()+12);d.compressed=u32(p.data()+16);d.expanded=u32(p.data()+20);d.crc=u32(p.data()+24);d.reserved=u32(p.data()+28);
  return (d.kind==1||d.kind==2)&&d.count&&d.expanded&&d.compressed&&d.expanded<=block_limit&&d.compressed<=block_limit&&d.offset>=h.data&&std::size_t(d.offset)+d.compressed<=h.size&&!d.reserved;
}
bool payload_checksum_valid(ICommunityCatalogFile& file,const Header& h){
  std::uint32_t crc=0xffffffffU;network::ResponseBody bytes(4096);
  if(!bytes.resize_uninitialized(4096))return false;
  std::size_t offset=header_size;while(offset<h.size){const auto count=std::min(bytes.size(),std::size_t(h.size-offset));if(!file.read(offset,bytes.mutable_data(),count))return false;const auto* data=reinterpret_cast<const std::uint8_t*>(bytes.data());for(std::size_t n=0;n<count;++n){crc^=data[n];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&-(crc&1));}offset+=count;}return (crc^0xffffffffU)==h.crc;
}
core::Result<GuardedBuffer> inflate(ICommunityCatalogFile& file,const Directory& d,std::size_t block_index,ICommunityCatalogMemory& memory){
  using Result=core::Result<GuardedBuffer>;const auto started=memory.milliseconds();
  const bool metadata_ok=d.compressed&&d.expanded&&d.compressed<=block_limit&&d.expanded<=block_limit&&
      d.offset<=file.size()&&d.compressed<=file.size()-d.offset;
  if(!metadata_ok){log_inflate_begin(memory,d,block_index,nullptr,nullptr,nullptr);log_inflate_complete(memory,d,block_index,"metadata_invalid",0,0,0,false,true,true,started);return Result::failure(invalid("Community catalog block metadata invalid"));}
  GuardedBuffer input(memory,d.compressed),output(memory,d.expanded);InternalInflater inflater(memory);
  log_inflate_begin(memory,d,block_index,input.data(),output.data(),inflater.get());
  if(!input||!output){log_inflate_complete(memory,d,block_index,"buffer_allocation_failed",0,0,0,false,true,true,started);return Result::failure({core::ErrorCategory::backend_unavailable,"Community catalog block memory unavailable",true});}
  if(!inflater.get()){log_inflate_complete(memory,d,block_index,"inflater_allocation_failed",0,0,0,false,true,true,started);return Result::failure({core::ErrorCategory::backend_unavailable,"Community catalog inflater memory unavailable",true});}
  const bool classes_ok=!memory.requires_strict_classes()||
      (memory.classify(input.data())==CommunityCatalogMemoryClass::external&&
       memory.classify(output.data())==CommunityCatalogMemoryClass::external&&
       memory.classify(inflater.get())==CommunityCatalogMemoryClass::internal);
  const bool guards_before=input.canaries_valid()&&output.canaries_valid();
  // One address per heap region keeps the integrity check bounded to two region scans.
  const bool heap_before=memory.check_heap(inflater.get())&&memory.check_heap(input.data());
  if(!classes_ok||!guards_before||!heap_before){log_inflate_complete(memory,d,block_index,"memory_capability_failed",0,0,0,false,guards_before,heap_before,started);return Result::failure({core::ErrorCategory::backend_unavailable,"Community catalog memory capability check failed",false});}
  if(!file.read(d.offset,input.data(),d.compressed)){const bool guards=input.canaries_valid()&&output.canaries_valid();const bool heap=memory.check_heap(inflater.get())&&memory.check_heap(input.data());log_inflate_complete(memory,d,block_index,"truncated",0,0,0,false,guards,heap,started);return Result::failure(invalid("Community catalog block truncated"));}
  tinfl_init(inflater.get());std::size_t input_size=d.compressed,output_size=d.expanded;
  const auto status=tinfl_decompress(inflater.get(),input.data(),&input_size,output.data(),output.data(),&output_size,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  const bool guards_after=input.canaries_valid()&&output.canaries_valid();
  const bool heap_after=memory.check_heap(inflater.get())&&memory.check_heap(input.data());
  const bool crc_ok=status==TINFL_STATUS_DONE&&output_size==d.expanded&&crc32(output.data(),d.expanded)==d.crc;
  const bool valid=status==TINFL_STATUS_DONE&&input_size==d.compressed&&output_size==d.expanded&&crc_ok&&guards_after&&heap_after;
  log_inflate_complete(memory,d,block_index,valid?"ok":"damaged",static_cast<int>(status),input_size,output_size,crc_ok,guards_after,heap_after,started);
  if(!valid)return Result::failure(invalid(guards_after&&heap_after?"Community catalog block damaged":"Community catalog inflate memory damaged"));
  return Result::success(std::move(output));
}
bool take_string(const std::uint8_t*& p,const std::uint8_t* end,std::string& out){if(end-p<2)return false;const auto n=u16(p);p+=2;if(std::size_t(end-p)<n)return false;out.assign(reinterpret_cast<const char*>(p),n);p+=n;return true;}
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string hex(const std::array<std::uint8_t,32>& bytes){constexpr char digits[]="0123456789abcdef";std::string result(64,'0');for(std::size_t n=0;n<bytes.size();++n){result[n*2]=digits[bytes[n]>>4];result[n*2+1]=digits[bytes[n]&15];}return result;}
bool matches(const std::string& text,const std::string& query){std::size_t begin=0;while(begin<query.size()){const auto end=query.find(' ',begin);const auto term=query.substr(begin,end-begin);if(!term.empty()&&text.find(term)==std::string::npos)return false;if(end==std::string::npos)break;begin=end+1;}return true;}
struct Index {std::uint32_t ordinal{};float weight{};std::string id,manufacturer,name,material,color,search;};
bool next_index(const std::uint8_t*& p,const std::uint8_t* end,Index& item){if(end-p<2)return false;const auto n=u16(p);p+=2;if(std::size_t(end-p)<n||n<8)return false;const auto* stop=p+n;item.ordinal=u32(p);item.weight=f32(p+4);p+=8;const bool ok=take_string(p,stop,item.id)&&take_string(p,stop,item.manufacturer)&&take_string(p,stop,item.name)&&take_string(p,stop,item.material)&&take_string(p,stop,item.color)&&take_string(p,stop,item.search)&&p==stop;p=stop;return ok;}
} // namespace

CommunityCatalogStatus CommunityCatalog::status(){CommunityCatalogStatus s;auto file=store_.open_active();if(!file)return s;Header h;if(!read_header(*file,h)||!payload_checksum_valid(*file,h)){s.state=CommunityCatalogStatus::State::damaged;return s;}s.state=CommunityCatalogStatus::State::ready;s.version=h.version;s.source_revision=h.revision;s.source_sha256=hex(h.source);s.records=h.records;s.size=h.size;return s;}
core::Result<CommunityCatalogStatus> CommunityCatalog::verify(ICommunityCatalogFile& file){
  using Result=core::Result<CommunityCatalogStatus>;auto& memory=memory_?*memory_:default_memory();Header h;if(!read_header(file,h))return Result::failure(invalid("Community catalog header is incompatible"));if(!payload_checksum_valid(file,h))return Result::failure(invalid("Community catalog checksum mismatch"));std::size_t payload=h.data;std::uint32_t next_index=0,next_detail=0;for(std::size_t n=0;n<std::size_t(h.indexes)+h.details;++n){Directory d;if(!read_directory(file,h,n,d)||d.kind!=(n<h.indexes?1:2)||d.offset!=payload)return Result::failure(invalid("Community catalog directory is invalid"));auto& next=d.kind==1?next_index:next_detail;if(d.first!=next||d.count>h.records-next)return Result::failure(invalid("Community catalog record ranges are invalid"));next+=d.count;payload+=d.compressed;auto block=inflate(file,d,n,memory);if(!block.ok())return Result::failure(block.error());}if(next_index!=h.records||next_detail!=h.records||payload!=h.size)return Result::failure(invalid("Community catalog record coverage is invalid"));CommunityCatalogStatus status;status.state=CommunityCatalogStatus::State::ready;status.version=h.version;status.source_revision=h.revision;status.source_sha256=hex(h.source);status.records=h.records;status.size=h.size;return Result::success(std::move(status));
}
core::Result<network::BackendDocument> CommunityCatalog::search(const std::string& raw,unsigned offset){
  using Result=core::Result<network::BackendDocument>;auto& memory=memory_?*memory_:default_memory();auto file=store_.open_active();if(!file)return Result::failure({core::ErrorCategory::configuration,"Community catalog is not installed.",false});Header h;if(!read_header(*file,h))return Result::failure(invalid("Community catalog is damaged. Redownload it."));if(raw.empty()||raw.size()>64||offset>100000)return Result::failure({core::ErrorCategory::configuration,"Enter a Community search of 1–64 characters",false});
  network::BackendDocument page;page["items"].to<JsonArray>();page["offset"]=offset;page["has_more"]=false;page["catalog_version"]=h.version;const auto query=lower(raw);unsigned matched=0;
  for(std::size_t n=0;n<h.indexes;++n){Directory d;if(!read_directory(*file,h,n,d)||d.kind!=1)return Result::failure(invalid("Community catalog index damaged"));auto block=inflate(*file,d,n,memory);if(!block.ok())return Result::failure(block.error());const auto* p=block.value().data();const auto* end=p+block.value().size();while(p<end){Index item;if(!next_index(p,end,item))return Result::failure(invalid("Community catalog index record damaged"));if(!matches(item.search,query))continue;if(matched++<offset)continue;if(page["items"].size()==8){page["has_more"]=true;page["next_offset"]=offset+8;return Result::success(std::move(page));}auto out=page["items"].add<JsonObject>();out["id"]=item.id;out["manufacturer"]=item.manufacturer;out["name"]=item.name;out["material"]=item.material;if(!item.color.empty())out["color_hex"]=item.color;if(item.weight>0)out["weight"]=item.weight;if(page.overflowed())return Result::failure({core::ErrorCategory::backend_unavailable,"Community result page memory unavailable",true});}}
  page["next_offset"]=offset+page["items"].size();return Result::success(std::move(page));
}
core::Result<network::BackendDocument> CommunityCatalog::detail(const std::string& id){
  using Result=core::Result<network::BackendDocument>;auto& memory=memory_?*memory_:default_memory();auto file=store_.open_active();if(!file)return Result::failure({core::ErrorCategory::configuration,"Community catalog is not installed.",false});Header h;if(!read_header(*file,h))return Result::failure(invalid("Community catalog is damaged. Redownload it."));std::uint32_t ordinal=std::numeric_limits<std::uint32_t>::max();
  for(std::size_t n=0;n<h.indexes&&ordinal==std::numeric_limits<std::uint32_t>::max();++n){Directory d;if(!read_directory(*file,h,n,d))return Result::failure(invalid("Community catalog index damaged"));auto block=inflate(*file,d,n,memory);if(!block.ok())return Result::failure(block.error());const auto* p=block.value().data();const auto* end=p+block.value().size();while(p<end){Index item;if(!next_index(p,end,item))return Result::failure(invalid("Community catalog index record damaged"));if(item.id==id){ordinal=item.ordinal;break;}}}
  if(ordinal==std::numeric_limits<std::uint32_t>::max())return Result::failure({core::ErrorCategory::conflict,"Community selection changed; search again",false});
  for(std::size_t n=h.indexes;n<h.indexes+h.details;++n){Directory d;if(!read_directory(*file,h,n,d)||d.kind!=2)return Result::failure(invalid("Community catalog detail directory damaged"));if(ordinal<d.first||ordinal>=d.first+d.count)continue;auto block=inflate(*file,d,n,memory);if(!block.ok())return Result::failure(block.error());const auto* p=block.value().data();const auto* end=p+block.value().size();for(std::uint32_t i=d.first;i<d.first+d.count;++i){if(end-p<2)return Result::failure(invalid("Community detail record truncated"));const auto bytes=u16(p);p+=2;if(std::size_t(end-p)<bytes)return Result::failure(invalid("Community detail record offset invalid"));if(i==ordinal){network::BackendDocument result;if(deserializeJson(result,p,bytes,DeserializationOption::NestingLimit(12))||result["id"]!=id)return Result::failure(invalid("Community detail record damaged"));return Result::success(std::move(result));}p+=bytes;}}
  return Result::failure(invalid("Community detail record missing"));
}
} // namespace opentag::services
