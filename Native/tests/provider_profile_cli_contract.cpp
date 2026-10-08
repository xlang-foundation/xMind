// Real native HTTP/profile/agent/store fixture for the independently driven CLI.
// Only the provider sockets and credentials are synthetic.
#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/http_server.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
constexpr const char* owner_token="synthetic-profile-cli-owner-token-not-live";
constexpr const char* first_answer="Synthetic selected Gemini CLI response.";
constexpr const char* second_answer="Synthetic Gemini CLI signed replay after reopen.";
constexpr const char* recovered_answer="Synthetic Gemini CLI recovered actual turn.";
const std::vector<std::string> secrets{
    "synthetic-profile-cli-openai-key-not-live",
    "synthetic-profile-cli-claude-key-not-live",
    "synthetic-profile-cli-gemini-key-not-live",
    "synthetic-profile-cli-rejected-key-not-live"
};
const std::string first_parts=R"([{"text":"Synthetic hidden CLI thought.","thoughtSignature":"Y2xpLWhpZGRlbi1zaWduYXR1cmU=","thought":true},{"text":"Synthetic selected Gemini CLI response.","thoughtSignature":"Y2xpLWFuc3dlci1zaWduYXR1cmU=","partMetadata":{"precision":1.2345678901234567890123456789,"large":18446744073709551615}},{"thoughtSignature":"Y2xpLXNpZ25hdHVyZS1vbmx5","thought":false}])";
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void private_absent(const std::string& source){
    for(const auto& secret:secrets)require(source.find(secret)==std::string::npos,"Synthetic provider credentials must not enter public messages or events");
    require(source.find(owner_token)==std::string::npos,"Backend access token must not enter public history");
}
std::vector<ProviderProfileExecutionPolicy> policies(const std::string& origin){
    std::vector<ProviderProfileExecutionPolicy> result;
    const auto add=[&](ProviderProfileRoute route,const std::string& catalogue,ProviderCatalogueFormat format){
        ChatProviderConfig provider;provider.endpoint=route.endpoint;provider.wire=route.wire;
        provider.tools=route.wire==ProviderWire::gemini_generate_content?Capability::unknown:Capability::supported;
        provider.stream_usage=Capability::supported;provider.output_limit=Capability::supported;
        provider.deadline=std::chrono::seconds(8);provider.idle_timeout=std::chrono::seconds(5);
        result.push_back({std::move(route),std::move(provider),ProviderCataloguePolicy{catalogue,format},{}});
    };
    add({"openai.chat","openai",origin+"/openai/chat/completions","fixture","fixture:openai",ProviderWire::chat_completions},origin+"/openai/models",ProviderCatalogueFormat::openai);
    add({"anthropic.messages","anthropic",origin+"/claude/messages","fixture","fixture:claude",ProviderWire::anthropic_messages},origin+"/claude/models",ProviderCatalogueFormat::anthropic);
    add({"gemini.generate-content","gemini",origin+"/v1beta","fixture","fixture:gemini",ProviderWire::gemini_generate_content},origin+"/v1beta/models",ProviderCatalogueFormat::gemini);
    return result;
}
struct Access {
    HttpServer server;int port;std::thread thread;
    Access(PersistenceService& store,ProviderProfileRuntime& runtime)
        :server(store,owner_token,&runtime,nullptr,{},{},{},nullptr,&runtime,&runtime),
         port(server.bind(0)),thread([this]{server.listen();}){}
    ~Access(){server.stop();if(thread.joinable())thread.join();}
};
void history(PersistenceService& store,const std::string& session,std::size_t messages,std::size_t run_count){
    const auto saved=store.history(session).get();const auto runs=store.runs(session).get();
    require(saved.size()==messages&&runs.size()==run_count,"Actual native CLI runs must retain the expected durable conversation rows");
    const Json context={{"profile_id","gemini"},{"profile_revision",3},{"route_id","gemini.generate-content"},{"provider","gemini"},{"wire","gemini-generate-content"},{"model_id","models/fixture-gemini"}};
    for(const auto& message:saved){private_absent(message.json);const auto data=Json::parse(message.json);
        require(data.at("provider_context")==context,"CLI admission must retain the actual selected profile version");
    }
    require(saved.size()>=2&&saved[0].role=="user"&&saved[1].role=="assistant","Actual selected Gemini turn must persist a user and assistant");
    const auto first=Json::parse(saved[1].json);
    require(first.at("content")==first_answer&&first.at("provider_items")[0].at("parts_json")==first_parts,"Exact signed raw parts and hidden thoughts must survive real CLI agent/store history");
    require(first.at("provider_items")[0].at("bindings").empty()&&!first.contains("tool_calls"),"Text-only execution must not fabricate native/provider tool identities");
    require(first.at("usage").at("prompt_tokens")==7&&first.at("usage").at("completion_tokens")==3&&first.at("usage").at("total_tokens")==83&&first.at("usage").at("prompt_tokens_details").at("cached_tokens")==2&&first.at("usage").at("completion_tokens_details").at("reasoning_tokens")==5,"CLI agent usage must retain supplied Gemini values, including a non-computed total");
    require(first.at("elapsed_ms").is_number_integer()&&first.at("elapsed_ms")>=0&&first.at("first_token_ms").is_number_integer()&&first.at("first_token_ms")>=0,"Actual native timing measurements must reach history");
    if(messages>=4){
        const auto second=Json::parse(saved[3].json);
        require(saved[2].role=="user"&&saved[3].role=="assistant"&&second.at("content")==second_answer,"Reopened native agent must persist the signed continuation reply");
        require(second.at("usage").at("prompt_tokens")==13&&second.at("usage").at("completion_tokens")==4&&!second.at("usage").contains("total_tokens"),"Missing supplied totals must remain absent after reopen");
    }
    if(messages>=5)require(saved[4].role=="user"&&Json::parse(saved[4].json).at("content")=="Synthetic blocked CLI turn","Blocked native turn must retain its real prompt without fabricating an assistant");
    if(messages==8)require(saved[5].role=="user"&&saved[6].role=="user"&&saved[7].role=="assistant"&&Json::parse(saved[7].json).at("content")==recovered_answer,"A later actual successful turn must recover after retained failed prompts");
    std::size_t completed=0,failed=0;
    for(const auto& run:runs){
        require(Json::parse(run.provider_context_json)==context,"Actual persisted run context must bind the selected profile");
        if(run.state==RunState::completed)++completed;else if(run.state==RunState::failed)++failed;else throw std::runtime_error("All observed CLI fixture runs must be terminal");
        const auto events=store.events(run.id).get();require(!events.empty(),"Actual observed runs require durable events");
        require(events.back().kind==(run.state==RunState::completed?"run.completed":"run.failed"),"A blocked provider must not produce a successful terminal event");
        for(const auto& event:events){private_absent(event.json);require(event.kind!="tool.started"&&event.kind!="tool.completed","Text-only fixture must not simulate executable tools");}
    }
    require(completed==(messages==2?1:messages==8?3:2)&&failed==(messages==5?1:messages==8?2:0),"Actual model failures and recovery must remain separately represented");
}
}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    try{
        const auto root=std::filesystem::u8path(argv[1]);require(std::filesystem::is_directory(root),"Independent CLI peer must own an existing unique scratch directory");
        const auto database=(root/"profile-cli.sqlite").string();const bool reopened=std::filesystem::exists(database);
        const std::vector<std::string> imports{argv[2],argv[3]};PersistenceService store(database,imports);
        AgentSettings base;base.instructions="Synthetic native CLI profile contract.";base.max_output_tokens=64;base.max_turns=4;base.run_timeout=std::chrono::seconds(10);
        bool stopped=false;
        {
            ProviderProfileRuntime runtime(store,base,policies(argv[4]),1,8);Access access(store,runtime);
            const auto initial=runtime.configuration();
            if(reopened){
                require(initial.revision==8&&initial.active=="gemini"&&initial.profiles.size()==3&&store.credentials("fixture").get().size()==5&&store.sessions().get().size()==1,"Actual SQLite restart must restore all encrypted profiles and the selected Gemini service, retaining saved-key update candidates");
                history(store,store.sessions().get()[0].id,2,1);
            }else require(initial.revision==0&&initial.profiles.empty()&&store.credentials("fixture").get().empty()&&store.sessions().get().empty(),"Fresh native fixture must begin without fabricated profiles, credentials or sessions");
            std::cout<<Json{{"type","fixture_ready"},{"port",access.port},{"revision",initial.revision},{"reopened",reopened}}.dump()<<'\n'<<std::flush;
            std::string line;
            while(std::getline(std::cin,line)){
                require(line.size()<=4096,"Native test control must remain bounded");const auto command=Json::parse(line);const auto name=command.at("command").get<std::string>();
                if(name=="arm_cas_failure"||name=="disarm_cas_failure"){
                    XlangSqlite sql(database,imports);
                    sql.execute(name=="arm_cas_failure"?"CREATE TRIGGER reject_cli_profile BEFORE UPDATE ON information WHEN NEW.category='native-provider-profiles' BEGIN SELECT RAISE(ABORT,'synthetic CLI profile CAS failure'); END":"DROP TRIGGER reject_cli_profile");
                    std::cout<<Json{{"type","fixture_fault"},{"armed",name=="arm_cas_failure"}}.dump()<<'\n'<<std::flush;continue;
                }
                if(name=="abort"){stopped=true;break;}
                require(name=="check"||name=="stop","Unknown independent fixture control");
                require(runtime.configuration().revision==command.at("revision").get<std::int64_t>()&&store.credentials("fixture").get().size()==command.at("credentials").get<std::size_t>()&&store.sessions().get().size()==command.at("sessions").get<std::size_t>(),"CLI profile effects must match actual native SQLite state");
                if(command.contains("session_id"))history(store,command.at("session_id").get<std::string>(),command.at("messages").get<std::size_t>(),command.at("runs").get<std::size_t>());
                if(runtime.configuration().revision>0)private_absent(store.information("native-provider-profiles","registry").get());
                else require(runtime.configuration().profiles.empty(),"Revision-zero draft discovery cannot publish a profile registry");
                std::cout<<Json{{"type","fixture_checked"},{"revision",runtime.configuration().revision}}.dump()<<'\n'<<std::flush;
                if(name=="stop"){stopped=true;break;}
            }
        }
        require(stopped,"Independent peer must explicitly close the native fixture");store.close();
        std::cout<<Json{{"type","fixture_closed"}}.dump()<<'\n'<<std::flush;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
