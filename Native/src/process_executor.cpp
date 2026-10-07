#include "agentflow/process_executor.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool text(const std::string& value,std::size_t limit=4096) {
    return value.size()<=limit && value.find('\0')==std::string::npos && (value.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)>0);
}
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
Json captured(const std::string& bytes,std::uint64_t count) {
    if(bytes.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0)>0)
        return {{"encoding","utf-8"},{"data",bytes},{"byte_count",count},{"retained_bytes",bytes.size()}};
    constexpr char hex[]="0123456789abcdef";std::string encoded;encoded.reserve(bytes.size()*2);
    for(unsigned char b:bytes){encoded.push_back(hex[b>>4]);encoded.push_back(hex[b&15]);}
    return {{"encoding","hex"},{"data",encoded},{"byte_count",count},{"retained_bytes",bytes.size()}};
}
}
ProcessExecutor::ProcessExecutor(PersistenceService& store,WorkspaceTools& workspace,std::string root,std::vector<ProcessProfile> profiles)
    :store_(store),workspace_(workspace),root_(std::move(root)),profiles_(std::move(profiles)) {
    if(profiles_.empty() || profiles_.size()>16 || !text(root_,32768) || !std::filesystem::u8path(root_).is_absolute())throw std::invalid_argument("Invalid process configuration");
    if(ForegroundProcess::directory_identity(root_)!=workspace_.identity())throw ToolAccessDenied("Process root is not the registered workspace");
    std::set<std::string> ids;
    for(const auto& profile:profiles_) {
        if(profile.id.empty() || profile.id.size()>64 || !ids.insert(profile.id).second || profile.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || profile.revision<=0 || !text(profile.executable,32768) || !std::filesystem::u8path(profile.executable).is_absolute() || !std::filesystem::is_regular_file(std::filesystem::u8path(profile.executable)) || profile.prefix_arguments.size()>32 || profile.max_timeout<std::chrono::milliseconds(1) || profile.max_timeout>std::chrono::minutes(10))throw std::invalid_argument("Invalid trusted process profile");
        for(const auto& arg:profile.prefix_arguments)if(!text(arg))throw std::invalid_argument("Invalid trusted process prefix argument");
    }
}
ModelToolDefinition ProcessExecutor::definition() const {
    Json ids=Json::array();for(const auto& p:profiles_)ids.push_back(p.id);
    Json properties=Json::object();
    properties["profile"]={{"type","string"},{"enum",ids}};
    properties["arguments"]={{"type","array"},{"items",{{"type","string"},{"maxLength",4096}}},{"maxItems",32}};
    properties["workdir"]={{"type","string"},{"maxLength",4096}};
    properties["timeout_ms"]={{"type","integer"},{"minimum",1},{"maximum",600000}};
    const auto schema=Json{{"type","object"},{"properties",properties},{"required",Json::array({"profile","arguments"})},{"additionalProperties",false}}.dump();
    return {"run_process","Propose a foreground command through a registered backend profile with literal arguments and a relative workspace directory. Always requires controller approval. A configured shell profile may interpret its arguments as shell text. Report actual exit/output only; timeout or cancellation after dispatch quarantines possible effects.",
        schema};
}
std::string ProcessExecutor::invoke(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,std::stop_token cancel) {
    if(source.size()>65536)throw std::invalid_argument("Process arguments exceed limits");
    Json args;try{args=Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid process argument JSON");}
    for(auto it=args.begin();it!=args.end();++it)if(it.key()!="profile" && it.key()!="arguments" && it.key()!="workdir" && it.key()!="timeout_ms")throw std::invalid_argument("Unknown process argument");
    if(!args.contains("profile") || !args["profile"].is_string() || !args.contains("arguments") || !args["arguments"].is_array() || args["arguments"].size()>32)throw std::invalid_argument("Invalid process profile/arguments");
    const auto selected=args["profile"].get<std::string>();const auto found=std::find_if(profiles_.begin(),profiles_.end(),[&](const auto& p){return p.id==selected;});
    if(found==profiles_.end())throw std::invalid_argument("Unknown registered process profile");
    ProcessConfiguration launch;launch.executable=found->executable;launch.arguments=found->prefix_arguments;launch.timeout=found->max_timeout;launch.output_limit=65536;
    for(const auto& arg:args["arguments"]){if(!arg.is_string() || !text(arg.get<std::string>()))throw std::invalid_argument("Invalid literal process argument");launch.arguments.push_back(arg.get<std::string>());}
    if(args.contains("timeout_ms")){if(!args["timeout_ms"].is_number_integer() || args["timeout_ms"]<1 || args["timeout_ms"]>found->max_timeout.count())throw std::invalid_argument("Process timeout exceeds profile budget");launch.timeout=std::chrono::milliseconds(args["timeout_ms"].get<std::int64_t>());}
    std::string relative=".";if(args.contains("workdir")){if(!args["workdir"].is_string() || !text(args["workdir"].get<std::string>()))throw std::invalid_argument("Invalid process workdir");relative=args["workdir"].get<std::string>();}
    const auto path=std::filesystem::u8path(relative);
    if(relative.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())throw ToolAccessDenied("Process workdir must be relative to its workspace");
    for(const auto& part:path)if(part==L"..")throw ToolAccessDenied("Process directory traversal is forbidden");
    launch.workspace_root=root_;launch.workspace_root_id=workspace_.identity();
    if(ForegroundProcess::directory_identity(root_)!=launch.workspace_root_id)throw ToolAccessDenied("Process workspace path identity changed");
    launch.working_directory=utf8((std::filesystem::u8path(root_)/path).lexically_normal());launch.working_directory_id=ForegroundProcess::directory_identity(launch.working_directory);
    OperationSpec spec{run,launch.workspace_root_id,"run_process",Json{{"profile_id",found->id},{"profile_revision",found->revision},{"executable",launch.executable},{"arguments",launch.arguments},{"workdir",utf8(path.lexically_normal())},{"directory_id",launch.working_directory_id},{"timeout_ms",launch.timeout.count()},{"output_limit",launch.output_limit}}.dump(),{"process-profile:"+found->id}};
    PermissionWaiter(store_).acquire(id,spec,expiry,cancel);
    auto finish=[&](OperationState state,const std::string& result){try{store_.finish_operation(id,state,result).get();}catch(...){throw ProcessOutcomeUnrecorded("Process outcome could not be recorded; claimed-effect recovery is required");}};
    try {
        ProcessResult result;
        try {result=ForegroundProcess::run(launch,cancel);}
        catch(const ProcessBeforeDispatchError&){finish(OperationState::failed,R"({"reason":"process_not_dispatched"})");throw;}
        catch(const std::invalid_argument&){finish(OperationState::failed,R"({"reason":"invalid_process_before_dispatch"})");throw;}
        catch(const ProcessEffectUncertain&){finish(OperationState::uncertain,R"({"reason":"process_effect_uncertain"})");throw;}
        catch(...){finish(OperationState::uncertain,R"({"reason":"unexpected_process_failure"})");throw ProcessEffectUncertain("Process dispatch outcome is not established");}
        const auto termination=result.termination==ProcessTermination::exited?"exited":result.termination==ProcessTermination::cancelled?"cancelled":"timed_out";
        const auto encoded=Json{{"operation_id",id},{"profile_id",found->id},{"pid",result.pid},{"exit_code",result.exit_code},{"termination",termination},{"elapsed_ms",result.elapsed_ms},{"stdout",captured(result.stdout_bytes,result.stdout_count)},{"stderr",captured(result.stderr_bytes,result.stderr_count)},{"truncated",result.truncated},{"process_tree_retired",true},{"independently_verified",false}}.dump();
        if(result.termination!=ProcessTermination::exited){finish(OperationState::uncertain,encoded);throw ProcessEffectUncertain("Interrupted process tree was retired; possible effects remain quarantined");}
        // Nonzero is an observed command result, not a claim of no effects.
        finish(OperationState::succeeded,encoded);return encoded;
    }catch(const ProcessOutcomeUnrecorded&){throw;}
    catch(const ProcessEffectUncertain&){throw;}
    catch(const ProcessBeforeDispatchError&){throw;}
    catch(const std::invalid_argument&){throw;}
    catch(...){throw ProcessOutcomeUnrecorded("Claimed process outcome could not be encoded or preserved; recovery is required");}
}
}
