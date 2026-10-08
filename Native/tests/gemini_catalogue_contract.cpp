#include "agentflow/provider_catalogue.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class F>void rejects(F action){try{action();}catch(const Error& error){require(std::string(error.what()).find("private-catalogue-body")==std::string::npos&&std::string(error.what()).find("fixture-gemini-catalogue-key")==std::string::npos,"Catalogue failures cannot reflect private provider data");return;}throw std::runtime_error("Expected native catalogue rejection");}
}
int main(int argc,char** argv){if(argc!=2)return 2;try{
 const std::string base=argv[1],token="fixture-gemini-catalogue-key";SecretBytes key({reinterpret_cast<const std::uint8_t*>(token.data()),token.size()});
 const auto policy=[&](const std::string& route){return ProviderCataloguePolicy{base+"/"+route+"/models",ProviderCatalogueFormat::gemini};};
 require(discover_provider_models(policy("good"),key)==std::vector<std::string>{"models/gemini-a","models/gemini-z"},"GenerateContent-eligible full model resources must be sorted and preserve their prefix");
 for(const auto* route:{"empty","omitted","embedding"})require(discover_provider_models(policy(route),key).empty(),"Terminal empty and non-GenerateContent catalogues must remain empty without invented models");
 for(const auto* route:{"repeat","duplicate","duplicate-late","duplicate-json","bare","path","dots","long","unicode","reflected","bad-models","bad-name","bad-method","bad-cursor","long-cursor","reflected-cursor","bad-json","depth","page-overflow","entry-overflow","page-limit","oversized","late-invalid"})rejects<TransportError>([&]{(void)discover_provider_models(policy(route),key);});
 for(const auto& entry:{std::pair{"unauthorized",401},std::pair{"redirect",307}}){bool rejected=false;try{(void)discover_provider_models(policy(entry.first),key);}catch(const ProviderHttpError& error){rejected=error.status==entry.second;}require(rejected,"Native catalogue transport must retain HTTP status without redirect or credential forwarding");}
 std::stop_source stopped;stopped.request_stop();rejects<TransportCancelled>([&]{(void)discover_provider_models(policy("must-not-arrive"),key,stopped.get_token());});
 {
  std::stop_source stop;std::jthread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(500));stop.request_stop();});rejects<TransportCancelled>([&]{(void)discover_provider_models(policy("cancel"),key,stop.get_token());});
 }
 const auto started=std::chrono::steady_clock::now();rejects<TransportTimeout>([&]{(void)discover_provider_models(policy("deadline"),key);});require(std::chrono::steady_clock::now()-started<std::chrono::seconds(15),"Native catalogue per-page deadline must bound an unresponsive provider");
 auto invalid=policy("must-not-arrive");invalid.endpoint+="?key=forbidden";rejects<std::invalid_argument>([&]{(void)discover_provider_models(invalid,key);});invalid=policy("must-not-arrive");invalid.format=static_cast<ProviderCatalogueFormat>(999);rejects<std::invalid_argument>([&]{(void)discover_provider_models(invalid,key);});
 std::cout<<"Native Gemini catalogue contract passed header-authenticated real socket discovery, opaque escaped pagination, GenerateContent method filtering, exact full resources, malformed/reflected/duplicate identity rejection, empty catalogues, page/entry/JSON bounds and HTTP/cancellation/deadline handling. Provider replies are synthetic; listing does not prove tools capability, enrollment or live model acceptance.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
