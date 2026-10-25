#pragma once
#include "agentflow/remote_enrollment_approval.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <future>
#include <random>

namespace enrollment_approval_fixture {
using namespace agentflow;
inline void check(bool value){if(!value)throw std::runtime_error("Enrollment approval contract failed");}
template<class Error,class F>void refuses(F action){try{action();}catch(const Error&){return;}throw std::runtime_error("Enrollment approval did not refuse");}
struct Workspace:RunExecutor {
    ExecutionWorkspaceMetadata metadata{true,"PRIVATE_LOCAL_ROOT","workspace-a","authority-a"};
    WorkspaceMembershipSnapshot membership{"workspace-a",7,{{"folder-a","PRIVATE_ATTACHED_ROOT"}}};
    Run submit(std::string,std::string,std::string)override{throw std::runtime_error("Approval cannot execute");}
    void cancel(const std::string&)override{throw std::runtime_error("Approval cannot cancel execution");}
    bool healthy()const override{return true;}
    ExecutionWorkspaceMetadata execution_workspace()const override{return metadata;}
    WorkspaceMembershipSnapshot workspace_membership_snapshot()const override{return membership;}
};
inline void run(const std::string& modules,const std::string& stdlib){
    const auto file=std::filesystem::temp_directory_path()/("xmind-enrollment-approval-"+std::to_string(std::random_device{}())+".sqlite");check(!std::filesystem::exists(file));
    const EnrollmentApprovalBinding binding{"connection-a","server-a","scope-a",1};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Workspace workspace;
    {
        PersistenceService store(file.string(),{modules,stdlib});RemoteEnrollmentApprovals journal(store,workspace,binding);
        const auto record=journal.approve_from_owner("approval-a",7,1,now+60000);check(record.revision==1&&!record.revoked&&record.effects==1);
        const auto raw=store.information("remote-enrollment-local-approval-v1","approval-a").get();check(raw.find("PRIVATE_LOCAL_ROOT")==std::string::npos&&raw.find("PRIVATE_ATTACHED_ROOT")==std::string::npos);
        check(journal.require_current("approval-a",1).authority_id=="authority-a");
        refuses<Conflict>([&]{journal.require_current("approval-a",2);});
        refuses<Conflict>([&]{journal.approve_from_owner("approval-a",7,7,now+60000);});
        refuses<std::invalid_argument>([&]{journal.approve_from_owner("empty-effects",7,0,now+60000);});
        refuses<std::invalid_argument>([&]{journal.approve_from_owner("unknown-effects",7,8,now+60000);});
        refuses<std::invalid_argument>([&]{journal.approve_from_owner("expired",7,1,now-1);});
        workspace.membership.revision=8;
        refuses<Conflict>([&]{journal.require_current("approval-a",1);});
        refuses<Conflict>([&]{journal.approve_from_owner("stale-membership",7,1,now+60000);});
        workspace.membership.revision=7;workspace.metadata.authority_id="authority-b";
        refuses<Conflict>([&]{journal.require_current("approval-a",1);});workspace.metadata.authority_id="authority-a";
        auto other_binding=binding;other_binding.scope_id="scope-b";RemoteEnrollmentApprovals other(store,workspace,other_binding);
        refuses<Conflict>([&]{other.observe("approval-a");});
        const auto attempt=[&]{try{journal.approve_from_owner("approval-race",7,1,now+60000);return 1;}catch(const Conflict&){return 0;}};
        auto first=std::async(std::launch::async,attempt),second=std::async(std::launch::async,attempt);check(first.get()+second.get()==1);
        // Offline revocation must not depend on current membership or networking.
        workspace.membership.revision=9;const auto revoked=journal.revoke("approval-a",1);check(revoked.revoked&&revoked.revision==2);
        refuses<Conflict>([&]{journal.revoke("approval-a",1);});check(journal.revoke("approval-a",2).revision==2);
        auto expired=nlohmann::json::parse(store.information("remote-enrollment-local-approval-v1","approval-race").get());expired["expires_unix_ms"]=now-1;store.put_information("remote-enrollment-local-approval-v1","approval-race",expired.dump()).get();
        workspace.membership.revision=7;refuses<Conflict>([&]{journal.require_current("approval-race",1);});
        expired["version"]=1.0;store.put_information("remote-enrollment-local-approval-v1","approval-race",expired.dump()).get();refuses<DatabaseError>([&]{journal.observe("approval-race");});
        expired["version"]=1;expired["revision"]=2;store.put_information("remote-enrollment-local-approval-v1","approval-race",expired.dump()).get();refuses<DatabaseError>([&]{journal.observe("approval-race");});store.close();
    }
    {
        PersistenceService store(file.string(),{modules,stdlib});RemoteEnrollmentApprovals reopened(store,workspace,binding);
        check(reopened.observe("approval-a").revoked&&reopened.observe("approval-a").revision==2);
        refuses<Conflict>([&]{reopened.require_current("approval-a",1);});store.close();
    }
    std::filesystem::remove(file);
}
}
