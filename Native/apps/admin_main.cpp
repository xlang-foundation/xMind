#include "agentflow/mcp_configuration.hpp"
#include "agentflow/process_configuration.hpp"
#include "agentflow/agent_instructions.hpp"
#include "agentflow/graph.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/owner_process.hpp"
#include "agentflow/legacy_owner.hpp"
#include "agentflow/legacy_owner_process.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <charconv>
#include <random>
#include <iomanip>
#include <sstream>
#include <vector>
namespace {
bool reflected(const nlohmann::json& value,const std::string& secret){
    const auto encoded=nlohmann::json(secret).dump();
    const auto contains=[&](const std::string& text){return text.find(secret)!=std::string::npos || text.find(encoded.substr(1,encoded.size()-2))!=std::string::npos;};
    if(value.is_string())return contains(value.get_ref<const std::string&>());
    if(value.is_object()){for(auto item=value.begin();item!=value.end();++item)if(contains(item.key()) || reflected(item.value(),secret))return true;}
    else if(value.is_array())for(const auto& item:value)if(reflected(item,secret))return true;
    return false;
}
}
int run_admin(int argc,char** argv){
    try {
        if(argc==3&&std::string(argv[1])=="inspect-legacy-listener"){
            const std::string input=argv[2];std::uint32_t port=0;const auto parsed=std::from_chars(input.data(),input.data()+input.size(),port);if(parsed.ec!=std::errc{}||parsed.ptr!=input.data()+input.size()||port==0||port>65535)throw std::invalid_argument("Invalid legacy listener port");const auto result=agentflow::discover_legacy_listener(static_cast<std::uint16_t>(port));std::cout<<nlohmann::json{{"process_id",result.source.process_id},{"process_birth",result.source.process_birth},{"image_path",result.image_path},{"server_sha256",result.source.server_sha256},{"authenticated",false},{"process_signalled",false}}.dump()<<'\n';return 0;
        }
        if(argc==9&&std::string(argv[1])=="inspect-legacy-owner"){
            const std::string input=argv[2];std::uint32_t port=0;const auto parsed=std::from_chars(input.data(),input.data()+input.size(),port);if(parsed.ec!=std::errc{}||parsed.ptr!=input.data()+input.size()||port==0||port>65535)throw std::invalid_argument("Invalid legacy listener port");
            const auto* raw=std::getenv("XMIND_AUTH_TOKEN");if(!raw)throw std::invalid_argument("Existing legacy owner authentication is required");const std::string_view value(raw);agentflow::SecretBytes token(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
            agentflow::VerifiedLegacyOwnerProcess owner(static_cast<std::uint16_t>(port),argv[3],argv[4],argv[5],argv[6],argv[7],argv[8],token);owner.revalidate();const auto& observed=owner.observation();
            std::cout<<nlohmann::json{{"process_id",observed.source.process_id},{"process_birth",observed.source.process_birth},{"server_sha256",observed.source.server_sha256},{"image_path",observed.image_path},{"database_path",observed.database_path},{"authenticated",true},{"database_command_line_verified",true},{"process_signalled",false},{"migration_ticket_created",false}}.dump()<<'\n';return 0;
        }
        if(argc==3&&std::string(argv[1])=="inspect-owner-process"){
            const std::string input=argv[2];std::uint32_t pid=0;const auto parsed=std::from_chars(input.data(),input.data()+input.size(),pid);if(parsed.ec!=std::errc{}||parsed.ptr!=input.data()+input.size())throw std::invalid_argument("Invalid native process ID");std::cout<<nlohmann::json{{"process_id",pid},{"process_birth",agentflow::inspect_owner_process_birth(pid)}}.dump()<<'\n';return 0;
        }
        if(argc==5&&std::string(argv[1])=="observe-owner-exit"){
            const auto number=[](const char* value){std::uint32_t out=0;const auto end=value+std::char_traits<char>::length(value);const auto parsed=std::from_chars(value,end,out);if(parsed.ec!=std::errc{}||parsed.ptr!=end)throw std::invalid_argument("Invalid owner process argument");return out;};
            const auto pid=number(argv[2]);const auto observed=agentflow::observe_owner_exit(pid,argv[3],number(argv[4]));
            std::cout<<nlohmann::json{{"exited",observed.exited},{"identity_matches",observed.identity_matches},{"process_id",pid},{"process_birth",argv[3]}}.dump()<<'\n';return 0;
        }
        std::map<std::string,std::string> options;int command=1;
        while(command<argc && std::string(argv[command]).starts_with("--")){
            const std::string key=argv[command];if(command+1>=argc || (key!="--db" && key!="--modules" && key!="--stdlib") || !options.emplace(key,argv[command+1]).second)throw std::invalid_argument("Invalid native admin options");command+=2;
        }
        for(const auto* key:{"--db","--modules","--stdlib"})if(!options.contains(key))throw std::invalid_argument("Native admin requires --db FILE --modules DIR --stdlib DIR");
        if(command<argc&&std::string(argv[command])=="stop-and-prepare-legacy-owner"){
            if(command+14!=argc||std::string(argv[command+13])!="confirmed-stop")throw std::invalid_argument("Explicit confirmed-stop is required for legacy migration");
            const auto number=[](const char* value){std::uint32_t out=0;const auto end=value+std::char_traits<char>::length(value);const auto parsed=std::from_chars(value,end,out);if(parsed.ec!=std::errc{}||parsed.ptr!=end)throw std::invalid_argument("Invalid legacy operator argument");return out;};
            const auto port=number(argv[command+4]),pid=number(argv[command+9]);if(port==0||port>65535||pid==0)throw std::invalid_argument("Invalid legacy owner port or process ID");const std::string edits=argv[command+12];if(edits!="approved"&&edits!="read-only")throw std::invalid_argument("Invalid legacy replacement policy");
            const auto* auth=std::getenv("XMIND_AUTH_TOKEN");if(!auth)throw std::invalid_argument("Existing legacy owner authentication is required");const std::string_view value(auth);agentflow::SecretBytes token(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
            agentflow::VerifiedRuntimeGeneration runtime(argv[command+1],argv[command+2],argv[command+3]);agentflow::WorkspaceTools workspace(argv[command+3]);
            const auto qualified=agentflow::qualify_backend_target(runtime,workspace,auth,edits=="approved",false);
            if(options.at("--modules")!=qualified.runtime.root+"/modules"&&options.at("--modules")!=qualified.runtime.root+"\\modules")throw std::invalid_argument("Legacy migration requires the target package modules");
            if(options.at("--stdlib")!=qualified.runtime.root+"/stdlib"&&options.at("--stdlib")!=qualified.runtime.root+"\\stdlib")throw std::invalid_argument("Legacy migration requires the target package library source");
            agentflow::VerifiedLegacyOwnerProcess owner(static_cast<std::uint16_t>(port),argv[command+5],argv[command+6],options.at("--db"),argv[command+3],argv[command+7],argv[command+8],token);
            if(owner.observation().source.process_id!=pid||owner.observation().source.process_birth!=argv[command+10])throw std::runtime_error("Legacy process changed after its preflight");
            const agentflow::LegacyOwnerBootstrap boot{argv[command+11],qualified};owner.stop_and_prepare(boot,{options.at("--modules"),options.at("--stdlib")});
            std::cout<<nlohmann::json{{"legacy_ticket_id",boot.ticket_id},{"admission_closed",true},{"quiescence_receipt",false},{"schema_migrated",false},{"source_terminated",true},{"process_id",pid},{"process_birth",owner.observation().source.process_birth}}.dump()<<'\n';return 0;
        }
        if(command<argc&&std::string(argv[command])=="inspect-legacy-ticket"){
            if(command+10!=argc)throw std::invalid_argument("Invalid legacy ticket inspection arguments");
            const auto* auth=std::getenv("XMIND_AUTH_TOKEN");if(!auth)throw std::invalid_argument("Existing legacy owner authentication is required");const std::string edits=argv[command+9];if(edits!="approved"&&edits!="read-only")throw std::invalid_argument("Invalid legacy replacement policy");
            const std::string number=argv[command+4];std::uint32_t pid=0;const auto parsed=std::from_chars(number.data(),number.data()+number.size(),pid);if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||!agentflow::observe_owner_exit(pid,argv[command+5],0).exited)throw std::runtime_error("Legacy source exit is unverified");
            agentflow::VerifiedRuntimeGeneration runtime(argv[command+1],argv[command+2],argv[command+3]);agentflow::WorkspaceTools workspace(argv[command+3]);const auto qualified=agentflow::qualify_backend_target(runtime,workspace,auth,edits=="approved",false);
            agentflow::BackendLease lease(options.at("--db"));agentflow::XlangSqlite database(lease.canonical_database_path(),{options.at("--modules"),options.at("--stdlib")});const agentflow::LegacyOwnerBootstrap boot{argv[command+7],qualified};
            if(std::string(argv[command+8])!="read-only-inspection")throw std::invalid_argument("Invalid legacy ticket inspection mode");database.begin();try{agentflow::require_legacy_owner_bootstrap(database,lease,boot);const auto record=nlohmann::json::parse(*agentflow::legacy_owner_record(database));if(record.at("source")!=nlohmann::json{{"process_id",pid},{"process_birth",argv[command+5]},{"server_sha256",argv[command+6]}})throw std::runtime_error("Legacy ticket source changed");database.commit();}catch(...){database.rollback();throw;}
            std::cout<<nlohmann::json{{"legacy_ticket_id",boot.ticket_id},{"admission_closed",true},{"quiescence_receipt",false},{"process_signalled",false}}.dump()<<'\n';return 0;
        }
        if(command<argc&&std::string(argv[command])=="prepare-legacy-owner"){
            if(command+8!=argc)throw std::invalid_argument("prepare-legacy-owner requires TARGET_ROOT MANIFEST_SHA256 WORKSPACE PID PROCESS_BIRTH SOURCE_SERVER_SHA256 approved|read-only. The legacy owner must already be stopped by its operator.");
            std::uint32_t pid=0;const std::string number=argv[command+4];const auto parsed=std::from_chars(number.data(),number.data()+number.size(),pid);if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size())throw std::invalid_argument("Invalid legacy process ID");
            const std::string edits=argv[command+7];if(edits!="approved"&&edits!="read-only")throw std::invalid_argument("Invalid legacy replacement policy");
            const auto* auth=std::getenv("XMIND_AUTH_TOKEN");if(!auth)throw std::invalid_argument("Legacy preparation requires the existing native owner token");
            agentflow::WorkspaceTools workspace(argv[command+3]);agentflow::VerifiedRuntimeGeneration target(argv[command+1],argv[command+2],workspace.root_path());
            const auto qualified=agentflow::qualify_backend_target(target,workspace,auth,edits=="approved",false);
            if(!agentflow::observe_owner_exit(pid,argv[command+5],0).exited)throw std::runtime_error("Legacy process is still running; no migration ticket was published");
            agentflow::BackendLease lease(options.at("--db"));agentflow::XlangSqlite database(lease.canonical_database_path(),{options.at("--modules"),options.at("--stdlib")});
            std::random_device random;std::ostringstream identity;identity<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)identity<<std::setw(8)<<random();
            const agentflow::LegacyOwnerBootstrap boot{identity.str(),qualified};database.begin();try{const auto snapshot=agentflow::snapshot_legacy_database(database);agentflow::publish_legacy_owner_ticket(database,lease,boot,{pid,argv[command+5],argv[command+6]},snapshot);database.commit();}catch(...){database.rollback();throw;}
            std::cout<<nlohmann::json{{"legacy_ticket_id",boot.ticket_id},{"admission_closed",true},{"quiescence_receipt",false},{"schema_migrated",false}}.dump()<<'\n';return 0;
        }
        if(command>=argc)throw std::invalid_argument("Commands: import-graphs FILE; import-instructions FILE; import-processes FILE; import-mcp FILE; discover-mcp SERVER_ID WORKSPACE; put-mcp-credential SERVER_ID ENV_NAME SECRET_SOURCE_ENV. Run while the backend is stopped.");
        agentflow::PersistenceService store(options.at("--db"),{options.at("--modules"),options.at("--stdlib")});agentflow::McpConfigurationStore configurations(store);
        using Json=nlohmann::json;const std::string action=argv[command];
        if(action=="import-graphs" && command+2==argc){
            std::ifstream file(argv[command+1],std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted graph catalog");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Graph catalog exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted graph catalog");
            const auto catalog=agentflow::GraphCatalogStore(store).apply(source);auto graphs=Json::array();for(const auto& graph:catalog.entries)graphs.push_back({{"id",graph.id},{"revision",graph.revision},{"node_count",graph.plan.nodes().size()}});
            std::cout<<Json{{"catalog_revision",catalog.revision},{"graphs",std::move(graphs)},{"execution_available",false}}.dump()<<'\n';
        }else if(action=="import-instructions" && command+2==argc){
            std::ifstream file(argv[command+1],std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted instruction configuration");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Instruction configuration exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted instruction configuration");
            const auto value=agentflow::AgentInstructionStore(store).apply(source);std::cout<<Json{{"revision",value.revision},{"byte_count",value.instructions.size()}}.dump()<<'\n';
        }else if(action=="import-processes" && command+2==argc){
            std::ifstream file(argv[command+1],std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted process configuration");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("Process configuration exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted process configuration");
            const auto values=agentflow::ProcessConfigurationStore(store).apply(source);Json metadata=Json::array();for(const auto& value:values)metadata.push_back({{"id",value.id},{"revision",value.revision}});std::cout<<Json{{"profiles",metadata}}.dump()<<'\n';
        }else if(action=="import-mcp" && command+2==argc){
            std::ifstream file(argv[command+1],std::ios::binary);if(!file)throw std::invalid_argument("Cannot read trusted MCP configuration");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=256*1024)throw std::invalid_argument("MCP configuration exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read trusted MCP configuration");
            const auto values=configurations.apply(source);Json metadata=Json::array();for(const auto& value:values)metadata.push_back({{"id",value.id},{"revision",value.revision},{"enabled",value.enabled}});std::cout<<Json{{"servers",metadata}}.dump()<<'\n';
        }else if(action=="discover-mcp" && command+3==argc){
            const auto settings=configurations.load();const agentflow::McpServerSetting* server=nullptr;
            for(const auto& value:settings)if(value.id==argv[command+1])server=&value;
            if(!server || !server->enabled)throw std::invalid_argument("MCP discovery requires an enabled registered server");
            try {
                agentflow::WorkspaceTools workspace(argv[command+2]);
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
                agentflow::McpStdioConfiguration config{server->executable,server->working_directory,server->arguments,{}};
                struct ClearEnvironment {agentflow::McpStdioConfiguration& config;~ClearEnvironment(){for(auto& entry:config.environment)if(!entry.second.empty())SecureZeroMemory(entry.second.data(),entry.second.size());}} clear{config};
                for(const auto& reference:server->credentials){
                    auto secret=store.resolve_credential(reference.scope,reference.id,agentflow::mcp_credential_purpose(*server,reference.name)).get();
                    const auto bytes=secret.view();config.environment.emplace_back(reference.name,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
                }
                if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("MCP discovery deadline exceeded");
                agentflow::McpStdioClient client(config);client.connect(deadline);
                agentflow::McpToolRegistry registry(client,store,workspace,server->id,server->revision,deadline);
                auto tools=Json::array();for(const auto& tool:registry.definitions()){
                    // Peer descriptions/schemas remain untrusted public
                    // metadata. Never print an injected credential reflected
                    // in a string, object key or JSON-escaped peer name.
                    const auto description=Json(tool.description),schema=Json::parse(tool.input_schema_json);
                    for(const auto& entry:config.environment)if(reflected(description,entry.second) || reflected(schema,entry.second) || reflected(Json(tool.input_schema_json),entry.second))throw std::runtime_error("MCP public catalogue reflected a private credential");
                    tools.push_back({{"alias",tool.name},{"description",tool.description},{"input_schema_json",tool.input_schema_json}});
                }
                std::cout<<Json{{"server_id",server->id},{"config_revision",server->revision},{"protocol_version",client.server().protocol_version},{"tools",std::move(tools)},{"tool_dispatch_performed",false}}.dump()<<'\n';
            }catch(...){throw std::runtime_error("MCP tool discovery did not complete");}
        }else if(action=="put-mcp-credential" && command+4==argc){
            const std::string id=argv[command+1],name=argv[command+2],source=argv[command+3];
            const auto settings=configurations.load();const agentflow::McpServerSetting* server=nullptr;for(const auto& value:settings)if(value.id==id)server=&value;
            if(!server)throw std::invalid_argument("MCP server is not registered");const agentflow::McpEnvironmentCredential* reference=nullptr;for(const auto& value:server->credentials)if(value.name==name)reference=&value;
            if(!reference)throw std::invalid_argument("MCP credential environment is not registered; use its normalized uppercase name");
            const auto* input=std::getenv(source.c_str());if(!input || !*input)throw std::invalid_argument("Private secret source environment is empty");
            const auto count=std::char_traits<char>::length(input);if(count>32768)throw std::invalid_argument("MCP credential exceeds environment limits");
            agentflow::SecretBytes secret({reinterpret_cast<const std::uint8_t*>(input),count});
#if defined(_WIN32)
            _putenv_s(source.c_str(),"");
#endif
            std::int64_t revision=0;for(const auto& value:store.credentials(reference->scope).get())if(value.id==reference->id)revision=value.revision;
            const auto metadata=store.put_credential(reference->scope,reference->id,agentflow::mcp_credential_purpose(*server,reference->name),"MCP "+id+" "+name,std::move(secret),revision).get();
            std::cout<<Json{{"scope",metadata.scope},{"id",metadata.id},{"revision",metadata.revision}}.dump()<<'\n';
        }else throw std::invalid_argument("Unknown native admin command or incorrect arguments");
        store.close();return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
int wmain(int argc,wchar_t** argv){
    std::vector<std::string> values;std::vector<char*> pointers;
    for(int i=0;i<argc;++i){const std::wstring value=argv[i];if(value.size()>32768)return 2;if(value.empty()){values.emplace_back();continue;}const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(size<=0)return 2;std::string utf8(size,'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),utf8.data(),size,nullptr,nullptr)!=size)return 2;values.push_back(std::move(utf8));}
    for(auto& value:values)pointers.push_back(value.data());return run_admin(argc,pointers.data());
}
