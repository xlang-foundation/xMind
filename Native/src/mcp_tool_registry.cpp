#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/schema_worker.hpp"
#include "nlohmann/json.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>

namespace agentflow {
namespace {
using Json=nlohmann::json;
enum class EntryKind {tool,resource,prompt};
std::string_view kind_name(EntryKind kind){switch(kind){case EntryKind::tool:return "tool";case EntryKind::resource:return "resource";case EntryKind::prompt:return "prompt";}throw McpProtocolError("Invalid MCP catalogue entry kind");}
std::string digest(const std::string& source) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> bytes{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw McpProtocolError("Cannot fingerprint MCP description");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(source.data())),static_cast<ULONG>(source.size()),bytes.data(),static_cast<ULONG>(bytes.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw McpProtocolError("Cannot fingerprint MCP description");
    std::ostringstream value;value<<std::hex<<std::setfill('0');for(const auto byte:bytes)value<<std::setw(2)<<static_cast<unsigned>(byte);return value.str();
}
bool reflects_credential(std::string_view source,const std::vector<SecretBytes>* credentials){
    if(!credentials)return false;for(const auto& credential:*credentials){const auto secret=credential.view();if(secret.empty()||secret.size()>source.size())continue;const auto* begin=reinterpret_cast<const std::uint8_t*>(source.data());if(std::search(begin,begin+source.size(),secret.begin(),secret.end())!=begin+source.size())return true;}return false;
}
std::string prompt_schema(const Json& prompt){
    Json properties=Json::object(),required=Json::array();
    if(prompt.contains("arguments"))for(const auto& argument:prompt["arguments"]){Json property={{"type","string"}};if(argument.contains("description"))property["description"]=argument["description"];properties[argument.at("name").get<std::string>()]=std::move(property);if(argument.value("required",false))required.push_back(argument.at("name"));}
    return Json{{"type","object"},{"properties",std::move(properties)},{"required",std::move(required)},{"additionalProperties",false}}.dump();
}
std::string file_uri(std::string path){
    std::replace(path.begin(),path.end(),'\\','/');std::string uri;
    if(path.size()>=3&&path[1]==':'&&path[2]=='/')uri="file:///"+path;
    else if(path.starts_with("//"))uri="file:"+path;
    else uri="file://"+(path.starts_with('/')?path:"/"+path);
    constexpr char hex[]="0123456789ABCDEF";std::string encoded;encoded.reserve(uri.size());
    for(const auto byte:uri){const auto c=static_cast<unsigned char>(byte);if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||c=='/'||c==':' )encoded.push_back(static_cast<char>(c));else{encoded.push_back('%');encoded.push_back(hex[c>>4]);encoded.push_back(hex[c&15]);}}
    return encoded;
}
}
struct McpToolRegistry::Impl {
    struct Entry {
        EntryKind kind=EntryKind::tool;
        McpToolDescription description;
        std::string peer_name,resource_uri,metadata_json,schema_json,alias,fingerprint;
    };
    McpToolClient& client;PersistenceService& store;WorkspaceTools& workspace;
    const std::vector<SecretBytes>* private_credentials;
    std::shared_ptr<std::atomic<bool>> catalogue_invalidated;
    std::string config_id,protocol_version;std::int64_t revision;
    std::map<std::string,Entry> entries;
    Impl(McpToolClient& peer,PersistenceService& persistence,WorkspaceTools& root,std::string id,std::int64_t version,const std::vector<SecretBytes>* credentials,std::shared_ptr<std::atomic<bool>> invalidated):client(peer),store(persistence),workspace(root),private_credentials(credentials),catalogue_invalidated(std::move(invalidated)),config_id(std::move(id)),revision(version) {}
    Json approval_binding(const Entry& entry) const {
        if(entry.kind==EntryKind::tool)return {{"server_config_id",config_id},{"config_revision",revision},
            {"peer_tool",entry.peer_name},{"alias",entry.alias},{"catalogue_fingerprint",entry.fingerprint},
            {"protocol_version",protocol_version},{"input_schema_json",entry.schema_json},
            {"output_schema_json",entry.description.output_schema_json?Json(*entry.description.output_schema_json):Json(nullptr)},
            {"annotations_json",entry.description.annotations_json}};
        Json binding={{"server_config_id",config_id},{"config_revision",revision},{"kind",kind_name(entry.kind)},
            {"peer_name",entry.peer_name},{"alias",entry.alias},{"catalogue_fingerprint",entry.fingerprint},
            {"protocol_version",protocol_version},{"input_schema_json",entry.schema_json},
            {"output_schema_json",entry.description.output_schema_json?Json(*entry.description.output_schema_json):Json(nullptr)},
            {"annotations_json",entry.description.annotations_json},{"metadata_json",entry.metadata_json}};
        if(entry.kind==EntryKind::resource)binding["resource_uri"]=entry.resource_uri;
        return binding;
    }
};
McpToolRegistry::McpToolRegistry(McpToolClient& client,PersistenceService& store,WorkspaceTools& workspace,
    std::string id,std::int64_t revision,McpToolClient::Deadline deadline,std::stop_token cancel,const std::vector<SecretBytes>* private_credentials,std::shared_ptr<std::atomic<bool>> catalogue_invalidated):impl_(std::make_unique<Impl>(client,store,workspace,std::move(id),revision,private_credentials,std::move(catalogue_invalidated))) {
    auto& state=*impl_;
    if(state.config_id.empty() || state.config_id.size()>128 || state.config_id.find('\0')!=std::string::npos || revision<=0)throw std::invalid_argument("Invalid trusted MCP configuration identity");
    if(!client.ready())throw McpProtocolError("MCP discovery requires a ready owned client");
    state.protocol_version=client.server().protocol_version;
    const auto capabilities=Json::parse(client.server().capabilities_json);
    std::set<std::string> identities;std::size_t bytes=0;
    const auto add=[&](Impl::Entry entry){
        if(state.entries.size()>=256||!identities.insert(std::string(kind_name(entry.kind))+"\n"+entry.peer_name).second)throw McpProtocolError("Excessive or duplicate MCP registry entry");
        if(reflects_credential(entry.peer_name,state.private_credentials)||reflects_credential(entry.metadata_json,state.private_credentials)||reflects_credential(entry.schema_json,state.private_credentials)||reflects_credential(entry.description.description,state.private_credentials))throw McpProtocolError("MCP server metadata reflected a configured credential");
        bytes+=entry.peer_name.size()+entry.metadata_json.size()+entry.schema_json.size()+entry.description.description.size();
        if(bytes>4*1024*1024)throw McpProtocolError("MCP registry descriptions exceed limits");
        SchemaWorker().evaluate(entry.schema_json,{},deadline,cancel);if(entry.description.output_schema_json)SchemaWorker().evaluate(*entry.description.output_schema_json,{},deadline,cancel);
        entry.fingerprint=digest(Json{{"config_id",state.config_id},{"config_revision",revision},{"protocol_version",state.protocol_version},{"kind",kind_name(entry.kind)},{"peer_name",entry.peer_name},{"metadata_json",entry.metadata_json},{"schema_json",entry.schema_json}}.dump());
        entry.alias="mcp_"+entry.fingerprint.substr(0,48);const auto alias=entry.alias;
        if(!state.entries.emplace(alias,std::move(entry)).second)throw McpProtocolError("MCP alias collision");
    };
    const auto collect=[&](EntryKind kind){
        const auto capability=kind==EntryKind::tool?"tools":kind==EntryKind::resource?"resources":"prompts";
        if(!capabilities.contains(capability))return;
        std::optional<std::string> cursor;std::set<std::string> cursors;
        for(std::size_t page_number=0;page_number<32;++page_number){
            if(kind==EntryKind::tool){
                const auto tool_page=client.list_tools(cursor,deadline,cancel);
                for(const auto& description:tool_page.tools){Impl::Entry entry;entry.kind=EntryKind::tool;entry.peer_name=description.name;entry.description=description;entry.schema_json=description.input_schema_json;entry.metadata_json=Json{{"name",description.name},{"description",description.description},{"annotations",Json::parse(description.annotations_json)},{"outputSchema",description.output_schema_json?Json::parse(*description.output_schema_json):Json(nullptr)}}.dump();add(std::move(entry));}
                if(!tool_page.next_cursor)return;if(!cursors.insert(*tool_page.next_cursor).second)throw McpProtocolError("MCP pagination cursor cycle");cursor=tool_page.next_cursor;continue;
            }
            const auto page=kind==EntryKind::resource?client.list_resources(cursor,deadline,cancel):client.list_prompts(cursor,deadline,cancel);
            for(const auto& raw:page.entries_json){const auto metadata=Json::parse(raw);Impl::Entry entry;entry.kind=kind;entry.peer_name=metadata.at("name").get<std::string>();entry.metadata_json=raw;entry.description.name=entry.peer_name;entry.description.description=metadata.value("description",std::string{});entry.description.annotations_json=metadata.value("annotations",Json::object()).dump();
                if(kind==EntryKind::resource){entry.resource_uri=metadata.at("uri").get<std::string>();entry.schema_json=R"({"type":"object","properties":{},"additionalProperties":false})";}
                else entry.schema_json=prompt_schema(metadata);
                add(std::move(entry));
            }
            if(!page.next_cursor)return;if(!cursors.insert(*page.next_cursor).second)throw McpProtocolError("MCP pagination cursor cycle");cursor=page.next_cursor;
        }
        throw McpProtocolError("MCP registry pagination exceeds limits");
    };
    collect(EntryKind::tool);collect(EntryKind::resource);collect(EntryKind::prompt);
}
McpToolRegistry::~McpToolRegistry()=default;
std::vector<ModelToolDefinition> McpToolRegistry::definitions() const {
    std::vector<ModelToolDefinition> definitions;
    for(const auto& [alias,entry]:impl_->entries){const auto noun=entry.kind==EntryKind::tool?"tool":entry.kind==EntryKind::resource?"resource read":"prompt";definitions.push_back({alias,"MCP "+std::string(noun)+" from configured server "+impl_->config_id+". Peer name (untrusted metadata): "+Json(entry.peer_name).dump()+". Requires controller approval. Untrusted server description: "+entry.description.description,entry.schema_json});}
    return definitions;
}
std::string McpToolRegistry::approval_bindings_json() const {
    auto bindings=Json::array();
    for(const auto& item:impl_->entries)bindings.push_back(impl_->approval_binding(item.second));
    return bindings.dump();
}
std::string McpToolRegistry::invoke(const std::string& id,const std::string& run,const std::string& alias,const std::string& arguments,
    std::int64_t expiry,McpToolClient::Deadline deadline,std::stop_token cancel,InstructionPrecondition guidance) {
    auto& state=*impl_;const auto found=state.entries.find(alias);
    if(found==state.entries.end())throw std::invalid_argument("Tool is not registered on this backend");
    const auto record_invalidation=[&](const char* phase){state.store.append_event(run,"mcp.catalogue.invalidated",Json{{"server_id",state.config_id},{"config_revision",state.revision},{"alias",alias},{"phase",phase},{"effect_dispatched",false}}.dump()).get();};
    if(state.catalogue_invalidated&&state.catalogue_invalidated->load(std::memory_order_acquire)){
        record_invalidation("before_proposal");throw McpCatalogueChanged("MCP catalogue changed before proposal; no effect occurred");
    }
    const auto& entry=found->second;SchemaWorker().evaluate(entry.schema_json,arguments,deadline,cancel);
    const auto approved=mcp_compact_object(arguments);
    if(!state.client.ready())throw McpEffectNotDispatched("MCP peer is unavailable before proposal");
    auto proposal=state.approval_binding(entry);proposal["arguments_json"]=approved;
    guidance.validate();if(!guidance.metadata_json.empty())proposal["instructions"]=Json::parse(guidance.metadata_json);
    OperationSpec spec{run,state.workspace.identity(),"mcp_tool",proposal.dump(),{"mcp-server:"+state.config_id}};
    PermissionWaiter(state.store).acquire(id,spec,expiry,cancel);
    // Once claimed, even result-encoding/allocation failures must leave a
    // fail-stop recovery signal, never a generic run failure or lost claim.
    try {
    auto finish=[&](OperationState outcome,const std::string& result) {
        try{state.store.finish_operation(id,outcome,result).get();}
        catch(...){throw McpOutcomeUnrecorded("MCP outcome could not be recorded; stop execution and recover the claim");}
    };
    if(state.catalogue_invalidated&&state.catalogue_invalidated->load(std::memory_order_acquire)){
        record_invalidation("after_approval");finish(OperationState::failed,R"({"reason":"mcp_catalogue_changed_before_dispatch"})");
        throw McpCatalogueChanged("MCP catalogue changed after approval; no effect occurred");
    }
    if(cancel.stop_requested() || std::chrono::steady_clock::now()>=deadline) {
        finish(OperationState::failed,R"({"reason":"stopped_before_mcp_dispatch"})");throw McpEffectNotDispatched("MCP operation stopped before dispatch");
    }
    try{if(guidance.verify)guidance.verify(cancel);}
    catch(const ToolGuidanceChanged&){finish(OperationState::failed,R"({"reason":"repository_instructions_changed_before_mcp_dispatch"})");throw;}
    catch(...){finish(OperationState::failed,R"({"reason":"guidance_unavailable_before_mcp_dispatch"})");throw McpEffectNotDispatched("MCP guidance could not be verified before dispatch");}
    if(state.catalogue_invalidated&&state.catalogue_invalidated->load(std::memory_order_acquire)){
        record_invalidation("before_dispatch");finish(OperationState::failed,R"({"reason":"mcp_catalogue_changed_before_dispatch"})");
        throw McpCatalogueChanged("MCP catalogue changed before dispatch; no effect occurred");
    }
    McpToolReply reply;McpReadOnlyReply readonly;
    const McpInputFulfillment fulfill_input=[&](std::string_view raw)->std::optional<std::string>{
        const auto request=Json::parse(raw);if(!request.is_object()||!request.contains("method")||!request["method"].is_string())throw McpProtocolError("Invalid embedded MCP input request");
        if(request["method"]!="roots/list")return std::nullopt;
        const auto root=state.workspace.root_path();const auto slash=root.find_last_of("/\\");const auto name=slash==std::string::npos?root:root.substr(slash+1);
        return Json{{"roots",Json::array({Json{{"uri",file_uri(root)},{"name",name}}})}}.dump();
    };
    try {if(entry.kind==EntryKind::tool)reply=state.client.call_tool(entry.peer_name,approved,deadline,cancel,fulfill_input);else if(entry.kind==EntryKind::resource)readonly=state.client.read_resource(entry.resource_uri,deadline,cancel);else readonly=state.client.get_prompt(entry.peer_name,approved,deadline,cancel);}
    catch(const McpDispatchFailure& failure) {
        if(entry.kind!=EntryKind::tool){finish(OperationState::failed,Json{{"reason","mcp_read_failed"},{"request_id",failure.request_id}}.dump());throw McpReadFailed("MCP read did not complete; no result was exposed");}
        finish(failure.possibly_sent?OperationState::uncertain:OperationState::failed,Json{{"reason",failure.possibly_sent?"mcp_dispatch_outcome_unknown":"mcp_not_dispatched"},{"request_id",failure.request_id},{"response_json",failure.response_json}}.dump());
        if(failure.possibly_sent)throw McpEffectUncertain("MCP effect is uncertain; quarantine remains in force");
        throw McpEffectNotDispatched("MCP request was not dispatched");
    }
    catch(...) {if(entry.kind!=EntryKind::tool){finish(OperationState::failed,R"({"reason":"mcp_read_failed"})");throw McpReadFailed("MCP read did not complete; no result was exposed");}finish(OperationState::uncertain,R"({"reason":"unexpected_mcp_dispatch_failure"})");throw McpEffectUncertain("Unexpected MCP dispatch failure; quarantine remains in force");}
    if(entry.kind==EntryKind::tool&&reflects_credential(reply.response_json,state.private_credentials)){finish(OperationState::uncertain,Json{{"reason","mcp_secret_reflection"},{"request_id",reply.request_id}}.dump());throw McpEffectUncertain("MCP peer reflected a configured credential; quarantine remains in force");}
    if(entry.kind==EntryKind::tool&&reply.input_required){
        finish(OperationState::uncertain,Json{{"reason","mcp_additional_input_required"},{"request_id",reply.request_id},{"response_json",reply.response_json}}.dump());
        throw McpEffectUncertain("MCP requested additional input; xMind has recorded the unresolved operation and will not replay it without a continuation owner");
    }
    if(entry.kind!=EntryKind::tool&&reflects_credential(readonly.response_json,state.private_credentials)){finish(OperationState::failed,Json{{"reason","mcp_secret_reflection"},{"request_id",readonly.request_id}}.dump());throw McpReadFailed("MCP peer reflected a configured credential; no result was exposed");}
    if(entry.kind!=EntryKind::tool){
        try{const auto result=Json{{"operation_id",id},{"request_id",readonly.request_id},{"server_config_id",state.config_id},{"config_revision",state.revision},{"kind",kind_name(entry.kind)},{"acknowledged_by_peer",true},{"independently_verified",false},{"response_json",readonly.response_json}}.dump();finish(OperationState::succeeded,result);return result;}
        catch(const McpOutcomeUnrecorded&){throw;}catch(...){throw McpOutcomeUnrecorded("Acknowledged MCP read outcome could not be recorded; recovery is required");}
    }
    try {
        // A peer error does not establish that no partial external effect took
        // place. Preserve its actual reply and quarantine instead of retrying.
        if(reply.is_error)throw McpEffectUncertain("MCP peer reported a tool error with unknown effects");
        if(entry.description.output_schema_json) {
            const auto content=mcp_object_member(reply.result_json,"structuredContent");
            if(!content)throw SchemaArgumentsInvalid("MCP output schema requires structured content");
            // After a received acknowledgement, finish validating/journaling
            // its actual outcome despite run cancellation, within a fresh
            // independent bounded worker budget. Do not discard or replay it.
            SchemaWorker().evaluate(*entry.description.output_schema_json,*content,std::chrono::steady_clock::now()+std::chrono::seconds(2));
        }
    }catch(const SchemaArgumentsInvalid&) {finish(OperationState::uncertain,Json{{"reason","mcp_output_schema_rejected"},{"request_id",reply.request_id},{"response_json",reply.response_json}}.dump());throw McpEffectUncertain("MCP output was rejected; quarantine remains in force");}
    catch(const McpEffectUncertain&) {finish(OperationState::uncertain,Json{{"reason","mcp_peer_reported_tool_error"},{"request_id",reply.request_id},{"response_json",reply.response_json}}.dump());throw;}
    catch(...) {finish(OperationState::uncertain,Json{{"reason","mcp_output_validation_unavailable"},{"request_id",reply.request_id},{"response_json",reply.response_json}}.dump());throw McpEffectUncertain("MCP output validation did not complete; quarantine remains in force");}
    try {
        const auto result=Json{{"operation_id",id},{"request_id",reply.request_id},{"server_config_id",state.config_id},{"config_revision",state.revision},{"acknowledged_by_peer",true},{"independently_verified",false},{"response_json",reply.response_json}}.dump();
        finish(OperationState::succeeded,result);return result;
    }catch(const McpOutcomeUnrecorded&){throw;}
    catch(...){throw McpOutcomeUnrecorded("Acknowledged MCP outcome could not be encoded; recovery is required");}
    }catch(const McpOutcomeUnrecorded&){throw;}
    catch(const ToolGuidanceChanged&){throw;}
    catch(const McpEffectUncertain&){throw;}
    catch(const McpEffectNotDispatched&){throw;}
    catch(const McpReadFailed&){throw;}
    catch(const McpCatalogueChanged&){throw;}
    catch(...){throw McpOutcomeUnrecorded("Claimed MCP outcome could not be encoded or recorded; recovery is required");}
}
}
