// Actual native files, permission journal and xlang3 SQLite. Direct controller
// decisions and repository lifecycle below are labeled synthetic test inputs.
#include "agentflow/patch_tool.hpp"
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
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected patch tool rejection");}
struct Directory {std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-patch-tool-"+std::to_string(std::random_device{}()));Directory(){need(std::filesystem::create_directory(path),"Owned fixture must be fresh");}~Directory(){if(path.parent_path()==parent&&path.filename().string().starts_with("xmind-patch-tool-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}};
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;file.close();need(bool(file),"Fixture write failed");}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);need(bool(file),"Actual file missing");return {std::istreambuf_iterator<char>(file),{}};}
std::int64_t expiry(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
Operation pending(PersistenceService& store,const std::string& id){const auto deadline=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<deadline){try{auto result=store.operation(id).get();need(result.state==OperationState::awaiting_approval,"Actual file must await approval");return result;}catch(const NotFound&){std::this_thread::sleep_for(5ms);}}throw std::runtime_error("Patch tool proposal deadline");}
struct Task {std::stop_source stop;std::future<std::string> result;Task(PatchTool& tool,std::string id,PreparedPatch plan):result(std::async(std::launch::async,[this,&tool,id=std::move(id),plan=std::move(plan)]()mutable{return tool.execute(id,"run",std::move(plan),expiry(),stop.get_token());})){}~Task(){stop.request_stop();if(result.valid())result.wait();}};
std::string args(const std::string& patch,bool parents=false){return Json{{"patch_text",patch},{"create_parents",parents}}.dump();}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
 Directory owned;const auto root=owned.path/"workspace";std::filesystem::create_directory(root);std::filesystem::create_directory(root/"src");std::filesystem::create_directory(root/"dest");
 write(root/"AGENTS.md","Synthetic root guidance.\n");write(root/"src/AGENTS.md","Synthetic source guidance.\n");write(root/"dest/AGENTS.md","Synthetic destination guidance.\n");write(root/"src/source.txt","before\n");
 const auto database=(owned.path/"state.sqlite").string();std::string saved_guidance_report;
 {
  PersistenceService store(database,{argv[1],argv[2]});store.create_session("session","Synthetic native patch model adapter fixture").get();store.start_prompt_run("run","session",R"({"content":"Synthetic native patch tool fixture; no provider invocation"})").get();store.transition("run",RunState::queued,RunState::running).get();
  WorkspaceTools workspace(root.string());RepositoryInstructionContext context(workspace,workspace.repository_instructions("."));PatchTool tool(store,workspace,context);
  (void)context.prepare();const auto move=args("*** Begin Patch\n*** Update File: src/source.txt\n*** Move to: dest/moved.txt\n@@\n-before\n+after\n*** End Patch\n");
  need(!tool.prepare(move),"Both undelivered move scopes must require another model request");need(store.operations("run").get().empty()&&read(root/"src/source.txt")=="before\n"&&!std::filesystem::exists(root/"dest/moved.txt"),"Guidance discovery cannot propose or perform effects");
  const auto supplied=context.prepare();need(supplied.find("Synthetic source guidance.")!=std::string::npos&&supplied.find("Synthetic destination guidance.")!=std::string::npos,"Both actual move scopes must reach supplied model context");
  auto prepared=tool.prepare(move);need(prepared&&prepared->guidance.size()==1,"Delivered move must prepare exactly one bound proposal");const auto metadata=Json::parse(prepared->guidance[0].metadata_json);need(metadata.at("scope_directories")==Json::array({"src","dest"})&&metadata.at("sources").size()==3,"Move approval must bind both scopes and deduplicate common root guidance");
  write(root/"dest/AGENTS.md","Synthetic changed destination guidance.\n");rejects<ToolGuidanceChanged>([&]{prepared->guidance[0].verify({});});
  const auto refused=Json::parse(tool.execute("changed-before-review","run",std::move(*prepared),expiry()));need(refused["state"]=="not_applied"&&refused["stop_reason"]=="guidance_changed"&&refused["not_requested"].size()==1,"Changed destination scope must retire batch before any operation");rejects<NotFound>([&]{store.operation("changed-before-review-0").get();});
  (void)context.prepare();prepared=tool.prepare(move);need(prepared.has_value(),"New destination guidance must prepare again");
  {Task task(tool,"changed-after-review",std::move(*prepared));const auto proposal=pending(store,"changed-after-review-0");const auto bound=Json::parse(proposal.spec.arguments_json);need(bound["repository_guidance"]["scope_directories"].size()==2,"Actual durable proposal must expose both scopes");write(root/"dest/AGENTS.md","Synthetic changed while awaiting approval.\n");store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();const auto report=Json::parse(task.result.get());need(report["state"]=="not_applied"&&report["stop_reason"]=="guidance_changed"&&store.operation(proposal.id).get().state==OperationState::failed,"Changed guidance after approval must retire claimed file before effects");}
  need(read(root/"src/source.txt")=="before\n"&&!std::filesystem::exists(root/"dest/moved.txt"),"Both guidance failures must preserve the actual move source");
  (void)context.prepare();
  const auto shared=args("*** Begin Patch\n*** Add File: shared/first.txt\n+first\n*** Add File: shared/second.txt\n+second\n*** End Patch\n",true);prepared=tool.prepare(shared);need(prepared.has_value(),"Missing folders with delivered root guidance must prepare");
  {Task task(tool,"shared-parents",std::move(*prepared));for(int index=0;index<2;++index){const auto proposal=pending(store,"shared-parents-"+std::to_string(index));const auto bound=Json::parse(proposal.spec.arguments_json);need(bound["repository_guidance"]["directory"]==(index?"shared":"."),"Later file must disclose refreshed actual guidance directory");if(index)need(bound["create_directories"].empty(),"Owned existing folder must not be claimed as newly created twice");store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();}need(Json::parse(task.result.get())["state"]=="succeeded","Both actual shared-parent creations must complete");}
  need(read(root/"shared/first.txt")=="first\n"&&read(root/"shared/second.txt")=="second\n","Actual sibling files must match approved snapshots");
  const auto own_guide=args("*** Begin Patch\n*** Add File: new-scope/AGENTS.md\n+Synthetic newly approved scope guidance.\n*** Add File: new-scope/code.txt\n+code\n*** End Patch\n",true);prepared=tool.prepare(own_guide);need(prepared.has_value(),"Root-delivered patch must prepare new scoped guide");
  {Task task(tool,"new-guide",std::move(*prepared));const auto proposal=pending(store,"new-guide-0");store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();saved_guidance_report=task.result.get();const auto report=Json::parse(saved_guidance_report);need(report["state"]=="partial"&&report["succeeded_files"]==1&&report["stop_reason"]=="guidance_changed"&&report["not_requested"].size()==1,"Own guide change must stop before later proposal and preserve actual first effect");rejects<NotFound>([&]{store.operation("new-guide-1").get();});}
  need(read(root/"new-scope/AGENTS.md")=="Synthetic newly approved scope guidance.\n"&&!std::filesystem::exists(root/"new-scope/code.txt"),"Undelivered newly created guidance cannot authorize later code creation");need(context.prepare().find("Synthetic newly approved scope guidance.")!=std::string::npos,"Next actual model context must contain own approved guidance");
  const auto accepted=args("*** Begin Patch\n*** Add File: new-scope/code.txt\n+code\n*** End Patch\n");prepared=tool.prepare(accepted);need(prepared.has_value(),"Delivered approved guidance permits a fresh patch");
  {Task task(tool,"after-guide-delivery",std::move(*prepared));const auto proposal=pending(store,"after-guide-delivery-0");store.decide_operation(proposal.id,OperationDecision::allow,"fixture-controller").get();need(Json::parse(task.result.get())["state"]=="succeeded","Fresh proposal after guidance delivery must create actual code");}
  const auto unchanged=store.operations("run").get().size();const auto definition=PatchTool::definition();need(definition.name=="apply_patch"&&Json::parse(definition.input_schema_json)["additionalProperties"]==false,"Model tool must declare its strict input contract");
  for(const auto& invalid:std::vector<std::string>{R"({"patch_text":"bad","root":"outside"})",R"({"patch_text":"one","patch_text":"two"})",R"({"patch_text":7})",R"({"patch_text":"bad","create_parents":"true"})"})rejects<std::invalid_argument>([&]{tool.prepare(invalid);});
  std::stop_source stopped;stopped.request_stop();rejects<ToolCancelled>([&]{tool.prepare(accepted,stopped.get_token());});need(store.operations("run").get().size()==unchanged,"Invalid/cancelled arguments cannot create proposals");store.close();
 }
 {PersistenceService store(database,{argv[1],argv[2]});need(store.operation("new-guide-0").get().state==OperationState::succeeded&&store.operation("changed-after-review-0").get().state==OperationState::failed,"Restart must preserve actual success and guidance failure journals");rejects<NotFound>([&]{store.operation("new-guide-1").get();});need(read(root/"new-scope/code.txt")=="code\n","Restart cannot replay or undo actual completed code creation");store.close();}
 std::cout<<"Native patch tool contract passed strict input rejection, source/destination guidance delivery, approval-time recheck, shared created-parent refresh, own AGENTS change with real partial outcome, fresh delivery and xlang3 SQLite restart. Controller/lifecycle inputs are synthetic; no provider, CLI or rendered UI acceptance.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
