#include "agentflow/persistence_service.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>
#if defined(_WIN32)
#include "agentflow/view_sessions.hpp"
#endif

using namespace agentflow;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        std::random_device random;
        for(int i=0;i<32;++i) {
            auto candidate=std::filesystem::temp_directory_path()/("xmind-service-"+std::to_string(random())+"-"+std::to_string(random()));
            if(std::filesystem::create_directory(candidate)) {path=std::move(candidate);return;}
        }
        throw std::runtime_error("Cannot create test directory");
    }
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory directory;const auto path=(directory.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
#if defined(_WIN32)
        const auto viewPath=(directory.path/"views.sqlite").string();const std::string authority(64,'a'),origin="http://127.0.0.1:60405";std::string cookie;
        {
            PersistenceService store(viewPath,roots);ViewSessions views(store,authority);
            const auto issued=views.issue(origin);cookie=issued.credential;require(issued.max_age_seconds==28800 && views.accepts(cookie,origin),"Issued view credential must authenticate with a bounded native cookie lifetime");
            require(!views.accepts(cookie,"http://127.0.0.1:60406"),"View credential must bind to its origin");
            auto tampered=cookie;tampered.back()=tampered.back()=='a'?'b':'a';require(!views.accepts(tampered,origin),"Altered view secret must be rejected");
            const auto record=store.information("local-view-sessions",cookie.substr(0,64)).get();require(record.find(cookie.substr(65))==std::string::npos && record.find(authority)==std::string::npos,"Public view metadata cannot contain credentials");
            rejects<std::invalid_argument>([&]{views.issue("https://untrusted.invalid");});
        }
        {
            PersistenceService store(viewPath,roots);ViewSessions views(store,authority);require(views.accepts(cookie,origin),"View session must survive real xlang3 repository reopen");
            ViewSessions rotated(store,std::string(64,'b'));require(!rotated.accepts(cookie,origin),"Master authority rotation must invalidate old view sessions");
            views.revoke(cookie,origin);require(!views.accepts(cookie,origin),"Revocation must remove encrypted view credential");
            ViewSessions brief(store,authority,std::chrono::seconds(1));const auto expiring=brief.issue(origin);require(expiring.max_age_seconds==1,"Cookie duration must follow the native lifetime policy");std::this_thread::sleep_for(std::chrono::milliseconds(1100));require(!brief.accepts(expiring.credential,origin),"Expired cookie must not authenticate");
        }
        {
            PersistenceService store(viewPath,roots);ViewSessions views(store,authority);require(!views.accepts(cookie,origin),"Revocation must survive repository reopen");
            const auto cleaned=views.issue(origin);views.revoke(cleaned.credential,origin);
            require(store.credentials("local-view-sessions").get().empty(),"Issuance must prune expired encrypted credentials");
            {
                XlangSqlite inject(viewPath,roots);inject.execute("CREATE TRIGGER reject_view_metadata BEFORE INSERT ON information WHEN NEW.category='local-view-sessions' BEGIN SELECT RAISE(ABORT,'actual view publication fault'); END");
                rejects<DatabaseError>([&]{views.issue(origin);});inject.execute("DROP TRIGGER reject_view_metadata");
            }
            const auto orphaned=store.credentials("local-view-sessions").get();require(orphaned.size()==1,"Failed publication fixture must leave an unpublished encrypted candidate");
            rejects<NotFound>([&]{store.information("local-view-sessions",orphaned[0].id).get();});
            const auto recovered=views.issue(origin);require(views.accepts(recovered.credential,origin) && store.credentials("local-view-sessions").get().size()==1,"Next issuance must prune unpublished candidates before admitting a session");
            {
                XlangSqlite inject(viewPath,roots);inject.execute("CREATE TRIGGER reject_view_revoke BEFORE DELETE ON credentials WHEN OLD.scope='local-view-sessions' BEGIN SELECT RAISE(ABORT,'actual view revocation fault'); END");
                rejects<DatabaseError>([&]{views.revoke(recovered.credential,origin);});require(views.accepts(recovered.credential,origin),"Failed revocation must preserve the previous credential transactionally");inject.execute("DROP TRIGGER reject_view_revoke");
            }
            views.revoke(recovered.credential,origin);
            std::vector<std::string> retained;for(int i=0;i<32;++i)retained.push_back(views.issue(origin).credential);
            rejects<Conflict>([&]{views.issue(origin);});require(store.credentials("local-view-sessions").get().size()==32,"Session limit must reject before persisting another credential");
            views.revoke(retained.back(),origin);const auto replacement=views.issue(origin);require(views.accepts(replacement.credential,origin),"Revocation must release session capacity");
        }
#endif
        {
            Repository before(path,roots);
            before.create_session("interrupted","Before restart");before.create_run("interrupted","interrupted");
            before.create_session("paused","Awaiting approval");before.create_run("paused","paused");
            before.transition("paused",RunState::queued,RunState::running);
            before.transition("paused",RunState::running,RunState::paused);
        }
        {
            PersistenceService service(path,roots);
            require(service.run("interrupted").get().state==RunState::failed,"Owner startup must reconcile interrupted run");
            require(service.events("interrupted").get().size()==2,"Recovery must add one failure event");
            require(service.run("paused").get().state==RunState::paused,"Paused run must survive recovery");
            rejects<Conflict>([&]{PersistenceService competitor(path,roots);});
            std::string id="shared",title="Owned request";
            auto created=service.create_session(id,title);id="changed";title="changed";
            require(created.get().id=="shared","Requests must own their arguments");
            rejects<NotFound>([&]{service.session("missing").get();});
            require(service.session("shared").get().title=="Owned request","Failed request must not stop worker");
            std::vector<std::future<void>> producers;
            for(int i=0;i<4;++i) producers.push_back(std::async(std::launch::async,[&service] {
                for(int j=0;j<25;++j) service.append_message("shared","user",R"({"text":"concurrent"})").get();
            }));
            for(auto& producer:producers) producer.get();
            require(service.history("shared").get().size()==100,"Concurrent clients must persist all requests");
            const auto message_boundary=service.history("shared").get().at(49).sequence;
            service.replace_session_agent("shared",AgentDefinition{"fork-reviewer",1,"deepseek-flash","Use the review agent."},0).get();
            service.replace_session_skills("shared",SkillSelections{"workspace-scope",{"review-skill"},{"review-skill"}},0).get();
            const auto model_state=service.session_model("shared").get();require(model_state.revision==0&&model_state.editable&&!model_state.selected,"New conversations have no implicit session model override");
            const auto selected_model=service.replace_session_model("shared",SessionModelSelection{"claude-sonnet-4-5","claude-team","anthropic.messages"},model_state.revision).get();require(selected_model.revision==1&&selected_model.selected&&selected_model.selected->model_id=="claude-sonnet-4-5"&&selected_model.selected->provider_profile_id=="claude-team"&&selected_model.selected->provider_route_id=="anthropic.messages","Session model selection must pin the model to its provider profile and route");
            rejects<Conflict>([&]{service.replace_session_model("shared",SessionModelSelection{"other-model","claude-team","anthropic.messages"},0).get();});
            const auto before_fork_history=service.history("shared").get();
            const auto forked=service.fork_session("forked","shared",message_boundary).get();
            require(forked.id=="forked"&&forked.parent_id=="shared"&&forked.fork_boundary_sequence==message_boundary&&forked.fork_boundary_type=="before"&&forked.title=="Owned request (fork #1)","Fork must persist source lineage, exclusive boundary and derived title");
            const auto fork_history=service.history("forked").get();
            require(fork_history.size()==49&&fork_history.back().json==before_fork_history[48].json,"Fork must copy exactly the settled transcript before its boundary");
            require(service.history("shared").get().size()==100,"Fork cannot mutate source history");
            const auto fork_agent=service.session_agent("forked").get();require(fork_agent.selected&&fork_agent.selected->id=="fork-reviewer","Fork must copy the session agent selection");
            const auto fork_skills=service.session_skills("forked","workspace-scope").get();require(fork_skills.revision==1&&fork_skills.selections.ids==std::vector<std::string>{"review-skill"},"Fork must copy workspace-scoped skill selections");
            const auto fork_model=service.session_model("forked").get();require(fork_model.selected&&fork_model.revision==1&&fork_model.selected->model_id=="claude-sonnet-4-5"&&fork_model.selected->provider_route_id=="anthropic.messages","Fork must copy the session's provider-bound model selection");
            const auto full_fork=service.fork_session("forked-full","shared").get();require(service.history("forked-full").get().size()==100&&full_fork.fork_boundary_type=="through","Fork without a boundary must include the complete settled transcript");
            rejects<NotFound>([&]{service.fork_session("fork-missing","shared",999999).get();});
            service.create_run("race","shared").get();
            auto a=service.transition("race",RunState::queued,RunState::running);
            auto b=service.transition("race",RunState::queued,RunState::running);
            require(a.get().state==RunState::running,"First transition must succeed");
            rejects<Conflict>([&]{b.get();});
            require(service.events("race").get().size()==2,"Rejected transition must not publish an event");
#if defined(_WIN32)
            const std::vector<std::uint8_t> bytes{12,0,255,33};
            service.put_credential("scope","key","model","Synthetic",SecretBytes(bytes),0).get();
            auto resolved=service.resolve_credential("scope","key","model").get();
            require(std::ranges::equal(resolved.view(),bytes),"Move-only secrets must cross request/result boundary");
#endif
            // close must drain every accepted operation before releasing ownership.
            std::vector<std::future<void>> accepted;
            for(int i=0;i<25;++i) accepted.push_back(service.append_message("shared","assistant","{}"));
            auto close_a=std::async(std::launch::async,[&]{service.close();});
            auto close_b=std::async(std::launch::async,[&]{service.close();});
            close_a.get();close_b.get();
            for(auto& future:accepted) future.get();
            rejects<PersistenceClosed>([&]{service.sessions();});
        }
        {
            PersistenceService reopened(path,roots);
            require(reopened.history("shared").get().size()==125,"Drained requests must survive restart");
            const auto restored_fork=reopened.session("forked").get();require(restored_fork.parent_id=="shared"&&restored_fork.fork_boundary_type=="before"&&reopened.history("forked").get().size()==49,"Fork lineage and transcript must survive xlang3 repository restart");
            require(reopened.session_agent("forked").get().selected->id=="fork-reviewer"&&reopened.session_skills("forked","workspace-scope").get().selections.ids==std::vector<std::string>{"review-skill"},"Forked conversation configuration must survive restart");
            require(reopened.history("forked-full").get().size()==100,"Full branch history must survive restart");
            require(reopened.run("race").get().state==RunState::failed,"Next owner must recover running root");
            require(reopened.events("interrupted").get().size()==2,"Startup recovery must be idempotent");
        }
        {
            // Hold a real write transaction to make backpressure observable without
            // exposing arbitrary callbacks or runtime objects through the service.
            PersistenceService bounded(path,roots,1);XlangSqlite blocker(path,roots);blocker.begin();
            std::vector<std::future<Session>> accepted;
            bool busy=false;
            for(int i=0;i<10;++i) {
                try {accepted.push_back(bounded.create_session("bounded-"+std::to_string(i),"Bounded"));}
                catch(const PersistenceBusy&) {busy=true;break;}
            }
            blocker.rollback();require(busy,"Full queue must reject before unbounded growth");
            for(auto& future:accepted) future.get();
        }
        rejects<std::invalid_argument>([&]{PersistenceService invalid(path,roots,0);});
        // Missing module roots produce the embedding SDK's runtime_error at import.
        rejects<std::runtime_error>([&]{PersistenceService invalid(path,{});});
        // Initialization failure must join its thread and release its lease.
        {PersistenceService after_failure(path,roots);require(!after_failure.sessions().get().empty(),"Lease must release after initialization failure");}
        std::cout<<"Persistence service ownership/concurrency contracts passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
