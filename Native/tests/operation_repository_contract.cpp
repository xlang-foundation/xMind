#include "graph_schema_fixture.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <atomic>
#include <random>

using namespace agentflow;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
std::int64_t expiry() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/ ("xmind-operations-"+std::to_string(std::random_device{}()));
    Directory() {std::filesystem::create_directory(path);}
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void start(Repository& store,const std::string& id) {
    store.create_session(id,id);store.start_prompt_run(id,id,R"({"content":"operation contract"})");store.transition(id,RunState::queued,RunState::running);
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory folder;const auto database=(folder.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
        OperationSpec spec{"main",folder.path.string(),"write_file",R"({"path":"result.txt","content":"Actual fixture effect"})"};
        {
            Repository store(database,roots);XlangSqlite inspect(database,roots);
            start(store,"main");
            rejects<std::invalid_argument>([&]{store.request_operation("invalid",spec,0);});
            auto malformed=spec;malformed.arguments_json="[]";
            rejects<std::invalid_argument>([&]{store.request_operation("invalid",malformed,expiry());});
            malformed.arguments_json=R"({"path":"safe","path":"different"})";
            rejects<std::invalid_argument>([&]{store.request_operation("invalid",malformed,expiry());});
            malformed.arguments_json=R"({"nested":{"same":1,"same":2}})";
            rejects<std::invalid_argument>([&]{store.request_operation("invalid",malformed,expiry());});
            malformed.arguments_json.clear();
            for(int depth=0;depth<70;++depth) malformed.arguments_json+="{\"nested\":";
            malformed.arguments_json+="0";for(int depth=0;depth<70;++depth) malformed.arguments_json+="}";
            rejects<std::invalid_argument>([&]{store.request_operation("invalid",malformed,expiry());});
            require(store.operations("main").empty(),"Invalid proposals must not persist");
            auto precise=spec;precise.tool="remote_effect";precise.arguments_json=R"({"value":18446744073709551616})";
            store.request_operation("precision",precise,expiry());store.decide_operation("precision",OperationDecision::allow,"fixture-controller");
            auto adjacent=precise;adjacent.arguments_json=R"({"value":18446744073709551617})";
            rejects<Conflict>([&]{store.claim_operation("precision",adjacent);});
            require(store.operation("precision").spec.arguments_json==precise.arguments_json,"Approval must retain exact JSON numeric bytes");
            inspect.execute("UPDATE operations SET expires_ms=1 WHERE id='precision'");
            rejects<Conflict>([&]{store.claim_operation("precision",precise);});
            store.request_operation("edit",spec,expiry());
            rejects<std::invalid_argument>([&]{store.decide_operation("edit",OperationDecision::allow,"");});
            require(store.operation("edit").decision_actor.empty() && store.operation("edit").state==OperationState::awaiting_approval,"Missing controller identity must not grant an operation");
            rejects<Conflict>([&]{store.claim_operation("edit",spec);});
            rejects<Conflict>([&]{store.complete_run("main",R"({"content":"premature"})");});
            rejects<std::invalid_argument>([&]{store.append_event("main","operation.succeeded","{}");});
            store.decide_operation("edit",OperationDecision::allow,"fixture-controller");
            rejects<Conflict>([&]{store.decide_operation("edit",OperationDecision::deny,"fixture-controller");});
            for(int field=0;field<4;++field) {
                auto changed=spec;
                if(field==0) changed.run_id="other";
                if(field==1) changed.workspace="other-workspace";
                if(field==2) changed.tool="shell";
                if(field==3) changed.arguments_json=R"({"path":"other.txt","content":"Actual fixture effect"})";
                rejects<Conflict>([&]{store.claim_operation("edit",changed);});
            }
            const auto events=store.events("main").size();
            inspect.execute("CREATE TRIGGER reject_claim BEFORE INSERT ON events WHEN NEW.kind='operation.executing' BEGIN SELECT RAISE(ABORT,'fault'); END");
            rejects<DatabaseError>([&]{store.claim_operation("edit",spec);});
            require(store.operation("edit").state==OperationState::ready && store.events("main").size()==events,"Claim/event fault must retain unconsumed approval atomically");
            inspect.execute("DROP TRIGGER reject_claim");
            auto reordered=spec;reordered.arguments_json=R"({"content":"Actual fixture effect","path":"result.txt"})";
            rejects<Conflict>([&]{store.claim_operation("edit",reordered);});
            require(store.claim_operation("edit",spec).state==OperationState::executing,"Only the exact approved payload may execute");
            rejects<Conflict>([&]{store.claim_operation("edit",spec);});
            rejects<Conflict>([&]{store.transition("main",RunState::running,RunState::cancelled);});
            start(store,"competing");auto competing=spec;competing.run_id="competing";
            store.request_operation("competing",competing,expiry());store.decide_operation("competing",OperationDecision::allow,"fixture-controller");
            rejects<Conflict>([&]{store.claim_operation("competing",competing);});
            require(store.operation("competing").state==OperationState::ready,"Competing workspace claim must not consume approval");
            // Actual fixture-side effect after the durable claim. This verifies
            // journal semantics, not the future production workspace write tool.
            {std::ofstream file(folder.path/"result.txt",std::ios::binary);file<<"Actual fixture effect";file.close();require(!file.fail(),"Actual fixture write failed");}
            inspect.execute("CREATE TRIGGER reject_outcome BEFORE INSERT ON events WHEN NEW.kind='operation.succeeded' BEGIN SELECT RAISE(ABORT,'fault'); END");
            rejects<DatabaseError>([&]{store.finish_operation("edit",OperationState::succeeded,R"({"path":"result.txt"})");});
            require(store.operation("edit").state==OperationState::executing,"Failed outcome journal must not imply success or repeatability");
            inspect.execute("DROP TRIGGER reject_outcome");
            store.finish_operation("edit",OperationState::succeeded,R"({"path":"result.txt"})");
            store.claim_operation("competing",competing);store.finish_operation("competing",OperationState::failed,R"({"reason":"fixture_no_effect"})");
            store.transition("competing",RunState::running,RunState::failed);
            rejects<Conflict>([&]{store.finish_operation("edit",OperationState::succeeded,"{}");});
            store.complete_run("main",R"({"content":"Recorded fixture result"})");
            start(store,"expiry");auto timed=spec;timed.run_id="expiry";
            store.request_operation("expired-pending",timed,expiry());
            inspect.execute("UPDATE operations SET expires_ms=1 WHERE id='expired-pending'");
            rejects<Conflict>([&]{store.decide_operation("expired-pending",OperationDecision::allow,"fixture-controller");});
            require(store.operation("expired-pending").state==OperationState::expired,"Expired decision must persist expiration");
            require(store.operation("expired-pending").decision_actor.empty(),"An expired attempted decision must not record a grant actor");
            store.request_operation("expired-ready",timed,expiry());store.decide_operation("expired-ready",OperationDecision::allow,"fixture-controller");
            inspect.execute("UPDATE operations SET expires_ms=1 WHERE id='expired-ready'");
            rejects<Conflict>([&]{store.claim_operation("expired-ready",timed);});
            require(store.operation("expired-ready").state==OperationState::expired,"An old granted approval must not execute");
            require(store.operation("expired-ready").decision_actor=="fixture-controller","Grant expiry must preserve the original controller identity");
            store.request_operation("denied",timed,expiry());store.decide_operation("denied",OperationDecision::deny,"fixture-controller");
            rejects<Conflict>([&]{store.claim_operation("denied",timed);});
            store.request_operation("cancel-pending",timed,expiry());
            store.request_operation("cancel-ready",timed,expiry());store.decide_operation("cancel-ready",OperationDecision::allow,"fixture-controller");
            rejects<Conflict>([&]{store.expire_operation("cancel-ready");});
            auto wrong_owner=timed;wrong_owner.run_id="other-owner";
            rejects<Conflict>([&]{store.cancel_operation("cancel-ready",wrong_owner);});
            require(store.operation("cancel-ready").state==OperationState::ready,"Invalid cancellation/expiry must not retire a valid grant");
            store.transition("expiry",RunState::running,RunState::cancelled);
            require(store.operation("cancel-pending").state==OperationState::cancelled && store.operation("cancel-ready").state==OperationState::cancelled,"Terminal owner must retire unused grants");
            require(store.operation("denied").state==OperationState::denied,"Cancellation must preserve past decisions");
            start(store,"interrupted");auto interrupted=spec;interrupted.run_id="interrupted";
            store.request_operation("uncertain",interrupted,expiry());store.decide_operation("uncertain",OperationDecision::allow,"fixture-controller");store.claim_operation("uncertain",interrupted);
            store.request_operation("interrupted-unused",interrupted,expiry());store.decide_operation("interrupted-unused",OperationDecision::allow,"fixture-controller");
            inspect.execute("CREATE TRIGGER reject_recovery BEFORE INSERT ON events WHEN NEW.kind='operation.uncertain' BEGIN SELECT RAISE(ABORT,'fault'); END");
            {BackendLease lease(database);rejects<DatabaseError>([&]{store.recover_interrupted(lease);});}
            require(store.run("interrupted").state==RunState::running && store.operation("uncertain").state==OperationState::executing && store.operation("interrupted-unused").state==OperationState::ready,"Recovery fault must roll back effect and run states together");
            inspect.execute("DROP TRIGGER reject_recovery");
            start(store,"paused");auto paused=spec;paused.run_id="paused";store.request_operation("paused-proposal",paused,expiry());store.transition("paused",RunState::running,RunState::paused);
        }
        {
            PersistenceService store(database,roots);
            require(store.run("interrupted").get().state==RunState::failed,"Restart must fail the interrupted owner");
            require(store.operation("uncertain").get().state==OperationState::uncertain,"Interrupted claim must persist an uncertain effect");
            require(store.operation("uncertain").get().decision_actor=="fixture-controller","Uncertain recovery must preserve controller attribution");
            require(store.operation("interrupted-unused").get().state==OperationState::cancelled,"Restart must retire an unused grant owned by the interrupted run");
            rejects<Conflict>([&]{store.claim_operation("uncertain",spec).get();});
            store.create_session("quarantined","quarantined").get();store.start_prompt_run("quarantined","quarantined",R"({"content":"new effect"})").get();store.transition("quarantined",RunState::queued,RunState::running).get();
            auto quarantined=spec;quarantined.run_id="quarantined";store.request_operation("new-id",quarantined,expiry()).get();store.decide_operation("new-id",OperationDecision::allow,"fixture-controller").get();
            rejects<Conflict>([&]{store.claim_operation("new-id",quarantined).get();});
            require(store.operation("new-id").get().state==OperationState::ready,"A new ID must not bypass an uncertain workspace effect");
            store.transition("quarantined",RunState::running,RunState::failed).get();
            require(store.operation("edit").get().state==OperationState::succeeded,"Recorded effect outcome must survive restart");
            {std::ifstream file(folder.path/"result.txt",std::ios::binary);const std::string content{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};require(content=="Actual fixture effect","Actual recorded fixture effect must survive reopen");}
            require(store.run("paused").get().state==RunState::paused && store.operation("paused-proposal").get().state==OperationState::awaiting_approval,"Paused decision waits must survive restart");
            store.create_session("race","race").get();store.start_prompt_run("race","race",R"({"content":"race"})").get();store.transition("race",RunState::queued,RunState::running).get();
            std::filesystem::create_directory(folder.path/"separate-workspace");
            auto race=spec;race.run_id="race";race.workspace=(folder.path/"separate-workspace").string();store.request_operation("racing",race,expiry()).get();
            auto a=store.decide_operation("racing",OperationDecision::allow,"fixture-controller"),b=store.decide_operation("racing",OperationDecision::deny,"competing-controller");
            int winners=0;for(auto* future:{&a,&b}) {try {future->get();++winners;} catch(const Conflict&) {}}
            require(winners==1,"Only one competing controller decision may commit");
            require(store.operation("racing").get().state==OperationState::ready,"Worker preserves decision request order");
            require(store.operation("racing").get().decision_actor=="fixture-controller","Losing controller must not replace committed decision attribution");
            auto first=store.claim_operation("racing",race),second=store.claim_operation("racing",race);winners=0;
            for(auto* future:{&first,&second}) {try {future->get();++winners;} catch(const Conflict&) {}}
            require(winners==1,"Only one competing executor may claim");
            store.finish_operation("racing",OperationState::uncertain,R"({"reason":"explicit_uncertainty"})").get();
            rejects<Conflict>([&]{store.complete_run("race",R"({"content":"false success"})").get();});
            store.transition("race",RunState::running,RunState::failed).get();store.close();
        }
        // Actual schema-v2 upgrade preserves existing conversations/credentials.
        const auto legacy=(folder.path/"legacy.sqlite").string();
        {Repository store(legacy,roots);store.create_session("retained","retained");store.append_message("retained","user",R"({"content":"retained"})");
#if defined(_WIN32)
         const SqlBytes bytes{1,2,3};store.put_credential("local","key","purpose","retained",SecretBytes(bytes),0);
#endif
        }
        {XlangSqlite old(legacy,roots);remove_graph_schema_fixture(old);old.execute("DROP TABLE operation_resources");old.execute("DROP TABLE operations");old.execute("PRAGMA user_version=2");old.execute("CREATE INDEX run_operations ON messages(session_id)");}
        rejects<DatabaseError>([&]{Repository failed_upgrade(legacy,roots);});
        {XlangSqlite old(legacy,roots);
         require(std::get<std::int64_t>(old.execute("PRAGMA user_version").rows[0][0])==2,"Failed v3 migration must retain the previous version");
         require(old.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='operations'").rows.empty(),"Failed v3 migration must roll back the newly created table");
         old.execute("DROP INDEX run_operations");}
        {Repository upgraded(legacy,roots);require(upgraded.history("retained").size()==1,"v2 migration must preserve conversations");
#if defined(_WIN32)
         require(upgraded.credentials("local").size()==1,"v2 migration must preserve encrypted credential metadata");
#endif
         XlangSqlite inspect(legacy,roots);require(std::get<std::int64_t>(inspect.execute("PRAGMA user_version").rows[0][0])==10,"Operation schema migration must advance version");}
        const auto resource_database=(folder.path/"resources.sqlite").string();
        {
            Repository store(resource_database,roots);start(store,"a");start(store,"b");start(store,"c");
            OperationSpec a{"a","workspace-a","mcp_tool",R"({"server_config_id":"shared-server","arguments_json":"{}"})",{"mcp-server:shared-server"}};
            auto b=a;b.run_id="b";b.workspace="workspace-b";
            auto bad=a;bad.resources.push_back(bad.resources[0]);rejects<std::invalid_argument>([&]{store.request_operation("duplicate-resource",bad,expiry());});
            store.request_operation("resource-a",a,expiry());store.decide_operation("resource-a",OperationDecision::allow,"fixture-controller");
            auto changed=a;changed.resources={"mcp-server:another-server"};rejects<Conflict>([&]{store.claim_operation("resource-a",changed);});
            store.claim_operation("resource-a",a);store.request_operation("resource-b",b,expiry());store.decide_operation("resource-b",OperationDecision::allow,"fixture-controller");
            rejects<WorkspaceEffectBusy>([&]{store.claim_operation("resource-b",b);});
            require(store.operation("resource-b").state==OperationState::ready,"Contending multi-resource claim must remain unconsumed");
            store.finish_operation("resource-a",OperationState::succeeded,R"({"acknowledged":true})");store.claim_operation("resource-b",b);
            auto c=a;c.run_id="c";c.workspace="workspace-c";c.resources={"mcp-server:independent-server"};c.arguments_json=R"({"server_config_id":"independent-server"})";
            store.request_operation("resource-c",c,expiry());store.decide_operation("resource-c",OperationDecision::allow,"fixture-controller");store.claim_operation("resource-c",c);store.finish_operation("resource-c",OperationState::succeeded,"{}");
            store.finish_operation("resource-b",OperationState::uncertain,R"({"reason":"labeled_resource_uncertainty"})");
            auto rotated=a;rotated.arguments_json=R"({"server_config_id":"shared-server","config_revision":2})";
            store.request_operation("rotated",rotated,expiry());store.decide_operation("rotated",OperationDecision::allow,"fixture-controller");rejects<WorkspaceEffectUncertain>([&]{store.claim_operation("rotated",rotated);});
            // Reconstruct an actual schema-v3 journal, retaining the old MCP
            // operation payload; v4 must derive its stable server resource.
            XlangSqlite old(resource_database,roots);remove_graph_schema_fixture(old);old.execute("DROP TABLE operation_resources");old.execute("PRAGMA user_version=3");old.execute("CREATE INDEX resource_operations ON messages(session_id)");
        }
        rejects<DatabaseError>([&]{Repository failed_v4(resource_database,roots);});
        {
            XlangSqlite old(resource_database,roots);require(std::get<std::int64_t>(old.execute("PRAGMA user_version").rows[0][0])==3,"Failed resource migration must preserve version three");
            require(old.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='operation_resources'").rows.empty(),"Resource migration failure must roll back the new table");old.execute("DROP INDEX resource_operations");
        }
        {
            Repository migrated(resource_database,roots);require(migrated.operation("resource-b").spec.resources==std::vector<std::string>{"mcp-server:shared-server"},"v3 MCP migration must restore the stable server resource");
            start(migrated,"after-migration");OperationSpec next{"after-migration","another-workspace","mcp_tool",R"({"server_config_id":"shared-server"})",{"mcp-server:shared-server"}};
            migrated.request_operation("after-migration",next,expiry());migrated.decide_operation("after-migration",OperationDecision::allow,"fixture-controller");rejects<WorkspaceEffectUncertain>([&]{migrated.claim_operation("after-migration",next);});
            require(migrated.operation("resource-b").state==OperationState::uncertain,"Migration must not release or resolve an uncertain remote effect");
        }
        std::cout<<"Native operation journal contracts passed: exact approvals, one-shot claims, expiry, atomic faults, concurrent decisions, real fixture effect and uncertain recovery. Production effect tools are not integrated.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
