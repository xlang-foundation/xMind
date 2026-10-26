#include "agentflow/agent_permission_grants.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <chrono>
#include "agentflow/xlang_sqlite.hpp"
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Function>void rejects(Function action,const char* message){try{action();}catch(const Error&){return;}throw std::runtime_error(message);}
struct Directory {
    std::filesystem::path path;
    Directory(){std::random_device random;path=std::filesystem::temp_directory_path()/std::filesystem::path("xmind-permission-grants-"+std::to_string(random())+"-"+std::to_string(random()));if(!std::filesystem::create_directory(path))throw std::runtime_error("Cannot create permission grant fixture directory");}
    ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::invalid_argument("Usage: permission_grants_contract MODULES STDLIB");
        Directory directory;const std::vector<std::string> imports{argv[1],argv[2]};agentflow::PersistenceService persistence((directory.path/"grants.sqlite").string(),imports);agentflow::AgentPermissionGrantStore store(persistence);
        const std::vector<agentflow::AgentPermissionRule> selected{{"read_file","src","ask"}};
        const auto policy=agentflow::agent_permission_policy_binding({},selected);
        const auto changed_policy=agentflow::agent_permission_policy_binding({},{{"read_file","src","deny"}});
        require(policy!=changed_policy,"A policy edit must invalidate reusable grants");
        const auto grant_id=agentflow::agent_permission_grant_identifier("workspace-a",policy,"read_file","src/main.cpp");
        const agentflow::AgentPermissionAskContext context{"operation-a","session-a","workspace-a",policy,"read_file","src/main.cpp",grant_id};
        const auto seed_expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;
        for(const auto* session:{"session-a","session-b"}){persistence.create_session(session,"Scoped grant fixture").get();persistence.create_run(std::string(session)+"-run",session).get();persistence.transition(std::string(session)+"-run",agentflow::RunState::queued,agentflow::RunState::running).get();}
        persistence.request_operation("operation-a",{"session-a-run","workspace-a","read_file","{}"},seed_expiry).get();
        store.record_request(context);require(store.request("operation-a")==context,"The operation-bound permission scope must persist exactly");
        store.reply("session-a","operation-a",agentflow::OperationDecision::allow,true);auto saved=store.list("session-a");require(saved.revision==1&&saved.grants.size()==1&&saved.grants.front().id==grant_id,"Granting an observed operation must create one revisioned persistent grant");
        rejects<agentflow::Conflict>([&]{store.reply("session-a","operation-a",agentflow::OperationDecision::allow,true);},"Repeated replies must refuse without another publication");require(store.list("session-a").revision==1,"Repeated reply leaves authority unchanged");
        require(store.allows("session-a","workspace-a",policy,"read_file","src/main.cpp"),"Exact session, workspace, policy, tool and resource should match");
        require(!store.allows("session-b","workspace-a",policy,"read_file","src/main.cpp"),"A grant must not cross sessions");
        require(!store.allows("session-a","workspace-b",policy,"read_file","src/main.cpp"),"A grant must not cross workspace identities");
        require(!store.allows("session-a","workspace-a",changed_policy,"read_file","src/main.cpp"),"A grant must not survive a permission policy change");
        require(!store.allows("session-a","workspace-a",policy,"read_file","src/other.cpp"),"A grant must not widen to another resource");
        rejects<agentflow::Conflict>([&]{store.revoke("session-a",grant_id,0);},"Stale revocation revisions must be rejected");
        saved=store.revoke("session-a",grant_id,1);require(saved.revision==2&&saved.grants.empty(),"Revocation must advance revision and remove authority");
        require(!store.allows("session-a","workspace-a",policy,"read_file","src/main.cpp"),"Revoked authority must stop matching immediately");
        const auto second=agentflow::AgentPermissionAskContext{"operation-b","session-b","workspace-a",policy,"read_file","src/main.cpp",grant_id};
        persistence.request_operation("operation-b",{"session-b-run","workspace-a","read_file","{}"},seed_expiry).get();store.record_request(second);store.reply("session-b","operation-b",agentflow::OperationDecision::allow,true);saved=store.list("session-b");require(saved.revision==1&&saved.grants.size()==1,"Independent session grants must have independent revisions");
        rejects<agentflow::NotFound>([&]{store.reply("session-a","operation-b",agentflow::OperationDecision::allow,true);},"An operation request must not be granted from another session");
        persistence.create_session("atomic","Atomic permission replies").get();persistence.create_run("atomic-run","atomic").get();persistence.transition("atomic-run",agentflow::RunState::queued,agentflow::RunState::running).get();
        const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;
        const auto propose=[&](const std::string& id,const std::string& resource){persistence.request_operation(id,{"atomic-run","workspace-a","read_file","{}"},expiry).get();store.record_request({id,"atomic","workspace-a",policy,"read_file",resource,agentflow::agent_permission_grant_identifier("workspace-a",policy,"read_file",resource)});};
        propose("atomic-expired","src/expired.cpp");{agentflow::XlangSqlite inspect((directory.path/"grants.sqlite").string(),imports);inspect.execute("UPDATE operations SET expires_ms=1 WHERE id='atomic-expired'");}rejects<agentflow::Conflict>([&]{store.reply("atomic","atomic-expired",agentflow::OperationDecision::allow,true);},"Expired reply must refuse");require(persistence.operation("atomic-expired").get().state==agentflow::OperationState::expired&&store.list("atomic").revision==0&&store.list("atomic").grants.empty(),"Expired reply must publish no reusable authority");
        propose("atomic-allowed","src/allowed.cpp");require(store.reply("atomic","atomic-allowed",agentflow::OperationDecision::allow,true).state==agentflow::OperationState::ready&&store.list("atomic").revision==1,"Ready approval and reusable authority must publish together");rejects<agentflow::Conflict>([&]{store.reply("atomic","atomic-allowed",agentflow::OperationDecision::allow,true);},"Duplicate reply cannot publish again");require(store.list("atomic").revision==1,"Duplicate approval cannot advance grant revision");
        propose("atomic-rollback","src/rollback.cpp");{agentflow::XlangSqlite inspect((directory.path/"grants.sqlite").string(),imports);inspect.execute("CREATE TRIGGER reject_permission_ready BEFORE INSERT ON events WHEN NEW.kind='operation.ready' BEGIN SELECT RAISE(ABORT,'synthetic permission event failure'); END");}rejects<agentflow::DatabaseError>([&]{store.reply("atomic","atomic-rollback",agentflow::OperationDecision::allow,true);},"Failed operation publication must roll back the grant");{agentflow::XlangSqlite inspect((directory.path/"grants.sqlite").string(),imports);inspect.execute("DROP TRIGGER reject_permission_ready");}require(persistence.operation("atomic-rollback").get().state==agentflow::OperationState::awaiting_approval&&store.list("atomic").revision==1&&store.list("atomic").grants.size()==1,"SQL failure must preserve both original operation and exact grant authority");
        propose("atomic-denied","src/denied.cpp");rejects<std::invalid_argument>([&]{store.reply("atomic","atomic-denied",agentflow::OperationDecision::deny,true);},"Denied replies cannot grant reusable authority");require(store.reply("atomic","atomic-denied",agentflow::OperationDecision::deny,false).state==agentflow::OperationState::denied&&store.list("atomic").revision==1,"Denial cannot create a saved grant");
        persistence.create_session("access","Access mode fixture").get();persistence.create_run("access-run","access").get();persistence.transition("access-run",agentflow::RunState::queued,agentflow::RunState::running).get();
        require(persistence.session_access("access","workspace-a").get().mode=="default","New conversations require normal approval");
        const agentflow::OperationSpec effect{"access-run","workspace-a","create_file",R"({"path":"fixture.txt"})"};
        persistence.request_operation("access-pending",effect,expiry).get();
        auto access=persistence.set_session_access("access","workspace-a","full",0).get();
        require(access.mode=="full"&&access.revision==1&&persistence.operation("access-pending").get().state==agentflow::OperationState::ready,"Full access must resolve existing approval prompts");
        require(persistence.request_operation("access-auto",effect,expiry).get().state==agentflow::OperationState::ready,"Full access must authorize future effects natively");
        require(persistence.session_access("session-b","workspace-a").get().mode=="default"&&persistence.session_access("access","workspace-b").get().mode=="default","Full access cannot cross conversations or workspaces");
        rejects<agentflow::Conflict>([&]{persistence.set_session_access("access","workspace-a","default",0).get();},"Stale permission changes must fail");
        rejects<std::invalid_argument>([&]{persistence.set_session_access("access","workspace-a","forged",1).get();},"Unknown access modes must fail");
        persistence.set_session_access("access","workspace-a","default",1).get();
        rejects<agentflow::Conflict>([&]{persistence.claim_operation("access-auto",effect).get();},"Revocation must prevent an unconsumed automatic approval from dispatching");
        require(persistence.request_operation("access-default",effect,expiry).get().state==agentflow::OperationState::awaiting_approval,"Default mode must resume prompts");
        persistence.set_session_access("access","workspace-a","full",2).get();
        const auto claimed=persistence.claim_operation("access-default",effect).get();require(claimed.state==agentflow::OperationState::executing,"Full access still claims the immutable native proposal");
        persistence.finish_operation("access-default",agentflow::OperationState::uncertain,R"({"process_tree_retired":true})").get();
        persistence.request_operation("access-blocked",effect,expiry).get();
        rejects<agentflow::WorkspaceEffectUncertain>([&]{persistence.claim_operation("access-blocked",effect).get();},"Full access must preserve unresolved effects and quarantine");
        persistence.close();
        {agentflow::PersistenceService reopened((directory.path/"grants.sqlite").string(),imports);agentflow::AgentPermissionGrantStore restored(reopened);require(restored.allows("session-b","workspace-a",policy,"read_file","src/main.cpp"),"An approved scope must survive SQLite owner restart");require(!restored.allows("session-a","workspace-a",policy,"read_file","src/main.cpp"),"A revoked scope must remain absent after SQLite owner restart");require(restored.request("operation-b")==second,"The approval audit binding must survive SQLite owner restart");}
        {agentflow::PersistenceService reopened((directory.path/"grants.sqlite").string(),imports);const auto access=reopened.session_access("access","workspace-a").get();require(access.mode=="full"&&access.revision==3,"Full access must restore from native SQLite after restart");}
        std::cout<<"Native scoped permission grant and full access contract passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
