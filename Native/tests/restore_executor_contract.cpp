#include "agentflow/restore_executor.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;using namespace std::chrono_literals;using Json=nlohmann::json;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Directory {std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-restore-"+std::to_string(std::random_device{}()));Directory(){require(std::filesystem::create_directory(path),"Fixture must be fresh");}~Directory(){if(path.parent_path()==parent&&path.filename().string().starts_with("xmind-restore-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}};
std::int64_t expiry(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
Operation proposal(PersistenceService& store,const std::string& id,std::future<std::string>& result){const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline){if(result.wait_for(0ms)==std::future_status::ready)throw std::runtime_error("Operation ended before approval: "+result.get());try{auto value=store.operation(id).get();require(value.state==OperationState::awaiting_approval,"Restoration must use the normal approval journal");return value;}catch(const NotFound&){std::this_thread::sleep_for(5ms);}}throw std::runtime_error("Approval proposal timed out");}
struct Async {std::stop_source stop;std::future<std::string> result;template<class F>explicit Async(F action):result(std::async(std::launch::async,[this,action=std::move(action)]()mutable{return action(stop.get_token());})){}~Async(){stop.request_stop();if(result.valid())result.wait();}};
void write(const std::filesystem::path& path,const std::string& value){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<value;file.close();require(bool(file),"Fixture write failed");}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
 Directory directory;const auto root=directory.path/"workspace";std::filesystem::create_directory(root);
 write(root/"update.txt","before update\n");write(root/"delete.txt","before delete\n");write(root/"move.txt","before move\n");
 const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};WorkspaceTools workspace(root.string());
 PersistenceService store(database,roots);PatchExecutor patcher(store,workspace);RestoreExecutor restorer(store,workspace);
 store.create_session("session","restore executor contract").get();store.start_prompt_run("run","session",R"({"content":"restore executor contract"})").get();store.transition("run",RunState::queued,RunState::running).get();
 auto create_original=[&](const std::string& patch_id,const std::string& patch_text){auto plan=workspace.plan_patch(parse_file_patch(patch_text),{},true);Async task([&,patch_id,plan=std::move(plan)](std::stop_token cancel)mutable{return patcher.execute(patch_id,"run",std::move(plan),expiry(),cancel);});const auto pending=proposal(store,patch_id+"-0",task.result);store.decide_operation(pending.id,OperationDecision::allow,"fixture-controller").get();const auto report=Json::parse(task.result.get());require(report.value("state",std::string{})=="succeeded","Original file change must succeed");return report["outcomes"][0]["operation_id"].get<std::string>();};
 auto restore=[&](const std::string& restore_id,const std::string& original_id,const std::string& expected_action){Async task([&,restore_id,original_id](std::stop_token cancel){return restorer.invoke(restore_id,"run",Json{{"operation_id",original_id}}.dump(),expiry(),cancel);});const auto pending=proposal(store,restore_id+"-0",task.result);const auto review=Json::parse(pending.spec.arguments_json);require(review.value("action",std::string{})==expected_action,"Restore proposal must contain the exact inverse action");store.decide_operation(pending.id,OperationDecision::allow,"fixture-controller").get();const auto report=Json::parse(task.result.get());require(report.value("state",std::string{})=="succeeded"&&report.value("restored_operation_id",std::string{})==original_id,"Restore report must identify the source and actual inverse result");};
 const auto update=create_original("original-update","*** Begin Patch\n*** Update File: update.txt\n@@\n-before update\n+after update\n*** End Patch\n");require(read(root/"update.txt")=="after update\n","Original update must apply");restore("a001",update,"update");require(read(root/"update.txt")=="before update\n","Approved update restore must restore exact original bytes");
 const auto addition=create_original("original-add","*** Begin Patch\n*** Add File: added.txt\n+created bytes\n*** End Patch\n");require(read(root/"added.txt")=="created bytes\n","Original creation must apply");restore("a002",addition,"delete");require(!std::filesystem::exists(root/"added.txt"),"Approved creation restore must remove only the created file");
 const auto deletion=create_original("original-delete","*** Begin Patch\n*** Delete File: delete.txt\n*** End Patch\n");require(!std::filesystem::exists(root/"delete.txt"),"Original deletion must apply");restore("a003",deletion,"add");require(read(root/"delete.txt")=="before delete\n","Approved deletion restore must recreate the saved bytes");
 const auto move=create_original("original-move","*** Begin Patch\n*** Update File: move.txt\n*** Move to: moved.txt\n@@\n-before move\n+after move\n*** End Patch\n");require(!std::filesystem::exists(root/"move.txt")&&read(root/"moved.txt")=="after move\n","Original move must apply");restore("a004",move,"move");require(read(root/"move.txt")=="before move\n"&&!std::filesystem::exists(root/"moved.txt"),"Approved move restore must return the file and exact saved bytes");
 const auto stale=create_original("original-stale","*** Begin Patch\n*** Update File: update.txt\n@@\n-before update\n+stale target\n*** End Patch\n");write(root/"update.txt","external change\n");
 try{(void)restorer.invoke("a005","run",Json{{"operation_id",stale}}.dump(),expiry());throw std::runtime_error("Changed file must not receive a restore proposal");}catch(const ToolContentConflict&){}try{(void)store.operation("a005-0").get();throw std::runtime_error("Stale restore must not create a journal operation");}catch(const NotFound&){}
 store.close();std::cout<<"Restore executor contract passed for update/add/delete/move with exact bytes, durable approval and stale-state rejection\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
