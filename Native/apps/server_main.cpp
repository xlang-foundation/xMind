#include "agentflow/http_server.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#if defined(_WIN32)
#include "agentflow/agent_service.hpp"
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
            if(i+1>=argc || (key!="--db" && key!="--modules" && key!="--stdlib" && key!="--port" && key!="--model" && key!="--model-endpoint" && key!="--model-tools" && key!="--models" && key!="--model-stream-usage" && key!="--workspace" && key!="--workspace-edits" && key!="--credential-id" && key!="--workers" && key!="--queue-limit") || !options.emplace(key,argv[i+1]).second)
                throw std::invalid_argument("Usage: xmind_server --db FILE --modules DIR --stdlib DIR [--port PORT] [--model ID --model-endpoint URL] [--model-tools supported|unsupported|unknown] [--model-stream-usage supported|unsupported|unknown] [--models ID1,ID2] [--workspace DIR] [--workspace-edits approved] [--credential-id ID] [--workers 1..16] [--queue-limit 1..4096]");
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
        const std::string auth=token;
        if(port<0 || port>65535) throw std::invalid_argument("Invalid port");
        if(options.contains("--model")!=options.contains("--model-endpoint")) throw std::invalid_argument("Model ID and endpoint must be configured together");
        if(!options.contains("--model") && (options.contains("--workspace") || options.contains("--credential-id") || options.contains("--workers") || options.contains("--queue-limit") || options.contains("--model-tools"))) throw std::invalid_argument("Agent settings require a model configuration");
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
        std::unique_ptr<agentflow::RunExecutor> executor;
        if(options.contains("--model")) {
#if defined(_WIN32)
            agentflow::AgentSettings settings;settings.provider.model=options.at("--model");settings.provider.endpoint=options.at("--model-endpoint");
            if(settings.provider.endpoint.size()>8192) throw std::invalid_argument("Provider endpoint exceeds its limit");
            if(options.contains("--workspace")) settings.workspace=options.at("--workspace");
            settings.approved_edits=options.contains("--workspace-edits");
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
            executor=std::make_unique<agentflow::AgentService>(persistence,std::move(settings),workers,queue);
#else
            throw std::invalid_argument("Native provider execution currently requires Windows");
#endif
        }
        agentflow::HttpServer server(persistence,auth,executor.get());const auto bound=server.bind(port);
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
