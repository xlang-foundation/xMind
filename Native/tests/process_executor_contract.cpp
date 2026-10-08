#include "agentflow/process_executor.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;using Json=nlohmann::json;
namespace {
void require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
template<class E,class F>void rejects(F f){try{f();}catch(const E&){return;}throw std::runtime_error("Expected process executor rejection did not occur");}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string file(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
std::string unhex(const std::string& value) {
    require(value.size()%2==0,"Journal hex must preserve whole bytes");std::string bytes;
    for(std::size_t i=0;i<value.size();i+=2)bytes.push_back(static_cast<char>(std::stoul(value.substr(i,2),nullptr,16)));
    return bytes;
}
std::pair<std::string,std::string> output(PersistenceService& store,const std::string& operation,const std::string& run="run") {
    std::string channels[2];std::size_t events=0;
    for(const auto& event:store.events(run).get())if(event.kind=="process.output") {
        const auto value=Json::parse(event.json);if(value["operation_id"]!=operation)continue;
        require(value["encoding"]=="hex" && (value["channel"]=="stdout" || value["channel"]=="stderr"),"Journal must identify raw bytes and their channel");
        auto& bytes=channels[value["channel"]=="stderr"?1:0];const auto chunk=unhex(value["data"].get<std::string>());
        require(value["offset"]==bytes.size() && value["retained_bytes"]==chunk.size() && !chunk.empty() && chunk.size()<=4096,"Journal offsets and chunks must be contiguous and bounded");bytes+=chunk;++events;
    }
    require(events<=82 && channels[0].size()+channels[1].size()<=65536,"One operation must bound durable retained output and event count");
    return {channels[0],channels[1]};
}
struct Task {
    std::stop_source stop;std::future<std::string> result;
    Task(ProcessExecutor& executor,std::string id,std::string args,std::string run="run",std::int64_t expiry=0)
        :result(std::async(std::launch::async,[this,&executor,id=std::move(id),args=std::move(args),run=std::move(run),expiry]{return executor.invoke(id,run,args,expiry?expiry:now()+600000,stop.get_token());})) {}
    ~Task(){stop.request_stop();if(result.valid())result.wait();}
};
Operation proposed(PersistenceService& store,const std::string& id) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline){try{const auto p=store.operation(id).get();require(p.state==OperationState::awaiting_approval,"Command must await actual approval");return p;}catch(const NotFound&){std::this_thread::sleep_for(10ms);}}
    throw std::runtime_error("Process approval proposal deadline");
}
std::string args(const std::string& mode,const std::string& marker,const std::string& workdir=".") {
    return Json{{"profile","fixture"},{"arguments",Json::array({mode,marker})},{"workdir",workdir}}.dump();
}
void owner(PersistenceService& store,const std::string& run="run",const std::string& session="session"){
    store.create_session(session,"Actual approved process contract").get();store.start_prompt_run(run,session,R"({"content":"actual native process fixture"})").get();store.transition(run,RunState::queued,RunState::running).get();
}
}
int main(int argc,char** argv){if(argc!=6)return 2;try{
    const auto started=std::chrono::steady_clock::now();
    const auto phase=[&](const char* name){std::cerr<<"[process-contract] phase="<<name<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<'\n'<<std::flush;};
    phase("workspace-setup");
    const auto root=std::filesystem::u8path(argv[3]);const std::vector<std::string> imports{argv[4],argv[5]};
    const std::vector<ProcessProfile> profiles{{"fixture",argv[1],1,{argv[2]},10s}};WorkspaceTools workspace(root.string());
    std::filesystem::create_directory(root/"sub");const auto database=(root.parent_path()/"process-state.sqlite").string();
    {
        phase("repository-open");
        PersistenceService store(database,imports);owner(store);ProcessExecutor executor(store,workspace,root.string(),profiles);
        phase("request-validation");
        const auto definition=Json::parse(executor.definition().input_schema_json);require(definition["properties"]["profile"]["enum"]==Json::array({"fixture"}),"Model schema must expose registered IDs only");
        for(const auto source:{R"({"profile":"unknown","arguments":[]})",R"({"profile":"fixture","arguments":[],"executable":"spoof"})",R"({"profile":"fixture","arguments":[],"environment":{"SECRET":"spoof"}})",R"({"profile":"fixture","profile":"fixture","arguments":[]})",R"({"profile":"fixture","arguments":[],"timeout_ms":10001})"})
            rejects<std::invalid_argument>([&]{executor.invoke("invalid","run",source,now()+600000);});
        rejects<ToolAccessDenied>([&]{executor.invoke("invalid","run",args("normal","escape.txt",".."),now()+600000);});rejects<NotFound>([&]{store.operation("invalid").get();});
        phase("approval-denial");
        Task denied(executor,"denied",args("normal","denied.txt"));proposed(store,"denied");store.decide_operation("denied",OperationDecision::deny,"actual-fixture-controller").get();rejects<PermissionDenied>([&]{denied.result.get();});require(!std::filesystem::exists(root/"denied.txt"),"Denied command must not run effect");
        phase("approval-cancellation");
        Task cancelled(executor,"cancelled",args("normal","cancelled.txt"));proposed(store,"cancelled");cancelled.stop.request_stop();rejects<PermissionCancelled>([&]{cancelled.result.get();});require(!std::filesystem::exists(root/"cancelled.txt"),"Cancelled approval must not run effect");
        phase("approval-expiry");
        Task expired(executor,"expired",args("normal","expired.txt"),"run",now()+1000);proposed(store,"expired");rejects<PermissionExpired>([&]{expired.result.get();});require(!std::filesystem::exists(root/"expired.txt") && store.operation("expired").get().state==OperationState::expired,"Actual expiry must stop dispatch");
        phase("stale-directory");
        Task stale(executor,"stale",args("normal","stale.txt","sub"));proposed(store,"stale");std::filesystem::rename(root/"sub",root/"old-sub");std::filesystem::create_directory(root/"sub");store.decide_operation("stale",OperationDecision::allow,"actual-fixture-controller").get();rejects<ProcessBeforeDispatchError>([&]{stale.result.get();});require(store.operation("stale").get().state==OperationState::failed && !std::filesystem::exists(root/"sub/stale.txt") && !std::filesystem::exists(root/"old-sub/stale.txt"),"Directory replacement after approval must preserve no-dispatch outcome");
        phase("approved-dispatch");
        Task allowed(executor,"allowed",args("normal","allowed.txt","sub"));const auto proposal=proposed(store,"allowed");const auto payload=Json::parse(proposal.spec.arguments_json);
        require(payload["profile_id"]=="fixture" && payload["profile_revision"]==1 && payload["executable"]==argv[1] && payload["arguments"]==Json::array({argv[2],"normal","allowed.txt"}) && payload["workdir"]=="sub" && !payload["directory_id"].get<std::string>().empty() && proposal.spec.resources==std::vector<std::string>{"process-profile:fixture"},"Proposal must retain exact command, identity and stable profile resource");
        require(!std::filesystem::exists(root/"sub/allowed.txt"),"Awaiting command must not execute");store.decide_operation("allowed",OperationDecision::allow,"actual-fixture-controller").get();const auto result=Json::parse(allowed.result.get());
        require(result["exit_code"]==0 && result["termination"]=="exited" && result["process_tree_retired"]==true && result["independently_verified"]==false && file(root/"sub/allowed.txt")=="actual child effect","Approved command must return actual result without claiming independent effect verification");
        require(Json::parse(result["stdout"]["data"].get<std::string>())["inherited"]==false && store.operation("allowed").get().state==OperationState::succeeded,"Actual output and successful journal required");
        require(output(store,"allowed")==std::pair{result["stdout"]["data"].get<std::string>(),result["stderr"]["data"].get<std::string>()},"Durable channels must equal actual observed output bytes");
        rejects<Conflict>([&]{executor.invoke("allowed","run",args("normal","duplicate.txt"),now()+600000);});require(!std::filesystem::exists(root/"duplicate.txt"),"Duplicate operation must not dispatch again");
        phase("binary-output");
        Task binary(executor,"binary",args("bytes","unused.txt"));proposed(store,"binary");store.decide_operation("binary",OperationDecision::allow,"actual-fixture-controller").get();const auto bytes=Json::parse(binary.result.get());
        require(bytes["exit_code"]==7 && bytes["stdout"]["encoding"]=="hex" && bytes["stdout"]["data"]=="ff00fe0a" && bytes["stderr"]["data"]=="800d00" && store.operation("binary").get().state==OperationState::succeeded,"Nonzero exit and invalid raw bytes must be explicitly encoded and journalled");
        require(output(store,"binary")==std::pair{std::string("\xff\0\xfe\n",4),std::string("\x80\r\0",3)},"Durable replay must preserve invalid UTF-8 and NUL bytes");
        phase("streamed-output");
        Task streamed(executor,"streamed",args("stream-wait","streamed.txt"));proposed(store,"streamed");store.decide_operation("streamed",OperationDecision::allow,"actual-fixture-controller").get();
        const auto streaming_deadline=std::chrono::steady_clock::now()+5s;
        while(output(store,"streamed").first.empty() && std::chrono::steady_clock::now()<streaming_deadline)std::this_thread::sleep_for(10ms);
        require(output(store,"streamed").first=="Actual streamed output 🌍\n" && streamed.result.wait_for(0ms)==std::future_status::timeout && store.operation("streamed").get().state==OperationState::executing,"Actual output must be journalled while the child remains running");
        {std::ofstream acknowledge(root/"streamed.txt.ack");acknowledge<<"actual output observed before exit";require(bool(acknowledge),"Actual fixture acknowledgement must be written");}
        const auto streamed_result=Json::parse(streamed.result.get());require(output(store,"streamed").first==streamed_result["stdout"]["data"].get<std::string>(),"Final buffered output must be flushed before result acknowledgement");
        phase("output-flood");
        Task flood(executor,"flood",args("flood","flood.txt"));proposed(store,"flood");store.decide_operation("flood",OperationDecision::allow,"actual-fixture-controller").get();const auto flooded=Json::parse(flood.result.get());const auto retained=output(store,"flood");
        require(flooded["stdout"]["byte_count"]==512*1024 && flooded["stderr"]["byte_count"]==512*1024 && flooded["truncated"]==true && retained.first.size()+retained.second.size()==65536,"Actual high-volume output must drain beyond the bounded durable capture");
        require(retained==std::pair{flooded["stdout"]["data"].get<std::string>(),flooded["stderr"]["data"].get<std::string>()},"Truncated replay must match the exact retained result channels");
        {
            phase("changed-executable");
            const auto copy=root/"changed-node.exe";std::filesystem::copy_file(std::filesystem::u8path(argv[1]),copy);
            ProcessExecutor bound(store,workspace,root.string(),{{"changed-fixture",copy.string(),1,{argv[2]},10s}});
            auto request=Json::parse(args("normal","changed-executable-effect.txt"));request["profile"]="changed-fixture";
            Task changed(bound,"changed-executable",request.dump());const auto pending=proposed(store,"changed-executable");require(!Json::parse(pending.spec.arguments_json)["executable_id"].get<std::string>().empty(),"Executable binding must be included in the reviewed proposal");
            {std::ofstream mutation(copy,std::ios::binary|std::ios::app);mutation<<"actual fixture binary mutation";require(bool(mutation),"Actual binary mutation must succeed while approval is pending");}
            store.decide_operation("changed-executable",OperationDecision::allow,"actual-fixture-controller").get();rejects<ProcessBeforeDispatchError>([&]{changed.result.get();});require(store.operation("changed-executable").get().state==OperationState::failed && !std::filesystem::exists(root/"changed-executable-effect.txt"),"Changed actual executable must not use an earlier grant");
        }
        phase("result-journal-fault");
        store.transition("run",RunState::running,RunState::completed).get();owner(store,"fault-run","fault-session");
        {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_process_success BEFORE UPDATE OF state ON operations WHEN NEW.id='storage-fault' AND NEW.state='succeeded' BEGIN SELECT RAISE(ABORT,'actual fixture process journal fault'); END");}
        Task fault(executor,"storage-fault",args("normal","fault.txt"),"fault-run");proposed(store,"storage-fault");store.decide_operation("storage-fault",OperationDecision::allow,"actual-fixture-controller").get();rejects<ProcessOutcomeUnrecorded>([&]{fault.result.get();});require(file(root/"fault.txt")=="actual child effect" && store.operation("storage-fault").get().state==OperationState::executing,"Actual effect must survive unrecorded result with claim retained");phase("result-fault-close");store.close();
    }
    {
        phase("restart-recovery");
        PersistenceService reopened(database,imports);require(reopened.operation("storage-fault").get().state==OperationState::uncertain && reopened.run("fault-run").get().state==RunState::failed && file(root/"fault.txt")=="actual child effect","Restart must quarantine effect and never replay/erase it");
        require(output(reopened,"binary")==std::pair{std::string("\xff\0\xfe\n",4),std::string("\x80\r\0",3)},"Output journal must survive actual SQLite close/reopen without byte loss");
        phase("cross-workspace-quarantine");
        const auto other=root.parent_path()/"other-workspace";std::filesystem::create_directory(other);WorkspaceTools second(other.string());ProcessExecutor executor(reopened,second,other.string(),profiles);owner(reopened,"blocked-run","blocked-session");
        Task blocked(executor,"blocked",args("normal","blocked.txt"),"blocked-run");proposed(reopened,"blocked");reopened.decide_operation("blocked",OperationDecision::allow,"actual-fixture-controller").get();rejects<WorkspaceEffectUncertain>([&]{blocked.result.get();});require(!std::filesystem::exists(other/"blocked.txt"),"Stable profile resource must quarantine another workspace after uncertain dispatch");phase("recovery-close");reopened.close();
    }
    {
        phase("output-journal-fault");
        const auto directory=root.parent_path()/"output-fault";std::filesystem::create_directory(directory);const auto db=(root.parent_path()/"output-fault.sqlite").string();
        WorkspaceTools target(directory.string());PersistenceService store(db,imports);owner(store);ProcessExecutor executor(store,target,directory.string(),profiles);
        {XlangSqlite inject(db,imports);inject.execute("CREATE TRIGGER reject_process_output BEFORE INSERT ON events WHEN NEW.kind='process.output' BEGIN SELECT RAISE(ABORT,'actual fixture output journal fault'); END");}
        Task fault(executor,"output-fault",args("normal","effect.txt"));proposed(store,"output-fault");store.decide_operation("output-fault",OperationDecision::allow,"actual-fixture-controller").get();rejects<ProcessEffectUncertain>([&]{fault.result.get();});
        require(file(directory/"effect.txt")=="actual child effect" && store.operation("output-fault").get().state==OperationState::uncertain && output(store,"output-fault").first.empty(),"A real failed output journal must preserve actual effects and quarantine without fabricated output");phase("output-fault-close");store.close();
    }
    for(const auto mode:{"timeout","cancel"}) {
        phase(mode);
        const auto directory=root.parent_path()/mode;std::filesystem::create_directory(directory);WorkspaceTools workspace2(directory.string());PersistenceService store((root.parent_path()/(std::string(mode)+".sqlite")).string(),imports);owner(store);ProcessExecutor executor(store,workspace2,directory.string(),profiles);
        auto request=Json::parse(args(std::string(mode)=="timeout"?"tree":"cancel-effect","effect.txt"));if(std::string(mode)=="timeout")request["timeout_ms"]=1500;
        Task effect(executor,"interrupted",request.dump());proposed(store,"interrupted");store.decide_operation("interrupted",OperationDecision::allow,"actual-fixture-controller").get();
        if(std::string(mode)=="cancel"){const auto until=std::chrono::steady_clock::now()+5s;while(file(directory/"effect.txt")!="actual effect before cancellation" && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(10ms);require(file(directory/"effect.txt")=="actual effect before cancellation","Observe actual dispatch before cancelling");effect.stop.request_stop();}
        rejects<ProcessEffectUncertain>([&]{effect.result.get();});const auto operation=store.operation("interrupted").get();const auto result=Json::parse(operation.result_json);
        if(!(operation.state==OperationState::uncertain && result["termination"]==(std::string(mode)=="timeout"?"timed_out":"cancelled") && result["process_tree_retired"]==true && result["independently_verified"]==false))throw std::runtime_error("Interrupted process lifecycle evidence failed: state="+to_string(operation.state)+" result="+operation.result_json);
        if(std::string(mode)=="cancel")require(file(directory/"effect.txt")=="actual effect before cancellation","Cancellation must not claim rollback or remove effect");phase("interruption-close");store.close();
    }
    phase("complete");
    std::cout<<"Native process executor passed actual approved child effects, deny/cancel/expiry, exact profiles/arguments/workdir, stale directory, explicit binary output/nonzero exit, duplicate rejection, real result-storage fault, uncertain restart and cross-workspace profile quarantine, timeout and post-effect cancellation. Model/server/editor integration remains separate.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
