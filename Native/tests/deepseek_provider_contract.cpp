// Actual native HTTP/SSE adapter + AgentRunner/workspace/SQLite boundaries.
// All model outputs and credentials are supplied by the isolated synthetic peer.
#include "agentflow/deepseek_provider.hpp"
#include "agentflow/deepseek_model_policy.hpp"
#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>

using namespace agentflow;using Json=nlohmann::json;
namespace {
const std::string key="deepseek-native-fixture-not-a-live-key";
const std::string reasoning=std::string("Synthetic \xe4\xb8\xad reasoning ")+std::string(1,'\0')+" exact";
const std::string arguments=R"({"path":"README.md","n":1.00000000000000000001})";
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E,class F>void rejects(F action){try{action();}catch(const E&){return;}throw std::runtime_error("Expected DeepSeek boundary rejection missing");}
ChatProviderConfig configuration(const std::string& endpoint){
    ChatProviderConfig config{endpoint,"deepseek-flash",Capability::supported,Capability::supported,Capability::supported};
    config.chat_dialect=ChatDialect::deepseek;config.reasoning=Capability::supported;
    config.deadline=std::chrono::seconds(5);config.idle_timeout=std::chrono::seconds(3);return config;
}
std::string frame(Json delta,const std::string& finish={},Json usage=nullptr){
    Json choice={{"index",0},{"delta",std::move(delta)},{"finish_reason",finish.empty()?Json(nullptr):Json(finish)}};
    return "data: "+Json{{"id","synthetic-response"},{"model","deepseek-flash"},{"object","chat.completion.chunk"},{"choices",Json::array({choice})},{"usage",std::move(usage)}}.dump()+"\n\n";
}
Json usage(){return {{"prompt_tokens",19},{"completion_tokens",11},{"total_tokens",30},{"prompt_cache_hit_tokens",7},{"prompt_cache_miss_tokens",12},{"prompt_tokens_details",{{"cached_tokens",7}}},{"completion_tokens_details",{{"reasoning_tokens",4}}}};}
ModelMessage saved(const ModelCompletion& result){return {MessageRole::assistant,result.content,result.tool_calls,{},result.refusal,result.provider_items_json};}
void pure_contract(){
    const auto policy=deepseek_documented_tool_policy();require(policy.size()==2&&policy.at("deepseek-flash")==Capability::supported&&policy.at("deepseek-v4-pro")==Capability::supported&&!policy.contains("deepseek-guessed-prefix"),"DeepSeek tools require exact documented identities");
    const auto wire=frame({{"reasoning_content",reasoning.substr(0,8)}})+frame({{"reasoning_content",reasoning.substr(8)},{"content","Visible answer"}},"stop",usage())+"data: [DONE]\n\n";
    ModelCompletion result;
    for(std::size_t split=0;split<=wire.size();++split){
        std::vector<ModelEvent> events;ChatCompletionStream stream([&](const auto& event){events.push_back(event);},ChatDialect::deepseek,"deepseek-flash");
        stream.feed(std::string_view(wire).substr(0,split));stream.feed(std::string_view(wire).substr(split));result=stream.finish();
        const auto receipt=Json::parse(result.provider_items_json)[0];
        require(result.content=="Visible answer"&&receipt["message"]["reasoning_content"]==reasoning&&Json::parse(result.usage_json)==usage(),"Exact fragmented DeepSeek reasoning/usage must survive");
        int reasoning_events=0,usage_events=0;for(const auto& event:events){if(event.kind=="model.reasoning")++reasoning_events;if(event.kind=="model.usage")++usage_events;require(event.kind!="model.extension","DeepSeek reasoning must have its native event");}
        require(reasoning_events==2&&usage_events==1&&events.back().kind=="model.done","Terminal finish-chunk usage must emit exactly once");
    }
    auto config=configuration("https://fixture.example.test/chat/completions");
    ModelRequest request{{{MessageRole::developer,"Trusted native instructions"},{MessageRole::user,"Earlier turn"},saved(result),{MessageRole::user,"Next turn"}},{{"read_file","Read",R"({"type":"object"})"}},true,64};
    auto body=Json::parse(serialize_deepseek_request(config,request));
    require(body["messages"][0]["role"]=="system"&&body["messages"][2]["reasoning_content"]==reasoning&&body["max_tokens"]==64&&!body.contains("n")&&!body.contains("max_completion_tokens")&&!body.contains("thinking")&&!body.contains("reasoning_effort"),"Default thinking and full prior reasoning replay must remain provider-native");
    for(const auto effort:{ReasoningEffort::none,ReasoningEffort::minimal,ReasoningEffort::low,ReasoningEffort::medium,ReasoningEffort::high,ReasoningEffort::xhigh,ReasoningEffort::max}){
        config.reasoning_effort=effort;body=Json::parse(serialize_deepseek_request(config,request));require(body.contains("reasoning_effort"),"Explicit native effort must be sent");
    }
    config.reasoning_effort.reset();
    for(int mode=0;mode<8;++mode){
        auto invalid=request;auto receipt=Json::parse(invalid.messages[2].provider_items_json);
        if(mode==0)receipt[0]["model"]="deepseek-v4-pro";
        if(mode==1)receipt[0]["type"]="foreign_receipt";
        if(mode==2)receipt[0]["message"]["content"]="altered";
        if(mode==3)receipt[0]["message"]["reasoning_content"]=17;
        if(mode==4)receipt[0]["message"]["private_key"]="not allowed";
        if(mode==5)receipt[0]["message"]["tool_calls"]=Json::array({{{"id","foreign"}}});
        if(mode==7)receipt[0]["message"].erase("reasoning_content");
        invalid.messages[2].provider_items_json=mode==6?"[]":receipt.dump();
        rejects<IncompatibleProviderHistory>([&]{serialize_deepseek_request(config,invalid);});
    }
    auto invalid=request;invalid.messages[2].provider_items_json=R"([{"type":"deepseek_assistant","type":"deepseek_assistant"}])";rejects<IncompatibleProviderHistory>([&]{serialize_deepseek_request(config,invalid);});
    rejects<IncompatibleProviderHistory>([&]{serialize_chat_request(config,request);});
    config.wire=ProviderWire::responses;rejects<std::invalid_argument>([&]{serialize_responses_request(config,request);});config.wire=ProviderWire::chat_completions;
    for(int mode=0;mode<12;++mode){
        auto metrics=usage();Json delta={{"content","partial"}};std::string finish="stop",model="deepseek-flash";
        if(mode==0)metrics["prompt_cache_hit_tokens"]=-1;
        if(mode==1)metrics["completion_tokens_details"]["reasoning_tokens"]=12;
        if(mode==2)metrics["prompt_tokens_details"]["cached_tokens"]=8;
        if(mode==3)metrics["total_tokens"]=29;
        if(mode==4)metrics["completion_tokens"]=1.5;
        if(mode==5)metrics["unknown_private_field"]="do not publish";
        if(mode==6)delta["reasoning_content"]=false;
        if(mode==7)finish="insufficient_system_resource";
        if(mode==8)finish="aborted";
        if(mode==10){metrics.erase("prompt_cache_miss_tokens");metrics.erase("prompt_tokens_details");metrics["prompt_cache_hit_tokens"]=20;}
        if(mode==11){metrics.erase("prompt_cache_hit_tokens");metrics.erase("prompt_tokens_details");metrics["prompt_cache_miss_tokens"]=20;}
        std::string input=frame(delta,finish,mode==9?Json(nullptr):metrics)+"data: [DONE]\n\n";
        bool done=false;ChatCompletionStream stream([&](const auto& event){if(event.kind=="model.done")done=true;},ChatDialect::deepseek,model);
        rejects<ModelProtocolError>([&]{stream.feed(input);stream.finish();});require(!done,"Malformed DeepSeek completion cannot acknowledge execution");
    }
    for(const auto* field:{"prompt_cache_hit_tokens","prompt_cache_miss_tokens"}){
        Json metrics={{"prompt_tokens",19},{"completion_tokens",11},{"total_tokens",30},{field,0}};
        ChatCompletionStream stream([](const auto&){},ChatDialect::deepseek,"deepseek-flash");stream.feed(frame({{"content","Answer"}},"stop",metrics)+"data: [DONE]\n\n");
        require(Json::parse(stream.finish().usage_json)==metrics,"A supplied zero cache counter must preserve its missing counterpart");
    }
    const auto no_reasoning=frame({{"content","Non-thinking response"},{"reasoning_content",nullptr}},"stop",{{"prompt_tokens",0},{"completion_tokens",0},{"total_tokens",0}})+"data: [DONE]\n\n";
    ChatCompletionStream stream([](const auto&){},ChatDialect::deepseek,"deepseek-flash");stream.feed(no_reasoning);result=stream.finish();require(Json::parse(result.provider_items_json)[0]["message"]["reasoning_content"].is_null(),"Supplied null reasoning and missing cache counts must not be invented");
}
void transport_contract(const std::string& base){
    SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});auto config=configuration(base+"/tools");
    ModelRequest request{{{MessageRole::system,"Native fixture instructions"},{MessageRole::user,"Gateway fixture"}},{{"read_file","Read",R"({"type":"object"})"}},true,64};
    int done=0;std::vector<Json> observed;const auto sink=[&](const ModelEvent& event){if(event.kind=="model.done")++done;if(event.kind=="model.usage")observed.push_back(Json::parse(event.json));};
    auto result=complete_model(config,request,&secret,sink);require(done==1&&result.tool_calls.size()==1&&result.tool_calls[0].arguments_json==arguments&&observed==std::vector<Json>{usage()},"Actual native socket tool args and final usage must stay exact");
    request.messages.push_back(saved(result));request.messages.push_back({MessageRole::tool,"Synthetic gateway tool result",{},result.tool_calls[0].id});
    config.endpoint=base+"/continuation";result=complete_model(config,request,&secret,sink);require(result.content=="Gateway completed"&&result.tool_calls.empty(),"DeepSeek actual continuation failed");
    request.messages.push_back(saved(result));request.messages.push_back({MessageRole::user,"Second gateway turn"});config.endpoint=base+"/next-turn";result=complete_model(config,request,&secret,sink);require(result.content=="Next turn completed"&&done==3,"Later user turn must replay earlier non-tool reasoning too");
    request.messages.resize(2);request.tools.clear();config.reasoning_effort=ReasoningEffort::none;config.endpoint=base+"/none";result=complete_model(config,request,&secret,sink);require(result.content=="Non-thinking completed"&&done==4,"Explicit none must retain documented non-thinking support");
    config.reasoning_effort.reset();request.tools={{"read_file","Read",R"({"type":"object"})"}};
    for(const auto* path:{"/unknown","/bad-usage","/missing-usage","/missing-reasoning","/resource","/incomplete"}){
        config.endpoint=base+path;const auto prior=done;rejects<ModelProtocolError>([&]{complete_model(config,request,&secret,sink);});require(done==prior,"Rejected native socket response cannot publish done");
    }
    for(const auto* path:{"/unauthorized","/redirect"}){
        config.endpoint=base+path;bool rejected=false;const auto prior=done;
        try{complete_model(config,request,&secret,sink);}catch(const ProviderHttpError& error){rejected=error.status==(std::string(path)=="/redirect"?307:401);}require(rejected&&done==prior,"DeepSeek HTTP failure must not redirect/retry/complete");
    }
    config.endpoint=base+"/must-not-arrive";rejects<std::invalid_argument>([&]{complete_model(config,request,nullptr,sink);});
    auto incompatible=request;incompatible.messages.push_back({MessageRole::assistant,"Unknown old thinking receipt"});rejects<IncompatibleProviderHistory>([&]{complete_model(config,incompatible,&secret,sink);});
    auto unavailable=config;unavailable.tools=Capability::unknown;rejects<std::invalid_argument>([&]{complete_model(unavailable,request,&secret,sink);});
    auto foreign=config;foreign.wire=ProviderWire::responses;rejects<std::invalid_argument>([&]{complete_model(foreign,request,&secret,sink);});
    auto excessive=request;excessive.max_output_tokens=393217;rejects<std::invalid_argument>([&]{complete_model(config,excessive,&secret,sink);});
    std::stop_source cancel;cancel.request_stop();rejects<TransportCancelled>([&]{complete_model(config,request,&secret,sink,cancel.get_token());});
}
void engine_contract(const std::string& database,const std::vector<std::string>& roots,const std::string& workspace,const std::string& origin){
    AgentSettings settings;settings.provider=configuration(origin+"/engine");settings.workspace=workspace;settings.credential=CredentialReference{"fixture","provider","provider:deepseek"};
    settings.provider_identity=ProviderExecutionIdentity{"deepseek","deepseek.chat","deepseek",1};settings.max_output_tokens=128;
    {
        PersistenceService store(database,roots);SecretBytes secret({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()});store.put_credential("fixture","provider","provider:deepseek","Synthetic DeepSeek fixture",std::move(secret),0).get();
        store.create_session("deepseek-session","Synthetic model/native reads").get();AgentRunner runner(store,settings);runner.start("deepseek-first","deepseek-session","Read both fixture files");
        require(runner.execute("deepseek-first").state==RunState::completed,"Actual DeepSeek native two-file loop must complete");
        const auto rows=store.history("deepseek-session").get();require(rows.size()==6,"Native two-read transcript cardinality must be actual");
        require(Json::parse(Json::parse(rows[2].json)["content"].get<std::string>())["content"]=="Actual fixture file A\n"&&Json::parse(Json::parse(rows[4].json)["content"].get<std::string>())["content"]=="Actual fixture file B\n","Two independent native file results must persist");
        int starts=0,completions=0,reasons=0;for(const auto& event:store.events("deepseek-first").get()){if(event.kind=="tool.started")++starts;if(event.kind=="tool.completed")++completions;if(event.kind=="model.reasoning")++reasons;}
        require(starts==2&&completions==2&&reasons==3,"Actual native tool/reasoning events must correlate with real calls");
        for(const auto index:{1u,3u,5u}){const auto assistant=Json::parse(rows[index].json);require(assistant["provider_items"][0]["type"]=="deepseek_assistant"&&assistant["provider_context"]["provider"]=="deepseek"&&assistant["usage"]==usage()&&assistant["elapsed_ms"].is_number_integer(),"Actual provider receipt/usage/timing must be stored per response");}
    }
    {
        PersistenceService reopened(database,roots);require(reopened.run("deepseek-first").get().state==RunState::completed,"Reopen must retain actual completed root");AgentRunner runner(reopened,settings);runner.start("deepseek-second","deepseek-session","Continue the prior native session");require(runner.execute("deepseek-second").state==RunState::completed,"Actual reopened DeepSeek conversation must replay all reasoning receipts");
        const auto rows=reopened.history("deepseek-session").get();require(rows.size()==8&&Json::parse(rows.back().json)["content"]=="Reopened native session completed","Actual later prompt must append only its actual response");
        require(reopened.owned_children("deepseek-first").get().empty()&&reopened.owned_children("deepseek-second").get().empty(),"Simple provider read fixture must not invent delegated children");
    }
}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;const char* stage="pure";
    try{
        pure_contract();stage="transport";transport_contract(argv[5]);stage="engine";engine_contract(argv[1],{argv[2],argv[3]},argv[4],argv[5]);
        std::cout<<"Native DeepSeek dialect passed synthetic wire assertions, 16 actual local HTTP requests, two actual native workspace reads, encrypted credential and SQLite session reopen; no live provider or compaction acceptance claimed\n";return 0;
    }catch(const std::exception& error){
        // Only the test runner's owned synthetic directory receives raw detail.
        // Public stderr has a fixed stage; no provider payload can escape there.
        const auto detail=std::string(error.what()).substr(0,65536);
        std::ofstream file(std::filesystem::path(argv[4]).parent_path()/"private-native-contract-error.log",std::ios::binary);file<<detail;
        std::cerr<<"Native DeepSeek contract failed at "<<stage<<'\n';return 1;
    }
}
