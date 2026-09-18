#include <unity.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <string>
#include "services/community_catalog.hpp"
#include "services/community_catalog_updater.hpp"
using namespace opentag;
namespace {
class MemoryFile final:public services::ICommunityCatalogFile {
 public:
  explicit MemoryFile(std::vector<std::uint8_t> data):data_(std::move(data)){}
  std::size_t size() const override{return data_.size();}
  bool read(std::size_t offset,void* output,std::size_t count) override {
    if(offset>data_.size()||count>data_.size()-offset)return false;
    std::memcpy(output,data_.data()+offset,count);return true;
  }
 private:std::vector<std::uint8_t> data_;
};
class Store final:public services::ICommunityCatalogStore {
 public:
  Store(){std::ifstream file("test/test_community_catalog/fixture.pack",std::ios::binary);active.assign(std::istreambuf_iterator<char>(file),{});}
  void load(const char* path){std::ifstream file(path,std::ios::binary);active.assign(std::istreambuf_iterator<char>(file),{});}
  std::unique_ptr<services::ICommunityCatalogFile> open_active() override {
    ++opens;if(unavailable||active.empty())return {};auto copy=active;if(corrupt&&!copy.empty())copy.back()^=1;return std::unique_ptr<services::ICommunityCatalogFile>(new MemoryFile(std::move(copy)));
  }
  std::unique_ptr<services::ICommunityCatalogFile> open_staging() override {return staging.empty()?nullptr:std::unique_ptr<services::ICommunityCatalogFile>(new MemoryFile(staging));}
  bool begin_staging(std::size_t expected) override {staging.clear();expected_=expected;return !fail_begin;}
  bool append_staging(const std::uint8_t* p,std::size_t n) override {if(fail_write||staging.size()+n>expected_)return false;staging.insert(staging.end(),p,p+n);return true;}
  void discard_staging() override {staging.clear();}
  bool install_staging() override {if(fail_install||staging.size()!=expected_)return false;active=staging;staging.clear();return true;}
  std::vector<std::uint8_t> active,staging;std::size_t expected_{};unsigned opens{0};bool unavailable{false},corrupt{false},fail_begin{false},fail_write{false},fail_install{false};
  Store(const Store&)=delete;Store& operator=(const Store&)=delete;
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
void updater_valid_download_installs_atomically(){Store store;auto pack=store.active;store.active.assign({1,2,3});services::CommunityCatalog catalog(store);Transport transport(pack);Sha sha;services::CommunityCatalogUpdater updater(store,catalog,transport,sha,[]{return true;});auto result=updater.update();TEST_ASSERT_TRUE(result.ok());TEST_ASSERT_EQUAL(19,result.value().records);TEST_ASSERT_TRUE(store.active==pack);TEST_ASSERT_TRUE(store.staging.empty());TEST_ASSERT_EQUAL(pack.size(),sha.bytes);}
void updater_failures_retain_previous_catalog(){
  for(unsigned mode=0;mode<5;++mode){Store store;const auto pack=store.active;const std::vector<std::uint8_t> previous{9,8,7};store.active=previous;Transport transport(pack);Sha sha;if(mode==0)transport.fail_pack=true;if(mode==1)transport.truncate=true;if(mode==2)sha.result.fill(0);if(mode==3)transport.payload[8]=2;if(mode==4)transport.interrupt=true;services::CommunityCatalog catalog(store);services::CommunityCatalogUpdater updater(store,catalog,transport,sha);TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);TEST_ASSERT_TRUE(store.staging.empty());}
}
void updater_rejects_manifest_and_install_failure(){Store store;auto pack=store.active;auto previous=store.active;services::CommunityCatalog catalog(store);Transport transport(pack);Sha sha;services::CommunityCatalogUpdater updater(store,catalog,transport,sha);transport.schema=2;TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);transport.schema=1;transport.wrong_source=true;TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);transport.wrong_source=false;store.fail_install=true;TEST_ASSERT_FALSE(updater.update().ok());TEST_ASSERT_TRUE(store.active==previous);TEST_ASSERT_TRUE(store.staging.empty());}
}
void setUp(){}void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(status_and_offline_search);RUN_TEST(case_material_zero_and_paging);RUN_TEST(detail_lookup_and_boundaries);RUN_TEST(production_pack_crosses_compressed_block_boundaries);RUN_TEST(missing_and_damage_fail_closed);RUN_TEST(malformed_block_and_offset_rejected);RUN_TEST(updater_valid_download_installs_atomically);RUN_TEST(updater_failures_retain_previous_catalog);RUN_TEST(updater_rejects_manifest_and_install_failure);return UNITY_END();}
