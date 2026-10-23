#include "agentflow/http_server.hpp"
#if defined(_WIN32)
#include "agentflow/xmind_ipc_api.hpp"
#endif
#include "agentflow/agent_instructions.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <fstream>
#include <filesystem>
#if defined(_WIN32)
#include "agentflow/agent_service.hpp"
#include "agentflow/provider_setup.hpp"
#include "agentflow/provider_profile_legacy_setup.hpp"
#include "agentflow/gemini_model_policy.hpp"
#include "agentflow/deepseek_model_policy.hpp"
#include "agentflow/context_model_policy.hpp"
#include "agentflow/provider_yaml_config.hpp"
#include "agentflow/execution_platform.hpp"
#include "agentflow/agent_worker_read_executor.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/process_configuration.hpp"
#include "agentflow/language_server_configuration.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/workspace_registry.hpp"
#include "agentflow/local_profile.hpp"
#include "agentflow/mcp_oauth_service.hpp"
#include "agentflow/mcp_resource_catalog.hpp"
#include "agentflow/xlang_python_config.hpp"
#include "agentflow/pty_sessions.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
namespace {
class LocalWorkspaceEditRecovery final : public agentflow::EditRecoveryReader {
public:
    LocalWorkspaceEditRecovery(agentflow::PersistenceService& store,std::string primary_root):store_(store),primary_(std::move(primary_root)){}
    agentflow::EditRecoveryInspection inspect_uncertain(const std::string& id,std::stop_token cancel={})override{
        const auto operation=store_.operation(id).get();const auto proposal=nlohmann::json::parse(operation.spec.arguments_json);
        agentflow::WorkspaceTools primary(primary_);
        if(!proposal.contains("folder_id"))return agentflow::EditExecutor(store_,primary).inspect_uncertain(id,cancel);
        if(proposal.value("folder_id",std::string{})==primary.identity()&&proposal.value("target_workspace_id",std::string{})==primary.identity()&&operation.spec.workspace==primary.identity())return agentflow::EditExecutor(store_,primary,operation.spec.workspace,primary.identity(),proposal.value("workspace_revision",std::int64_t{1})).inspect_uncertain(id,cancel);
        if(!proposal.contains("workspace_id")||!proposal["workspace_id"].is_string()||proposal["workspace_id"].get<std::string>()!=operation.spec.workspace||
           !proposal.contains("workspace_revision")||!proposal["workspace_revision"].is_number_integer()||proposal["workspace_revision"].get<std::int64_t>()<1)
            throw agentflow::ToolAccessDenied("Recovery proposal has no valid workspace membership binding");
        std::optional<agentflow::WorkspaceMembershipSnapshot> membership;
        for(const auto& event:store_.events(operation.spec.run_id).get())if(event.kind=="agent.workspace_membership"){
            if(membership)throw agentflow::ToolAccessDenied("Run contains duplicate workspace membership snapshots");
            const auto value=nlohmann::json::parse(event.json);if(!value.is_object()||!value.contains("workspace_id")||!value["workspace_id"].is_string()||!value.contains("revision")||!value["revision"].is_number_integer()||!value.contains("folders")||!value["folders"].is_array()||value["folders"].empty()||value["folders"].size()>256)throw agentflow::ToolAccessDenied("Saved workspace membership snapshot is invalid");
            agentflow::WorkspaceMembershipSnapshot saved;saved.workspace_id=value["workspace_id"].get<std::string>();saved.revision=value["revision"].get<std::int64_t>();for(const auto& folder:value["folders"]){if(!folder.is_object()||!folder.contains("folder_id")||!folder["folder_id"].is_string()||!folder.contains("root")||!folder["root"].is_string())throw agentflow::ToolAccessDenied("Saved workspace folder binding is invalid");saved.folders.push_back({folder["folder_id"].get<std::string>(),folder["root"].get<std::string>()});}membership=std::move(saved);
        }
        if(!membership||membership->workspace_id!=operation.spec.workspace||membership->revision!=proposal["workspace_revision"].get<std::int64_t>()||membership->folders.empty()||membership->folders.front().root!=primary.root_path())throw agentflow::ToolAccessDenied("Recovery workspace membership differs from the admitted run");
        agentflow::WorkspaceToolSet tools(membership->workspace_id,membership->revision,membership->folders);const auto& folder_id=proposal["folder_id"].get<std::string>();const auto index=tools.folder_index(folder_id);auto& target=tools.folder_tools(index);
        return agentflow::EditExecutor(store_,target,membership->workspace_id,folder_id,membership->revision).inspect_uncertain(id,cancel);
    }
private:agentflow::PersistenceService& store_;std::string primary_;
};
std::mutex control_mutex;
agentflow::HttpServer* active_server=nullptr;
std::string loaded_runtime_root(){std::wstring image(32768,L'\0');const auto count=GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));if(!count||count>=image.size())throw std::runtime_error("Cannot qualify native process image");image.resize(count);if(image.starts_with(L"\\\\?\\")&&image.size()>6&&image[5]==L':')image.erase(0,4);const auto value=std::filesystem::path(image).parent_path().u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string system_command_shell(){std::array<wchar_t,32768> directory{};const auto count=GetSystemDirectoryW(directory.data(),static_cast<UINT>(directory.size()));if(!count||count>=directory.size())throw std::runtime_error("Cannot locate the native terminal shell");const auto path=(std::filesystem::path(std::wstring(directory.data(),count))/L"cmd.exe").u8string();return {reinterpret_cast<const char*>(path.data()),path.size()};}
std::optional<agentflow::ProcessProfile> paired_xlang3_process_profile(const std::string& modules_directory,const agentflow::ProcessProfile& trusted_shell){
    const auto modules=std::filesystem::absolute(std::filesystem::u8path(modules_directory)).lexically_normal();const auto path=modules.parent_path()/L"xlang3.exe";
    if(!std::filesystem::is_regular_file(path))return std::nullopt;const auto executable=path.u8string();agentflow::ProcessProfile profile;
    profile.id="xlang3";profile.executable=std::string(reinterpret_cast<const char*>(executable.data()),executable.size());profile.revision=1;
    profile.max_timeout=trusted_shell.max_timeout;profile.executable_id=agentflow::ForegroundProcess::executable_identity(profile.executable);profile.auto_approve=trusted_shell.auto_approve;return profile;
}
void append_paired_xlang3_process_profile(std::vector<agentflow::ProcessProfile>& profiles,const std::string& modules_directory){
    if(profiles.size()>=16||std::any_of(profiles.begin(),profiles.end(),[](const auto& profile){return profile.id=="xlang3";}))return;
    const auto shell=std::find_if(profiles.begin(),profiles.end(),[](const auto& profile){return profile.id=="powershell"||profile.id=="cmd";});
    if(shell!=profiles.end())if(auto paired=paired_xlang3_process_profile(modules_directory,*shell))profiles.push_back(std::move(*paired));
}
std::string default_windows_process_profiles(){
    std::array<wchar_t,32768> directory{};const auto count=GetSystemDirectoryW(directory.data(),static_cast<UINT>(directory.size()));if(!count||count>=directory.size())throw std::runtime_error("Cannot locate the native process shell");
    const auto system=std::filesystem::path(std::wstring(directory.data(),count));const auto powershell=system/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
    nlohmann::json profiles=nlohmann::json::array();
    if(std::filesystem::is_regular_file(powershell))profiles.push_back({{"id","powershell"},{"executable",powershell.string()},{"prefix_arguments",{"-NoLogo","-NoProfile","-NonInteractive","-Command"}},{"max_timeout_ms",600000},{"auto_approve",true}});
    else {const auto command=system/L"cmd.exe";if(!std::filesystem::is_regular_file(command))throw std::runtime_error("No supported native process shell is installed");profiles.push_back({{"id","cmd"},{"executable",command.string()},{"prefix_arguments",{"/d","/s","/c"}},{"max_timeout_ms",600000},{"auto_approve",true}});}
    return nlohmann::json{{"profiles",std::move(profiles)}}.dump();
}
agentflow::BackendOwnerReceipt startup_receipt(const std::string& value){const auto first=value.find(':'),last=value.rfind(':');if(first!=32||last==first||last+33!=value.size())throw std::invalid_argument("Invalid native replacement receipt");std::int64_t revision=0;const auto parsed=std::from_chars(value.data()+first+1,value.data()+last,revision);const auto generation=value.substr(0,first),id=value.substr(last+1);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+last||revision<1||revision>=9007199254740991LL||generation.find_first_not_of("0123456789abcdef")!=std::string::npos||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid native replacement receipt");return {generation,id,revision};}
std::string provider_purpose(const std::string& endpoint,const char* domain="provider:chat:") {
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("Cannot bind provider credential context");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(endpoint.data())),static_cast<ULONG>(endpoint.size()),hash.data(),static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0) throw std::runtime_error("Cannot bind provider credential context");
    std::ostringstream result;result<<domain<<std::hex<<std::setfill('0');
    for(auto byte:hash) result<<std::setw(2)<<static_cast<unsigned>(byte);return result.str();
}
std::string credential_id() {
    std::random_device random;std::ostringstream result;result<<"provider-"<<std::hex<<std::setfill('0');
    for(int i=0;i<4;++i) result<<std::setw(8)<<random();return result.str();
}
BOOL WINAPI control(DWORD event) {
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT) return FALSE;
    std::lock_guard lock(control_mutex);
    if(active_server) active_server->stop();return TRUE;
}
}
#endif
int run_server(int argc,char** argv) {
    try {
        std::map<std::string,std::string> options;
        for(int i=1;i<argc;i+=2) {
            const std::string key=argv[i];
            if(i+1>=argc || (key!="--db" && key!="--modules" && key!="--stdlib" && key!="--port" && key!="--model" && key!="--model-endpoint" && key!="--model-wire" && key!="--model-tools" && key!="--models" && key!="--model-stream-usage" && key!="--workspace" && key!="--workspace-registry" && key!="--inspection-workspace" && key!="--workspace-edits" && key!="--credential-id" && key!="--workers" && key!="--queue-limit" && key!="--mcp-config" && key!="--process-config" && key!="--language-server-config" && key!="--instructions-config" && key!="--graphs-config" && key!="--provider-config" && key!="--python-tools-config" && key!="--runtime-manifest-sha256" && key!="--owner-receipt" && key!="--legacy-owner-ticket" && key!="--profile-state" && key!="--agent-execution") || !options.emplace(key,argv[i+1]).second)
                throw std::invalid_argument("Usage: xmind_server --db FILE --modules DIR --stdlib DIR [--port PORT] [--provider-config FILE | --model ID --model-endpoint URL] [--python-tools-config FILE] [--language-server-config FILE] [--model-wire chat-completions|responses] [--model-tools supported|unsupported|unknown] [--model-stream-usage supported|unsupported|unknown] [--models ID1,ID2] [--workspace DIR | --inspection-workspace DIR] [--workspace-edits approved] [--credential-id ID] [--workers 1..16] [--queue-limit 1..4096] [--instructions-config FILE] [--graphs-config FILE] [--agent-execution thread|process-read|process-code]");
        }
        for(const auto* key:{"--db","--modules","--stdlib"}) if(!options.contains(key)) throw std::invalid_argument("Missing server configuration");
        const auto execution_mode=options.contains("--agent-execution")?options.at("--agent-execution"):"thread";
        if(execution_mode!="thread"&&execution_mode!="process-read"&&execution_mode!="process-code")throw std::invalid_argument("Agent execution must be thread, process-read or process-code");
        const bool process_read=execution_mode=="process-read",process_code=execution_mode=="process-code",process_worker=process_read||process_code;
        if(process_worker){if(process_code&&!options.contains("--workspace"))throw std::invalid_argument("Process coding execution requires an execution workspace");if(process_read)for(const auto* key:{"--mcp-config","--language-server-config","--python-tools-config"})if(options.contains(key))throw std::invalid_argument("Standalone process execution received incompatible configuration");if(process_read&&options.contains("--process-config"))throw std::invalid_argument("Process read execution cannot acquire command profiles");if(process_read&&options.contains("--workspace-edits"))throw std::invalid_argument("Process read execution is read-only");if(process_code&&(!options.contains("--workspace-edits")||options.at("--workspace-edits")!="approved"))throw std::invalid_argument("Process coding execution requires approved workspace edits");}
        int port=8765;
        if(options.contains("--port")) {
            const auto& value=options.at("--port");const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid port");
        }
        const auto* token=std::getenv("XMIND_AUTH_TOKEN");
        if(!token) throw std::invalid_argument("Set XMIND_AUTH_TOKEN for the local server");
        agentflow::validate_local_auth_token(token);
#if !defined(_WIN32)
        if(process_worker)throw std::invalid_argument("Process agent execution requires Windows");
        if(options.contains("--runtime-manifest-sha256")||options.contains("--owner-receipt")||options.contains("--legacy-owner-ticket"))throw std::invalid_argument("Native owner replacement currently requires Windows");
        if(options.contains("--mcp-config"))throw std::invalid_argument("Native MCP configuration currently requires Windows");
        if(options.contains("--process-config"))throw std::invalid_argument("Native process configuration currently requires Windows");
        if(options.contains("--language-server-config"))throw std::invalid_argument("Native language-server configuration currently requires Windows");
        if(options.contains("--graphs-config"))throw std::invalid_argument("Native graph execution currently requires Windows");
        if(options.contains("--provider-config"))throw std::invalid_argument("Native provider configuration currently requires Windows");
#endif
        const std::string auth=token;
        if(port<0 || port>65535) throw std::invalid_argument("Invalid port");
        if(options.contains("--model")!=options.contains("--model-endpoint")) throw std::invalid_argument("Model ID and endpoint must be configured together");
        if(options.contains("--provider-config")&&options.contains("--model"))throw std::invalid_argument("Provider YAML requires the configurable profile runtime");
        if(options.contains("--model-wire")&&(!options.contains("--model")||(options.at("--model-wire")!="chat-completions"&&options.at("--model-wire")!="responses")))throw std::invalid_argument("Model wire requires a configured model and chat-completions or responses");
        if(options.contains("--inspection-workspace") && options.contains("--workspace")) throw std::invalid_argument("Select the execution workspace or an inspection-only workspace");
        if(!options.contains("--model") && options.contains("--credential-id")) throw std::invalid_argument("Startup credential references require a model configuration");
        if(options.contains("--model-tools") && options.at("--model-tools")!="supported" && options.at("--model-tools")!="unsupported" && options.at("--model-tools")!="unknown") throw std::invalid_argument("Invalid model tool capability declaration");
        if(options.contains("--workspace") && options.contains("--model") && (!options.contains("--model-tools") || options.at("--model-tools")!="supported")) throw std::invalid_argument("Workspace model execution requires --model-tools supported");
        if(options.contains("--workspace-edits") && (options.at("--workspace-edits")!="approved" || !options.contains("--workspace"))) throw std::invalid_argument("Approved edits require a workspace and --workspace-edits approved");
        if((options.contains("--models") || options.contains("--model-stream-usage")) && !options.contains("--model")) throw std::invalid_argument("Model settings require a configured model");
        if(options.contains("--model-stream-usage") && options.at("--model-stream-usage")!="supported" && options.at("--model-stream-usage")!="unsupported" && options.at("--model-stream-usage")!="unknown") throw std::invalid_argument("Invalid usage capability declaration");
        auto capacity=[&](const char* key,std::size_t fallback,std::size_t limit) {
            if(!options.contains(key)) return fallback;
            const auto& input=options.at(key);std::size_t result=0;
            const auto parsed=std::from_chars(input.data(),input.data()+input.size(),result);
            if(parsed.ec!=std::errc{} || parsed.ptr!=input.data()+input.size() || result==0 || result>limit) throw std::invalid_argument("Invalid executor capacity");return result;
        };
        const auto workers=capacity("--workers",2,16),queue=capacity("--queue-limit",128,4096);
        std::optional<agentflow::BackendOwnerBootstrap> bootstrap;
        std::optional<agentflow::LegacyOwnerBootstrap> legacy;
#if defined(_WIN32)
        std::unique_ptr<agentflow::VerifiedRuntimeGeneration> runtime_generation;
        std::unique_ptr<agentflow::WorkspaceTools> startup_workspace;
        std::string protected_mcp_document;
        if(options.contains("--profile-state")&&!options.contains("--runtime-manifest-sha256"))throw std::invalid_argument("Managed profile state requires a qualified native package");
        if(options.contains("--owner-receipt")&&options.contains("--legacy-owner-ticket"))throw std::invalid_argument("Native and legacy owner preconditions are distinct");
        if(options.contains("--profile-state")&&options.contains("--legacy-owner-ticket"))throw std::invalid_argument("Managed profile startup cannot use a legacy operator ticket");
        if((options.contains("--owner-receipt")||options.contains("--legacy-owner-ticket"))&&!options.contains("--runtime-manifest-sha256"))throw std::invalid_argument("Replacement requires a verified native package");
        if(options.contains("--runtime-manifest-sha256")){
            if(!options.contains("--workspace"))throw std::invalid_argument("Native owner controls require an execution workspace");
            startup_workspace=std::make_unique<agentflow::WorkspaceTools>(options.at("--workspace"));
            const auto runtime_root=loaded_runtime_root();
            if(options.contains("--profile-state")){
                const auto owner_receipt=options.find("--owner-receipt");
                agentflow::validate_local_profile_launch(options.at("--profile-state"),startup_workspace->root_path(),runtime_root,options.at("--runtime-manifest-sha256"),auth,owner_receipt==options.end()?std::string{}:owner_receipt->second,options.contains("--language-server-config")?options.at("--language-server-config"):std::string{},options.contains("--mcp-config")?options.at("--mcp-config"):std::string{},&protected_mcp_document,execution_mode);
                runtime_generation=std::make_unique<agentflow::VerifiedRuntimeGeneration>(agentflow::VerifiedRuntimeGeneration::managed_profile_copy(runtime_root,options.at("--runtime-manifest-sha256"),startup_workspace->root_path()));
                runtime_generation->require_loaded_server_image();
            }else{
                runtime_generation=std::make_unique<agentflow::VerifiedRuntimeGeneration>(runtime_root,options.at("--runtime-manifest-sha256"),startup_workspace->root_path());runtime_generation->require_current_server();
            }
            const auto root=std::filesystem::u8path(runtime_generation->binding().root);
            if(std::filesystem::canonical(std::filesystem::u8path(options.at("--modules")))!=std::filesystem::canonical(root/"modules")||std::filesystem::canonical(std::filesystem::u8path(options.at("--stdlib")))!=std::filesystem::canonical(root/"stdlib"))throw std::invalid_argument("Qualified startup requires the verified package's import roots");
            if(options.contains("--owner-receipt")||options.contains("--legacy-owner-ticket")){
                for(const auto* key:{"--model","--model-endpoint","--provider-config","--instructions-config","--graphs-config","--python-tools-config"})if(options.contains(key))throw std::invalid_argument("Replacement startup retains saved configuration; startup overrides are not allowed");
                if(options.contains("--mcp-config")&&(!options.contains("--owner-receipt")||!options.contains("--profile-state")))throw std::invalid_argument("Only a protected managed-profile upgrade may apply trusted MCP configuration");
                if(options.contains("--language-server-config")&&(!options.contains("--owner-receipt")||!options.contains("--profile-state")))throw std::invalid_argument("Only a protected managed-profile upgrade may apply trusted language-server configuration");
                if(options.contains("--process-config")&&!options.contains("--owner-receipt"))throw std::invalid_argument("Only a managed native owner handoff may apply a trusted process configuration");
                if(options.contains("--owner-receipt"))bootstrap=agentflow::qualify_backend_bootstrap(startup_receipt(options.at("--owner-receipt")),*runtime_generation,*startup_workspace,auth,options.contains("--workspace-edits"));
                else legacy=agentflow::LegacyOwnerBootstrap{options.at("--legacy-owner-ticket"),agentflow::qualify_backend_target(*runtime_generation,*startup_workspace,auth,options.contains("--workspace-edits"))};
            }
        }
#endif
        agentflow::PersistenceService persistence(options.at("--db"),{options.at("--modules"),options.at("--stdlib")},1024,bootstrap,legacy);
#if defined(_WIN32)
        std::shared_ptr<agentflow::WorkspaceRegistry> workspace_registry;
        if(options.contains("--workspace-registry")){
            if(!options.contains("--workspace"))throw std::invalid_argument("A workspace registry requires the native execution workspace");
            const auto registry_path=std::filesystem::absolute(std::filesystem::u8path(options.at("--workspace-registry"))).lexically_normal();
            if(registry_path.filename()!=L"workspaces.sqlite")throw std::invalid_argument("Invalid local workspace registry location");
            const auto registry_path_value=registry_path.u8string();
            const auto private_root=registry_path.parent_path().u8string();
            workspace_registry=std::make_shared<agentflow::WorkspaceRegistry>(
                std::string(reinterpret_cast<const char*>(registry_path_value.data()),registry_path_value.size()),
                std::vector<std::string>{options.at("--modules"),options.at("--stdlib")},
                std::string(reinterpret_cast<const char*>(private_root.data()),private_root.size()));
        }
        const auto bind_workspace_membership=[&](agentflow::AgentSettings& settings){
            if(!settings.workspace)return;
            const agentflow::WorkspaceTools primary(*settings.workspace);
            const auto workspace_id=primary.identity();const auto primary_root=primary.root_path();
            const auto registry=workspace_registry;
            settings.workspace_membership_snapshot=[registry,workspace_id,primary_root]{
                if(registry)try {
                    const auto group=registry->get(workspace_id);agentflow::WorkspaceMembershipSnapshot snapshot;
                    snapshot.workspace_id=group.workspace_id;snapshot.revision=group.revision;snapshot.folders.reserve(group.folders.size());
                    for(const auto& folder:group.folders)snapshot.folders.push_back({folder.folder_id,folder.root});
                    return snapshot;
                } catch(const std::out_of_range&) {}
                // A direct server and an unregistered local profile both own
                // one verified primary folder. Capture it before execution so
                // process children inherit the exact same immutable snapshot.
                const agentflow::WorkspaceTools verified(primary_root);
                if(verified.identity()!=workspace_id||verified.root_path()!=primary_root)throw agentflow::Conflict("Primary workspace identity changed before capture");
                return agentflow::WorkspaceMembershipSnapshot{workspace_id,1,{{workspace_id,primary_root}}};
            };
        };
        std::vector<agentflow::XlangPythonToolRegistration> xlang_python_tools;
        auto python_config=agentflow::load_xlang_python_tool_config(persistence);
        if(options.contains("--python-tools-config")){
            const auto config_path=std::filesystem::u8path(options.at("--python-tools-config"));
            std::ifstream file(config_path,std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted xlang3 tool configuration");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=262144)throw std::invalid_argument("Trusted xlang3 tool configuration exceeds limits");source.push_back(byte);}
            if(!file.eof())throw std::invalid_argument("Cannot read trusted xlang3 tool configuration");
            python_config=agentflow::import_xlang_python_tool_config(persistence,source);
        }
        if(!python_config.tools.empty()){
            const auto& config=python_config;
            const auto code_root=std::filesystem::canonical(std::filesystem::u8path(config.code_root));
            if(!std::filesystem::is_directory(code_root))throw std::invalid_argument("Trusted xlang3 code root is not a directory");
            const auto utf8=[](const std::filesystem::path& path){const auto value=path.u8string();return std::string(reinterpret_cast<const char*>(value.data()),value.size());};
            const auto code_root_utf8=utf8(code_root);
            for(const auto& tool:config.tools)xlang_python_tools.push_back({
                agentflow::ModelToolDefinition{tool.name,tool.description,tool.input_schema_json},
                agentflow::XlangPythonCallable(options.at("--modules"),options.at("--stdlib"),code_root_utf8,tool.module,tool.function),
                tool.execution=="worker"?agentflow::XlangPythonExecution::worker:agentflow::XlangPythonExecution::in_process});
        }
#else
        if(options.contains("--python-tools-config"))throw std::invalid_argument("Trusted xlang3 tool registrations require the native Windows agent runtime");
#endif
        agentflow::AgentInstructionStore instruction_configurations(persistence);agentflow::AgentInstructionPolicy instruction_policy;
        if(options.contains("--instructions-config")) {
            std::ifstream file(std::filesystem::u8path(options.at("--instructions-config")),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted instruction configuration file");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Instruction configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted instruction configuration file");instruction_policy=instruction_configurations.apply(source);
        }else instruction_policy=instruction_configurations.load();
#if defined(_WIN32)
        if(options.contains("--graphs-config")) {
            std::ifstream file(std::filesystem::u8path(options.at("--graphs-config")),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted graph catalog");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=262144)throw std::invalid_argument("Graph catalog exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted graph catalog");
            agentflow::GraphCatalogStore(persistence).apply(source);
        }
#endif
        std::unique_ptr<agentflow::RunExecutor> executor;
        agentflow::ProviderSetup* provider_setup=nullptr;
        agentflow::ProviderProfileSetup* provider_profiles=nullptr;
        agentflow::GraphExecution* graph_execution=nullptr;
        std::vector<agentflow::McpServerMetadata> mcp_metadata;
        std::vector<agentflow::ProcessProfileMetadata> process_metadata;
#if defined(_WIN32)
        std::unique_ptr<agentflow::ProviderProfileLegacySetup> legacy_provider_setup;
        agentflow::McpConfigurationStore mcp_configurations(persistence);
        std::vector<agentflow::McpServerSetting> mcp_settings;
        if(options.contains("--mcp-config")) {
            // Managed startup imports the exact bytes authenticated against its
            // protected fingerprint, without reopening a mutable path.
            std::string source=protected_mcp_document;
            if(!options.contains("--profile-state")){
                std::ifstream file(std::filesystem::u8path(options.at("--mcp-config")),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted MCP configuration file");
                char byte;while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("MCP configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted MCP configuration file");
            }
            mcp_settings=mcp_configurations.apply(source,options.contains("--owner-receipt"));
        }else mcp_settings=mcp_configurations.load();
        agentflow::ProcessConfigurationStore process_configurations(persistence);
        std::vector<agentflow::ProcessProfile> process_profiles;
        if(options.contains("--process-config")) {
            std::ifstream file(std::filesystem::u8path(options.at("--process-config")),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted process configuration file");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Process configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted process configuration file");process_profiles=process_configurations.apply(source,options.contains("--owner-receipt"));
        }else {
            process_profiles=process_configurations.load_or_initialize(default_windows_process_profiles);
        }
        // Keep the runtime-owned executable binding process-local so an
        // xlang3 runtime update cannot leave a stale persisted hash. Pair it
        // with a configured trusted shell even when a managed local view
        // supplies --process-config; an explicitly empty/custom-only list is
        // still an opt-out. Inherit the shell's approval and timeout policy.
        append_paired_xlang3_process_profile(process_profiles,options.at("--modules"));
        // Bind child xlang3 startup to the same trusted pure source used by
        // this backend. A developer machine's fallback library must not hide
        // an unbound runtime in an installed package or clean CI environment.
        const auto process_library=std::filesystem::absolute(std::filesystem::u8path(options.at("--stdlib"))).lexically_normal().u8string();
        for(auto& profile:process_profiles)if(profile.id=="xlang3")
            profile.xlang3_library=std::string(reinterpret_cast<const char*>(process_library.data()),process_library.size());
        agentflow::LanguageServerConfigurationStore language_server_configurations(persistence);
        std::vector<agentflow::LanguageServerProfile> language_servers;
        if(options.contains("--language-server-config")){
            std::ifstream file(std::filesystem::u8path(options.at("--language-server-config")),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted language-server configuration");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Language-server configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted language-server configuration");language_servers=language_server_configurations.apply(source,options.contains("--owner-receipt"));
        }else language_servers=language_server_configurations.load();
        for(const auto& item:process_profiles)process_metadata.push_back({item.id,item.revision,item.max_timeout.count()});
        for(const auto& item:mcp_settings)mcp_metadata.push_back({item.id,item.revision,item.enabled,item.transport});
        std::unique_ptr<agentflow::WorkspaceTools> recovery_workspace;
        std::unique_ptr<agentflow::EditRecoveryReader> recovery;
        agentflow::AgentRootExecutorFactory root_executor_factory;
        const auto image_path=(std::filesystem::u8path(loaded_runtime_root())/"xmind.exe").u8string();auto worker_image=agentflow::ForegroundProcess::bind_executable(std::string(reinterpret_cast<const char*>(image_path.data()),image_path.size()));
        if(process_worker){root_executor_factory=[image=worker_image,process_code](agentflow::PersistenceService& store,const agentflow::AgentSettings& settings){return process_code?agentflow::make_agent_worker_coding_executor(store,settings,image):agentflow::make_agent_worker_read_executor(store,settings,image);};}
        const auto configure_execution_mode=[&](agentflow::AgentSettings& settings){
            settings.delegated_execution=agentflow::make_agent_worker_delegated_executor(persistence,worker_image);
            if(!process_worker)return;
            settings.graph_execution=agentflow::make_agent_worker_graph_executor(persistence,worker_image);
            if(process_code){settings.process_profiles=process_profiles;settings.tool_allowlist.reset();return;}
            settings.a2a_enabled=false;if(!settings.workspace){settings.tool_allowlist=std::vector<std::string>{};return;}settings.tool_allowlist=std::vector<std::string>{"read_file","list_files","search_files","find_files","glob_files","plan_tasks","revise_plan","inspect_plan"};
            settings.tool_allowlist->push_back("delegate_tasks");
        };
#endif
        if(options.contains("--model")) {
#if defined(_WIN32)
            agentflow::AgentSettings settings;settings.provider.model=options.at("--model");settings.provider.endpoint=options.at("--model-endpoint");
            if(options.contains("--model-wire")&&options.at("--model-wire")=="responses")settings.provider.wire=agentflow::ProviderWire::responses;
            if(settings.provider.endpoint.size()>8192) throw std::invalid_argument("Provider endpoint exceeds its limit");
            if(options.contains("--workspace")) settings.workspace=options.at("--workspace");
            bind_workspace_membership(settings);
            settings.approved_edits=options.contains("--workspace-edits");
            if(!process_read)settings.mcp_servers=mcp_settings;
            // A server can start before a browser creates/selects its first
            // workspace. Keep the local shell catalog configured for that
            // later workspace, but do not bind execution tools to an agent
            // whose request has no verified workspace.
            if(!process_read&&settings.workspace)settings.process_profiles=process_profiles;
            if(!process_read)settings.language_servers=language_servers;
            settings.instruction_policy=instruction_policy;
            if(!process_read)settings.xlang_python_tools=xlang_python_tools;
            configure_execution_mode(settings);
            if(options.contains("--models")) {std::istringstream configured(options.at("--models"));std::string model;while(std::getline(configured,model,',')){if(model.empty())throw std::invalid_argument("Empty configured model");settings.selectable_models.push_back(model);}if(options.at("--models").empty() || options.at("--models").back()==',')throw std::invalid_argument("Empty configured model");}
            if(options.contains("--model-stream-usage")) settings.provider.stream_usage=options.at("--model-stream-usage")=="supported"?agentflow::Capability::supported:(options.at("--model-stream-usage")=="unsupported"?agentflow::Capability::unsupported:agentflow::Capability::unknown);
            if(options.contains("--model-tools")) settings.provider.tools=options.at("--model-tools")=="supported"?agentflow::Capability::supported:(options.at("--model-tools")=="unsupported"?agentflow::Capability::unsupported:agentflow::Capability::unknown);
            if(settings.workspace&&settings.provider.tools==agentflow::Capability::supported){settings.delegation=agentflow::AgentDelegationPolicy{};if(process_worker)settings.planning.emplace();}
            const auto purpose=provider_purpose(settings.provider.endpoint);
            const auto* key=std::getenv("XMIND_API_KEY");
            if(key && *key) {
                const auto id=options.contains("--credential-id")?options.at("--credential-id"):credential_id();
                agentflow::SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key),std::char_traits<char>::length(key)});
                _putenv_s("XMIND_API_KEY",""); // Clear this server process's inherited key after copying.
                std::int64_t revision=0;
                for(const auto& record:persistence.credentials("server").get()) if(record.id==id) revision=record.revision;
                persistence.put_credential("server",id,purpose,"Configured model provider",std::move(secret),revision).get();
                settings.credential=agentflow::CredentialReference{"server",id,purpose};
                std::cout<<"Provider credential reference: "<<id<<std::endl;
            } else if(options.contains("--credential-id")) {
                const auto id=options.at("--credential-id");
                auto verified=persistence.resolve_credential("server",id,purpose).get();
                settings.credential=agentflow::CredentialReference{"server",id,purpose};
            }
            auto platform=std::make_unique<agentflow::ExecutionPlatform>(persistence,std::move(settings),workers,queue,root_executor_factory);graph_execution=platform.get();executor=std::move(platform);
#else
            throw std::invalid_argument("Native provider execution currently requires Windows");
#endif
        }
#if defined(_WIN32)
        if(!executor){
            agentflow::AgentSettings settings;if(!process_read){settings.mcp_servers=mcp_settings;if(options.contains("--workspace"))settings.process_profiles=process_profiles;settings.language_servers=language_servers;settings.xlang_python_tools=xlang_python_tools;}settings.instruction_policy=instruction_policy;
            if(options.contains("--workspace"))settings.workspace=options.at("--workspace");settings.approved_edits=options.contains("--workspace-edits");
            bind_workspace_membership(settings);
            // The configured profile generation enables the registered leaf
            // policy only when its actual workspace/model tools are eligible.
            configure_execution_mode(settings);
            if(settings.workspace){settings.delegation=agentflow::AgentDelegationPolicy{};if(process_worker)settings.planning.emplace();}
            std::vector<agentflow::ProviderProfileExecutionPolicy> policies;
            std::vector<agentflow::ProviderProfileRoute> routes;
            const auto add=[&](std::string id,std::string provider,std::string endpoint,agentflow::ProviderWire wire,std::string catalogue,agentflow::ProviderCatalogueFormat format,agentflow::ChatDialect dialect=agentflow::ChatDialect::openai){
                agentflow::ProviderProfileRoute route{std::move(id),std::move(provider),endpoint,"server",provider_purpose(endpoint,"provider:setup:"),wire};
                agentflow::ChatProviderConfig configuration;configuration.endpoint=std::move(endpoint);configuration.wire=wire;configuration.chat_dialect=dialect;
                configuration.tools=wire==agentflow::ProviderWire::gemini_generate_content||dialect==agentflow::ChatDialect::deepseek?agentflow::Capability::unknown:agentflow::Capability::supported;configuration.stream_usage=agentflow::Capability::supported;
                if(wire==agentflow::ProviderWire::responses||wire==agentflow::ProviderWire::anthropic_messages||wire==agentflow::ProviderWire::gemini_generate_content||dialect==agentflow::ChatDialect::deepseek)configuration.output_limit=agentflow::Capability::supported;
                if(dialect==agentflow::ChatDialect::deepseek)configuration.reasoning=agentflow::Capability::supported;
                auto tools=wire==agentflow::ProviderWire::gemini_generate_content?agentflow::gemini_documented_tool_policy():std::map<std::string,agentflow::Capability>{};
                if(dialect==agentflow::ChatDialect::deepseek)tools=agentflow::deepseek_documented_tool_policy();
                routes.push_back(route);policies.push_back({std::move(route),std::move(configuration),agentflow::ProviderCataloguePolicy{std::move(catalogue),format},std::move(tools)});
            };
            add("openai.chat","openai","https://api.openai.com/v1/chat/completions",agentflow::ProviderWire::chat_completions,"https://api.openai.com/v1/models",agentflow::ProviderCatalogueFormat::openai);
            policies.back().model_capabilities=agentflow::documented_openai_model_policy(agentflow::ProviderWire::chat_completions);
            add("openai.responses","openai","https://api.openai.com/v1/responses",agentflow::ProviderWire::responses,"https://api.openai.com/v1/models",agentflow::ProviderCatalogueFormat::openai);
            policies.back().context=agentflow::documented_openai_context_policy();
            policies.back().model_capabilities=agentflow::documented_openai_model_policy(agentflow::ProviderWire::responses);
            add("anthropic.messages","anthropic","https://api.anthropic.com/v1/messages",agentflow::ProviderWire::anthropic_messages,"https://api.anthropic.com/v1/models",agentflow::ProviderCatalogueFormat::anthropic);
            add("gemini.generate-content","gemini","https://generativelanguage.googleapis.com/v1beta",agentflow::ProviderWire::gemini_generate_content,"https://generativelanguage.googleapis.com/v1beta/models",agentflow::ProviderCatalogueFormat::gemini);
            add("deepseek.chat","deepseek","https://api.deepseek.com/chat/completions",agentflow::ProviderWire::chat_completions,"https://api.deepseek.com/models",agentflow::ProviderCatalogueFormat::openai,agentflow::ChatDialect::deepseek);
            add("xai.responses","xai","https://api.x.ai/v1/responses",agentflow::ProviderWire::responses,"https://api.x.ai/v1/models",agentflow::ProviderCatalogueFormat::openai);
            policies.back().model_capabilities=agentflow::documented_xai_model_policy();
            add("mistral.chat","mistral","https://api.mistral.ai/v1/chat/completions",agentflow::ProviderWire::chat_completions,"https://api.mistral.ai/v1/models",agentflow::ProviderCatalogueFormat::openai,agentflow::ChatDialect::mistral);
            policies.back().model_capabilities=agentflow::documented_mistral_model_policy();
            add("openrouter.chat","openrouter","https://openrouter.ai/api/v1/chat/completions",agentflow::ProviderWire::chat_completions,"https://openrouter.ai/api/v1/models",agentflow::ProviderCatalogueFormat::openrouter);
            auto configurable=std::make_unique<agentflow::ProviderProfileRuntime>(persistence,std::move(settings),std::move(policies),workers,queue,root_executor_factory);
            if(!bootstrap&&!legacy)configurable->import_legacy_configuration();
            if(options.contains("--provider-config")){
                try{configurable->import_yaml_configuration(std::filesystem::absolute(options.at("--provider-config")),configurable->configuration().revision);}
                catch(...){throw std::runtime_error("Provider YAML configuration could not be imported");}
            }
            legacy_provider_setup=std::make_unique<agentflow::ProviderProfileLegacySetup>(*configurable,std::move(routes));
            provider_setup=legacy_provider_setup.get();provider_profiles=configurable.get();graph_execution=configurable.get();executor=std::move(configurable);
        }
        if(options.contains("--workspace") || options.contains("--inspection-workspace")) {
            recovery_workspace=std::make_unique<agentflow::WorkspaceTools>(options.at(options.contains("--workspace")?"--workspace":"--inspection-workspace"));
            recovery=std::make_unique<LocalWorkspaceEditRecovery>(persistence,recovery_workspace->root_path());
        }
#else
        if(options.contains("--inspection-workspace")) throw std::invalid_argument("Native file inspection currently requires Windows");
#endif
#if defined(_WIN32)
        std::unique_ptr<agentflow::BackendOwnerControl> owner_control;
        if(runtime_generation){const bool managed_profile=options.contains("--profile-state");if(managed_profile)runtime_generation->require_loaded_server_image();else runtime_generation->require_current_server();const auto actual=executor->execution_workspace();if(actual.root!=startup_workspace->root_path()||actual.workspace_id!=startup_workspace->identity())throw std::runtime_error("Qualified execution workspace changed during startup");owner_control=std::make_unique<agentflow::BackendOwnerControl>(persistence,*executor,*runtime_generation,graph_execution,auth,managed_profile);}
        auto mcp_oauth=std::make_unique<agentflow::McpOAuthService>(persistence,mcp_settings);
        auto mcp_resources=std::make_unique<agentflow::NativeMcpResourceCatalog>(persistence,mcp_settings);
        std::unique_ptr<agentflow::PtySessions> terminals;
        if(executor&&options.contains("--workspace"))terminals=std::make_unique<agentflow::PtySessions>(executor->execution_workspace().root,system_command_shell());
#endif
#if defined(_WIN32)
        agentflow::XMindIpcServer ipc_api(persistence,executor.get(),auth,owner_control.get());
        std::cout<<"xMind API listening on "<<ipc_api.endpoint()<<std::endl;
#endif
        agentflow::HttpServer server(persistence,auth,executor.get()
#if defined(_WIN32)
            ,recovery.get(),std::move(mcp_metadata),std::move(process_metadata)
#else
            ,nullptr,{},{}
#endif
            ,agentflow::AgentInstructionMetadata{instruction_policy.revision,instruction_policy.instructions.size()},provider_setup,graph_execution,provider_profiles
#if defined(_WIN32)
            ,owner_control.get(),mcp_oauth.get(),terminals.get(),mcp_resources.get(),&ipc_api.connection_bindings()
#endif
        );const auto bound=server.bind(port);
#if defined(_WIN32)
        if(options.contains("--profile-state")){if(!runtime_generation||!executor)throw std::invalid_argument("Managed profile publication requires a qualified workspace backend");const auto metadata=executor->execution_workspace();agentflow::publish_local_profile_ready(options.at("--profile-state"),bound,auth,metadata.root,metadata.workspace_id,metadata.authority_id);}
        {std::lock_guard lock(control_mutex);active_server=&server;}
        SetConsoleCtrlHandler(control,TRUE);
#endif
        std::cout<<"xMind Server listening on http://127.0.0.1:"<<bound<<std::endl;
        const auto ok=server.listen();
#if defined(_WIN32)
        {std::lock_guard lock(control_mutex);active_server=nullptr;}
        SetConsoleCtrlHandler(control,FALSE);
#endif
        return ok?0:1;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
#if !defined(XMIND_UNIFIED_EXECUTABLE)
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv){std::vector<std::string> values;std::vector<char*> pointers;for(int i=0;i<argc;++i){const std::wstring value=argv[i];if(value.size()>32768)return 2;const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(size<=0)return 2;std::string utf8(size,'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),utf8.data(),size,nullptr,nullptr)!=size)return 2;values.push_back(std::move(utf8));}for(auto& value:values)pointers.push_back(value.data());return run_server(argc,pointers.data());}
#else
int main(int argc,char** argv){return run_server(argc,argv);}
#endif
#endif
