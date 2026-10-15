#include "agentflow/agent_permission_grants.hpp"
#include <filesystem>
#include <iostream>
#include <random>
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
        store.record_request(context);require(store.request("operation-a")==context,"The operation-bound permission scope must persist exactly");
        auto saved=store.grant("session-a","operation-a");require(saved.revision==1&&saved.grants.size()==1&&saved.grants.front().id==grant_id,"Granting an observed operation must create one revisioned persistent grant");
        require(store.grant("session-a","operation-a").revision==1,"Repeating the same approval must be idempotent");
        require(store.allows("session-a","workspace-a",policy,"read_file","src/main.cpp"),"Exact session, workspace, policy, tool and resource should match");
        require(!store.allows("session-b","workspace-a",policy,"read_file","src/main.cpp"),"A grant must not cross sessions");
        require(!store.allows("session-a","workspace-b",policy,"read_file","src/main.cpp"),"A grant must not cross workspace identities");
        require(!store.allows("session-a","workspace-a",changed_policy,"read_file","src/main.cpp"),"A grant must not survive a permission policy change");
        require(!store.allows("session-a","workspace-a",policy,"read_file","src/other.cpp"),"A grant must not widen to another resource");
        rejects<agentflow::Conflict>([&]{store.revoke("session-a",grant_id,0);},"Stale revocation revisions must be rejected");
        saved=store.revoke("session-a",grant_id,1);require(saved.revision==2&&saved.grants.empty(),"Revocation must advance revision and remove authority");
        require(!store.allows("session-a","workspace-a",policy,"read_file","src/main.cpp"),"Revoked authority must stop matching immediately");
        const auto second=agentflow::AgentPermissionAskContext{"operation-b","session-b","workspace-a",policy,"read_file","src/main.cpp",grant_id};
        store.record_request(second);saved=store.grant("session-b","operation-b");require(saved.revision==1&&saved.grants.size()==1,"Independent session grants must have independent revisions");
        rejects<agentflow::NotFound>([&]{store.grant("session-a","operation-b");},"An operation request must not be granted from another session");
        persistence.close();
        {agentflow::PersistenceService reopened((directory.path/"grants.sqlite").string(),imports);agentflow::AgentPermissionGrantStore restored(reopened);require(restored.allows("session-b","workspace-a",policy,"read_file","src/main.cpp"),"An approved scope must survive SQLite owner restart");require(!restored.allows("session-a","workspace-a",policy,"read_file","src/main.cpp"),"A revoked scope must remain absent after SQLite owner restart");require(restored.request("operation-b")==second,"The approval audit binding must survive SQLite owner restart");}
        std::cout<<"Native scoped permission grant contract passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
