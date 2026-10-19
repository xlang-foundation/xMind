#include "agentflow/language_server_tools.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string lower(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
std::size_t position(const Json& value,const char* key){
    if(!value.contains(key))return 0;const auto& item=value[key];
    if(!item.is_number_integer()||(!item.is_number_unsigned()&&item.get<std::int64_t>()<0)||item.get<std::uint64_t>()>1000000)throw std::invalid_argument("Language-server cursor position exceeds limits");
    return static_cast<std::size_t>(item.get<std::uint64_t>());
}
}
LanguageServerToolRegistry::LanguageServerToolRegistry(WorkspaceTools& workspace,std::vector<LanguageServerProfile> profiles):workspace_(workspace),profiles_(std::move(profiles)){
    if(profiles_.empty()||profiles_.size()>16)throw std::invalid_argument("Language-server profile count exceeds limits");
    std::set<std::string> ids;
    for(const auto& profile:profiles_){
        if(!ids.insert(profile.id).second)throw std::invalid_argument("Duplicate language-server profile identity");
        clients_.emplace(profile.id,std::make_unique<LanguageServerClient>(workspace_,profile));
    }
}
LanguageServerToolRegistry::~LanguageServerToolRegistry()=default;
std::vector<ModelToolDefinition> LanguageServerToolRegistry::definitions()const{
    Json servers=Json::array();for(const auto& profile:profiles_)servers.push_back(profile.id);
    Json properties=Json::object();
    properties["server_id"]={{"type","string"},{"enum",std::move(servers)}};
    properties["method"]={{"type","string"},{"enum",Json::array({"hover","definition","references","documentSymbol","diagnostics"})}};
    properties["path"]={{"type","string"},{"minLength",1},{"maxLength",4096}};
    properties["folder_id"]={{"type","string"},{"minLength",1},{"maxLength",256},{"description","Registered workspace folder ID from this run's admitted workspace membership. Omit to use the primary folder."}};
    properties["line"]={{"type","integer"},{"minimum",0},{"maximum",1000000}};
    properties["character"]={{"type","integer"},{"minimum",0},{"maximum",1000000}};
    const auto schema=Json{{"type","object"},{"properties",std::move(properties)},{"required",Json::array({"server_id","method","path"})},{"additionalProperties",false}}.dump();
    return {{"language_server","Query a registered native language server for hover, definition, references, document symbols or diagnostics. Paths are relative to the primary folder unless folder_id selects a member from this run's admitted workspace snapshot. The server executable and environment come only from trusted backend configuration. This tool is read-only.",schema}};
}
std::string LanguageServerToolRegistry::invoke(const std::string& source,std::stop_token cancel,
    const std::vector<WorkspaceToolFolder>* admitted_folders,const LanguageServerDispatch& dispatch){
    if(source.size()>16384)throw std::invalid_argument("Language-server tool arguments exceed limits");
    Json args;try{args=Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid language-server tool arguments");}
    if(!args.is_object())throw std::invalid_argument("Invalid language-server tool arguments");
    for(auto it=args.begin();it!=args.end();++it)if(it.key()!="server_id"&&it.key()!="method"&&it.key()!="path"&&it.key()!="line"&&it.key()!="character"&&it.key()!="folder_id")throw std::invalid_argument("Unknown language-server tool argument");
    for(const auto* key:{"server_id","method","path"})if(!args.contains(key)||!args[key].is_string())throw std::invalid_argument("Invalid language-server tool arguments");
    const auto method=args["method"].get<std::string>();if(method!="hover"&&method!="definition"&&method!="references"&&method!="documentSymbol"&&method!="diagnostics")throw ToolAccessDenied("The AgentRunner language-server tool is read-only");
    const auto id=args["server_id"].get<std::string>();const auto found=clients_.find(id);if(found==clients_.end())throw ToolAccessDenied("Language-server profile is not registered");
    LanguageServerRequest request;request.path=args["path"].get<std::string>();request.method=args["method"].get<std::string>();request.line=position(args,"line");request.character=position(args,"character");
    try {
    if(args.contains("folder_id")){
        if(!args["folder_id"].is_string())throw std::invalid_argument("Language-server folder ID must be text");
        request.folder_id=args["folder_id"].get<std::string>();
        if(request.folder_id.empty())throw std::invalid_argument("Language-server folder ID must not be empty");
        if(!admitted_folders||!dispatch)throw ToolAccessDenied("This run does not have multi-folder language-server access");
        if(std::none_of(admitted_folders->begin(),admitted_folders->end(),[&](const auto& folder){return folder.folder_id==request.folder_id;}))
            throw ToolAccessDenied("Language-server folder is not part of this run's admitted workspace membership");
        return dispatch(id,request,*admitted_folders);
    }
    return query(request,id,cancel);
    }catch(const McpTransportCancelled&){throw;}
    catch(const McpTransportError& error){
        // A failed read-only LSP check is not an uncertain remote effect.
        // Return its failure without aborting coding or replaying the call.
        std::string detail;
        for(const unsigned char c:std::string(error.what())){
            if(detail.size()>=512)break;
            if(c=='\r'||c=='\n'||c=='\t')detail.push_back(' ');
            else if(c>=0x20&&c!=0x7f)detail.push_back(static_cast<char>(c));
        }
        return Json{{"error",{{"code","language_server_unavailable"},{"server_id",id},{"method",method},
            {"message","The language-server check failed; no diagnostic result was obtained. Report this limitation and use other available checks. This call was not retried."},
            {"detail",detail}}}}.dump();
    }
}
std::string LanguageServerToolRegistry::query(const LanguageServerRequest& request,std::stop_token cancel){
    const auto extension=lower(std::filesystem::u8path(request.path).extension().string());const LanguageServerProfile* selected=nullptr;
    for(const auto& profile:profiles_)if(std::any_of(profile.extensions.begin(),profile.extensions.end(),[&](const auto& value){return lower(value)==extension;})){if(selected)throw ToolAccessDenied("More than one configured language server owns this file type");selected=&profile;}
    if(!selected)throw ToolAccessDenied("No configured language server owns this file type");
    return clients_.at(selected->id)->request(request,cancel);
}
std::string LanguageServerToolRegistry::query(const LanguageServerRequest& request,const std::string& server_id,std::stop_token cancel){
    const auto selected=std::find_if(profiles_.begin(),profiles_.end(),[&](const auto& profile){return profile.id==server_id;});
    if(selected==profiles_.end())throw ToolAccessDenied("Language-server profile is not registered");
    const auto extension=lower(std::filesystem::u8path(request.path).extension().string());
    if(!std::any_of(selected->extensions.begin(),selected->extensions.end(),[&](const auto& value){return lower(value)==extension;}))
        throw ToolAccessDenied("Selected language-server profile does not own this file type");
    return clients_.at(server_id)->request(request,cancel);
}
}
