#include "agentflow/process_executor.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <cctype>
#include <string_view>

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool text(const std::string& value,std::size_t limit=4096) {
    return value.size()<=limit && value.find('\0')==std::string::npos && (value.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)>0);
}
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
bool cpython_leaf(std::string token) {
    while(!token.empty() && (token.front()=='\'' || token.front()=='"' || token.front()=='`'))token.erase(token.begin());
    while(!token.empty() && (token.back()=='\'' || token.back()=='"' || token.back()=='`' || token.back()==',' || token.back()=='.'))token.pop_back();
    const auto separator=token.find_last_of("/\\");if(separator!=std::string::npos)token.erase(0,separator+1);
    std::transform(token.begin(),token.end(),token.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    const auto extension=token.find('.');if(extension!=std::string::npos)token.resize(extension);
    if(token=="py" || token=="python" || token=="pythonw")return true;
    if(!token.starts_with("python") && !token.starts_with("pythonw"))return false;
    const auto prefix=token.starts_with("pythonw")?7u:6u;
    if(token.size()==prefix)return true;
    if(token[prefix]<'0'||token[prefix]>'9')return false;
    for(std::size_t i=prefix+1;i<token.size();++i)if((token[i]<'0'||token[i]>'9')&&token[i]!='.')return false;
    return true;
}
bool shell_executable(const std::string& executable) {
    auto name=std::filesystem::u8path(executable).filename().u8string();
    std::string leaf(reinterpret_cast<const char*>(name.data()),name.size());
    std::transform(leaf.begin(),leaf.end(),leaf.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return leaf=="powershell.exe" || leaf=="pwsh.exe" || leaf=="cmd.exe" || leaf=="bash.exe" || leaf=="sh.exe";
}
bool invokes_cpython(const std::string& executable,const std::vector<std::string>& arguments) {
    if(cpython_leaf(std::filesystem::u8path(executable).filename().string()))return true;
    if(!shell_executable(executable))return false;
    for(const auto& argument:arguments) {
        std::string token;
        auto inspect=[&]{if(!token.empty() && cpython_leaf(token))return true;token.clear();return false;};
        for(unsigned char c:argument) {
            if(std::isspace(c)||c=='\''||c=='"'||c=='`'||c==';'||c=='&'||c=='|'||c=='('||c==')'||c=='='||c==',') {
                if(inspect())return true;
            } else token.push_back(static_cast<char>(c));
        }
        if(inspect())return true;
    }
    return false;
}
std::string hex_bytes(std::string_view bytes) {
    constexpr char digits[]="0123456789abcdef";std::string encoded;encoded.reserve(bytes.size()*2);
    for(unsigned char b:bytes){encoded.push_back(digits[b>>4]);encoded.push_back(digits[b&15]);}
    return encoded;
}
Json captured(const std::string& bytes,std::uint64_t count) {
    if(bytes.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0)>0)
        return {{"encoding","utf-8"},{"data",bytes},{"byte_count",count},{"retained_bytes",bytes.size()}};
    return {{"encoding","hex"},{"data",hex_bytes(bytes)},{"byte_count",count},{"retained_bytes",bytes.size()}};
}
// Journal retained raw bytes before clients see them. Hex preserves fragments
// across UTF-8 boundaries. Full 4 KiB chunks plus at most 64 early partial
// flushes and two final fragments bound one 64 KiB capture to 82 events.
class OutputJournal {
public:
    OutputJournal(PersistenceService& store,const std::string& run,const std::string& operation,const std::string& profile)
        :store_(store),run_(run),operation_(operation),profile_(profile) {}
    void append(bool error,std::string_view bytes) {
        constexpr std::size_t event_limit=65536;const auto pending_total=pending_[0].size()+pending_[1].size();const auto saved=static_cast<std::size_t>(offsets_[0]+offsets_[1])+pending_total;if(saved>=event_limit)return;
        const auto keep=std::min(bytes.size(),event_limit-saved);if(!keep)return;const auto channel=error?1:0;auto& pending=pending_[channel];pending.append(bytes.data(),keep);
        while(pending.size()>=4096)flush(channel,4096);
        const auto now=std::chrono::steady_clock::now();
        if(!pending.empty() && early_<64 && (early_==0 || now-last_>=std::chrono::milliseconds(100))) {
            flush(channel,pending.size());++early_;last_=now;
        }
    }
    void finish(){for(int channel=0;channel<2;++channel)if(!pending_[channel].empty())flush(channel,pending_[channel].size());}
private:
    void flush(int channel,std::size_t size) {
        const auto json=Json{{"operation_id",operation_},{"profile_id",profile_},{"channel",channel?"stderr":"stdout"},{"encoding","hex"},
            {"offset",offsets_[channel]},{"retained_bytes",size},{"data",hex_bytes(std::string_view(pending_[channel]).substr(0,size))}}.dump();
        store_.append_event(run_,"process.output",json).get();
        offsets_[channel]+=size;pending_[channel].erase(0,size);
    }
    PersistenceService& store_;const std::string& run_;const std::string& operation_;const std::string& profile_;
    std::string pending_[2];std::uint64_t offsets_[2]{};std::size_t early_=0;
    std::chrono::steady_clock::time_point last_{};
};
class OutputArtifactWriter {
public:
    OutputArtifactWriter(PersistenceService& store,const std::string& run,const std::string& operation)
        :store_(store),run_(run),operation_(operation),state_(store.begin_process_output(operation,run).get()){}
    ~OutputArtifactWriter(){if(!finished_)try{state_=store_.finish_process_output(operation_,run_,false).get();}catch(...) {}}
    void append(bool error,std::string_view bytes){if(bytes.empty())return;store_.append_process_output(operation_,run_,error,std::string(bytes)).get();state_.stdout_bytes+=(error?0:bytes.size());state_.stderr_bytes+=(error?bytes.size():0);}
    ProcessOutputArtifact finish(bool complete){if(!finished_){state_=store_.finish_process_output(operation_,run_,complete).get();finished_=true;}return state_;}
private:
    PersistenceService& store_;const std::string& run_;const std::string& operation_;ProcessOutputArtifact state_;bool finished_=false;
};
}
ProcessExecutor::ProcessExecutor(PersistenceService& store,WorkspaceTools& workspace,std::string root,std::vector<ProcessProfile> profiles)
    :store_(store),workspace_(workspace),root_(std::move(root)),profiles_(std::move(profiles)) {
    if(profiles_.empty() || profiles_.size()>16 || !text(root_,32768) || !std::filesystem::u8path(root_).is_absolute())throw std::invalid_argument("Invalid process configuration");
    if(ForegroundProcess::directory_identity(root_)!=workspace_.identity())throw ToolAccessDenied("Process root is not the registered workspace");
    std::set<std::string> ids;
    for(auto& profile:profiles_) {
        if(profile.id.empty() || profile.id.size()>64 || !ids.insert(profile.id).second || profile.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos || profile.revision<=0 || !text(profile.executable,32768) || !std::filesystem::u8path(profile.executable).is_absolute() || !std::filesystem::is_regular_file(std::filesystem::u8path(profile.executable)) || profile.prefix_arguments.size()>32 || profile.max_timeout<std::chrono::milliseconds(1) || profile.max_timeout>std::chrono::minutes(10))throw std::invalid_argument("Invalid trusted process profile");
        for(const auto& arg:profile.prefix_arguments)if(!text(arg))throw std::invalid_argument("Invalid trusted process prefix argument");
        const auto actual=ForegroundProcess::executable_identity(profile.executable);
        if(profile.executable_id.empty())profile.executable_id=actual;
        else if(profile.executable_id!=actual)throw ProcessBeforeDispatchError("Registered process executable changed; administrator must reimport its profile");
    }
}
ModelToolDefinition ProcessExecutor::definition() const {
    Json ids=Json::array();for(const auto& p:profiles_)ids.push_back(p.id);
    Json properties=Json::object();
    properties["profile"]={{"type","string"},{"enum",ids}};
    properties["arguments"]={{"type","array"},{"items",{{"type","string"},{"maxLength",4096}}},{"maxItems",32}};
    properties["workdir"]={{"type","string"},{"maxLength",4096}};
    properties["folder_id"]={{"type","string"},{"description","Optional registered workspace folder ID; omit it to use the primary folder."}};
    properties["timeout_ms"]={{"type","integer"},{"minimum",1},{"maximum",600000}};
    properties["background"]={{"type","boolean"}};
    const auto schema=Json{{"type","object"},{"properties",properties},{"required",Json::array({"profile","arguments"})},{"additionalProperties",false}}.dump();
    std::string description="Run a command through a registered backend profile with literal arguments and a relative workspace directory. The trusted backend profile determines whether each operation requires approval. Before reviewing changes, use the bounded workspace tools to inspect the relevant files and confirm the target directory is a Git repository before running Git diff commands. Do not chain Git status and diff commands: if the directory is not a repository, skip Git review and inspect the relevant files with workspace tools. Set background=true for long-running work; then call wait_process with the returned job_id before giving a final answer. Do not detach child processes with shell-level Start-Process, start, or output redirection; use background=true so the native runtime owns and joins the process. Background commands keep their exact authorization, timeout and workspace/profile resource lock. A configured shell profile may interpret its arguments as shell text.";
    if(std::any_of(profiles_.begin(),profiles_.end(),[](const auto& profile){return profile.id=="powershell";}))description+=" For the powershell profile, pass one complete PowerShell script as arguments[0]; it uses a no-profile, noninteractive PowerShell process and runs without a per-command approval prompt.";
    if(std::any_of(profiles_.begin(),profiles_.end(),[](const auto& profile){return profile.id=="cmd";}))description+=" For the cmd profile, pass one complete Windows command as arguments[0]; it uses the system command processor and runs without a per-command approval prompt.";
    if(std::any_of(profiles_.begin(),profiles_.end(),[](const auto& profile){return profile.id=="xlang3";}))description+=" Use the xlang3 profile for Python-compatible workspace scripts, tests and package commands; never invoke python or python.exe. xMind already prepends -B to this profile, so do not pass -B or a python launcher name. Pass module commands directly, for example ['-m','pip','install','-r','requirements.txt'], ['-m','pytest','-q'] or ['-c','print(1 + 1)']. Its executable is pinned to the xlang3 runtime paired with this xMind backend, so it does not depend on whichever xlang3.exe happens to appear first on machine PATH. xMind adds the paired runtime modules and a .xlang3-site directory in the selected working directory to PYTHONPATH when those directories exist.";
    description+=" The native executor rejects direct CPython executable profiles and common python/python3.x/pythonw/py launcher names in registered shell commands before dispatch; use the xlang3 profile for Python-compatible work.";
    return {"run_process",std::move(description),
        schema};
}
ModelToolDefinition ProcessExecutor::wait_definition() const {
    const auto schema=Json{{"type","object"},{"properties",{{"job_id",{{"type","string"},{"minLength",1},{"maxLength",128}}},{"timeout_ms",{{"type","integer"},{"minimum",1},{"maximum",600000}}}}},{"required",Json::array({"job_id"})},{"additionalProperties",false}}.dump();
    return {"wait_process","Wait for a background run_process job owned by this run and return its actual exit status and retained output. Use the job_id returned by run_process; if it is still running, call again. Never report completion before this tool returns a terminal state.",schema};
}
ProcessExecutor::~ProcessExecutor(){
    std::lock_guard lock(workers_mutex_);for(auto& worker:workers_)worker.thread.request_stop();workers_.clear();
}
void ProcessExecutor::reap_workers(){
    std::lock_guard lock(workers_mutex_);
    for(auto it=workers_.begin();it!=workers_.end();){if(it->done->load(std::memory_order_acquire)){if(it->thread.joinable())it->thread.join();it=workers_.erase(it);}else ++it;}
}
std::string ProcessExecutor::execute_claimed(const std::string& id,const std::string& run,const std::string& profile,
        ProcessConfiguration launch,std::stop_token cancel) {
    auto finish=[&](OperationState state,const std::string& result){try{store_.finish_operation(id,state,result).get();}catch(...){throw ProcessOutcomeUnrecorded("Process outcome could not be recorded; claimed-effect recovery is required");}};
    try {
        OutputJournal output(store_,run,id,profile);OutputArtifactWriter artifact(store_,run,id);
        ProcessResult result;
        try {result=ForegroundProcess::run(launch,cancel,[&](bool error,std::string_view bytes){artifact.append(error,bytes);output.append(error,bytes);});}
        catch(const ProcessBeforeDispatchError&){(void)artifact.finish(true);finish(OperationState::failed,R"({"reason":"process_not_dispatched"})");throw;}
        catch(const std::invalid_argument&){(void)artifact.finish(true);finish(OperationState::failed,R"({"reason":"invalid_process_before_dispatch"})");throw;}
        catch(const ProcessEffectUncertain&){(void)artifact.finish(false);finish(OperationState::uncertain,R"({"reason":"process_effect_uncertain"})");throw;}
        catch(...){(void)artifact.finish(false);finish(OperationState::uncertain,R"({"reason":"unexpected_process_failure"})");throw ProcessEffectUncertain("Process dispatch outcome is not established");}
        output.finish();const auto captured_output=artifact.finish(true);
        const auto termination=result.termination==ProcessTermination::exited?"exited":result.termination==ProcessTermination::cancelled?"cancelled":"timed_out";
        const auto encoded=Json{{"operation_id",id},{"job_id",id},{"profile_id",profile},{"pid",result.pid},{"exit_code",result.exit_code},{"termination",termination},{"elapsed_ms",result.elapsed_ms},{"stdout",captured(result.stdout_bytes,result.stdout_count)},{"stderr",captured(result.stderr_bytes,result.stderr_count)},{"truncated",result.truncated},{"output_artifact",{{"operation_id",id},{"stdout_bytes",captured_output.stdout_bytes},{"stderr_bytes",captured_output.stderr_bytes},{"state",captured_output.state}}},{"process_tree_retired",true},{"independently_verified",false}}.dump();
        if(result.termination!=ProcessTermination::exited){finish(OperationState::uncertain,encoded);throw ProcessEffectUncertain("Interrupted process tree was retired; possible effects remain quarantined");}
        finish(OperationState::succeeded,encoded);return encoded;
    }catch(const ProcessOutcomeUnrecorded&){throw;}
    catch(const ProcessEffectUncertain&){throw;}
    catch(const ProcessBeforeDispatchError&){throw;}
    catch(const std::invalid_argument&){throw;}
    catch(...){throw ProcessOutcomeUnrecorded("Claimed process outcome could not be encoded or preserved; recovery is required");}
}
std::string ProcessExecutor::invoke(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,std::stop_token cancel,InstructionPrecondition guidance,bool allow_background) {
    return invoke_scoped(id,run,source,expiry,workspace_,root_,workspace_.identity(),workspace_.identity(),1,true,cancel,std::move(guidance),allow_background);
}
std::string ProcessExecutor::invoke_in_workspace(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,const WorkspaceToolSet& membership,std::size_t folder_index,std::stop_token cancel,InstructionPrecondition guidance,bool allow_background){
    if(folder_index>=membership.folders().size()||membership.revision()<1||membership.workspace_id().empty())throw ToolAccessDenied("Process folder is not in the admitted workspace snapshot");
    const auto& binding=membership.folders()[folder_index];auto& selected=membership.folder_tools(folder_index);
    if(selected.identity()!=binding.folder_id||selected.root_path()!=binding.root)throw ToolAccessDenied("Process folder identity differs from the admitted workspace snapshot");
    return invoke_scoped(id,run,source,expiry,selected,binding.root,membership.workspace_id(),binding.folder_id,membership.revision(),folder_index==0,cancel,std::move(guidance),allow_background);
}
std::string ProcessExecutor::invoke_scoped(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,WorkspaceTools& workspace,const std::string& root,std::string workspace_id,std::string folder_id,std::int64_t workspace_revision,bool primary_folder,std::stop_token cancel,InstructionPrecondition guidance,bool allow_background) {
    if(source.size()>65536)throw std::invalid_argument("Process arguments exceed limits");
    if(workspace_id.empty()||folder_id.empty()||workspace_revision<1||workspace.identity()!=folder_id||workspace.root_path()!=root)throw ToolAccessDenied("Process folder binding is invalid");
    Json args;try{args=Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid process argument JSON");}
    if(!args.is_object())throw std::invalid_argument("Invalid process argument object");
    for(auto it=args.begin();it!=args.end();++it)if(it.key()!="profile" && it.key()!="arguments" && it.key()!="workdir" && it.key()!="folder_id" && it.key()!="timeout_ms" && it.key()!="background")throw std::invalid_argument("Unknown process argument");
    if(args.contains("folder_id")&&(!args["folder_id"].is_string()||args["folder_id"].get<std::string>()!=folder_id))throw ToolAccessDenied("Process folder does not match its admitted workspace folder");
    if(!args.contains("folder_id")&&!primary_folder)throw ToolAccessDenied("An attached process folder must be selected explicitly");
    if(!args.contains("profile") || !args["profile"].is_string() || !args.contains("arguments") || !args["arguments"].is_array() || args["arguments"].size()>32)throw std::invalid_argument("Invalid process profile/arguments");
    const auto selected=args["profile"].get<std::string>();const auto found=std::find_if(profiles_.begin(),profiles_.end(),[&](const auto& p){return p.id==selected;});
    if(found==profiles_.end())throw std::invalid_argument("Unknown registered process profile");
    if(args.contains("background")&&!args["background"].is_boolean())throw std::invalid_argument("Invalid process background mode");
    const bool background=args.value("background",false);if(background&&!allow_background)throw std::invalid_argument("Background process execution requires an agent owner");
    ProcessConfiguration launch;launch.executable=found->executable;launch.arguments=found->prefix_arguments;launch.timeout=found->max_timeout;launch.output_limit=65536;
    // xlang3 writes xlang3-specific .pyc files beside pure module facades by
    // default. Keep the paired, hash-verified runtime tree immutable during
    // workspace scripts and tests.
    if(found->id=="xlang3"&&std::find(launch.arguments.begin(),launch.arguments.end(),"-B")==launch.arguments.end())launch.arguments.insert(launch.arguments.begin(),"-B");
    launch.executable_id=found->executable_id;
    for(const auto& arg:args["arguments"]){if(!arg.is_string() || !text(arg.get<std::string>()))throw std::invalid_argument("Invalid literal process argument");launch.arguments.push_back(arg.get<std::string>());}
    if(invokes_cpython(launch.executable,launch.arguments))throw ToolAccessDenied("CPython execution is disabled; use the registered xlang3 profile for Python-compatible commands.");
    if(args.contains("timeout_ms")){if(!args["timeout_ms"].is_number_integer() || args["timeout_ms"]<1 || args["timeout_ms"]>found->max_timeout.count())throw std::invalid_argument("Process timeout exceeds profile budget");launch.timeout=std::chrono::milliseconds(args["timeout_ms"].get<std::int64_t>());}
    std::string relative=".";if(args.contains("workdir")){if(!args["workdir"].is_string() || !text(args["workdir"].get<std::string>()))throw std::invalid_argument("Invalid process workdir");relative=args["workdir"].get<std::string>();}
    const auto path=std::filesystem::u8path(relative);
    if(relative.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())throw ToolAccessDenied("Process workdir must be relative to its workspace");
    for(const auto& part:path)if(part==L"..")throw ToolAccessDenied("Process directory traversal is forbidden");
    const auto public_directory_id=workspace.directory_identity(relative,cancel);
    launch.workspace_root=root;launch.workspace_root_id=workspace.identity();
    if(ForegroundProcess::directory_identity(root)!=launch.workspace_root_id)throw ToolAccessDenied("Process workspace path identity changed");
    launch.working_directory=utf8((std::filesystem::u8path(root)/path).lexically_normal());launch.working_directory_id=ForegroundProcess::directory_identity(launch.working_directory);
    if(launch.working_directory_id!=public_directory_id)throw ToolAccessDenied("Process working directory changed during public workspace admission");
    if(found->id=="xlang3") {
        std::vector<std::filesystem::path> import_paths;
        const auto modules=std::filesystem::u8path(found->executable).parent_path()/L"modules";
        if(std::filesystem::is_directory(modules))import_paths.push_back(modules);
        const auto site_packages=std::filesystem::u8path(launch.working_directory)/L".xlang3-site";
        if(std::filesystem::is_directory(site_packages))import_paths.push_back(site_packages);
        if(!import_paths.empty()) {
            std::string python_path;
            for(const auto& import_path:import_paths) {
                if(!python_path.empty())python_path.push_back(';');
                const auto value=import_path.u8string();python_path.append(reinterpret_cast<const char*>(value.data()),value.size());
            }
            launch.environment.emplace_back("PYTHONPATH",std::move(python_path));
        }
    }
    Json environment=Json::array();for(const auto& [name,value]:launch.environment)environment.push_back({{"name",name},{"value",value}});
    OperationSpec spec{run,workspace_id,"run_process",Json{{"workspace_id",workspace_id},{"workspace_revision",workspace_revision},{"folder_id",folder_id},{"target_workspace_id",launch.workspace_root_id},{"profile_id",found->id},{"profile_revision",found->revision},{"executable",launch.executable},{"executable_id",launch.executable_id},{"arguments",launch.arguments},{"environment",std::move(environment)},{"workdir",utf8(path.lexically_normal())},{"directory_id",launch.working_directory_id},{"timeout_ms",launch.timeout.count()},{"output_limit",launch.output_limit},{"background",background}}.dump(),{"process-profile:"+found->id}};
    guidance.validate();if(guidance.verify){auto payload=Json::parse(spec.arguments_json);payload["repository_guidance"]=Json::parse(guidance.metadata_json);spec.arguments_json=payload.dump();}
    const auto trusted_actor=found->auto_approve?"trusted-process-profile:"+found->id:std::string{};
    PermissionWaiter(store_).acquire(id,spec,expiry,cancel,trusted_actor);
    auto finish_before_dispatch=[&](const std::string& reason){try{store_.finish_operation(id,OperationState::failed,Json{{"reason",reason}}.dump()).get();}catch(...){throw ProcessOutcomeUnrecorded("Process outcome could not be recorded; claimed-effect recovery is required");}};
    if(guidance.verify){try{guidance.verify(cancel);}catch(...){finish_before_dispatch("repository_guidance_changed_or_unavailable_before_effect");throw;}}
    if(!background)return execute_claimed(id,run,found->id,std::move(launch),cancel);
    reap_workers();
    auto done=std::make_shared<std::atomic_bool>(false);
    {
        std::lock_guard lock(workers_mutex_);
        if(workers_.size()>=16){finish_before_dispatch("background_process_capacity_exceeded");throw ProcessBeforeDispatchError("Background process capacity is full");}
        try {
            workers_.reserve(workers_.size()+1);
            auto* const self=this;const auto profile=found->id;auto owned_launch=std::move(launch);
            std::jthread worker([self,id,run,profile,launch=std::move(owned_launch),done,owner_cancel=cancel](std::stop_token shutdown)mutable{
                std::stop_source stop;std::stop_callback stop_shutdown(shutdown,[&]{stop.request_stop();});std::stop_callback stop_owner(owner_cancel,[&]{stop.request_stop();});
                try{(void)self->execute_claimed(id,run,profile,std::move(launch),stop.get_token());}
                catch(...){try{const auto current=self->store_.operation(id).get();if(current.state==OperationState::executing)self->store_.finish_operation(id,OperationState::uncertain,R"({"reason":"background_process_worker_failed"})").get();}catch(...) {}}
                done->store(true,std::memory_order_release);
            });
            workers_.push_back({done,std::move(worker)});
        }catch(const ProcessOutcomeUnrecorded&){throw;}
        catch(...){finish_before_dispatch("background_process_start_failed");throw ProcessBeforeDispatchError("Background process could not be started");}
    }
    return Json{{"job_id",id},{"operation_id",id},{"profile_id",found->id},{"state","running"},{"background",true}}.dump();
}
std::string ProcessExecutor::wait(const std::string& run,const std::string& source,std::stop_token cancel){
    Json args;try{args=Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid background process wait arguments");}
    if(!args.is_object()||!args.contains("job_id")||!args["job_id"].is_string()||args["job_id"].get_ref<const std::string&>().empty()||args["job_id"].get_ref<const std::string&>().size()>128||args.size()>(args.contains("timeout_ms")?2u:1u))throw std::invalid_argument("Invalid background process job ID");
    for(auto it=args.begin();it!=args.end();++it)if(it.key()!="job_id"&&it.key()!="timeout_ms")throw std::invalid_argument("Unknown background process wait argument");
    std::int64_t timeout=600000;if(args.contains("timeout_ms")){if(!args["timeout_ms"].is_number_integer()||args["timeout_ms"]<1||args["timeout_ms"]>600000)throw std::invalid_argument("Invalid background process wait timeout");timeout=args["timeout_ms"].get<std::int64_t>();}
    const auto id=args["job_id"].get<std::string>();const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    for(;;){if(cancel.stop_requested())throw ToolCancelled("Background process wait was cancelled");const auto op=store_.operation(id).get();if(op.spec.run_id!=run||op.spec.tool!="run_process")throw ToolAccessDenied("Background process belongs to another run");
        Json binding;try{binding=Json::parse(op.spec.arguments_json);}catch(...){throw DatabaseError("Saved process binding is invalid");}if(!binding.value("background",false))throw std::invalid_argument("Operation is not a background process job");
        if(op.state!=OperationState::executing){Json result={{"job_id",id},{"operation_id",id},{"state",to_string(op.state)}};if(op.state==OperationState::succeeded||op.state==OperationState::failed||op.state==OperationState::uncertain){try{result["result"]=Json::parse(op.result_json);}catch(...){throw DatabaseError("Saved process result is invalid");}}return result.dump();}
        if(std::chrono::steady_clock::now()>=deadline)return Json{{"job_id",id},{"operation_id",id},{"state","running"}}.dump();std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
std::vector<std::string> ProcessExecutor::join_run(const std::string& run,std::stop_token cancel){
    std::vector<std::string> completed;for(;;){if(cancel.stop_requested())throw ToolCancelled("Background process join was cancelled");const auto operations=store_.operations(run).get();auto found=std::find_if(operations.begin(),operations.end(),[](const auto& operation){if(operation.state!=OperationState::executing||operation.spec.tool!="run_process")return false;try{return Json::parse(operation.spec.arguments_json).value("background",false);}catch(...){return false;}});if(found==operations.end())return completed;
        const auto waited=Json::parse(wait(run,Json{{"job_id",found->id}}.dump(),cancel));if(waited.value("state",std::string{})=="running")continue;if(waited.value("state",std::string{})=="uncertain")throw ProcessEffectUncertain("Background process outcome is uncertain");completed.push_back(waited.dump());
    }
}
}
