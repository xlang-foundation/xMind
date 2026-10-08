#include "agentflow/agent_runner.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <vector>

using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr const char* fixture_key="synthetic-gemini-agent-key-not-live";
constexpr const char* first_file="Actual native Gemini file bytes: \"quoted\" and Unicode 雪\n";
constexpr const char* second_file="Second actual native Gemini file bytes\n";
constexpr const char* final_answer="Synthetic provider checked the native file reads 🌍.";
constexpr const char* reopened_answer="Synthetic provider accepted the SQLite-restored signed conversation.";
const std::string precise_metadata=R"({"precision":1.2345678901234567890123456789,"large":18446744073709551615})";
const std::string first_arguments=R"({"pa\u0074h":"README.md"})";
void same_history(const std::vector<Message>& before,const std::vector<Message>& after){
    require(before.size()==after.size(),"SQLite reopen must retain every conversation row");
    for(std::size_t index=0;index<before.size();++index)require(before[index].sequence==after[index].sequence&&before[index].role==after[index].role&&before[index].json==after[index].json,"SQLite reopen must preserve exact durable message bytes and order");
}
void public_context(const Json& data){
    const Json expected={{"profile_id","fixture-gemini-profile"},{"profile_revision",2},{"route_id","fixture.gemini"},{"provider","gemini"},{"wire","gemini-generate-content"},{"model_id","fixture-gemini"}};
    require(data.contains("provider_context")&&data["provider_context"]==expected,"Native agent messages must retain the admitted public Gemini profile identity");
}
void private_key_absent(const std::vector<Message>& history,const std::vector<Event>& events){
    for(const auto& message:history)require(message.json.find(fixture_key)==std::string::npos,"A provider credential cannot enter persisted conversation history");
    for(const auto& event:events)require(event.json.find(fixture_key)==std::string::npos,"A provider credential cannot enter durable model/tool events");
}
}
int main(int argc,char** argv){if(argc!=6)return 2;try{
    const std::vector<std::string> roots{argv[2],argv[3]};const std::string endpoint=argv[5];
    AgentSettings settings;settings.provider={endpoint+"/main/v1beta","fixture-gemini",Capability::supported,Capability::supported,Capability::supported};settings.provider.wire=ProviderWire::gemini_generate_content;
    settings.provider.deadline=std::chrono::seconds(5);settings.provider.idle_timeout=std::chrono::seconds(5);settings.workspace=argv[4];settings.max_output_tokens=64;settings.run_timeout=std::chrono::seconds(10);
    settings.instructions="Synthetic socket contract: use the authorized native file tools and report only observed results.";settings.credential=CredentialReference{"fixture","gemini-agent","provider:fixture-gemini"};settings.provider_identity=ProviderExecutionIdentity{"fixture-gemini-profile","fixture.gemini","gemini",2};
    std::vector<Message> saved;std::vector<Event> saved_events;std::string first_native_id,second_native_id,raw_parts;
    {
        PersistenceService store(argv[1],roots);const std::string key=fixture_key;store.put_credential("fixture","gemini-agent","provider:fixture-gemini","Synthetic contract credential",SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()}),0).get();store.create_session("gemini-session","Actual native tools with synthetic Gemini replies").get();
        {
            AgentRunner runner(store,settings);runner.start("gemini-first","gemini-session","Read the actual fixture files");require(runner.execute("gemini-first").state==RunState::completed,"Actual Gemini agent/file loop must complete from validated socket replies");
        }
        saved=store.history("gemini-session").get();saved_events=store.events("gemini-first").get();require(saved.size()==5&&saved[0].role=="user"&&saved[1].role=="assistant"&&saved[2].role=="tool"&&saved[3].role=="tool"&&saved[4].role=="assistant","AgentRunner must persist its real two-tool conversation atomically");
        const auto turn=Json::parse(saved[1].json),first=Json::parse(saved[2].json),second=Json::parse(saved[3].json),answer=Json::parse(saved[4].json);public_context(Json::parse(saved[0].json));public_context(turn);public_context(answer);
        require(turn["content"]==""&&turn["tool_calls"].size()==2,"Hidden signed thoughts must not become visible assistant content");first_native_id=turn["tool_calls"][0]["id"].get<std::string>();second_native_id=turn["tool_calls"][1]["id"].get<std::string>();
        require(first_native_id.starts_with("gm_")&&second_native_id.starts_with("gm_")&&first_native_id!=second_native_id&&first_native_id!="provider-identified-call"&&second_native_id!="provider-identified-call","Native agent tool identities must remain separate from optional provider identities");
        require(turn["tool_calls"][0]["arguments"]==first_arguments&&turn["tool_calls"][1]["arguments"]==R"({"path":"second.txt"})","AgentRunner persistence must retain exact native argument strings");
        const auto& receipt=turn["provider_items"][0];require(receipt["type"]=="gemini_content"&&receipt["parts_json"].is_string(),"AgentRunner must preserve raw Gemini parts as a string-valued native receipt");raw_parts=receipt["parts_json"].get<std::string>();const auto parts=Json::parse(raw_parts);
        require(parts.size()==4&&parts[0]["thought"]==true&&parts[1]["thoughtSignature"]=="Y2FsbC1zaWduYXR1cmU="&&parts[2]["thoughtSignature"]=="c2lnbmF0dXJlLW9ubHk="&&parts[2]["thought"]==false&&!parts[3]["functionCall"].contains("id"),"Signed, signature-only and anonymous call parts must keep their original identity and boundaries");
        require(raw_parts.find(precise_metadata)!=std::string::npos&&raw_parts.find(first_arguments)!=std::string::npos,"Provider receipt numeric tokens and escaped argument keys must survive actual JSON persistence");
        require(receipt["bindings"][0]==Json{{"tool_call_id",first_native_id},{"part_index",1}}&&receipt["bindings"][1]==Json{{"tool_call_id",second_native_id},{"part_index",3}},"Durable binding table must correlate native calls with original provider parts");
        require(first["tool_call_id"]==first_native_id&&second["tool_call_id"]==second_native_id&&Json::parse(first["content"].get<std::string>())==Json{{"path","README.md"},{"content",first_file}}&&Json::parse(second["content"].get<std::string>())==Json{{"path","second.txt"},{"content",second_file}},"Actual filesystem reads must reach the persisted native tool results");
        require(turn["usage"]["prompt_tokens"]==11&&turn["usage"]["completion_tokens"]==4&&turn["usage"]["total_tokens"]==47,"Agent usage must preserve supplied counts rather than compute a replacement total");
        require(answer["content"]==final_answer&&answer["usage"]["prompt_tokens"]==19&&answer["usage"]["completion_tokens"]==6&&!answer["usage"].contains("total_tokens")&&answer["usage"]["prompt_tokens_details"]["cached_tokens"]==4&&answer["usage"]["completion_tokens_details"]["reasoning_tokens"]==5,"Final native response must carry only supplied usage and peer-confirmed text");
        require(answer["elapsed_ms"].is_number_integer()&&answer["elapsed_ms"]>=0&&answer["first_token_ms"].is_number_integer()&&answer["first_token_ms"]>=0,"Native response timings must come from actual backend measurements");
        std::size_t started=0,completed=0,deltas=0,done=0;for(const auto& event:saved_events){
            if(event.kind=="tool.started"){const auto data=Json::parse(event.json);require(data["call_id"]==(started?second_native_id:first_native_id),"Real tool execution must use the native call identity");++started;}
            if(event.kind=="tool.completed")++completed;if(event.kind=="model.done")++done;
            if(event.kind=="model.tool_delta"){const auto data=Json::parse(event.json);require(data["id"]==(deltas?second_native_id:first_native_id)&&data["arguments"]==(deltas?std::string(R"({"path":"second.txt"})"):first_arguments),"Native model tool events must preserve validated identities and exact arguments");++deltas;}
            if(event.kind=="model.text")require(event.json.find("Synthetic hidden thought")==std::string::npos,"Hidden thought text cannot enter ordinary text events");
        }
        require(started==2&&completed==2&&deltas==2&&done==2&&saved_events.back().kind=="run.completed","Only actual validated native tool execution and terminal completion may be recorded");private_key_absent(saved,saved_events);store.close();
    }
    {
        PersistenceService store(argv[1],roots);same_history(saved,store.history("gemini-session").get());require(store.run("gemini-first").get().state==RunState::completed,"Real SQLite reopen must retain completed agent ownership");
        // Start a new real run in the existing session. AgentRunner itself loads
        // the durable transcript and replays its checked signed receipts.
        {
            AgentRunner runner(store,settings);runner.start("gemini-reopened","gemini-session","Continue after SQLite reopen");require(runner.execute("gemini-reopened").state==RunState::completed,"Reopened native agent must replay its persisted signed Gemini history successfully");
        }
        const auto after=store.history("gemini-session").get();require(after.size()==7,"New native run must append one prompt and one answer without replaying prior tools");same_history(saved,std::vector<Message>(after.begin(),after.begin()+saved.size()));require(Json::parse(after.back().json)["content"]==reopened_answer,"Independent provider peer must accept the SQLite-restored signed conversation");
        const auto reopened_events=store.events("gemini-reopened").get();for(const auto& event:reopened_events)require(event.kind!="tool.started"&&event.kind!="tool.completed","Conversation reload cannot silently re-execute earlier tool calls");private_key_absent(after,reopened_events);
        for(const auto* route:{"malformed","incomplete","truncated","unknown"}){
            store.create_session(route,std::string("Synthetic Gemini failure ")+route).get();auto rejected=settings;rejected.provider.endpoint=endpoint+"/"+route+"/v1beta";
            {
                AgentRunner runner(store,rejected);runner.start(route,route,std::string("Reject ")+route);require(runner.execute(route).state==RunState::failed,"Malformed, partial and unoffered Gemini replies cannot complete an agent run");
            }
            const auto history=store.history(route).get();const auto events=store.events(route).get();require(history.size()==1&&history[0].role=="user"&&store.operations(route).get().empty(),"Rejected Gemini turns cannot persist a successful assistant or authorize effects");
            for(const auto& event:events)require(event.kind!="tool.started"&&event.kind!="tool.completed"&&event.kind!="model.tool_delta"&&event.kind!="run.completed","Incomplete or rejected provider calls cannot reach executable native tool events");require(events.back().kind=="run.failed"&&Json::parse(events.back().json)["reason"]=="model_protocol_error","Actual provider rejection must have a durable native failure reason");private_key_absent(history,events);
        }
        store.close();
    }
    std::cout<<"Native Gemini AgentRunner contract passed real two-file execution, encrypted credential reuse, actual xlang3 SQLite persistence/reopen, signed precise receipt/result replay, native/provider ID separation, supplied usage and malformed/truncated/unoffered failure without tool execution. Socket model replies are synthetic; no product enrollment or live Gemini inference tested.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
