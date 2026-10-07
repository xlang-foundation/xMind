#include "agentflow/mcp_configuration.hpp"
#include "agentflow/process_configuration.hpp"
#include "agentflow/agent_instructions.hpp"
#include "agentflow/graph.hpp"
#include "nlohmann/json.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
int main(int argc,char** argv){
    try {
        std::map<std::string,std::string> options;int command=1;
        while(command<argc && std::string(argv[command]).starts_with("--")){
            const std::string key=argv[command];if(command+1>=argc || (key!="--db" && key!="--modules" && key!="--stdlib") || !options.emplace(key,argv[command+1]).second)throw std::invalid_argument("Invalid native admin options");command+=2;
        }
        for(const auto* key:{"--db","--modules","--stdlib"})if(!options.contains(key))throw std::invalid_argument("Native admin requires --db FILE --modules DIR --stdlib DIR");
        if(command>=argc)throw std::invalid_argument("Commands: import-graphs FILE; import-instructions FILE; import-processes FILE; import-mcp FILE; put-mcp-credential SERVER_ID ENV_NAME SECRET_SOURCE_ENV. Run while the backend is stopped.");
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
