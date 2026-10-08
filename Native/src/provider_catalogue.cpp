#include "agentflow/provider_catalogue.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;
bool identity(const std::string& id,const SecretBytes& key){
    if(id.empty()||id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)return false;
    const auto bytes=key.view();std::size_t difference=id.size()^bytes.size();
    for(std::size_t i=0;i<bytes.size();++i)difference|=bytes[i]^(i<id.size()?static_cast<unsigned char>(id[i]):0);
    return difference!=0;
}
std::string encode(const std::string& value){const char* hex="0123456789ABCDEF";std::string result;for(unsigned char ch:value){if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~')result+=ch;else{result+='%';result+=hex[ch>>4];result+=hex[ch&15];}}return result;}
Json parse(const std::string& source){
    std::vector<std::set<std::string>> fields;
    return Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>16)throw TransportError("Invalid provider model catalogue");
        if(event==Json::parse_event_t::object_start)fields.emplace_back();
        else if(event==Json::parse_event_t::object_end)fields.pop_back();
        else if(event==Json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)throw TransportError("Invalid provider model catalogue");
        return true;
    });
}
}
std::vector<std::string> discover_provider_models(const ProviderCataloguePolicy& policy,const SecretBytes& key,std::stop_token cancel){
    if(policy.endpoint.empty()||policy.endpoint.size()>8192||policy.endpoint.find_first_of("?#\r\n")!=std::string::npos||policy.endpoint.find('\0')!=std::string::npos)
        throw std::invalid_argument("Invalid backend catalogue endpoint");
    if(policy.format!=ProviderCatalogueFormat::openai&&policy.format!=ProviderCatalogueFormat::anthropic)throw std::invalid_argument("Unknown provider catalogue format");
    if(key.view().empty()||key.view().size()>32768)throw std::invalid_argument("Provider discovery requires a bounded key");
    for(auto byte:key.view())if(byte<33||byte>126)throw std::invalid_argument("Provider discovery key must be printable without spaces");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    std::set<std::string> models,cursors;std::string cursor;std::size_t entries=0;
    for(std::size_t page=0;page<8;++page){
        if(cancel.stop_requested())throw TransportCancelled("Provider model discovery cancelled");
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if(remaining.count()<=0)throw TransportTimeout("Provider model discovery timed out");
        HttpStreamRequest request;request.url=policy.endpoint;request.deadline=std::min(remaining,std::chrono::milliseconds(10000));request.idle_timeout=request.deadline;
        if(policy.format==ProviderCatalogueFormat::anthropic){
            request.credential_header=CredentialHeader::x_api_key;request.protocol=ProviderHttpProtocol::anthropic;
            request.url+="?limit=1000";if(!cursor.empty())request.url+="&after_id="+encode(cursor);
        }
        const auto source=get_json(request,&key,cancel);bool more=false;
        try{
            const auto value=parse(source);
            if(!value.is_object()||!value.at("data").is_array()||value["data"].size()>4096-entries)throw TransportError("Invalid provider model catalogue");
            entries+=value["data"].size();
            if(policy.format==ProviderCatalogueFormat::openai&&value.at("object")!="list")throw TransportError("Invalid provider model catalogue");
            std::string last;
            for(const auto& item:value["data"]){
                if(!item.is_object()||item.at(policy.format==ProviderCatalogueFormat::openai?"object":"type")!="model")throw TransportError("Invalid provider model catalogue");
                auto id=item.at("id").get<std::string>();if(!identity(id,key))throw TransportError("Invalid provider model catalogue");last=id;models.insert(std::move(id));
            }
            if(policy.format==ProviderCatalogueFormat::anthropic){
                if(!value.at("has_more").is_boolean())throw TransportError("Invalid provider model catalogue");
                more=value["has_more"].get<bool>();const auto& after=value.at("last_id");
                if(!after.is_null()&&(!after.is_string()||!identity(after.get<std::string>(),key)))throw TransportError("Invalid provider model catalogue");
                if(more){if(last.empty()||after!=last||!cursors.insert(last).second)throw TransportError("Invalid provider model catalogue");cursor=std::move(last);}
            }
        }catch(...){throw TransportError("Invalid provider model catalogue");}
        if(!more)return {models.begin(),models.end()};
    }
    throw TransportError("Provider model catalogue exceeds page limit");
}
}
