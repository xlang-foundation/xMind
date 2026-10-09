#include "agentflow/legacy_owner.hpp"
#include "agentflow/persistence_service.hpp"
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;
void require(bool yes){if(!yes)throw std::runtime_error("Legacy owner contract failed");}
template<class E,class F>void rejects(F f){try{f();}catch(const E&){return;}throw std::runtime_error("Expected legacy rejection is absent");}
struct Directory{std::filesystem::path root,parent=std::filesystem::canonical(std::filesystem::temp_directory_path());Directory(){std::random_device r;for(int i=0;i<32;++i){const auto p=parent/("xmind-legacy-owner-"+std::to_string(r()));if(std::filesystem::create_directory(p)){root=std::filesystem::canonical(p);return;}}throw std::runtime_error("Cannot create owned fixture");}~Directory(){try{if(root.parent_path()==parent&&root.filename().string().starts_with("xmind-legacy-owner-")&&std::filesystem::canonical(root)==root&&!std::filesystem::is_symlink(root)){std::error_code e;std::filesystem::remove_all(root,e);}}catch(...){}}};
int main(int argc,char** argv){if(argc!=3)return 2;try{
 Directory dir;const auto file=(dir.root/"state.sqlite").string();const std::vector<std::string> roots{argv[1],argv[2]};
 // Target/dead-process metadata and prompts are explicitly synthetic here.
 // Real runtime qualification and OS observation belong to separate fixtures.
 LegacyOwnerBootstrap boot{std::string(32,'a'),{{(dir.root/"fixture-generation").string(),"windows-runtime-file-v1:fixture",std::string(64,'b'),std::string(40,'c'),std::string(40,'d'),std::string(64,'e'),std::string(64,'f')},"D:\\fixture-workspace","windows-local-file-v1:fixture",std::string(64,'1'),true}};
 std::vector<Message> history;
 {PersistenceService store(file,roots);store.create_session("saved","retained legacy session").get();store.start_prompt_run("before","saved",R"({"content":"legacy fixture prompt"})").get();store.transition("before",RunState::queued,RunState::running,"{}").get();store.complete_run("before",R"({"content":"legacy fixture reply","usage":{"input_tokens":11,"output_tokens":7}})").get();history=store.history("saved").get();const std::uint8_t bytes[]{1,3,5,7};store.put_credential("legacy-fixture","secret","synthetic-test","retained",SecretBytes(bytes),0).get();rejects<std::invalid_argument>([&]{store.put_information(legacy_owner_category,"pending","{}").get();});}
 {XlangSqlite db(file,roots);db.execute("DELETE FROM information WHERE category='native-backend-owner'");db.execute("DROP TABLE run_skills");db.execute("DROP TABLE session_skills");db.execute("PRAGMA user_version=12");db.execute("CREATE TABLE legacy_commit_fault(ref TEXT REFERENCES sessions(id) DEFERRABLE INITIALLY DEFERRED)");db.execute("CREATE TRIGGER legacy_owner_fault AFTER INSERT ON information WHEN NEW.category='native-backend-owner' BEGIN INSERT INTO legacy_commit_fault(ref) VALUES('missing'); END");}
 std::string pending;
 {BackendLease lease(file);XlangSqlite db(file,roots);db.begin();const auto snapshot=snapshot_legacy_database(db);publish_legacy_owner_ticket(db,lease,boot,{123,"1234567",std::string(64,'2')},snapshot);db.commit();pending=*legacy_owner_record(db);rejects<Conflict>([&]{PersistenceService duplicate(file,roots,1024,{},boot);});}
 rejects<BackendQuiesced>([&]{PersistenceService ordinary(file,roots);});
 {auto wrong=boot;wrong.ticket_id=std::string(32,'9');rejects<Conflict>([&]{PersistenceService denied(file,roots,1024,{},wrong);});wrong=boot;wrong.target.approved_edits=false;rejects<Conflict>([&]{PersistenceService denied(file,roots,1024,{},wrong);});}
 rejects<DatabaseError>([&]{PersistenceService commitFault(file,roots,1024,{},boot);});
 {XlangSqlite db(file,roots);require(std::get<std::int64_t>(db.execute("PRAGMA user_version").rows[0][0])==12);require(db.execute("SELECT name FROM sqlite_master WHERE name='session_skills'").rows.empty());require(db.execute("SELECT payload FROM information WHERE category='native-backend-owner'").rows.empty());require(*legacy_owner_record(db)==pending);require(db.execute("SELECT * FROM legacy_commit_fault").rows.empty());db.execute("DROP TRIGGER legacy_owner_fault");db.execute("INSERT INTO sessions(id,title) VALUES('changed','must be rejected')");}
 rejects<Conflict>([&]{PersistenceService changed(file,roots,1024,{},boot);});
 {XlangSqlite db(file,roots);require(std::get<std::int64_t>(db.execute("PRAGMA user_version").rows[0][0])==12);db.execute("DELETE FROM sessions WHERE id='changed'");}
 BackendOwnerState first;
 {PersistenceService prepared(file,roots,1024,{},boot);first=prepared.backend_owner().get();require(first.quiesced&&first.replacement_prepared&&first.legacy_ticket_id==boot.ticket_id&&!first.replacement_source);require(prepared.history("saved").get().size()==history.size());rejects<BackendQuiesced>([&]{prepared.create_session("blocked","must not exist").get();});rejects<Conflict>([&]{prepared.resume_backend({first.generation,first.receipt_id,first.revision}).get();});}
 rejects<BackendQuiesced>([&]{PersistenceService ordinary(file,roots);});
 {PersistenceService prepared(file,roots,1024,{},boot);const auto state=prepared.backend_owner().get();require(state.generation!=first.generation&&state.quiesced);rejects<Conflict>([&]{prepared.activate_backend_replacement({first.generation,first.receipt_id,first.revision},boot.target).get();});const auto active=prepared.activate_backend_replacement({state.generation,state.receipt_id,state.revision},boot.target).get();require(!active.quiesced&&active.legacy_ticket_id.empty());const auto retained=prepared.history("saved").get();require(retained.size()==history.size());for(std::size_t i=0;i<history.size();++i)require(retained[i].sequence==history[i].sequence&&retained[i].role==history[i].role&&retained[i].json==history[i].json);require(prepared.resolve_credential("legacy-fixture","secret","synthetic-test").get().view().size()==4);prepared.create_session("after","actual post-activation admission").get();}
 rejects<Conflict>([&]{PersistenceService stale(file,roots,1024,{},boot);});
 {XlangSqlite db(file,roots);require(!legacy_owner_record(db));require(std::get<std::int64_t>(db.execute("PRAGMA user_version").rows[0][0])==13);}
 std::cout<<"Actual C++/embedded-xlang3 legacy operator-ticket preconditions, schema/publication rollback, exact saved records and encrypted credentials, closed prepare/restart, activation and source-ticket consumption passed; target/process metadata and messages are fixtures; no OS shutdown or installed migration claimed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
