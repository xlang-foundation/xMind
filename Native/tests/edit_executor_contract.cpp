#include "agentflow/edit_executor.hpp"
#include "agentflow/patch_file_executor.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action) {try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path();
    std::filesystem::path path=parent/("xmind-edit-"+std::to_string(std::random_device{}()));
    Directory() {require(std::filesystem::create_directory(path),"Private fixture directory must be newly created");}
    ~Directory() {if(path.parent_path()==parent && path.filename().string().starts_with("xmind-edit-")) {std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
std::int64_t expiry() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
struct Task {
    std::stop_source stop;
    std::future<WorkspaceSnapshot> result;
    Task(EditExecutor& executor,const std::string& id,WorkspaceEditPlan plan,std::string run="run"):result(std::async(std::launch::async,[this,&executor,id,run=std::move(run),plan=std::move(plan)]() mutable {return executor.execute(id,run,std::move(plan),expiry(),stop.get_token());})) {}
    ~Task() {stop.request_stop();if(result.valid()) result.wait();}
};
Operation proposed(PersistenceService& store,const std::string& id) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline) {
        try {auto value=store.operation(id).get();require(value.state==OperationState::awaiting_approval,"Proposal must wait for controller decision");return value;}
        catch(const NotFound&) {std::this_thread::sleep_for(5ms);}
    }
    throw std::runtime_error("Proposal timed out");
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory directory;const auto root=directory.path/"workspace";std::filesystem::create_directory(root);
        const auto file=root/"actual.txt";
        auto write=[&](const std::string& bytes) {std::ofstream output(file,std::ios::binary|std::ios::trunc);output<<bytes;output.close();require(bool(output),"Actual fixture write failed");};
        write("original\n");
        const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};
        WorkspaceTools workspace(root.string());
        {
            PersistenceService store(database,roots);EditExecutor executor(store,workspace);
            store.create_session("session","edit contract").get();store.start_prompt_run("run","session",R"({"content":"edit contract"})").get();store.transition("run",RunState::queued,RunState::running).get();
            rejects<std::invalid_argument>([&]{executor.invoke("invalid","run",R"({"path":"actual.txt","old_text":"original","new_text":"changed","actor":"spoofed"})",expiry());});
            rejects<std::invalid_argument>([&]{executor.invoke("invalid","run",R"({"path":"actual.txt","path":"other.txt","old_text":"original","new_text":"changed"})",expiry());});
            rejects<std::invalid_argument>([&]{executor.invoke("invalid","run",R"({"path":"actual.txt","old_text":"original","new_text":"changed","expected_occurrences":1.5})",expiry());});
            rejects<NotFound>([&]{store.operation("invalid").get();});
            const auto nested=std::string(R"({"path":"actual.txt","old_text":"original","new_text":)")+std::string(10000,'[')+"0"+std::string(10000,']')+"}";
            try {executor.invoke("invalid-depth","run",nested,expiry());throw std::runtime_error("Deep edit arguments were accepted");}
            catch(const std::invalid_argument& error) {require(std::string(error.what())=="Edit argument nesting exceeds limits","Deep JSON must be rejected during bounded parsing");}
            rejects<NotFound>([&]{store.operation("invalid-depth").get();});
            require(workspace.read_file("actual.txt").content=="original\n","Invalid model edit arguments must not create a proposal or modify bytes");
            const auto large_before=std::string(8192,'a')+"TARGET"+std::string(8192,'b');
            {std::ofstream output(root/"large-source.txt",std::ios::binary);output<<large_before;output.close();require(bool(output),"Large edit fixture must be written");}
            const auto whole_after=large_before+"\nwhole-file change";
            rejects<ToolEditNeedsPatch>([&]{executor.invoke("large-whole-file","run",nlohmann::json{{"path","large-source.txt"},{"old_text",large_before},{"new_text",whole_after}}.dump(),expiry());});
            rejects<NotFound>([&]{store.operation("large-whole-file").get();});
            require(workspace.read_file("large-source.txt").content==large_before,"Large whole-file edit rejection must create no approval proposal and preserve exact bytes");
            auto focused=std::async(std::launch::async,[&]{return executor.invoke("large-focused-edit","run",nlohmann::json{{"path","large-source.txt"},{"old_text","TARGET"},{"new_text","UPDATED"}}.dump(),expiry());});
            const auto focused_proposal=proposed(store,"large-focused-edit");
            const auto focused_payload=nlohmann::json::parse(focused_proposal.spec.arguments_json);
            require(focused_payload["before_content"].get<std::string>()==large_before&&focused_payload["after_content"].get<std::string>()==std::string(8192,'a')+"UPDATED"+std::string(8192,'b'),"A bounded literal edit remains reviewable in a large file");
            store.decide_operation("large-focused-edit",OperationDecision::allow,"test-controller").get();
            (void)focused.get();
            require(workspace.read_file("large-source.txt").content==focused_payload["after_content"].get<std::string>(),"Approved focused edit must apply exact intended bytes");
            const auto plan=workspace.plan_replacement("actual.txt","original","native change");
            Task denied(executor,"denied",plan);proposed(store,"denied");
            require(workspace.read_file("actual.txt").content=="original\n","Proposal must not apply edits");
            store.decide_operation("denied",OperationDecision::deny,"test-controller").get();
            rejects<PermissionDenied>([&]{denied.result.get();});
            Task stale(executor,"stale",plan);proposed(store,"stale");write("outside writer\n");
            store.decide_operation("stale",OperationDecision::allow,"test-controller").get();
            rejects<ToolContentConflict>([&]{stale.result.get();});
            require(store.operation("stale").get().state==OperationState::failed && workspace.read_file("actual.txt").content=="outside writer\n","Approved stale edit must record no-effect failure and preserve external bytes");
            write("original\n");
            Task cancelled(executor,"cancelled",plan);proposed(store,"cancelled");cancelled.stop.request_stop();
            rejects<PermissionCancelled>([&]{cancelled.result.get();});
            Task allowed(executor,"allowed",plan);const auto proposal=proposed(store,"allowed");
            const auto payload=nlohmann::json::parse(proposal.spec.arguments_json);
            require(payload["before_content"]==plan.before.content && payload["after_content"]==plan.after_content && payload["file_id"]==plan.before.file_id,"Durable review must contain exact actual before/after plan");
            require(allowed.result.wait_for(20ms)==std::future_status::timeout,"Waiting cannot imply approval");
            store.decide_operation("allowed",OperationDecision::allow,"test-controller").get();
            const auto actual=allowed.result.get();const auto outcome=store.operation("allowed").get();
            require(actual.content=="native change\n" && workspace.read_file("actual.txt").content==actual.content,"Approved executor must actually change file bytes");
            require(outcome.state==OperationState::succeeded && outcome.decision_actor=="test-controller","Actual effect must be durably attributed and completed");
            require(nlohmann::json::parse(outcome.result_json)["after_sha256"]==workspace.snapshot_file("actual.txt").content_sha256,"Outcome must match actual filesystem hash");
            rejects<Conflict>([&]{executor.execute("allowed","run",plan,expiry());});
            require(workspace.read_file("actual.txt").content==actual.content,"Duplicate operation must not repeat effect");
            const auto attached_root=directory.path/"attached";std::filesystem::create_directory(attached_root);const auto attached_file=attached_root/"actual.txt";
            {std::ofstream output(attached_file,std::ios::binary);output<<"attached original\n";}
            WorkspaceTools attached(attached_root.string());const auto attached_id=attached.identity();
            WorkspaceToolSet members(workspace.identity(),2,{{workspace.identity(),workspace.root_path()},{attached_id,attached.root_path()}});
            store.append_event("run","agent.workspace_membership",nlohmann::json{{"workspace_id",workspace.identity()},{"revision",2},{"primary_folder_id",workspace.identity()},{"folders",nlohmann::json::array({{{"folder_id",workspace.identity()},{"root",workspace.root_path()}},{{"folder_id",attached_id},{"root",attached.root_path()}}})},{"snapshot","run_admission"}}.dump()).get();
            EditExecutor attached_executor(store,members.folder_tools(1),members.workspace_id(),attached_id,members.revision());
            auto attached_effect=std::async(std::launch::async,[&]{return attached_executor.invoke("attached-edit","run",nlohmann::json{{"path","actual.txt"},{"old_text","attached original"},{"new_text","attached changed"},{"folder_id",attached_id}}.dump(),expiry());});
            const auto attached_proposal=proposed(store,"attached-edit");const auto attached_payload=nlohmann::json::parse(attached_proposal.spec.arguments_json);
            require(attached_proposal.spec.workspace==workspace.identity()&&attached_payload["workspace_id"]==workspace.identity()&&attached_payload["folder_id"]==attached_id&&attached_payload["workspace_revision"]==2&&attached_payload["target_workspace_id"]==attached.identity(),"Attached-folder approval must bind group, folder identity, target root identity and admitted revision");
            store.decide_operation("attached-edit",OperationDecision::allow,"test-controller").get();const auto attached_result=nlohmann::json::parse(attached_effect.get());
            require(attached_result["content_sha256"]==attached.snapshot_file("actual.txt").content_sha256&&attached.read_file("actual.txt").content=="attached changed\n"&&workspace.read_file("actual.txt").content==actual.content,"An approved attached-folder edit must modify exactly the selected folder and preserve the primary copy");
            store.transition("run",RunState::running,RunState::completed).get();
            store.start_prompt_run("journal-fault-run","session",R"({"content":"journal fault contract"})").get();store.transition("journal-fault-run",RunState::queued,RunState::running).get();
            // Fixture-only storage fault at the real durable outcome boundary.
            // The filesystem action remains the actual product implementation.
            {XlangSqlite inject(database,roots);inject.execute("CREATE TRIGGER reject_edit_success BEFORE UPDATE OF state ON operations WHEN NEW.id='journal-fault' AND NEW.state='succeeded' BEGIN SELECT RAISE(ABORT,'fixture journal fault'); END");}
            const auto fault_plan=workspace.plan_replacement("actual.txt","native change","actual effect before journal fault");
            Task fault(executor,"journal-fault",fault_plan,"journal-fault-run");proposed(store,"journal-fault");
            store.decide_operation("journal-fault",OperationDecision::allow,"test-controller").get();
            rejects<EditOutcomeUnrecorded>([&]{fault.result.get();});
            require(workspace.read_file("actual.txt").content==fault_plan.after_content,"Journal fault must not conceal an actual file effect");
            require(store.operation("journal-fault").get().state==OperationState::executing,"Failed outcome persistence must preserve the claimed operation for recovery");
            store.close();
        }
        {
            PersistenceService reopened(database,roots);
            require(reopened.operation("allowed").get().state==OperationState::succeeded,"Actual outcome must survive backend reopen");
            require(reopened.operation("cancelled").get().state==OperationState::cancelled,"Unused cancelled proposal must remain retired");
            require(workspace.read_file("actual.txt").content=="actual effect before journal fault\n","Reopening must not replay an effect");
            require(reopened.operation("journal-fault").get().state==OperationState::uncertain,"Restart must quarantine an effect whose outcome was not durably recorded");
            require(reopened.run("journal-fault-run").get().state==RunState::failed,"Interrupted owner must be recovered without inventing success");
            EditExecutor recovery(reopened,workspace);
            rejects<Conflict>([&]{recovery.inspect_uncertain("allowed");});
            const auto applied=recovery.inspect_uncertain("journal-fault");
            require(applied.same_file && applied.match==EditSnapshotMatch::after,"Recovery must inspect actual matching after bytes without declaring success");
            require(applied.observed.content_sha256==workspace.fingerprint_file("actual.txt").content_sha256 && applied.observed_unix_ms>0,"Inspection must retain actual fingerprint and observation time");
            const auto plan=nlohmann::json::parse(applied.operation.spec.arguments_json);
            write(plan["before_content"].get<std::string>());
            require(recovery.inspect_uncertain("journal-fault").match==EditSnapshotMatch::before,"Actual restored before bytes must remain distinct from successful execution");
            write(std::string("partial\0",8)+std::string(1,static_cast<char>(0xff)));
            const auto partial=recovery.inspect_uncertain("journal-fault");
            require(partial.match==EditSnapshotMatch::different && partial.observed.size==9,"Raw partial/binary effects must be inspectable without text decoding");
            std::filesystem::create_directory(directory.path/"other-workspace");WorkspaceTools other((directory.path/"other-workspace").string());
            rejects<ToolAccessDenied>([&]{EditExecutor(reopened,other).inspect_uncertain("journal-fault");});
            require(reopened.operation("journal-fault").get().state==OperationState::uncertain,"Inspection must not consume quarantine or invent an outcome");
            write(plan["after_content"].get<std::string>());
            reopened.create_session("patch-recovery-session","patch recovery").get();
            reopened.start_prompt_run("patch-recovery-run","patch-recovery-session",R"({"content":"patch recovery"})").get();
            reopened.transition("patch-recovery-run",RunState::queued,RunState::running).get();
            const auto patch_root=root.parent_path()/"patch-inspection-workspace";std::filesystem::create_directory(patch_root);
            {std::ofstream output(patch_root/"patch-recovery.txt",std::ios::binary);output<<"before patch update\n";output.close();require(bool(output),"Patch recovery fixture write failed");}
            WorkspaceTools patch_workspace(patch_root.string());
            const auto patch_prepared=patch_workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Update File: patch-recovery.txt\n@@\n-before patch update\n+after patch update\n*** End Patch\n")).files.at(0);
            const auto patch_before=patch_workspace.snapshot_file("patch-recovery.txt");
            PatchFileExecutor patch_executor(reopened,patch_workspace);
            const PatchFileIdentity patch_identity{"uncertain-patch",0,1,{}};
            const auto patch_spec=patch_executor.proposal("uncertain-patch-0","patch-recovery-run",patch_prepared,patch_identity);
            reopened.request_operation("uncertain-patch-0",patch_spec,expiry()).get();
            reopened.decide_operation("uncertain-patch-0",OperationDecision::allow,"test-controller").get();
            reopened.claim_operation("uncertain-patch-0",patch_spec).get();
            reopened.finish_operation("uncertain-patch-0",OperationState::uncertain,R"({"reason":"fixture_uncertain_patch_update"})").get();
            reopened.transition("patch-recovery-run",RunState::running,RunState::failed).get();
            const auto patch_inspection=EditExecutor(reopened,patch_workspace).inspect_uncertain("uncertain-patch-0");
            require(patch_inspection.operation.spec.tool=="patch_file"&&patch_inspection.same_file&&patch_inspection.match==EditSnapshotMatch::before,
                "An uncertain patch_file update must inspect its exact unchanged before image");
            require(patch_inspection.observed.content_sha256==patch_before.content_sha256,
                "Patch update inspection must report the actual native file fingerprint");
            const auto patch_payload=nlohmann::json::parse(patch_spec.arguments_json);
            auto write_patch_fixture=[&](const std::string& bytes){std::ofstream output(patch_root/"patch-recovery.txt",std::ios::binary|std::ios::trunc);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));output.close();require(bool(output),"Patch recovery fixture rewrite failed");};
            write_patch_fixture(patch_payload["after_content"].get<std::string>());
            require(EditExecutor(reopened,patch_workspace).inspect_uncertain("uncertain-patch-0").match==EditSnapshotMatch::after,
                "Patch update inspection must identify its exact after image");
            write_patch_fixture("third-party content\n");
            require(EditExecutor(reopened,patch_workspace).inspect_uncertain("uncertain-patch-0").match==EditSnapshotMatch::different,
                "Patch update inspection must distinguish intervening third-party bytes");
            rejects<Conflict>([&]{reopened.finish_operation("uncertain-patch-0",OperationState::failed,R"({})").get();});
            require(reopened.operation("uncertain-patch-0").get().state==OperationState::uncertain,
                "Patch inspection must not resolve or release the uncertain operation");
            auto record_uncertain_patch=[&](const std::string& run_id,const std::string& operation_id,WorkspaceTools& target,WorkspacePatchFilePlan planned,const PatchFileIdentity& identity){
                reopened.start_prompt_run(run_id,"patch-recovery-session",R"({"content":"patch action recovery"})").get();
                reopened.transition(run_id,RunState::queued,RunState::running).get();
                PatchFileExecutor executor(reopened,target);const auto spec=executor.proposal(operation_id,run_id,planned,identity);
                reopened.request_operation(operation_id,spec,expiry()).get();reopened.decide_operation(operation_id,OperationDecision::allow,"test-controller").get();
                reopened.claim_operation(operation_id,spec).get();reopened.finish_operation(operation_id,OperationState::uncertain,R"({"reason":"fixture_uncertain_patch_action"})").get();
                reopened.transition(run_id,RunState::running,RunState::failed).get();return spec;
            };
            const auto add_root=root.parent_path()/"patch-add-inspection-workspace";std::filesystem::create_directory(add_root);WorkspaceTools add_workspace(add_root.string());
            const auto add_plan=add_workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Add File: added.txt\n+added bytes\n*** End Patch\n")).files.at(0);
            const auto add_spec=record_uncertain_patch("patch-add-run","uncertain-patch-add-0",add_workspace,add_plan,{"uncertain-patch-add",0,1,{}});
            auto add_inspection=EditExecutor(reopened,add_workspace).inspect_uncertain("uncertain-patch-add-0");
            require(add_inspection.match==EditSnapshotMatch::before&&!add_inspection.paths.at(0).file,"Uncertain patch add must recognize a verified absent before image");
            const auto add_payload=nlohmann::json::parse(add_spec.arguments_json);{std::ofstream output(add_root/"added.txt",std::ios::binary);output<<add_payload["after_content"].get<std::string>();}
            add_inspection=EditExecutor(reopened,add_workspace).inspect_uncertain("uncertain-patch-add-0");
            require(add_inspection.match==EditSnapshotMatch::after&&add_inspection.paths.at(0).file.has_value(),"Uncertain patch add must recognize the proposed after image");

            const auto delete_root=root.parent_path()/"patch-delete-inspection-workspace";std::filesystem::create_directory(delete_root);WorkspaceTools delete_workspace(delete_root.string());
            {std::ofstream output(delete_root/"deleted.txt",std::ios::binary);output<<"remove bytes\n";}
            const auto delete_plan=delete_workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Delete File: deleted.txt\n*** End Patch\n")).files.at(0);
            record_uncertain_patch("patch-delete-run","uncertain-patch-delete-0",delete_workspace,delete_plan,{"uncertain-patch-delete",0,1,{}});
            require(EditExecutor(reopened,delete_workspace).inspect_uncertain("uncertain-patch-delete-0").match==EditSnapshotMatch::before,"Uncertain patch delete must recognize its exact before image");
            std::filesystem::remove(delete_root/"deleted.txt");
            const auto delete_inspection=EditExecutor(reopened,delete_workspace).inspect_uncertain("uncertain-patch-delete-0");
            require(delete_inspection.match==EditSnapshotMatch::after&&!delete_inspection.paths.at(0).file,"Uncertain patch delete must recognize a verified absent after image");

            const auto move_root=root.parent_path()/"patch-move-inspection-workspace";std::filesystem::create_directory(move_root);WorkspaceTools move_workspace(move_root.string());
            {std::ofstream output(move_root/"source.txt",std::ios::binary);output<<"move before\n";}
            const auto move_plan=move_workspace.plan_patch(parse_file_patch("*** Begin Patch\n*** Update File: source.txt\n*** Move to: destination.txt\n@@\n-move before\n+move after\n*** End Patch\n")).files.at(0);
            const auto move_spec=record_uncertain_patch("patch-move-run","uncertain-patch-move-0",move_workspace,move_plan,{"uncertain-patch-move",0,1,{}});
            require(EditExecutor(reopened,move_workspace).inspect_uncertain("uncertain-patch-move-0").match==EditSnapshotMatch::before,"Uncertain patch move must recognize its source and absent destination before image");
            const auto move_payload=nlohmann::json::parse(move_spec.arguments_json);std::filesystem::rename(move_root/"source.txt",move_root/"destination.txt");
            {std::ofstream output(move_root/"destination.txt",std::ios::binary|std::ios::trunc);output<<move_payload["after_content"].get<std::string>();}
            const auto move_inspection=EditExecutor(reopened,move_workspace).inspect_uncertain("uncertain-patch-move-0");
            require(move_inspection.match==EditSnapshotMatch::after&&move_inspection.paths.size()==2&&move_inspection.paths.at(1).file.has_value(),"Uncertain patch move must recognize the same file at its proposed destination");
            reopened.close();
        }
        std::cout<<"Approved native edit executor passed with actual files and embedded xlang3 persistence; no model or public approval route was used\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
