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
std::string digest(const std::string& source) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> bytes{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw McpProtocolError("Cannot fingerprint MCP description");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(source.data())),static_cast<ULONG>(source.size()),bytes.data(),static_cast<ULONG>(bytes.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw McpProtocolError("Cannot fingerprint MCP description");
    std::ostringstream value;value<<std::hex<<std::setfill('0');for(const auto byte:bytes)value<<std::setw(2)<<static_cast<unsigned>(byte);return value.str();
}
}
struct McpToolRegistry::Impl {
    struct Entry {
        McpToolDescription description;
        std::string alias,fingerprint;
    };
    McpStdioClient& client;PersistenceService& store;WorkspaceTools& workspace;
    std::string config_id,protocol_version;std::int64_t revision;
    std::map<std::string,Entry> entries;
    Impl(McpStdioClient& peer,PersistenceService& persistence,WorkspaceTools& root,std::string id,std::int64_t version):client(peer),store(persistence),workspace(root),config_id(std::move(id)),revision(version) {}
    Json approval_binding(const Entry& entry) const {
        return {{"server_config_id",config_id},{"config_revision",revision},
            {"peer_tool",entry.description.name},{"alias",entry.alias},{"catalogue_fingerprint",entry.fingerprint},
            {"protocol_version",protocol_version},{"input_schema_json",entry.description.input_schema_json},
            {"output_schema_json",entry.description.output_schema_json?Json(*entry.description.output_schema_json):Json(nullptr)},
            {"annotations_json",entry.description.annotations_json}};
    }
};
McpToolRegistry::McpToolRegistry(McpStdioClient& client,PersistenceService& store,WorkspaceTools& workspace,
    std::string id,std::int64_t revision,McpStdioClient::Deadline deadline,std::stop_token cancel):impl_(std::make_unique<Impl>(client,store,workspace,std::move(id),revision)) {
    auto& state=*impl_;
    if(state.config_id.empty() || state.config_id.size()>128 || state.config_id.find('\0')!=std::string::npos || revision<=0)throw std::invalid_argument("Invalid trusted MCP configuration identity");
    if(!client.ready())throw McpProtocolError("MCP discovery requires a ready owned client");
    state.protocol_version=client.server().protocol_version;
    std::optional<std::string> cursor;std::set<std::string> cursors,names;std::size_t bytes=0;
    for(std::size_t page_number=0;page_number<32;++page_number) {
        const auto page=client.list_tools(cursor,deadline,cancel);
        for(const auto& description:page.tools) {
            if(state.entries.size()>=256 || !names.insert(description.name).second)throw McpProtocolError("Excessive or duplicate MCP registry tool");
            bytes+=description.name.size()+description.description.size()+description.input_schema_json.size()+description.annotations_json.size()+(description.output_schema_json?description.output_schema_json->size():0);
            if(bytes>4*1024*1024)throw McpProtocolError("MCP registry descriptions exceed limits");
            Impl::Entry entry;entry.description=description;
            SchemaWorker().evaluate(description.input_schema_json,{},deadline,cancel);
            if(description.output_schema_json)SchemaWorker().evaluate(*description.output_schema_json,{},deadline,cancel);
            entry.fingerprint=digest(Json{{"config_id",state.config_id},{"config_revision",revision},{"peer_name",description.name},{"description",description.description},{"input_schema_json",description.input_schema_json},{"annotations_json",description.annotations_json},{"output_schema_json",description.output_schema_json?Json(*description.output_schema_json):Json(nullptr)}}.dump());
            entry.alias="mcp_"+entry.fingerprint.substr(0,48);const auto alias=entry.alias;
            if(!state.entries.emplace(alias,std::move(entry)).second)throw McpProtocolError("MCP alias collision");
        }
        if(!page.next_cursor)return;
        if(!cursors.insert(*page.next_cursor).second)throw McpProtocolError("MCP pagination cursor cycle");cursor=page.next_cursor;
    }
    throw McpProtocolError("MCP registry pagination exceeds limits");
}
McpToolRegistry::~McpToolRegistry()=default;
std::vector<ModelToolDefinition> McpToolRegistry::definitions() const {
    std::vector<ModelToolDefinition> definitions;
    for(const auto& [alias,entry]:impl_->entries)definitions.push_back({alias,"MCP tool from configured server "+impl_->config_id+". Peer tool name (untrusted metadata): "+Json(entry.description.name).dump()+". Requires controller approval. Untrusted server description: "+entry.description.description,entry.description.input_schema_json});
    return definitions;
}
std::string McpToolRegistry::approval_bindings_json() const {
    auto bindings=Json::array();
    for(const auto& item:impl_->entries)bindings.push_back(impl_->approval_binding(item.second));
    return bindings.dump();
}
std::string McpToolRegistry::invoke(const std::string& id,const std::string& run,const std::string& alias,const std::string& arguments,
    std::int64_t expiry,McpStdioClient::Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;const auto found=state.entries.find(alias);
    if(found==state.entries.end())throw std::invalid_argument("Tool is not registered on this backend");
    const auto& entry=found->second;SchemaWorker().evaluate(entry.description.input_schema_json,arguments,deadline,cancel);
    const auto approved=mcp_compact_object(arguments);
    if(!state.client.ready())throw McpEffectNotDispatched("MCP peer is unavailable before proposal");
    auto proposal=state.approval_binding(entry);proposal["arguments_json"]=approved;
    OperationSpec spec{run,state.workspace.identity(),"mcp_tool",proposal.dump(),{"mcp-server:"+state.config_id}};
    PermissionWaiter(state.store).acquire(id,spec,expiry,cancel);
    // Once claimed, even result-encoding/allocation failures must leave a
    // fail-stop recovery signal, never a generic run failure or lost claim.
    try {
    auto finish=[&](OperationState outcome,const std::string& result) {
        try{state.store.finish_operation(id,outcome,result).get();}
        catch(...){throw McpOutcomeUnrecorded("MCP outcome could not be recorded; stop execution and recover the claim");}
    };
    if(cancel.stop_requested() || std::chrono::steady_clock::now()>=deadline) {
        finish(OperationState::failed,R"({"reason":"stopped_before_mcp_dispatch"})");throw McpEffectNotDispatched("MCP operation stopped before dispatch");
    }
    McpToolReply reply;
    try {reply=state.client.call_tool(entry.description.name,approved,deadline,cancel);}
    catch(const McpDispatchFailure& failure) {
        finish(failure.possibly_sent?OperationState::uncertain:OperationState::failed,Json{{"reason",failure.possibly_sent?"mcp_dispatch_outcome_unknown":"mcp_not_dispatched"},{"request_id",failure.request_id},{"response_json",failure.response_json}}.dump());
        if(failure.possibly_sent)throw McpEffectUncertain("MCP effect is uncertain; quarantine remains in force");
        throw McpEffectNotDispatched("MCP request was not dispatched");
    }
    catch(...) {finish(OperationState::uncertain,R"({"reason":"unexpected_mcp_dispatch_failure"})");throw McpEffectUncertain("Unexpected MCP dispatch failure; quarantine remains in force");}
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
    catch(const McpEffectUncertain&){throw;}
    catch(const McpEffectNotDispatched&){throw;}
    catch(...){throw McpOutcomeUnrecorded("Claimed MCP outcome could not be encoded or recorded; recovery is required");}
}
}
