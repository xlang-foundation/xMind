// Real native AgentRunner/tools/encrypted xlang3 SQLite; labelled provider peer.
#include "agentflow/agent_runner.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
using namespace agentflow;using Json=nlohmann::json;
namespace {
constexpr const char* fixture_key="synthetic-anthropic-agent-key-not-live";
constexpr const char* first_file="Actual Claude native file bytes: \"quoted\" and Unicode 雪\n";
constexpr const char* second_file="Second actual Claude native file bytes\n";
constexpr const char* final_answer="Synthetic Claude peer checked both real native reads 🌍.";
constexpr const char* reopened_answer="Synthetic Claude peer accepted the SQLite-restored conversation.";
constexpr const char* recovered_answer="Synthetic Claude peer accepted recovery after actual cancellation.";
constexpr const char* batch_text="Synthetic visible text between the two actual reads.";
constexpr const char* escaped_arguments=R"({"pa\u0074h":"README.md"})";
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void same_history(const std::vector<Message>& before,const std::vector<Message>& after){
    require(before.size()==after.size(),"Actual SQLite history must retain every row");
    for(std::size_t index=0;index<before.size();++index)require(before[index].sequence==after[index].sequence&&before[index].role==after[index].role&&before[index].json==after[index].json,"Actual SQLite reload must preserve exact history bytes and ordering");
}
void private_absent(const std::vector<Message>& history,const std::vector<Event>& events){
    for(const auto& message:history)require(message.json.find(fixture_key)==std::string::npos,"Provider key must not enter durable conversation");
    for(const auto& event:events)require(event.json.find(fixture_key)==std::string::npos&&event.json.find("private-provider-error-body")==std::string::npos,"Private provider key/body must not enter durable events");
}
Json context(){return {{"profile_id","fixture-claude-profile"},{"profile_revision",2},{"route_id","fixture.anthropic"},{"provider","anthropic"},{"wire","anthropic-messages"},{"model_id","fixture-claude"}};}
void usage(const Json& value){
    require(value.at("prompt_tokens")==value.at("input_tokens")&&value.at("completion_tokens")==value.at("output_tokens")&&value.at("input_tokens_scope")=="uncached","Claude standard aliases must preserve the uncached provider input count");
    for(const auto* absent:{"total_tokens","reasoning_tokens","completion_tokens_details","output_tokens_details"})require(!value.contains(absent),"Claude usage must not invent totals or reasoning");
    if(value.contains("cache_read_input_tokens"))require(value.at("prompt_tokens_details")==Json{{"cached_tokens",value.at("cache_read_input_tokens")}},"Cache read detail must come only from a supplied cache counter");
    else require(!value.contains("prompt_tokens_details"),"Absent cache read must not fabricate cache detail");
}
void metrics(const Json& message){
    usage(message.at("usage"));require(message.at("elapsed_ms").is_number_integer()&&message.at("elapsed_ms")>=0&&message.at("first_token_ms").is_number_integer()&&message.at("first_token_ms")>=0,"Agent metrics require real measured native timings");
    require(message.at("provider_context")==context(),"Durable response must retain actual native profile attribution");
}
std::size_t count(const std::vector<Event>& events,const std::string& kind){std::size_t result=0;for(const auto& event:events)if(event.kind==kind)++result;return result;}
void normalized_events(const std::vector<Event>& events,const Json& expected){
    bool observed=false;Json last;for(const auto& event:events)if(event.kind=="model.usage"){last=Json::parse(event.json);usage(last);observed=true;}
    require(observed&&last==expected,"Actual streamed final usage must equal the persisted response metrics");
}
void no_effects(PersistenceService& store,const std::string& run,const std::string& session,RunState state){
    const auto history=store.history(session).get();const auto events=store.events(run).get();
    require(store.run(run).get().state==state&&history.size()==1&&history[0].role=="user"&&store.operations(run).get().empty(),"Rejected native turn must retain only its real prompt and no authorized operation");
    require(count(events,"tool.started")==0&&count(events,"tool.completed")==0&&count(events,"conversation.tool_turn")==0&&count(events,"run.completed")==0,"Provider rejection cannot execute tools or commit successful conversation");
    require(events.back().kind==(state==RunState::cancelled?"run.cancelled":"run.failed"),"Actual terminal state must match durable failure/cancellation");
    private_absent(history,events);
}
}
int main(int argc,char** argv){if(argc!=6)return 2;try{
    const std::vector<std::string> roots{argv[2],argv[3]};const std::string endpoint=argv[5];
    AgentSettings settings;settings.provider={endpoint+"/main","fixture-claude",Capability::supported,Capability::supported,Capability::supported};settings.provider.wire=ProviderWire::anthropic_messages;
    settings.provider.deadline=std::chrono::seconds(6);settings.provider.idle_timeout=std::chrono::seconds(5);settings.workspace=argv[4];settings.max_output_tokens=64;settings.max_turns=4;settings.run_timeout=std::chrono::seconds(12);
    settings.instructions="Synthetic Claude native agent contract: report only actual authorized results.";settings.credential=CredentialReference{"fixture","claude-agent","provider:fixture-claude"};settings.provider_identity=ProviderExecutionIdentity{"fixture-claude-profile","fixture.anthropic","anthropic",2};
    std::vector<Message> saved;
    {
        PersistenceService store(argv[1],roots);const std::string key=fixture_key;
        store.put_credential("fixture","claude-agent","provider:fixture-claude","Synthetic native Claude contract",SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()}),0).get();store.create_session("claude-main","Actual Claude tools with synthetic provider replies").get();
        {AgentRunner runner(store,settings);runner.start("claude-first","claude-main","Read the actual Claude fixture files");require(runner.execute("claude-first").state==RunState::completed,"Actual Claude two-file native tool loop must complete");}
        saved=store.history("claude-main").get();require(saved.size()==5&&saved[0].role=="user"&&saved[1].role=="assistant"&&saved[2].role=="tool"&&saved[3].role=="tool"&&saved[4].role=="assistant","Actual native two-tool turn must commit five conversation rows");
        const auto calls=Json::parse(saved[1].json),left=Json::parse(saved[2].json),right=Json::parse(saved[3].json),answer=Json::parse(saved[4].json);
        require(calls.at("content")==batch_text&&calls.at("tool_calls").size()==2&&calls.at("tool_calls")[0].at("id")=="toolu-native-left"&&calls.at("tool_calls")[1].at("id")=="toolu-native-right","Claude visible text and tool IDs must match original provider blocks");
        require(calls.at("tool_calls")[0].at("arguments")==escaped_arguments,"Original escaped argument JSON must survive native execution and persistence");
        const auto receipt=calls.at("provider_items").at(0),blocks=Json::parse(receipt.at("content_json").get<std::string>());
        require(receipt.at("type")=="anthropic_content"&&receipt.at("finish_reason")=="tool_calls"&&receipt.at("provider_finish_reason")=="tool_use"&&blocks.size()==5,"Native Claude receipt must preserve the original five blocks");
        require(blocks[0]==Json{{"type","thinking"},{"thinking","Synthetic hidden Claude thought"},{"signature","opaque-claude-fixture-signature"}}&&blocks[1].at("type")=="tool_use"&&blocks[2]==Json{{"type","text"},{"text",batch_text}}&&blocks[3]==Json{{"type","redacted_thinking"},{"data","opaque-redacted-claude-fixture"}}&&blocks[4].at("type")=="tool_use","Signed/redacted thinking and interleaved text must retain exact provider block order");
        require(receipt.at("content_json").get<std::string>().find(escaped_arguments)!=std::string::npos,"Receipt string must retain exact original JSON tokens");
        require(calls.at("tool_calls")[0].at("name")=="read_file"&&calls.at("tool_calls")[1].at("name")=="read_file"&&Json::parse(calls.at("tool_calls")[0].at("arguments").get<std::string>())==Json{{"path","README.md"}}&&Json::parse(calls.at("tool_calls")[1].at("arguments").get<std::string>())==Json{{"path","second.txt"}},"Tool arguments must retain actual native file requests");
        require(left.at("tool_call_id")=="toolu-native-left"&&right.at("tool_call_id")=="toolu-native-right"&&Json::parse(left.at("content").get<std::string>())==Json{{"path","README.md"},{"content",first_file}}&&Json::parse(right.at("content").get<std::string>())==Json{{"path","second.txt"},{"content",second_file}},"Matching actual filesystem bytes must reach each persisted Claude tool result");
        metrics(calls);metrics(answer);require(answer.at("content")==final_answer,"Only independent peer-confirmed answer text can persist");
        require(calls.at("usage").at("input_tokens")==11&&calls.at("usage").at("output_tokens")==4&&calls.at("usage").at("cache_creation_input_tokens")==0&&calls.at("usage").at("cache_read_input_tokens")==7,"Tool turn usage must retain raw supplied cache write zero/read count without adding input");
        require(answer.at("usage").at("prompt_tokens")==19&&answer.at("usage").at("completion_tokens")==6&&answer.at("usage").at("cache_creation_input_tokens")==5&&answer.at("usage").at("cache_read_input_tokens")==0,"Final usage must retain supplied cache read zero and exclude cache writes from input alias");
        const auto answer_blocks=Json::parse(answer.at("provider_items").at(0).at("content_json").get<std::string>());
        require(answer_blocks.size()==2&&answer_blocks[0]==Json{{"type","thinking"},{"thinking",""},{"signature","opaque-final-claude-fixture"}}&&answer_blocks[1]==Json{{"type","text"},{"text",final_answer}},"Signature-only thinking must survive alongside the actual final visible text");
        const auto events=store.events("claude-first").get();normalized_events(events,answer.at("usage"));
        for(const auto& event:events)if(event.kind=="model.text")require(event.json.find("Synthetic hidden Claude thought")==std::string::npos&&event.json.find("opaque-redacted-claude-fixture")==std::string::npos,"Hidden and redacted thinking cannot enter ordinary streamed text");
        require(count(events,"tool.started")==2&&count(events,"tool.completed")==2&&count(events,"model.done")==2&&count(events,"conversation.tool_turn")==1&&events.back().kind=="run.completed","Only the actual two read executions and validated terminal completions may be recorded");
        std::vector<std::string> ids;for(const auto& event:events)if(event.kind=="tool.started")ids.push_back(Json::parse(event.json).at("call_id").get<std::string>());require(ids==std::vector<std::string>{"toolu-native-left","toolu-native-right"},"Actual native tool activity must correlate to both provider IDs");
        private_absent(saved,events);store.close();
    }
    {
        PersistenceService store(argv[1],roots);same_history(saved,store.history("claude-main").get());require(store.credentials("fixture").get().size()==1&&store.run("claude-first").get().state==RunState::completed,"SQLite reopen must retain the original encrypted credential and terminal ownership");
        {AgentRunner runner(store,settings);runner.start("claude-reopened","claude-main","Continue Claude after SQLite reopen");require(runner.execute("claude-reopened").state==RunState::completed,"New actual native run must replay the complete persisted Claude tool conversation");}
        const auto after=store.history("claude-main").get();require(after.size()==7,"Reopened actual run must append only one prompt/answer");same_history(saved,std::vector<Message>(after.begin(),after.begin()+5));
        const auto answer=Json::parse(after.back().json);metrics(answer);require(answer.at("content")==reopened_answer&&answer.at("usage").at("input_tokens")==0&&answer.at("usage").at("output_tokens")==0&&!answer.at("usage").contains("cache_read_input_tokens")&&!answer.at("usage").contains("cache_creation_input_tokens"),"Supplied zero usage and absent caches must survive real history without estimates");
        const auto reopened_events=store.events("claude-reopened").get();normalized_events(reopened_events,answer.at("usage"));require(count(reopened_events,"tool.started")==0&&count(reopened_events,"tool.completed")==0,"Reloading history cannot re-execute earlier tools");private_absent(after,reopened_events);
        for(const auto* route:{"malformed","unknown","incomplete","late-error","truncated","unsigned"}){
            store.create_session(route,std::string("Synthetic Claude rejection ")+route).get();auto rejected=settings;rejected.provider.endpoint=endpoint+"/"+route;
            {AgentRunner runner(store,rejected);runner.start(route,route,std::string("Reject Claude ")+route);require(runner.execute(route).state==RunState::failed,"Malformed/unoffered/unfinished/late-error/truncated tool turn must fail actual agent");}
            no_effects(store,route,route,RunState::failed);const auto events=store.events(route).get();require(Json::parse(events.back().json).at("reason")=="model_protocol_error","Actual rejected provider must retain a protocol failure reason");
            if(std::string(route)!="truncated")require(count(events,"model.done")==0,"Unvalidated Claude turn must not acknowledge model completion");
        }
        store.create_session("cancel","Actual held Claude socket cancellation").get();auto cancel_settings=settings;cancel_settings.provider.endpoint=endpoint+"/cancel";
        {
            AgentRunner runner(store,cancel_settings);runner.start("claude-cancel","cancel","Hold actual Claude stream");std::stop_source cancellation;
            auto executing=std::async(std::launch::async,[&]{return runner.execute("claude-cancel",cancellation.get_token());});
            std::string signal;require(static_cast<bool>(std::getline(std::cin,signal))&&signal=="fixture-held","Independent socket peer must observe the held native request");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(count(store.events("claude-cancel").get(),"model.text")==0){require(std::chrono::steady_clock::now()<deadline,"Native agent must observe real streamed text before cancellation");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            cancellation.request_stop();require(executing.get().state==RunState::cancelled,"Actual held native transport must cancel");no_effects(store,"claude-cancel","cancel",RunState::cancelled);
        }
        const auto cancelled_history=store.history("cancel").get();
        {AgentRunner runner(store,cancel_settings);runner.start("claude-recovery","cancel","Recover Claude after cancellation");require(runner.execute("claude-recovery").state==RunState::completed,"Later actual run must recover with the owned credential after cancellation");}
        const auto recovered=store.history("cancel").get();require(recovered.size()==3&&Json::parse(recovered.back().json).at("content")==recovered_answer,"Cancelled partial text cannot persist as a fake assistant before recovery");same_history(cancelled_history,std::vector<Message>(recovered.begin(),recovered.begin()+1));metrics(Json::parse(recovered.back().json));normalized_events(store.events("claude-recovery").get(),Json::parse(recovered.back().json).at("usage"));
        store.create_session("rollback","Actual Claude tool-row rollback").get();auto rollback_settings=settings;rollback_settings.provider.endpoint=endpoint+"/rollback";
        // Reject the second tool row, after the assistant/first result/task link
        // have been inserted within the same native transaction.
        {XlangSqlite sql(argv[1],roots);sql.execute("CREATE TRIGGER reject_anthropic_tool BEFORE INSERT ON messages WHEN NEW.role='tool' AND NEW.payload LIKE '%toolu-rollback-right%' BEGIN SELECT RAISE(ABORT,'synthetic Claude tool-row transaction failure'); END");}
        {AgentRunner runner(store,rollback_settings);runner.start("claude-rollback","rollback","Rollback actual Claude tool history");require(runner.execute("claude-rollback").state==RunState::failed,"Real tool-row SQL failure must fail the actual agent");}
        {XlangSqlite sql(argv[1],roots);sql.execute("DROP TRIGGER reject_anthropic_tool");}
        const auto rollback_history=store.history("rollback").get(),rollback_run_history=store.run_history("claude-rollback").get();const auto rollback_events=store.events("claude-rollback").get();
        require(rollback_history.size()==1&&rollback_history[0].role=="user"&&rollback_run_history.size()==1,"SQL transaction rollback must leave no dangling assistant/tool/task rows");
        require(count(rollback_events,"tool.started")==2&&count(rollback_events,"tool.completed")==2&&count(rollback_events,"conversation.tool_turn")==0&&count(rollback_events,"run.completed")==0&&rollback_events.back().kind=="run.failed","Read effects occur before the failed atomic batch; conversation success must not commit");
        std::size_t index=0;for(const auto& event:rollback_events)if(event.kind=="tool.completed"){const auto data=Json::parse(event.json);require(data.at("call_id")==(index?"toolu-rollback-right":"toolu-rollback-left")&&data.at("data")==Json{{"path",index?"second.txt":"README.md"},{"content",index?second_file:first_file}},"Rollback fixture must still verify both actual native read results");++index;}
        require(Json::parse(rollback_events.back().json).at("reason")=="agent_error","SQL persistence failure must remain a native agent error");private_absent(rollback_history,rollback_events);
        same_history(after,store.history("claude-main").get());require(store.credentials("fixture").get().size()==1,"Failures/cancellation/rollback cannot rotate the owned encrypted provider key");store.close();
    }
    std::cout<<"Actual native Claude agent contract passed two filesystem reads, ordered text/tool/signed-redacted thinking receipts, exact escaped arguments, correlated provider IDs/results, encrypted xlang3 SQLite close/reopen and exact signed history replay without tool repetition, supplied uncached/cache/zero usage and timings, protocol/signature failure before effects, held-stream cancellation/recovery and actual SQL tool-row rollback. Provider socket replies/credentials/signatures are synthetic; no thinking request controls, cryptographic signature verification, live account or UI acceptance claimed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
