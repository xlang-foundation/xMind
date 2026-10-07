#include "agentflow/http_server.hpp"
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
#if defined(_WIN32)
#include "agentflow/agent_service.hpp"
#include "agentflow/provider_setup.hpp"
#include "agentflow/execution_platform.hpp"
#include "agentflow/edit_executor.hpp"
#include "agentflow/process_configuration.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
namespace {
std::mutex control_mutex;
agentflow::HttpServer* active_server=nullptr;
std::string provider_purpose(const std::string& endpoint) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("Cannot bind provider credential context");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(endpoint.data())),static_cast<ULONG>(endpoint.size()),hash.data(),static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0) throw std::runtime_error("Cannot bind provider credential context");
    std::ostringstream result;result<<"provider:chat:"<<std::hex<<std::setfill('0');
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
int main(int argc,char** argv) {
    try {
        std::map<std::string,std::string> options;
        for(int i=1;i<argc;i+=2) {
            const std::string key=argv[i];
            if(i+1>=argc || (key!="--db" && key!="--modules" && key!="--stdlib" && key!="--port" && key!="--model" && key!="--model-endpoint" && key!="--model-tools" && key!="--models" && key!="--model-stream-usage" && key!="--workspace" && key!="--inspection-workspace" && key!="--workspace-edits" && key!="--credential-id" && key!="--workers" && key!="--queue-limit" && key!="--mcp-config" && key!="--process-config" && key!="--instructions-config" && key!="--graphs-config") || !options.emplace(key,argv[i+1]).second)
                throw std::invalid_argument("Usage: xmind_server --db FILE --modules DIR --stdlib DIR [--port PORT] [--model ID --model-endpoint URL] [--model-tools supported|unsupported|unknown] [--model-stream-usage supported|unsupported|unknown] [--models ID1,ID2] [--workspace DIR | --inspection-workspace DIR] [--workspace-edits approved] [--credential-id ID] [--workers 1..16] [--queue-limit 1..4096] [--instructions-config FILE] [--graphs-config FILE]");
        }
        for(const auto* key:{"--db","--modules","--stdlib"}) if(!options.contains(key)) throw std::invalid_argument("Missing server configuration");
        int port=8765;
        if(options.contains("--port")) {
            const auto& value=options.at("--port");const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid port");
        }
        const auto* token=std::getenv("XMIND_AUTH_TOKEN");
        if(!token) throw std::invalid_argument("Set XMIND_AUTH_TOKEN for the local server");
        agentflow::validate_local_auth_token(token);
#if !defined(_WIN32)
        if(options.contains("--mcp-config"))throw std::invalid_argument("Native MCP configuration currently requires Windows");
        if(options.contains("--process-config"))throw std::invalid_argument("Native process configuration currently requires Windows");
        if(options.contains("--graphs-config"))throw std::invalid_argument("Native graph execution currently requires Windows");
#endif
        const std::string auth=token;
        if(port<0 || port>65535) throw std::invalid_argument("Invalid port");
        if(options.contains("--model")!=options.contains("--model-endpoint")) throw std::invalid_argument("Model ID and endpoint must be configured together");
        if(options.contains("--inspection-workspace") && options.contains("--workspace")) throw std::invalid_argument("Select the execution workspace or an inspection-only workspace");
        if(!options.contains("--model") && options.contains("--credential-id")) throw std::invalid_argument("Startup credential references require a model configuration");
        if(options.contains("--model-tools") && options.at("--model-tools")!="supported" && options.at("--model-tools")!="unsupported" && options.at("--model-tools")!="unknown") throw std::invalid_argument("Invalid model tool capability declaration");
        if(options.contains("--workspace") && (!options.contains("--model-tools") || options.at("--model-tools")!="supported")) throw std::invalid_argument("Workspace execution requires --model-tools supported");
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
        agentflow::PersistenceService persistence(options.at("--db"),{options.at("--modules"),options.at("--stdlib")});
        agentflow::AgentInstructionStore instruction_configurations(persistence);agentflow::AgentInstructionPolicy instruction_policy;
        if(options.contains("--instructions-config")) {
            std::ifstream file(options.at("--instructions-config"),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted instruction configuration file");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Instruction configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted instruction configuration file");instruction_policy=instruction_configurations.apply(source);
        }else instruction_policy=instruction_configurations.load();
#if defined(_WIN32)
        if(options.contains("--graphs-config")) {
            std::ifstream file(options.at("--graphs-config"),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted graph catalog");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=262144)throw std::invalid_argument("Graph catalog exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted graph catalog");
            agentflow::GraphCatalogStore(persistence).apply(source);
        }
#endif
        std::unique_ptr<agentflow::RunExecutor> executor;
        agentflow::ProviderSetup* provider_setup=nullptr;
        agentflow::GraphExecution* graph_execution=nullptr;
        std::vector<agentflow::McpServerMetadata> mcp_metadata;
        std::vector<agentflow::ProcessProfileMetadata> process_metadata;
#if defined(_WIN32)
        agentflow::McpConfigurationStore mcp_configurations(persistence);
        std::vector<agentflow::McpServerSetting> mcp_settings;
        if(options.contains("--mcp-config")) {
            std::ifstream file(options.at("--mcp-config"),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted MCP configuration file");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("MCP configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted MCP configuration file");
            mcp_settings=mcp_configurations.apply(source);
        }else mcp_settings=mcp_configurations.load();
        agentflow::ProcessConfigurationStore process_configurations(persistence);
        std::vector<agentflow::ProcessProfile> process_profiles;
        if(options.contains("--process-config")) {
            std::ifstream file(options.at("--process-config"),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted process configuration file");
            std::string source;char byte;while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Process configuration file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted process configuration file");process_profiles=process_configurations.apply(source);
        }else process_profiles=process_configurations.load();
        for(const auto& item:process_profiles)process_metadata.push_back({item.id,item.revision,item.max_timeout.count()});
        for(const auto& item:mcp_settings)mcp_metadata.push_back({item.id,item.revision,item.enabled});
        std::unique_ptr<agentflow::WorkspaceTools> recovery_workspace;
        std::unique_ptr<agentflow::EditExecutor> recovery;
#endif
        if(options.contains("--model")) {
#if defined(_WIN32)
            agentflow::AgentSettings settings;settings.provider.model=options.at("--model");settings.provider.endpoint=options.at("--model-endpoint");
            if(settings.provider.endpoint.size()>8192) throw std::invalid_argument("Provider endpoint exceeds its limit");
            if(options.contains("--workspace")) settings.workspace=options.at("--workspace");
            settings.approved_edits=options.contains("--workspace-edits");
            settings.mcp_servers=mcp_settings;
            settings.process_profiles=process_profiles;
            settings.instruction_policy=instruction_policy;
            if(options.contains("--models")) {std::istringstream configured(options.at("--models"));std::string model;while(std::getline(configured,model,',')){if(model.empty())throw std::invalid_argument("Empty configured model");settings.selectable_models.push_back(model);}if(options.at("--models").empty() || options.at("--models").back()==',')throw std::invalid_argument("Empty configured model");}
            if(options.contains("--model-stream-usage")) settings.provider.stream_usage=options.at("--model-stream-usage")=="supported"?agentflow::Capability::supported:(options.at("--model-stream-usage")=="unsupported"?agentflow::Capability::unsupported:agentflow::Capability::unknown);
            if(options.contains("--model-tools")) settings.provider.tools=options.at("--model-tools")=="supported"?agentflow::Capability::supported:(options.at("--model-tools")=="unsupported"?agentflow::Capability::unsupported:agentflow::Capability::unknown);
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
            auto platform=std::make_unique<agentflow::ExecutionPlatform>(persistence,std::move(settings),workers,queue);graph_execution=platform.get();executor=std::move(platform);
#else
            throw std::invalid_argument("Native provider execution currently requires Windows");
#endif
        }
#if defined(_WIN32)
        if(!executor){
            agentflow::AgentSettings settings;settings.mcp_servers=mcp_settings;settings.process_profiles=process_profiles;settings.instruction_policy=instruction_policy;
            if(options.contains("--workspace"))settings.workspace=options.at("--workspace");settings.approved_edits=options.contains("--workspace-edits");
            auto configurable=std::make_unique<agentflow::ProviderRuntime>(persistence,std::move(settings),workers,queue);provider_setup=configurable.get();graph_execution=configurable.get();executor=std::move(configurable);
        }
        if(options.contains("--workspace") || options.contains("--inspection-workspace")) {
            recovery_workspace=std::make_unique<agentflow::WorkspaceTools>(options.at(options.contains("--workspace")?"--workspace":"--inspection-workspace"));
            recovery=std::make_unique<agentflow::EditExecutor>(persistence,*recovery_workspace);
        }
#else
        if(options.contains("--inspection-workspace")) throw std::invalid_argument("Native file inspection currently requires Windows");
#endif
        agentflow::HttpServer server(persistence,auth,executor.get()
#if defined(_WIN32)
            ,recovery.get(),std::move(mcp_metadata),std::move(process_metadata)
#else
            ,nullptr,{},{}
#endif
            ,agentflow::AgentInstructionMetadata{instruction_policy.revision,instruction_policy.instructions.size()},provider_setup,graph_execution
        );const auto bound=server.bind(port);
#if defined(_WIN32)
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
