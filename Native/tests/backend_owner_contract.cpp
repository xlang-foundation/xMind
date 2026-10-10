#include "agentflow/persistence_service.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <barrier>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <random>

using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action> void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native rejection is absent");}
struct Directory {
    std::filesystem::path path,parent;
    Directory(){parent=std::filesystem::canonical(std::filesystem::temp_directory_path());std::random_device random;for(int i=0;i<32;++i){const auto p=parent/("xmind-owner-contract-"+std::to_string(random())+"-"+std::to_string(random()));if(std::filesystem::create_directory(p)){path=std::filesystem::canonical(p);return;}}throw std::runtime_error("Cannot create owned fixture directory");}
    ~Directory(){try{if(!path.empty()&&path.is_absolute()&&path.parent_path()==parent&&path.filename().string().starts_with("xmind-owner-contract-")&&std::filesystem::canonical(path)==path&&!std::filesystem::is_symlink(std::filesystem::symlink_status(path))){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}catch(...){}}
};
BackendOwnerPrecondition condition(const BackendOwnerState& s){return {s.generation,s.revision};}
BackendOwnerReceipt receipt(const BackendOwnerState& s){return {s.generation,s.receipt_id,s.revision};}
void complete(PersistenceService& store,const std::string& id){store.transition(id,RunState::queued,RunState::running,"{}").get();store.complete_run(id,R"({"content":"actual native fixture completion"})").get();}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try {
        Directory directory;const std::vector<std::string> roots{argv[1],argv[2]};const auto database=(directory.path/"state.sqlite").string();BackendOwnerState initial,last;
        {
            PersistenceService store(database,roots);initial=store.backend_owner().get();require(initial.revision==1&&!initial.quiesced&&initial.generation.size()==32&&initial.receipt_id.empty(),"Actual owner must initialize under its database lease");
            store.create_session("session","retained session").get();store.start_prompt_run("completed","session",R"({"content":"fixture prompt"})").get();complete(store,"completed");store.put_information("fixture","saved",R"({"value":"preserve"})").get();const auto history=store.history("session").get();const auto events=store.events("completed",0).get();
#if defined(_WIN32)
            const std::uint8_t secret_bytes[]{11,22,33,44};store.put_credential("owner-fixture","secret","synthetic-test","retained encrypted fixture",SecretBytes(secret_bytes),0).get();
#endif
            rejects<std::invalid_argument>([&]{store.put_information("native-backend-owner","owner","{}").get();});rejects<std::invalid_argument>([&]{store.compare_information("native-backend-owner","owner","{}",{}).get();});
            rejects<Conflict>([&]{store.quiesce_backend({std::string(32,'f'),initial.revision}).get();});rejects<Conflict>([&]{store.quiesce_backend({initial.generation,initial.revision+1}).get();});
            {
                XlangSqlite inject(database,roots);inject.execute("CREATE TRIGGER reject_owner_quiescence BEFORE UPDATE ON information WHEN NEW.category='native-backend-owner' BEGIN SELECT RAISE(ABORT,'synthetic owner publication failure'); END");
                rejects<DatabaseError>([&]{store.quiesce_backend(condition(initial)).get();});require(!store.backend_owner().get().quiesced,"Failed durable publication cannot close the live worker fence");store.create_session("after-failed-quiescence","still open").get();inject.execute("DROP TRIGGER reject_owner_quiescence");
                inject.execute("CREATE TABLE owner_commit_fault(ref TEXT REFERENCES sessions(id) DEFERRABLE INITIALLY DEFERRED)");inject.execute("CREATE TRIGGER reject_owner_commit AFTER UPDATE ON information WHEN NEW.category='native-backend-owner' BEGIN INSERT INTO owner_commit_fault(ref) VALUES('absent-commit-fixture'); END");rejects<DatabaseError>([&]{store.quiesce_backend(condition(initial)).get();});require(!store.backend_owner().get().quiesced&&inject.execute("SELECT ref FROM owner_commit_fault").rows.empty(),"Actual deferred commit failure must roll back the owner receipt and injected row");inject.execute("DROP TRIGGER reject_owner_commit");inject.execute("DROP TABLE owner_commit_fault");
            }
            // Enqueue the mutation before waiting for quiescence. The fence is
            // checked on the worker, not merely when a caller submits its task.
            auto barrier=store.quiesce_backend(condition(initial));auto queued=store.create_session("must-not-exist","queued after barrier");const auto paused=barrier.get();rejects<BackendQuiesced>([&]{queued.get();});
            require(paused.quiesced&&paused.revision==initial.revision+1&&paused.receipt_id.size()==32,"Quiescence must publish an exact durable receipt");
            require(store.session("session").get().title=="retained session"&&store.history("session").get().size()==history.size()&&store.events("completed",0).get().size()==events.size(),"Quiescence must retain observable transcript/events");
            require(store.information("fixture","saved").get()==R"({"value":"preserve"})"&&store.sessions().get().size()==2&&store.runs("session").get().size()==1&&store.credentials("fixture").get().empty(),"Public observations remain available without reopening admission");
#if defined(_WIN32)
            require(store.credentials("owner-fixture").get().size()==1&&store.resolve_credential("owner-fixture","secret","synthetic-test").get().view().size()==4,"Actual encrypted credential remains available to its native owner");
            rejects<BackendQuiesced>([&]{store.put_credential("owner-fixture","other","synthetic-test","blocked",SecretBytes(secret_bytes),0).get();});
#endif
            rejects<NotFound>([&]{store.session("must-not-exist").get();});
            rejects<BackendQuiesced>([&]{store.start_incoming_message("incoming","session","message","{}","{}").get();});
            rejects<BackendQuiesced>([&]{store.start_prompt_run("agent","session","{}").get();});
            rejects<BackendQuiesced>([&]{store.append_message("session","user","{}").get();});
            rejects<BackendQuiesced>([&]{store.rename_session("session","changed","retained session").get();});
            rejects<BackendQuiesced>([&]{store.compare_information("fixture","saved","{}",{}).get();});
            rejects<BackendQuiesced>([&]{store.reserve_model_call("completed","completed","attempt",ModelCallRole::parent).get();});
            rejects<BackendQuiesced>([&]{store.begin_context_compaction({}).get();});
            rejects<BackendQuiesced>([&]{store.request_context_compaction({}).get();});
            rejects<BackendQuiesced>([&]{store.claim_idle_context_owner({}).get();});
            rejects<BackendQuiesced>([&]{store.dynamic_context_pause("completed").get();});
            rejects<BackendQuiesced>([&]{store.request_operation("effect",{},0).get();});
            rejects<BackendQuiesced>([&]{store.delete_credential("fixture","missing",1).get();});
            rejects<Conflict>([&]{store.quiesce_backend(condition(paused)).get();});
            auto wrong=receipt(paused);wrong.receipt_id=std::string(32,'e');rejects<Conflict>([&]{store.resume_backend(wrong).get();});wrong=receipt(paused);++wrong.revision;rejects<Conflict>([&]{store.resume_backend(wrong).get();});
            {
                XlangSqlite inject(database,roots);inject.execute("CREATE TRIGGER reject_owner_resume BEFORE UPDATE ON information WHEN NEW.category='native-backend-owner' BEGIN SELECT RAISE(ABORT,'synthetic resume publication failure'); END");rejects<DatabaseError>([&]{store.resume_backend(receipt(paused)).get();});require(store.backend_owner().get().quiesced,"Failed resume keeps the admission fence closed");rejects<BackendQuiesced>([&]{store.create_session("failed-resume","must not exist").get();});inject.execute("DROP TRIGGER reject_owner_resume");
            }
            auto active=store.resume_backend(receipt(paused)).get();require(!active.quiesced&&active.receipt_id.empty()&&active.revision==paused.revision+1,"Exact receipt resumes only the same live generation");rejects<Conflict>([&]{store.resume_backend(receipt(paused)).get();});
            // An admitted queued, running or paused root prevents quiescence.
            store.start_prompt_run("busy","session",R"({"content":"synthetic busy prompt"})").get();rejects<Conflict>([&]{store.quiesce_backend(condition(active)).get();});store.transition("busy",RunState::queued,RunState::running,"{}").get();rejects<Conflict>([&]{store.quiesce_backend(condition(active)).get();});store.transition("busy",RunState::running,RunState::paused,"{}").get();rejects<Conflict>([&]{store.quiesce_backend(condition(active)).get();});store.transition("busy",RunState::paused,RunState::cancelled,"{}").get();
            // Real native manual/idle context ownership without an active run.
            const ContextBinding binding{R"({"model_id":"synthetic-owner-context","wire":"responses"})",std::string(64,'a')};const ContextScope scope{ContextScopeKind::session,"session"};store.request_context_compaction({"manual-context","fixture-owner",scope,binding,0}).get();rejects<Conflict>([&]{store.quiesce_backend(condition(active)).get();});store.claim_idle_context_owner({"idle-context","manual-context",scope,binding,0,4,120000}).get();rejects<Conflict>([&]{store.quiesce_backend(condition(active)).get();});store.retire_idle_context_owner("idle-context",ContextFailureCode::cancelled,0).get();require(store.context_manual_request("manual-context",binding).get().state=="failed","Native maintenance retirement must settle its authenticated pending request");
            // Actual parallel callers race through the same C++ worker queue.
            for(int round=0;round<12;++round){const auto prior=store.backend_owner().get();const auto id="race-"+std::to_string(round);std::barrier start(3);auto run=std::async(std::launch::async,[&]{start.arrive_and_wait();try{store.start_prompt_run(id,"session",R"({"content":"synthetic race prompt"})").get();return true;}catch(const BackendQuiesced&){return false;}});auto quiescence=std::async(std::launch::async,[&]{start.arrive_and_wait();try{return std::optional<BackendOwnerState>(store.quiesce_backend(condition(prior)).get());}catch(const Conflict&){return std::optional<BackendOwnerState>{};}});start.arrive_and_wait();const auto admitted=run.get();const auto stopped=quiescence.get();require(admitted!=stopped.has_value(),"Exactly admission or native barrier must win");if(admitted){complete(store,id);require(!store.backend_owner().get().quiesced,"Rejected quiescence leaves run ownership intact");}else{rejects<NotFound>([&]{store.run(id).get();});store.resume_backend(receipt(*stopped)).get();}}
            last=store.backend_owner().get();
        }
        {
            PersistenceService store(database,roots);const auto next=store.backend_owner().get();require(next.generation!=initial.generation&&next.revision==last.revision+1&&!next.quiesced,"Normal reopen binds a new native owner generation");require(store.session("session").get().title=="retained session"&&store.information("fixture","saved").get()==R"({"value":"preserve"})","Actual SQLite reopen preserves records");rejects<Conflict>([&]{store.quiesce_backend(condition(last)).get();});last=store.quiesce_backend(condition(next)).get();
        }
        // A normal restart cannot erase or silently adopt a durable barrier.
        rejects<BackendQuiesced>([&]{PersistenceService reopened(database,roots);});
        {XlangSqlite inspect(database,roots);const auto saved=inspect.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows;require(saved.size()==1&&std::get<std::string>(saved[0][0]).find(last.receipt_id)!=std::string::npos,"Failed restart retains the same receipt");require(inspect.execute("SELECT id FROM sessions WHERE id IN ('must-not-exist','failed-resume')").rows.empty(),"Rejected admission cannot leave hidden rows");}
        {
            const auto retiring=(directory.path/"retiring.sqlite").string();BackendOwnerState requested;
            {
                PersistenceService store(retiring,roots);store.create_session("saved","retirement retains state").get();const auto active=store.backend_owner().get();
                rejects<Conflict>([&]{store.request_backend_retirement({active.generation,std::string(32,'a'),active.revision}).get();});
                const auto paused=store.quiesce_backend(condition(active)).get();auto wrong=receipt(paused);++wrong.revision;
                rejects<Conflict>([&]{store.request_backend_retirement(wrong).get();});wrong=receipt(paused);wrong.generation=std::string(32,'e');rejects<Conflict>([&]{store.request_backend_retirement(wrong).get();});wrong=receipt(paused);wrong.receipt_id=std::string(32,'e');rejects<Conflict>([&]{store.request_backend_retirement(wrong).get();});
                {
                    XlangSqlite inject(retiring,roots);inject.execute("CREATE TABLE retirement_commit_fault(ref TEXT REFERENCES sessions(id) DEFERRABLE INITIALLY DEFERRED)");inject.execute("CREATE TRIGGER reject_retirement_commit AFTER UPDATE ON information WHEN NEW.category='native-backend-owner' BEGIN INSERT INTO retirement_commit_fault(ref) VALUES('absent-retirement-fixture'); END");
                    rejects<DatabaseError>([&]{store.request_backend_retirement(receipt(paused)).get();});const auto unchanged=store.backend_owner().get();require(unchanged.quiesced&&!unchanged.retirement_requested&&unchanged.revision==paused.revision&&unchanged.receipt_id==paused.receipt_id,"Failed retirement commit preserves the exact resumable quiescence receipt");require(inject.execute("SELECT ref FROM retirement_commit_fault").rows.empty(),"Failed commit must roll back its injected row");inject.execute("DROP TRIGGER reject_retirement_commit");inject.execute("DROP TABLE retirement_commit_fault");
                }
                std::barrier start(3);
                auto retirement=std::async(std::launch::async,[&]{start.arrive_and_wait();try{return std::optional<BackendOwnerState>(store.request_backend_retirement(receipt(paused)).get());}catch(const Conflict&){return std::optional<BackendOwnerState>{};}});
                auto resume=std::async(std::launch::async,[&]{start.arrive_and_wait();try{store.resume_backend(receipt(paused)).get();return true;}catch(const Conflict&){return false;}});
                start.arrive_and_wait();const auto retired=retirement.get();const auto resumed=resume.get();require(retired.has_value()!=resumed,"Exactly retirement or same-receipt resume wins on the actual native queue");
                if(retired)requested=*retired;else{const auto next=store.quiesce_backend(condition(store.backend_owner().get())).get();requested=store.request_backend_retirement(receipt(next)).get();}
                require(requested.quiesced&&requested.retirement_requested,"Retirement request keeps native admission closed");rejects<Conflict>([&]{store.resume_backend(receipt(paused)).get();});rejects<Conflict>([&]{store.resume_backend(receipt(requested)).get();});rejects<Conflict>([&]{store.request_backend_retirement(receipt(requested)).get();});rejects<BackendQuiesced>([&]{store.create_session("blocked","must not exist").get();});require(store.session("saved").get().title=="retirement retains state","Retirement remains observable without reopening admission");
            }
            rejects<BackendQuiesced>([&]{PersistenceService reopened(retiring,roots);});XlangSqlite inspect(retiring,roots);const auto raw=std::get<std::string>(inspect.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows.at(0).at(0));require(raw.find("\"retirement_requested\":true")!=std::string::npos&&raw.find(requested.receipt_id)!=std::string::npos,"Actual reopen refusal retains the retirement request, not a fabricated process-exit acknowledgement");
        }
        // Actual schema12 fixtures: startup must refuse closed/malformed owner
        // records before creating current selection tables or changing the version.
        for(int fixture=0;fixture<3;++fixture){
            const auto path=(directory.path/("premigration-"+std::to_string(fixture)+".sqlite")).string();
            {PersistenceService store(path,roots);store.create_session("saved","premigration retains state").get();const auto paused=store.quiesce_backend(condition(store.backend_owner().get())).get();if(fixture==1)store.request_backend_retirement(receipt(paused)).get();}
            std::string raw;
            {XlangSqlite inject(path,roots);inject.execute("DROP TABLE run_skills");inject.execute("DROP TABLE session_skills");inject.execute("DROP TABLE session_agents");inject.execute("PRAGMA user_version=12");if(fixture==2)inject.execute("UPDATE information SET payload='{}' WHERE category='native-backend-owner' AND id='owner'");raw=std::get<std::string>(inject.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows.at(0).at(0));}
            if(fixture==2)rejects<DatabaseError>([&]{PersistenceService reopened(path,roots);});else rejects<BackendQuiesced>([&]{PersistenceService reopened(path,roots);});
            XlangSqlite inspect(path,roots);require(std::get<std::int64_t>(inspect.execute("PRAGMA user_version").rows.at(0).at(0))==12&&inspect.execute("SELECT name FROM sqlite_master WHERE type='table' AND name IN ('session_skills','run_skills')").rows.empty(),"Rejected startup cannot perform schema migrations");require(std::get<std::string>(inspect.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows.at(0).at(0))==raw&&inspect.execute("SELECT id FROM sessions WHERE id='saved'").rows.size()==1,"Rejected startup must retain exact owner and saved session");
        }
        {
            const auto unresolved=(directory.path/"unresolved.sqlite").string();PersistenceService store(unresolved,roots);store.create_session("effect-session","synthetic effect").get();store.start_prompt_run("effect-root","effect-session",R"({"content":"synthetic effect ownership"})").get();store.transition("effect-root",RunState::queued,RunState::running,"{}").get();const OperationSpec effect{"effect-root","synthetic-workspace","write_file",R"({"path":"fixture.txt","content":"synthetic proposal only"})"};const auto expiry=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600000;store.request_operation("effect",effect,expiry).get();store.decide_operation("effect",OperationDecision::allow,"synthetic-controller").get();store.claim_operation("effect",effect).get();store.finish_operation("effect",OperationState::uncertain,"{}").get();store.transition("effect-root",RunState::running,RunState::failed,"{}").get();require(store.run("effect-root").get().state==RunState::failed&&store.operation("effect").get().state==OperationState::uncertain,"Synthetic ledger fixture must own an unresolved effect beside a terminal root");rejects<Conflict>([&]{store.quiesce_backend(condition(store.backend_owner().get())).get();});require(!store.backend_owner().get().quiesced,"Unresolved terminal-root effect cannot manufacture a safe barrier");
        }
        {
            const auto damaged=(directory.path/"damaged.sqlite").string();{PersistenceService store(damaged,roots);store.create_session("saved","retain malformed-owner fixture").get();XlangSqlite inject(damaged,roots);inject.execute("UPDATE information SET payload='{}' WHERE category='native-backend-owner' AND id='owner'");rejects<DatabaseError>([&]{store.create_session("replacement","must not exist").get();});}rejects<DatabaseError>([&]{PersistenceService reopen(damaged,roots);});XlangSqlite inspect(damaged,roots);require(inspect.execute("SELECT id FROM sessions WHERE id='saved'").rows.size()==1&&inspect.execute("SELECT id FROM sessions WHERE id='replacement'").rows.empty(),"Malformed durable owner state cannot admit writes or reset the profile");
        }
        {
            // Typed metadata is synthetic here; production runtime qualification
            // is checked separately against the actual loaded server image.
            const auto path=(directory.path/"replacement.sqlite").string();BackendOwnerBootstrap bootstrap;BackendOwnerTarget target{{(directory.path/"synthetic-generation").string(),"windows-runtime-file-v1:1:2",std::string(64,'a'),std::string(40,'b'),std::string(40,'c'),std::string(64,'d'),std::string(64,'e')},"D:\\synthetic-workspace","windows-local-file-v1:fixture",std::string(64,'f'),true};std::vector<Message> history;
            {PersistenceService store(path,roots);store.create_session("saved","retained replacement session").get();store.start_prompt_run("before","saved",R"({"content":"synthetic retained prompt"})").get();complete(store,"before");history=store.history("saved").get();
#if defined(_WIN32)
                const std::uint8_t bytes[]{11,22,33,44};store.put_credential("replacement-fixture","secret","synthetic-test","retained encrypted fixture",SecretBytes(bytes),0).get();
#endif
                const auto paused=store.quiesce_backend(condition(store.backend_owner().get())).get();bootstrap={receipt(paused),target};const auto retired=store.request_backend_retirement(bootstrap.receipt,target).get();require(retired.replacement_source&&*retired.replacement_source==bootstrap.receipt,"Target retirement retains its actually issued source receipt");rejects<Conflict>([&]{PersistenceService concurrent(path,roots,1024,bootstrap);});}
            std::string raw;
            {XlangSqlite inject(path,roots);inject.execute("DROP TABLE run_skills");inject.execute("DROP TABLE session_skills");inject.execute("DROP TABLE session_agents");inject.execute("PRAGMA user_version=12");raw=std::get<std::string>(inject.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows.at(0).at(0));}
            const auto unchanged=[&]{XlangSqlite inspect(path,roots);require(std::get<std::int64_t>(inspect.execute("PRAGMA user_version").rows.at(0).at(0))==12&&inspect.execute("SELECT name FROM sqlite_master WHERE name='session_skills'").rows.empty(),"Failed qualification cannot migrate the schema");require(std::get<std::string>(inspect.execute("SELECT payload FROM information WHERE category='native-backend-owner' AND id='owner'").rows.at(0).at(0))==raw,"Failed qualification cannot consume the source owner");};
            for(int field=0;field<14;++field){auto bad=bootstrap;switch(field){case 0:bad.target.runtime.root+="-other";break;case 1:bad.target.runtime.root_identity+="0";break;case 2:bad.target.runtime.manifest_sha256=std::string(64,'b');break;case 3:bad.target.runtime.native_revision=std::string(40,'a');break;case 4:bad.target.runtime.sdk_revision=std::string(40,'a');break;case 5:bad.target.runtime.source_manifest_sha256=std::string(64,'a');break;case 6:bad.target.runtime.server_sha256=std::string(64,'a');break;case 7:bad.target.workspace_root+="-other";break;case 8:bad.target.workspace_id+="0";break;case 9:bad.target.auth_binding=std::string(64,'a');break;case 10:bad.target.approved_edits=false;break;case 11:bad.receipt.generation=std::string(32,'a');break;case 12:bad.receipt.receipt_id=std::string(32,'a');break;case 13:++bad.receipt.revision;break;}rejects<Conflict>([&]{PersistenceService rejected(path,roots,1024,bad);});unchanged();}
            {XlangSqlite inject(path,roots);inject.execute("CREATE TABLE bootstrap_commit_fault(ref TEXT REFERENCES sessions(id) DEFERRABLE INITIALLY DEFERRED)");inject.execute("CREATE TRIGGER reject_bootstrap_commit AFTER UPDATE ON information WHEN NEW.category='native-backend-owner' BEGIN INSERT INTO bootstrap_commit_fault VALUES('absent-bootstrap-fixture'); END");rejects<DatabaseError>([&]{PersistenceService rejected(path,roots,1024,bootstrap);});unchanged();require(inject.execute("SELECT ref FROM bootstrap_commit_fault").rows.empty(),"Failed bootstrap commit rolls back the fault row too");inject.execute("DROP TRIGGER reject_bootstrap_commit");inject.execute("DROP TABLE bootstrap_commit_fault");}
            BackendOwnerState prepared;
            {PersistenceService store(path,roots,1024,bootstrap);prepared=store.backend_owner().get();require(prepared.quiesced&&prepared.replacement_prepared&&!prepared.retirement_requested&&prepared.generation!=bootstrap.receipt.generation,"Qualified startup prepares a closed new generation");rejects<BackendQuiesced>([&]{store.create_session("early","must not exist").get();});rejects<Conflict>([&]{store.resume_backend(receipt(prepared)).get();});const auto actual=store.history("saved").get();require(actual.size()==history.size(),"Prepared startup retains history");for(std::size_t i=0;i<actual.size();++i)require(actual[i].json==history[i].json&&actual[i].role==history[i].role&&actual[i].sequence==history[i].sequence,"Prepared startup preserves exact transcript rows");}
            {XlangSqlite inspect(path,roots);require(std::get<std::int64_t>(inspect.execute("PRAGMA user_version").rows.at(0).at(0))==14&&inspect.execute("SELECT name FROM sqlite_master WHERE name IN ('session_skills','run_skills','session_agents')").rows.size()==3,"Authorized migration creates current skill and agent selection tables with the prepared owner");}
            rejects<BackendQuiesced>([&]{PersistenceService ordinary(path,roots);});
            {PersistenceService store(path,roots,1024,bootstrap);const auto retry=store.backend_owner().get();require(retry.quiesced&&retry.replacement_prepared&&retry.generation!=prepared.generation&&retry.revision==prepared.revision+1,"A prepared-process restart remains closed and binds a fresh generation");auto bad=target;bad.approved_edits=false;rejects<Conflict>([&]{store.activate_backend_replacement(receipt(retry),bad).get();});rejects<Conflict>([&]{store.activate_backend_replacement(receipt(prepared),target).get();});const auto active=store.activate_backend_replacement(receipt(retry),target).get();require(!active.quiesced&&!active.replacement_prepared&&active.receipt_id.empty()&&active.revision==retry.revision+1,"Exact activation opens only the prepared generation");rejects<Conflict>([&]{store.activate_backend_replacement(receipt(retry),target).get();});store.start_prompt_run("after","saved",R"({"content":"synthetic new admitted prompt"})").get();complete(store,"after");
#if defined(_WIN32)
                const auto secret=store.resolve_credential("replacement-fixture","secret","synthetic-test").get();require(secret.view().size()==4&&secret.view()[0]==11&&secret.view()[3]==44,"Encrypted credential survives prepared restart and activation without re-entry");
#endif
            }
            rejects<Conflict>([&]{PersistenceService replay(path,roots,1024,bootstrap);});
        }
        std::cout<<"Actual C++/embedded-xlang3 owner fence, retirement race, exact target/bootstrap checks, atomic schema migration/publication rollback, closed preparation/restart and activation passed; typed target metadata and prompts are fixtures\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
