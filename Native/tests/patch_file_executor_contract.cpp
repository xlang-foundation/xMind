#include "agentflow/patch_file_executor.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;
using namespace std::chrono_literals;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected patch executor rejection");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-patch-journal-"+std::to_string(std::random_device{}()));
    Directory(){require(std::filesystem::create_directory(path),"Patch fixture must be newly created");}
    ~Directory(){if(path.parent_path()==parent&&path.filename().string().starts_with("xmind-patch-journal-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
std::int64_t expiry(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
Operation proposed(PersistenceService& store,const std::string& id){
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline){try{const auto operation=store.operation(id).get();require(operation.state==OperationState::awaiting_approval,"Patch must await explicit approval");return operation;}catch(const NotFound&){std::this_thread::sleep_for(5ms);}}
    throw std::runtime_error("Patch proposal timed out");
}
struct Task {
    std::stop_source stop;std::future<std::string> result;
    Task(PatchFileExecutor& executor,std::string batch,std::size_t index,std::size_t count,WorkspacePatchFilePlan plan,InstructionPrecondition guidance={}):
        result(std::async(std::launch::async,[this,&executor,batch=std::move(batch),index,count,plan=std::move(plan),guidance=std::move(guidance)]{return executor.execute(batch+"-"+std::to_string(index),"run",plan,{batch,index,count},expiry(),stop.get_token(),guidance);})){}
    ~Task(){stop.request_stop();if(result.valid())result.wait();}
};
struct StopFuture {std::stop_source stop;std::future<std::string> result;~StopFuture(){stop.request_stop();if(result.valid())result.wait();}};
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    Directory directory;const auto root=directory.path/"workspace";std::filesystem::create_directory(root);
    auto write=[&](const std::string& name,const std::string& content){std::ofstream stream(root/name,std::ios::binary|std::ios::trunc);stream.write(content.data(),content.size());stream.close();require(bool(stream),"Patch fixture write failed");};
    write("update.txt","before\n");write("move.txt","old move\n");write("delete.txt","remove me\n");write("fault.txt","before fault\n");write("immutable.txt","old\n");
    const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};WorkspaceTools workspace(root.string());
    const auto patch=parse_file_patch("*** Begin Patch\n*** Add File: added/deep/new.txt\n+added\n*** Update File: update.txt\n@@\n-before\n+after\n*** Update File: move.txt\n*** Move to: moved/deep/new.txt\n@@\n-old move\n+new move\n*** Delete File: delete.txt\n*** End Patch\n");
    const auto plan=workspace.plan_patch(patch,{},true);std::vector<std::string> receipts;
    {
        PersistenceService store(database,roots);PatchFileExecutor executor(store,workspace);
        store.create_session("session","native patch journal fixture").get();store.start_prompt_run("run","session",R"({"content":"native patch journal fixture; no model"})").get();store.transition("run",RunState::queued,RunState::running).get();
        rejects<std::invalid_argument>([&]{executor.proposal("wrong-id","run",plan.files[0],{"actual",0,4});});
        auto no_parent=std::get<WorkspaceEditPlan>(plan.files[1]);no_parent.parent_id.clear();rejects<std::invalid_argument>([&]{executor.proposal("unbound-0","run",no_parent,{"unbound",0,1});});
        {
            auto original=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Update File: immutable.txt\n@@\n-old\n+reviewed\n*** End Patch\n")).files[0];
            StopFuture task;task.result=std::async(std::launch::async,[&]{return executor.execute("immutable-0","run",original,{"immutable",0,1},expiry(),task.stop.get_token());});const auto reviewed=proposed(store,"immutable-0");
            original=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Update File: immutable.txt\n@@\n-old\n+unapproved caller replacement\n*** End Patch\n")).files[0];
            store.decide_operation("immutable-0",OperationDecision::allow,"fixture-controller").get();task.result.get();require(workspace.read_file("immutable.txt").content=="reviewed\n"&&Json::parse(reviewed.spec.arguments_json)["after_content"]=="reviewed\n","Caller changes must not replace the owned reviewed plan");
        }
        {
            Task denied(executor,"denied",0,1,plan.files[3]);proposed(store,"denied-0");store.decide_operation("denied-0",OperationDecision::deny,"fixture-controller").get();rejects<PermissionDenied>([&]{denied.result.get();});require(workspace.read_file("delete.txt").content=="remove me\n","Denied deletion must preserve actual bytes");
        }
        {
            Task cancelled(executor,"cancelled",0,1,plan.files[2]);proposed(store,"cancelled-0");cancelled.stop.request_stop();rejects<PermissionCancelled>([&]{cancelled.result.get();});require(workspace.read_file("move.txt").content=="old move\n","Cancelled move must not dispatch");
        }
        {
            InstructionPrecondition changed{R"({"version":1,"directory":".","sources":[]})",[](std::stop_token){throw ToolGuidanceChanged("Changed fixture guidance");}};
            Task task(executor,"guidance",0,1,plan.files[1],changed);proposed(store,"guidance-0");store.decide_operation("guidance-0",OperationDecision::allow,"fixture-controller").get();rejects<ToolGuidanceChanged>([&]{task.result.get();});require(store.operation("guidance-0").get().state==OperationState::failed&&workspace.read_file("update.txt").content=="before\n","Changed guidance must retire the claim before an effect");
        }
        {
            Task stale(executor,"stale",0,1,plan.files[1]);proposed(store,"stale-0");write("update.txt","changed after proposal\n");store.decide_operation("stale-0",OperationDecision::allow,"fixture-controller").get();rejects<ToolContentConflict>([&]{stale.result.get();});require(store.operation("stale-0").get().state==OperationState::failed&&workspace.read_file("update.txt").content=="changed after proposal\n","Stale patch must preserve externally changed bytes");write("update.txt","before\n");
        }
        for(std::size_t index=0;index<plan.files.size();++index){
            const auto id="actual-"+std::to_string(index);Task task(executor,"actual",index,4,plan.files[index]);const auto operation=proposed(store,id);const auto payload=Json::parse(operation.spec.arguments_json);
            require(operation.spec.tool=="patch_file"&&payload["patch_id"]=="actual"&&payload["patch_index"]==index&&payload["patch_file_count"]==4,"Proposal must retain native file/batch identity");
            if(index==2)require(payload["destination_path"]=="moved/deep/new.txt"&&payload["create_directories"].size()==2,"Move approval must disclose its destination and parents");
            require(task.result.wait_for(20ms)==std::future_status::timeout,"Waiting is not approval");store.decide_operation(id,OperationDecision::allow,"fixture-controller").get();receipts.push_back(task.result.get());const auto outcome=store.operation(id).get();
            require(outcome.state==OperationState::succeeded&&outcome.result_json==receipts.back()&&outcome.decision_actor=="fixture-controller","Actual patch outcome must be durably attributed");
        }
        require(workspace.read_file("added/deep/new.txt").content=="added\n"&&workspace.read_file("update.txt").content=="after\n"&&workspace.read_file("moved/deep/new.txt").content=="new move\n","Approved patch effects must change actual files");
        require(!std::filesystem::exists(root/"delete.txt")&&!std::filesystem::exists(root/"move.txt"),"Approved delete/move must remove actual old names");
        rejects<Conflict>([&]{executor.execute("actual-0","run",plan.files[0],{"actual",0,4},expiry());});
        const auto fault=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Update File: fault.txt\n@@\n-before fault\n+actual effect before journal fault\n*** End Patch\n")).files[0];
        // Deliberate failure injection in this disposable xlang3/SQLite fixture.
        // No product database is accessed or manually changed.
        {XlangSqlite inject(database,roots);inject.execute("CREATE TRIGGER reject_patch_success BEFORE UPDATE OF state ON operations WHEN NEW.id='fault-0' AND NEW.state='succeeded' BEGIN SELECT RAISE(ABORT,'fixture patch journal fault'); END");}
        {Task task(executor,"fault",0,1,fault);proposed(store,"fault-0");store.decide_operation("fault-0",OperationDecision::allow,"fixture-controller").get();rejects<EditOutcomeUnrecorded>([&]{task.result.get();});require(workspace.read_file("fault.txt").content=="actual effect before journal fault\n"&&store.operation("fault-0").get().state==OperationState::executing,"Lost outcome storage must retain the actual effect and executing claim");}
        store.close();
    }
    {
        PersistenceService reopened(database,roots);
        for(std::size_t index=0;index<receipts.size();++index){const auto operation=reopened.operation("actual-"+std::to_string(index)).get();require(operation.state==OperationState::succeeded&&operation.result_json==receipts[index],"Actual successful receipts must survive restart unchanged");}
        require(reopened.operation("fault-0").get().state==OperationState::uncertain&&reopened.run("run").get().state==RunState::failed,"Interrupted claim must be quarantined without inventing success");
        require(workspace.read_file("fault.txt").content=="actual effect before journal fault\n"&&workspace.read_file("moved/deep/new.txt").content=="new move\n"&&!std::filesystem::exists(root/"delete.txt"),"Restart must preserve actual effects without replay");
        const auto blocked=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: blocked.txt\n+must not dispatch\n*** End Patch\n")).files[0];
        reopened.start_prompt_run("blocked-run","session",R"({"content":"uncertainty fixture"})").get();reopened.transition("blocked-run",RunState::queued,RunState::running).get();PatchFileExecutor executor(reopened,workspace);
        StopFuture task;task.result=std::async(std::launch::async,[&]{return executor.execute("blocked-0","blocked-run",blocked,{"blocked",0,1},expiry(),task.stop.get_token());});proposed(reopened,"blocked-0");reopened.decide_operation("blocked-0",OperationDecision::allow,"fixture-controller").get();rejects<WorkspaceEffectUncertain>([&]{task.result.get();});require(!std::filesystem::exists(root/"blocked.txt"),"Quarantine must block further workspace effects after approval");
        reopened.close();
    }
    std::cout<<"Native patch per-file approval/journal/restart contract passed with actual disposable files and xlang3 SQLite; no model, client or batch tool acceptance\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
