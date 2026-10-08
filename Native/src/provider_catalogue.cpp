#include "agentflow/provider_catalogue.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>
namespace agentflow {
namespace {
using Json=nlohmann::json;
bool not_reflected(const std::string& id,const SecretBytes& key){
    const auto bytes=key.view();std::size_t difference=id.size()^bytes.size();
    for(std::size_t i=0;i<bytes.size();++i)difference|=bytes[i]^(i<id.size()?static_cast<unsigned char>(id[i]):0);
    return difference!=0&&std::search(id.begin(),id.end(),bytes.begin(),bytes.end())==id.end();
}
bool identity(const std::string& id,const SecretBytes& key){
    return !id.empty()&&id.size()<=256&&!id.starts_with("sk-")&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")==std::string::npos&&not_reflected(id,key);
}
bool gemini_resource(const std::string& id,const SecretBytes& key){
    if(!identity(id,key)||!id.starts_with("models/"))return false;const auto model=std::string_view(id).substr(7);
    return !model.empty()&&model.size()<=128&&model!="."&&model!=".."&&model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string_view::npos;
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
    if(policy.format!=ProviderCatalogueFormat::openai&&policy.format!=ProviderCatalogueFormat::anthropic&&policy.format!=ProviderCatalogueFormat::gemini)throw std::invalid_argument("Unknown provider catalogue format");
    if(key.view().empty()||key.view().size()>32768)throw std::invalid_argument("Provider discovery requires a bounded key");
    for(auto byte:key.view())if(byte<33||byte>126)throw std::invalid_argument("Provider discovery key must be printable without spaces");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    std::set<std::string> models,cursors,gemini_names;std::string cursor;std::size_t entries=0;
    for(std::size_t page=0;page<8;++page){
        if(cancel.stop_requested())throw TransportCancelled("Provider model discovery cancelled");
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if(remaining.count()<=0)throw TransportTimeout("Provider model discovery timed out");
        HttpStreamRequest request;request.url=policy.endpoint;request.deadline=std::min(remaining,std::chrono::milliseconds(10000));request.idle_timeout=request.deadline;
        if(policy.format==ProviderCatalogueFormat::anthropic){
            request.credential_header=CredentialHeader::x_api_key;request.protocol=ProviderHttpProtocol::anthropic;
            request.url+="?limit=1000";if(!cursor.empty())request.url+="&after_id="+encode(cursor);
        }else if(policy.format==ProviderCatalogueFormat::gemini){
            request.credential_header=CredentialHeader::x_goog_api_key;request.url+="?pageSize=1000";if(!cursor.empty())request.url+="&pageToken="+encode(cursor);
        }
        if(request.url.size()>8192)throw TransportError("Provider model catalogue exceeds URL bound");
        const auto source=get_json(request,&key,cancel);bool more=false;
        try{
            const auto value=parse(source);
            if(!value.is_object()||value.contains("error"))throw TransportError("Invalid provider model catalogue");
            const Json empty=Json::array();const auto& items=policy.format==ProviderCatalogueFormat::gemini?(value.contains("models")?value.at("models"):empty):value.at("data");
            if(!items.is_array()||items.size()>4096-entries||(policy.format==ProviderCatalogueFormat::gemini&&items.size()>1000))throw TransportError("Invalid provider model catalogue");
            entries+=items.size();
            if(policy.format==ProviderCatalogueFormat::openai&&value.at("object")!="list")throw TransportError("Invalid provider model catalogue");
            std::string last;
            for(const auto& item:items){
                if(policy.format==ProviderCatalogueFormat::gemini){
                    if(!item.is_object()||!item.contains("name")||!item["name"].is_string())throw TransportError("Invalid provider model catalogue");
                    auto id=item["name"].get<std::string>();if(!gemini_resource(id,key)||!gemini_names.insert(id).second)throw TransportError("Invalid provider model catalogue");bool eligible=false;
                    if(item.contains("supportedGenerationMethods")){
                        const auto& methods=item["supportedGenerationMethods"];if(!methods.is_array()||methods.size()>64)throw TransportError("Invalid provider model catalogue");
                        for(const auto& method:methods){if(!method.is_string())throw TransportError("Invalid provider model catalogue");const auto action=method.get<std::string>();if(action.empty()||action.size()>128||action.find('\0')!=std::string::npos)throw TransportError("Invalid provider model catalogue");if(action=="generateContent")eligible=true;}
                    }
                    if(eligible)models.insert(std::move(id));continue;
                }
                if(!item.is_object()||item.at(policy.format==ProviderCatalogueFormat::openai?"object":"type")!="model")throw TransportError("Invalid provider model catalogue");
                auto id=item.at("id").get<std::string>();if(!identity(id,key))throw TransportError("Invalid provider model catalogue");last=id;models.insert(std::move(id));
            }
            if(policy.format==ProviderCatalogueFormat::anthropic){
                if(!value.at("has_more").is_boolean())throw TransportError("Invalid provider model catalogue");
                more=value["has_more"].get<bool>();const auto& after=value.at("last_id");
                if(!after.is_null()&&(!after.is_string()||!identity(after.get<std::string>(),key)))throw TransportError("Invalid provider model catalogue");
                if(more){if(last.empty()||after!=last||!cursors.insert(last).second)throw TransportError("Invalid provider model catalogue");cursor=std::move(last);}
            }else if(policy.format==ProviderCatalogueFormat::gemini&&value.contains("nextPageToken")){
                if(!value["nextPageToken"].is_string())throw TransportError("Invalid provider model catalogue");const auto next=value["nextPageToken"].get<std::string>();
                if(next.size()>4096||(!next.empty()&&(!not_reflected(next,key)||!cursors.insert(next).second)))throw TransportError("Invalid provider model catalogue");more=!next.empty();if(more)cursor=next;
            }
        }catch(...){throw TransportError("Invalid provider model catalogue");}
        if(!more)return {models.begin(),models.end()};
    }
    throw TransportError("Provider model catalogue exceeds page limit");
}
}
