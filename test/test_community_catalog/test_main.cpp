#include <unity.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include "services/community_catalog.hpp"
#include "services/community_catalog_updater.hpp"
#include "network/miniz/miniz_tinfl.h"
using namespace opentag;
namespace {
class MemoryFile final:public services::ICommunityCatalogFile {
 public:
  explicit MemoryFile(std::vector<std::uint8_t> data,std::size_t readable=std::numeric_limits<std::size_t>::max()):data_(std::move(data)),readable_(readable){}
  std::size_t size() const override{return data_.size();}
  bool read(std::size_t offset,void* output,std::size_t count) override {
    if(offset>data_.size()||count>data_.size()-offset||offset>readable_||count>readable_-offset)return false;
    std::memcpy(output,data_.data()+offset,count);return true;
  }
 private:std::vector<std::uint8_t> data_;std::size_t readable_;
};
class Store final:public services::ICommunityCatalogStore {
 public:
  Store(){std::ifstream file("test/test_community_catalog/fixture.pack",std::ios::binary);active.assign(std::istreambuf_iterator<char>(file),{});}
  void load(const char* path){std::ifstream file(path,std::ios::binary);active.assign(std::istreambuf_iterator<char>(file),{});}
  std::unique_ptr<services::ICommunityCatalogFile> open_active() override {
    ++opens;if(unavailable||active.empty())return {};auto copy=active;if(corrupt&&!copy.empty())copy.back()^=1;return std::unique_ptr<services::ICommunityCatalogFile>(new MemoryFile(std::move(copy),readable));
  }
  std::unique_ptr<services::ICommunityCatalogFile> open_staging() override {return staging.empty()?nullptr:std::unique_ptr<services::ICommunityCatalogFile>(new MemoryFile(staging));}
  bool begin_staging(std::size_t expected) override {staging.clear();expected_=expected;return !fail_begin;}
  bool append_staging(const std::uint8_t* p,std::size_t n) override {if(fail_write||staging.size()+n>expected_)return false;staging.insert(staging.end(),p,p+n);return true;}
  void discard_staging() override {staging.clear();}
  bool install_staging() override {if(fail_install||staging.size()!=expected_)return false;active=staging;staging.clear();return true;}
  std::vector<std::uint8_t> active,staging;std::size_t expected_{};std::size_t readable{std::numeric_limits<std::size_t>::max()};unsigned opens{0};bool unavailable{false},corrupt{false},fail_begin{false},fail_write{false},fail_install{false};
  Store(const Store&)=delete;Store& operator=(const Store&)=delete;
};
std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,std::size_t offset){return bytes[offset]|std::uint32_t(bytes[offset+1])<<8|std::uint32_t(bytes[offset+2])<<16|std::uint32_t(bytes[offset+3])<<24;}
void write_u32(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes[offset+n]=std::uint8_t(value>>(n*8));}
class TrackingMemory final:public services::ICommunityCatalogMemory {
 public:
  struct Allocation{std::uint8_t* memory;std::size_t size;services::CommunityCatalogMemoryClass memory_class;};
  ~TrackingMemory(){for(const auto& allocation:allocations)std::free(allocation.memory);}
  void* allocate(services::CommunityCatalogMemoryClass memory_class,std::size_t bytes) override{
    if((memory_class==services::CommunityCatalogMemoryClass::internal&&fail_internal)||(memory_class==services::CommunityCatalogMemoryClass::external&&fail_external))return nullptr;
    auto* memory=static_cast<std::uint8_t*>(std::malloc(bytes));if(!memory)return nullptr;
    allocations.push_back({memory,bytes,memory_class});if(memory_class==services::CommunityCatalogMemoryClass::internal){++internal_allocations;if(bytes>max_internal_bytes)max_internal_bytes=bytes;}else ++external_allocations;return memory;
  }
  void release(void* memory) override{
    for(auto it=allocations.begin();it!=allocations.end();++it)if(it->memory==memory){if(it->memory_class==services::CommunityCatalogMemoryClass::internal)++internal_releases;else ++external_releases;std::free(memory);allocations.erase(it);return;}
    TEST_FAIL_MESSAGE("released unknown Community allocation");
  }
  services::CommunityCatalogMemoryClass classify(const void* memory) const override{
    const auto p=reinterpret_cast<std::uintptr_t>(memory);for(const auto& allocation:allocations){const auto begin=reinterpret_cast<std::uintptr_t>(allocation.memory);if(p>=begin&&p<begin+allocation.size)return misclassify_internal&&allocation.memory_class==services::CommunityCatalogMemoryClass::internal?services::CommunityCatalogMemoryClass::external:allocation.memory_class;}return services::CommunityCatalogMemoryClass::unknown;
  }
  bool requires_strict_classes() const override{return true;}
  bool check_heap(const void*) const override{++heap_checks;return heap_valid;}
  services::CommunityCatalogMemorySnapshot snapshot() const override{return {};}
  std::uint32_t milliseconds() const override{return ++clock;}
  std::vector<Allocation> allocations;std::size_t max_internal_bytes{};unsigned internal_allocations{},external_allocations{},internal_releases{},external_releases{};bool fail_internal{},fail_external{},misclassify_internal{},heap_valid{true};mutable unsigned heap_checks{};mutable std::uint32_t clock{};
};
ota::Sha256Digest fixture_digest(){
  constexpr char value[]="c9fba0c6c9d381864696f7273b7674d51bc39b507412ed122c81f7897da66eba";
  ota::Sha256Digest result{};for(std::size_t i=0;i<result.size();++i){auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};result[i]=std::uint8_t(digit(value[i*2])*16+digit(value[i*2+1]));}return result;
}
class Sha final:public ota::ISha256 {
 public:
  core::Result<void> begin() override{active=true;return core::Result<void>::success();}
  core::Result<void> update(core::ByteView chunk) override{bytes+=chunk.size;return fail_update?core::Result<void>::failure({core::ErrorCategory::storage,"hash update",false}):core::Result<void>::success();}
  core::Result<ota::Sha256Digest> finish() override{active=false;return core::Result<ota::Sha256Digest>::success(result);}
  void abort() override{active=false;++aborts;}
  ota::Sha256Digest result{fixture_digest()};std::size_t bytes{};unsigned aborts{};bool active{},fail_update{};
};
class Transport final:public network::IHttpTransport {
 public:
  explicit Transport(const std::vector<std::uint8_t>& pack):payload(pack){}
  core::Result<network::HttpResponse> perform(const network::HttpRequest& request) override{
    ++calls;if(request.url==services::CommunityCatalogUpdater::manifest_url){
      if(fail_manifest)return core::Result<network::HttpResponse>::failure({core::ErrorCategory::network,"offline",true});
      const auto size=declared_size?declared_size:payload.size();
      std::string body="{\"schema\":"+std::to_string(schema)+",\"version\":\"fixture\",\"source_revision\":\""+(wrong_source?"wrong":"fixture")+"\",\"source_sha256\":\"d65885ad17350b1447ee656d1472c7939e6e1583884202cdcfdf5eccf8d300ba\",\"records\":19,\"size\":"+std::to_string(size)+",\"sha256\":\"c9fba0c6c9d381864696f7273b7674d51bc39b507412ed122c81f7897da66eba\",\"url\":\""+services::CommunityCatalogUpdater::pack_url+"\"}";
      network::HttpResponse response;response.status_code=200;response.body=network::ResponseBody(body);return core::Result<network::HttpResponse>::success(std::move(response));
    }
    TEST_ASSERT_EQUAL_STRING(services::CommunityCatalogUpdater::pack_url,request.url.c_str());TEST_ASSERT_TRUE(request.catalog_update);TEST_ASSERT_TRUE(bool(request.response_consumer));
    if(fail_pack)return core::Result<network::HttpResponse>::failure({core::ErrorCategory::network,"offline",true});
    const auto count=truncate?payload.size()-1:payload.size();const auto disposition=request.response_consumer(payload.data(),count);
    if(interrupt)return core::Result<network::HttpResponse>::failure({core::ErrorCategory::network,"interrupted",true});
    if(disposition==network::StreamDisposition::error)return core::Result<network::HttpResponse>::failure({core::ErrorCategory::storage,"sink",true});
    network::HttpResponse response;response.status_code=200;return core::Result<network::HttpResponse>::success(std::move(response));
  }
  std::vector<std::uint8_t> payload;unsigned calls{},schema{1};std::size_t declared_size{};bool fail_manifest{},fail_pack{},truncate{},interrupt{},wrong_source{};
};
void status_and_offline_search(){Store store;services::CommunityCatalog catalog(store);auto status=catalog.status();TEST_ASSERT_TRUE(status.state==services::CommunityCatalogStatus::State::ready);TEST_ASSERT_EQUAL(19,status.records);TEST_ASSERT_EQUAL_STRING("fixture",status.version.c_str());auto result=catalog.search("sunlu pla",0);TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_EQUAL(8,result.value()["items"].size());TEST_ASSERT_TRUE(result.value()["has_more"]);TEST_ASSERT_EQUAL_STRING("sunlu-0",result.value()["items"][0]["id"]);TEST_ASSERT_EQUAL(2,store.opens);store.corrupt=true;TEST_ASSERT_TRUE(catalog.status().state==services::CommunityCatalogStatus::State::damaged);}
void case_material_zero_and_paging(){Store store;services::CommunityCatalog catalog(store);auto second=catalog.search("SuNlU pLa+",8);TEST_ASSERT_TRUE(second.ok());TEST_ASSERT_EQUAL(8,second.value()["items"].size());TEST_ASSERT_TRUE(second.value()["has_more"]);auto exact=catalog.search("sunlu",10);TEST_ASSERT_TRUE(exact.ok());TEST_ASSERT_EQUAL(8,exact.value()["items"].size());TEST_ASSERT_FALSE(exact.value()["has_more"]);auto last=catalog.search("sunlu",16);TEST_ASSERT_TRUE(last.ok());TEST_ASSERT_EQUAL(2,last.value()["items"].size());TEST_ASSERT_FALSE(last.value()["has_more"]);auto zero=catalog.search("absent",0);TEST_ASSERT_TRUE(zero.ok());TEST_ASSERT_EQUAL(0,zero.value()["items"].size());auto material=catalog.search("petg acme",0);TEST_ASSERT_TRUE(material.ok());TEST_ASSERT_EQUAL_STRING("acme-petg",material.value()["items"][0]["id"]);}
void detail_lookup_and_boundaries(){Store store;services::CommunityCatalog catalog(store);for(auto id:{"sunlu-0","sunlu-17","acme-petg"}){auto result=catalog.detail(id);TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_EQUAL_STRING(id,result.value()["id"]);TEST_ASSERT_TRUE(result.value()["density"].as<double>()>0);}TEST_ASSERT_FALSE(catalog.detail("missing").ok());}
void production_pack_crosses_compressed_block_boundaries(){Store store;store.load("community/community.pack");services::CommunityCatalog catalog(store);auto verified=catalog.verify(*store.open_active());TEST_ASSERT_TRUE(verified.ok());TEST_ASSERT_EQUAL_UINT32(53424,verified.value().records);for(auto id:{"123-3d_abs_absblack_1000_175_p","yxpolyer_pla+_pla+yellow-foodsafe_1000_175_p"}){auto result=catalog.detail(id);TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_EQUAL_STRING(id,result.value()["id"]);}}
void missing_and_damage_fail_closed(){Store store;services::CommunityCatalog catalog(store);store.unavailable=true;TEST_ASSERT_TRUE(catalog.status().state==services::CommunityCatalogStatus::State::not_installed);TEST_ASSERT_FALSE(catalog.search("pla",0).ok());store.unavailable=false;store.active[0]^=1;TEST_ASSERT_TRUE(catalog.status().state==services::CommunityCatalogStatus::State::damaged);TEST_ASSERT_FALSE(catalog.search("pla",0).ok());}
void malformed_block_and_offset_rejected(){Store store;services::CommunityCatalog catalog(store);store.corrupt=true;TEST_ASSERT_FALSE(catalog.detail("acme-petg").ok());store.corrupt=false;store.active[132]=0xff;store.active[133]=0xff;store.active[134]=0xff;store.active[135]=0x7f;TEST_ASSERT_FALSE(catalog.search("pla",0).ok());}
void miniz_c_bridge_reports_authoritative_state_size(){TEST_ASSERT_TRUE(opentag_tinfl_state_size()>0);TEST_ASSERT_EQUAL_UINT(sizeof(tinfl_decompressor),opentag_tinfl_state_size());}
void valid_inflate_uses_external_buffers_and_internal_state(){Store store;TrackingMemory memory;services::CommunityCatalog catalog(store,&memory);auto result=catalog.search("sunlu",0);TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_TRUE(memory.external_allocations>=2);TEST_ASSERT_TRUE(memory.internal_allocations>=1);TEST_ASSERT_TRUE(memory.max_internal_bytes>=opentag_tinfl_state_size()+128);TEST_ASSERT_EQUAL(memory.external_allocations,memory.external_releases);TEST_ASSERT_EQUAL(memory.internal_allocations,memory.internal_releases);TEST_ASSERT_TRUE(memory.allocations.empty());}
void corrupt_compressed_block_is_rejected(){Store store;const auto offset=read_u32(store.active,120+12);store.active[offset+3]^=0x5a;services::CommunityCatalog catalog(store);TEST_ASSERT_FALSE(catalog.search("sunlu",0).ok());}
void wrong_block_crc_is_rejected(){Store store;write_u32(store.active,120+24,read_u32(store.active,120+24)^1U);services::CommunityCatalog catalog(store);TEST_ASSERT_FALSE(catalog.search("sunlu",0).ok());}
void truncated_block_read_is_rejected(){Store store;const auto offset=read_u32(store.active,120+12),compressed=read_u32(store.active,120+16);store.readable=offset+compressed-1;services::CommunityCatalog catalog(store);TEST_ASSERT_FALSE(catalog.search("sunlu",0).ok());}
void oversized_block_metadata_is_rejected_before_allocation(){Store store;write_u32(store.active,120+20,65537);TrackingMemory memory;services::CommunityCatalog catalog(store,&memory);TEST_ASSERT_FALSE(catalog.search("sunlu",0).ok());TEST_ASSERT_TRUE(memory.allocations.empty());TEST_ASSERT_EQUAL(0,memory.internal_allocations);TEST_ASSERT_EQUAL(0,memory.external_allocations);}
void allocation_failure_releases_partial_state(){Store store;TrackingMemory memory;memory.fail_internal=true;services::CommunityCatalog catalog(store,&memory);TEST_ASSERT_FALSE(catalog.search("sunlu",0).ok());TEST_ASSERT_TRUE(memory.internal_allocations==0);TEST_ASSERT_EQUAL(memory.external_allocations,memory.external_releases);TEST_ASSERT_TRUE(memory.allocations.empty());}
void inflater_lifetime_releases_internal_allocation(){Store store;TrackingMemory memory;services::CommunityCatalog catalog(store,&memory);TEST_ASSERT_TRUE(catalog.search("sunlu",0).ok());TEST_ASSERT_TRUE(memory.internal_allocations>0);TEST_ASSERT_EQUAL(memory.internal_allocations,memory.internal_releases);TEST_ASSERT_TRUE(memory.allocations.empty());}
void wrong_memory_class_fails_before_inflate(){Store store;TrackingMemory memory;memory.misclassify_internal=true;services::CommunityCatalog catalog(store,&memory);TEST_ASSERT_FALSE(catalog.search("sunlu",0).ok());TEST_ASSERT_EQUAL(memory.external_allocations,memory.external_releases);TEST_ASSERT_EQUAL(memory.internal_allocations,memory.internal_releases);TEST_ASSERT_TRUE(memory.allocations.empty());}
void heap_integrity_callback_is_not_used_by_inflate(){Store store;TrackingMemory memory;memory.heap_valid=false;services::CommunityCatalog catalog(store,&memory);auto result=catalog.search("sunlu",0);TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_EQUAL_UINT32(0,memory.heap_checks);TEST_ASSERT_TRUE(memory.allocations.empty());}
void repeated_search_releases_inflate_memory(){Store store;TrackingMemory memory;services::CommunityCatalog catalog(store,&memory);for(unsigned n=0;n<20;++n){TEST_ASSERT_TRUE(catalog.search("sunlu",n%17).ok());TEST_ASSERT_TRUE(memory.allocations.empty());}TEST_ASSERT_EQUAL(memory.external_allocations,memory.external_releases);TEST_ASSERT_EQUAL(memory.internal_allocations,memory.internal_releases);}
void repeated_detail_releases_inflate_memory(){Store store;TrackingMemory memory;services::CommunityCatalog catalog(store,&memory);for(unsigned n=0;n<20;++n){TEST_ASSERT_TRUE(catalog.detail("sunlu-0").ok());TEST_ASSERT_TRUE(memory.allocations.empty());}TEST_ASSERT_EQUAL(memory.external_allocations,memory.external_releases);TEST_ASSERT_EQUAL(memory.internal_allocations,memory.internal_releases);}
void updater_valid_download_installs_atomically(){Store store;auto pack=store.active;store.active.assign({1,2,3});services::CommunityCatalog catalog(store);Transport transport(pack);Sha sha;services::CommunityCatalogUpdater updater(store,catalog,transport,sha,[]{return true;});auto result=updater.update();TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_EQUAL(19,result.value().records);TEST_ASSERT_TRUE(store.active==pack);TEST_ASSERT_TRUE(store.staging.empty());TEST_ASSERT_EQUAL(pack.size(),sha.bytes);}
void updater_failures_retain_previous_catalog(){
  for(unsigned mode=0;mode<5;++mode){Store store;const auto pack=store.active;const std::vector<std::uint8_t> previous{9,8,7};store.active=previous;Transport transport(pack);Sha sha;if(mode==0)transport.fail_pack=true;if(mode==1)transport.truncate=true;if(mode==2)sha.result.fill(0);if(mode==3)transport.payload[8]=2;if(mode==4)transport.interrupt=true;services::CommunityCatalog catalog(store);services::CommunityCatalogUpdater updater(store,catalog,transport,sha);TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);TEST_ASSERT_TRUE(store.staging.empty());}
}
void updater_rejects_manifest_and_install_failure(){Store store;auto pack=store.active;auto previous=store.active;services::CommunityCatalog catalog(store);Transport transport(pack);Sha sha;services::CommunityCatalogUpdater updater(store,catalog,transport,sha);transport.schema=2;TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);transport.schema=1;transport.wrong_source=true;TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);transport.wrong_source=false;store.fail_install=true;TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);TEST_ASSERT_TRUE(store.staging.empty());}
}
void setUp(){}void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(status_and_offline_search);RUN_TEST(case_material_zero_and_paging);RUN_TEST(detail_lookup_and_boundaries);RUN_TEST(production_pack_crosses_compressed_block_boundaries);RUN_TEST(missing_and_damage_fail_closed);RUN_TEST(malformed_block_and_offset_rejected);RUN_TEST(miniz_c_bridge_reports_authoritative_state_size);RUN_TEST(valid_inflate_uses_external_buffers_and_internal_state);RUN_TEST(corrupt_compressed_block_is_rejected);RUN_TEST(wrong_block_crc_is_rejected);RUN_TEST(truncated_block_read_is_rejected);RUN_TEST(oversized_block_metadata_is_rejected_before_allocation);RUN_TEST(allocation_failure_releases_partial_state);RUN_TEST(inflater_lifetime_releases_internal_allocation);RUN_TEST(wrong_memory_class_fails_before_inflate);RUN_TEST(heap_integrity_callback_is_not_used_by_inflate);RUN_TEST(repeated_search_releases_inflate_memory);RUN_TEST(repeated_detail_releases_inflate_memory);RUN_TEST(updater_valid_download_installs_atomically);RUN_TEST(updater_failures_retain_previous_catalog);RUN_TEST(updater_rejects_manifest_and_install_failure);return UNITY_END();}
