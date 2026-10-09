#include "agentflow/patch_executor.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <random>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;using Json=nlohmann::json;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected batch rejection");}
struct Directory {std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-patch-batch-"+std::to_string(std::random_device{}()));Directory(){require(std::filesystem::create_directory(path),"Fixture must be fresh");}~Directory(){if(path.parent_path()==parent&&path.filename().string().starts_with("xmind-patch-batch-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}};
std::int64_t expiry(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
Operation proposed(PersistenceService& store,const std::string& id){const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline){try{const auto op=store.operation(id).get();require(op.state==OperationState::awaiting_approval,"File must wait for approval");return op;}catch(const NotFound&){std::this_thread::sleep_for(5ms);}}throw std::runtime_error("Proposal timed out");}
struct Task {std::stop_source stop;std::future<std::string> result;Task(PatchExecutor& executor,std::string id,WorkspacePatchPlan plan,std::vector<InstructionPrecondition> guidance={}):result(std::async(std::launch::async,[this,&executor,id=std::move(id),plan=std::move(plan),guidance=std::move(guidance)]{return executor.execute(id,"run",plan,expiry(),stop.get_token(),guidance);})){}~Task(){stop.request_stop();if(result.valid())result.wait();}};
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
 Directory dir;const auto root=dir.path/"workspace";std::filesystem::create_directory(root);
 auto write=[&](const std::string& name,const std::string& text){std::ofstream out(root/name,std::ios::binary|std::ios::trunc);out<<text;out.close();require(bool(out),"Fixture write failed");};
 auto read=[&](const std::string& name){std::ifstream in(root/name,std::ios::binary);require(bool(in),"Actual output file missing");return std::string(std::istreambuf_iterator<char>(in),{});};
 write("update.txt","old\n");write("move.txt","old move\n");write("delete.txt","remove\n");
 const auto database=(dir.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};WorkspaceTools workspace(root.string());
 std::string full_report,partial_report;
 {
  PersistenceService store(database,roots);PatchExecutor executor(store,workspace);store.create_session("session","native patch batch fixture").get();store.start_prompt_run("run","session",R"({"content":"native patch batch fixture; no model"})").get();store.transition("run",RunState::queued,RunState::running).get();
  const auto full=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: shared/a.txt\n+first\n*** Add File: Shared/deep/b.txt\n+second\n*** Update File: move.txt\n*** Move to: shared/c.txt\n@@\n-old move\n+new move\n*** Update File: update.txt\n@@\n-old\n+new\n*** Delete File: delete.txt\n*** End Patch\n"),{},true);
  std::vector<InstructionPrecondition> invalid(5);invalid[4].metadata_json="{}";rejects<std::invalid_argument>([&]{executor.execute("invalid","run",full,expiry(),{},invalid);});rejects<NotFound>([&]{store.operation("invalid-0").get();});require(!std::filesystem::exists(root/"shared"),"All late guidance must validate before the first effect");
  {
   Task task(executor,"full",full);
   for(std::size_t index=0;index<5;++index){const auto id="full-"+std::to_string(index);const auto op=proposed(store,id);const auto payload=Json::parse(op.spec.arguments_json);require(payload["patch_files"].size()==5&&payload["patch_files"][index]["operation_id"]==id,"Every proposal must disclose the requested batch");if(index==1)require(payload["create_directories"]==Json::array({"Shared/deep"}),"Only the still-missing parent must be approved for the sibling file");store.decide_operation(id,OperationDecision::allow,"fixture-controller").get();}
   full_report=task.result.get();const auto report=Json::parse(full_report);require(report["state"]=="succeeded"&&report["atomic"]==false&&report["succeeded_files"]==5&&report["outcomes"].size()==5&&report["not_requested"].empty(),"Complete report must reflect all actual journals");
  }
  require(read("shared/a.txt")=="first\n"&&read("Shared/deep/b.txt")=="second\n"&&read("shared/c.txt")=="new move\n"&&read("update.txt")=="new\n"&&!std::filesystem::exists(root/"move.txt")&&!std::filesystem::exists(root/"delete.txt"),"Batch must apply actual shared-parent add/move/update/delete effects");
  rejects<Conflict>([&]{executor.execute("full","run",full,expiry());});
  const auto partial=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: partial/a.txt\n+kept first effect\n*** Add File: partial/b.txt\n+denied second effect\n*** Add File: partial/c.txt\n+never requested\n*** End Patch\n"),{},true);
  {
   Task task(executor,"partial",partial);proposed(store,"partial-0");store.decide_operation("partial-0",OperationDecision::allow,"fixture-controller").get();proposed(store,"partial-1");store.decide_operation("partial-1",OperationDecision::deny,"fixture-controller").get();partial_report=task.result.get();const auto report=Json::parse(partial_report);
   require(report["state"]=="partial"&&report["succeeded_files"]==1&&report["stop_reason"]=="permission_denied"&&report["outcomes"].size()==2&&report["outcomes"][1]["state"]=="denied"&&report["not_requested"].size()==1,"Denial must retain the actual earlier success and absence of a third request");
  }
  require(read("partial/a.txt")=="kept first effect\n"&&!std::filesystem::exists(root/"partial/b.txt")&&!std::filesystem::exists(root/"partial/c.txt"),"Partial report must match actual files");rejects<NotFound>([&]{store.operation("partial-2").get();});
  const auto stale=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: stale-first.txt\n+kept\n*** Update File: update.txt\n@@\n-new\n+requested later\n*** Add File: stale-last.txt\n+not requested\n*** End Patch\n"));
  {
   Task task(executor,"stale",stale);proposed(store,"stale-0");store.decide_operation("stale-0",OperationDecision::allow,"fixture-controller").get();proposed(store,"stale-1");write("update.txt","changed after batch planning\n");store.decide_operation("stale-1",OperationDecision::allow,"fixture-controller").get();const auto report=Json::parse(task.result.get());require(report["state"]=="partial"&&report["stop_reason"]=="content_conflict"&&report["outcomes"][1]["state"]=="failed"&&report["not_requested"].size()==1,"Stale later file must stop without concealing an earlier effect");
  }
  require(read("update.txt")=="changed after batch planning\n"&&read("stale-first.txt")=="kept\n"&&!std::filesystem::exists(root/"stale-last.txt"),"Stale failure must preserve actual external and earlier bytes");
  const auto cancelled=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: cancel/a.txt\n+kept\n*** Add File: cancel/b.txt\n+cancelled\n*** Add File: cancel/c.txt\n+not requested\n*** End Patch\n"),{},true);
  {Task task(executor,"cancel",cancelled);proposed(store,"cancel-0");store.decide_operation("cancel-0",OperationDecision::allow,"fixture-controller").get();proposed(store,"cancel-1");task.stop.request_stop();const auto report=Json::parse(task.result.get());require(report["state"]=="partial"&&report["succeeded_files"]==1&&report["stop_reason"]=="cancelled"&&report["outcomes"][1]["state"]=="cancelled"&&report["not_requested"].size()==1,"Cancellation must retain earlier recorded success and unused requests");}
  require(read("cancel/a.txt")=="kept\n"&&!std::filesystem::exists(root/"cancel/b.txt")&&!std::filesystem::exists(root/"cancel/c.txt"),"Cancellation report must match actual files");
  const auto replaced=workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: replaced/a.txt\n+kept in original directory\n*** Add File: replaced/b.txt\n+must not enter replacement directory\n*** End Patch\n"),{},true);
  {Task task(executor,"replaced",replaced);proposed(store,"replaced-0");store.decide_operation("replaced-0",OperationDecision::allow,"fixture-controller").get();proposed(store,"replaced-1");std::filesystem::rename(root/"replaced",root/"original-replaced");std::filesystem::create_directory(root/"replaced");store.decide_operation("replaced-1",OperationDecision::allow,"fixture-controller").get();const auto report=Json::parse(task.result.get());require(report["state"]=="partial"&&report["succeeded_files"]==1&&report["stop_reason"]=="content_conflict"&&report["outcomes"][1]["state"]=="failed","An externally replaced patch-owned directory must fail the later effect");}
  require(read("original-replaced/a.txt")=="kept in original directory\n"&&!std::filesystem::exists(root/"replaced/b.txt"),"Replacement directory must not receive an unapproved rebound effect");
  store.close();
 }
 {
  PersistenceService reopened(database,roots);require(reopened.run("run").get().state==RunState::failed,"Interrupted fixture owner must recover without replay");
  const auto recorded_full=Json::parse(full_report);for(const auto& outcome:recorded_full["outcomes"]){const auto op=reopened.operation(outcome["operation_id"].get<std::string>()).get();require(op.state==OperationState::succeeded&&Json::parse(op.result_json)==outcome["result"],"Exact batch receipts must persist across xlang3 SQLite restart");}
  require(reopened.operation("partial-0").get().state==OperationState::succeeded&&reopened.operation("partial-1").get().state==OperationState::denied,"Partial outcomes must survive unchanged");rejects<NotFound>([&]{reopened.operation("partial-2").get();});require(read("partial/a.txt")=="kept first effect\n"&&!std::filesystem::exists(root/"partial/c.txt"),"Restart must not replay denied or unrequested files");reopened.close();
 }
 std::cout<<"Native multi-file patch coordination contract passed with actual files, independent reads and xlang3 SQLite; no model/client acceptance\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
