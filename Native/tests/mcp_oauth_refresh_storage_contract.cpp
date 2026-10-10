#include "agentflow/mcp_oauth_credentials.hpp"
#include "agentflow/mcp_wire.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/graph.hpp"
#include "agentflow/context_selection.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
using namespace agentflow;
namespace {
using Json=nlohmann::json;
constexpr const char* category="native-mcp-oauth-refresh";
void require(bool value){if(!value)throw std::runtime_error("Native durable refresh fixture invariant failed");}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected durable refresh rejection did not occur");}
SecretBytes secret(std::string_view value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool same(const SecretBytes& value,std::string_view expected){return std::ranges::equal(value.view(),std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(expected.data()),expected.size()));}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
McpOAuthTokens tokens(std::string_view access="synthetic-old-access",std::string_view refresh="synthetic old refresh +&=",std::vector<std::string> scopes={"tools.read","tools.list"}){return {secret(access),refresh.empty()?std::optional<SecretBytes>{}:std::optional<SecretBytes>{secret(refresh)},600,std::move(scopes)};}
struct Directory {
    std::filesystem::path parent=std::filesystem::temp_directory_path(),path=parent/("xmind-durable-refresh-"+std::to_string(std::random_device{}()));bool passed=false;
    Directory(){require(std::filesystem::create_directory(path));}
    ~Directory(){if(passed&&path.parent_path()==parent&&path.filename().string().starts_with("xmind-durable-refresh-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}else std::cerr<<"Retained synthetic refresh fixture: "<<path.string()<<'\n';}
};
}
int main(int argc,char** argv){if(argc!=3)return 2;try{
    Directory directory;const auto database=(directory.path/"state.sqlite").string();const std::vector<std::string> imports{argv[1],argv[2]};const std::string endpoint="https://issuer.example.test/token";
    Json definitions=Json::array();for(const auto* name:{"commit","cancel","prepared-restart","dispatched-restart","config-change","compete"})definitions.push_back({{"id",name},{"transport","http"},{"endpoint",std::string("https://resource.example.test/")+name},{"oauth",{{"scope","server"},{"id",std::string(name)+"-grant"},{"issuer","https://issuer.example.test"},{"client_id","synthetic-public-client"}}}});
    std::vector<McpServerSetting> configs;McpOAuthRefreshRecord prepared_restart,dispatched_restart,committed;
    {
        PersistenceService store(database,imports);configs=McpConfigurationStore(store).apply(Json{{"servers",definitions}}.dump());McpOAuthCredentialStore grants(store);for(const auto& c:configs)grants.save(c,c.id=="cancel"?"HTTPS://issuer.example.test/token":endpoint,tokens(),now(),0);
        XlangSqlite inspect(database,imports);const auto& config=configs.at(0);
        store.create_session("admission","Synthetic idle admission").get();
        store.create_run("pre-active","admission").get();
        rejects<Conflict>([&]{grants.prepare_refresh(config,"active-denied",1);});
        rejects<NotFound>([&]{store.mcp_oauth_refresh("active-denied").get();});
        store.transition("pre-active",RunState::queued,RunState::cancelled).get();
        const std::string prompt=R"({"content":"Synthetic inbound objective"})",identity=R"({"role":"user","parts":[{"kind":"text","text":"Synthetic inbound objective"}]})";
        store.start_incoming_message("prior-inbound","admission","prior-message",prompt,identity).get();store.transition("prior-inbound",RunState::queued,RunState::cancelled).get();
        for(int n=0;n<3;++n){store.append_message("admission","user",Json{{"content","Synthetic older objective "+std::to_string(n)}}.dump()).get();store.append_message("admission","assistant",R"({"content":"Synthetic older answer"})").get();}
        rejects<Conflict>([&]{grants.prepare_refresh(config,"stale",2);});
        rejects<std::invalid_argument>([&]{store.put_information(category,"forged","{}").get();});
        rejects<std::invalid_argument>([&]{store.compare_information(category,"forged","{}",{}).get();});
        auto first=grants.prepare_refresh(config,"commit-first",1);require(bool(first.grant)&&first.record.state=="prepared"&&same(first.grant->tokens.access_token,"synthetic-old-access"));
        const auto history_size=store.history("admission").get().size();
        const auto admission_fence=[&]{
            rejects<Conflict>([&]{store.create_run("denied-root","admission").get();});
            rejects<Conflict>([&]{store.start_prompt_run("denied-prompt","admission",prompt).get();});
            rejects<Conflict>([&]{store.start_incoming_message("denied-inbound","","denied-message",prompt,identity).get();});
            rejects<NotFound>([&]{store.session("ctx_denied-inbound").get();});
            const GraphPlan plan(R"({"nodes":[{"id":"leaf","type":"agent","prompt":"Synthetic graph"}]})");
            rejects<Conflict>([&]{store.start_graph_run("denied-graph","admission","synthetic-graph",1,plan,prompt).get();});
            require(store.start_incoming_message("ignored-retry","admission","prior-message",prompt,identity).get().id=="prior-inbound");
            require(store.history("admission").get().size()==history_size&&store.runs("admission").get().size()==2);
        };admission_fence();
        const ContextBinding binding{R"({"model_id":"synthetic-model","wire":"responses"})",std::string(64,'a')};
        const ContextScope scope{ContextScopeKind::session,"admission"};const auto snapshot=store.context_snapshot(scope,binding).get();
        store.request_context_compaction({"pending-context","local-owner",scope,binding,snapshot.head.revision}).get();
        rejects<Conflict>([&]{store.claim_idle_context_owner({"denied-idle","pending-context",scope,binding,snapshot.head.revision}).get();});
        ContextCompactionSpec maintenance;maintenance.id="denied-compaction";maintenance.maintenance_attempt_id="denied-maintenance";maintenance.snapshot=snapshot;const auto selected=select_context(snapshot,maintenance.policy);require(bool(selected));maintenance.selection=*selected;maintenance.input_json=R"({"model":"synthetic-model","input":[]})";maintenance.input_binding=context_digest(maintenance.input_json);maintenance.admission=IdleContextAdmission{"denied-idle"};maintenance.manual_request_id="pending-context";
        rejects<Conflict>([&]{store.begin_context_compaction(maintenance).get();});
        ContextMeasureRequestSpec measure;measure.id="denied-count";measure.owner_id="denied-idle";measure.payload_binding=context_digest("synthetic inference");measure.model_id="synthetic-model";measure.scope=scope;measure.binding=binding;measure.head_revision=snapshot.head.revision;measure.source_watermark=snapshot.source_watermark;measure.serialized_bytes=64;measure.input_json=maintenance.input_json;measure.count_request_binding=context_digest(measure.input_json);
        rejects<Conflict>([&]{store.begin_context_measure(measure).get();});
        require(inspect.execute("SELECT id FROM context_idle_owners").rows.empty()&&inspect.execute("SELECT id FROM context_compactions").rows.empty()&&inspect.execute("SELECT id FROM context_measures").rows.empty());
        require(store.context_manual_request("pending-context",binding).get().state=="pending");store.retire_context_request("pending-context",binding,ContextFailureCode::cancelled).get();
        auto duplicate=grants.prepare_refresh(config,"commit-first",1);require(!duplicate.grant&&duplicate.record.state=="prepared");
        rejects<Conflict>([&]{grants.prepare_refresh(config,"different-request",1);});
        rejects<Conflict>([&]{grants.save(config,endpoint,tokens(),now(),1);});rejects<Conflict>([&]{grants.remove(config,1);});
        const auto owner=store.backend_owner().get();rejects<Conflict>([&]{store.quiesce_backend({owner.generation,owner.revision}).get();});
        auto wrong=first.record;wrong.generation="different-owner";rejects<Conflict>([&]{grants.dispatch_refresh(wrong);});
        auto dispatched=grants.dispatch_refresh(first.record);require(dispatched.state=="dispatched"&&dispatched.dispatched_unix_ms>0);rejects<Conflict>([&]{grants.dispatch_refresh(first.record);});
        admission_fence();
        rejects<Conflict>([&]{store.put_credential(config.oauth->scope,config.oauth->id,mcp_credential_purpose(config,"OAUTH"),"Cannot bypass owner",secret("synthetic replacement"),1).get();});
        const auto old_ciphertext=std::get<SqlBytes>(inspect.execute("SELECT ciphertext FROM credentials WHERE id='commit-grant'").rows.at(0).at(0));const auto before=store.information(category,"commit-first").get();
        inspect.execute("CREATE TRIGGER reject_refresh_receipt BEFORE UPDATE ON information WHEN OLD.category='native-mcp-oauth-refresh' AND OLD.id='commit-first' BEGIN SELECT RAISE(ABORT,'synthetic receipt storage fault'); END");
        rejects<DatabaseError>([&]{grants.publish_refresh(config,dispatched,tokens("synthetic-rotated-access","synthetic rotated refresh",{"tools.read"}),now());});
        require(grants.load(config).revision==1&&same(grants.load(config).tokens.access_token,"synthetic-old-access")&&store.information(category,"commit-first").get()==before);
        require(std::get<SqlBytes>(inspect.execute("SELECT ciphertext FROM credentials WHERE id='commit-grant'").rows.at(0).at(0))==old_ciphertext);inspect.execute("DROP TRIGGER reject_refresh_receipt");
        rejects<McpProtocolError>([&]{grants.publish_refresh(config,dispatched,tokens("synthetic-wrong-access","synthetic wrong refresh",{"tools.admin"}),now());});
        committed=grants.publish_refresh(config,dispatched,tokens("synthetic-rotated-access","synthetic rotated refresh",{"tools.read"}),now());require(committed.state=="committed"&&committed.published_revision==2);
        require(grants.load(config).revision==2&&same(*grants.load(config).tokens.refresh_token,"synthetic rotated refresh"));
        duplicate=grants.prepare_refresh(config,"commit-first",1);require(!duplicate.grant&&duplicate.record.state=="committed"&&duplicate.record.published_revision==2);
        rejects<Conflict>([&]{grants.prepare_refresh(config,"commit-first",2);});rejects<Conflict>([&]{grants.publish_refresh(config,dispatched,tokens(),now());});
        auto retained=grants.prepare_refresh(config,"commit-retained",2);auto retained_dispatched=grants.dispatch_refresh(retained.record);require(grants.publish_refresh(config,retained_dispatched,tokens("synthetic-retained-access","",{"tools.read"}),now()).published_revision==3);require(same(*grants.load(config).tokens.refresh_token,"synthetic rotated refresh"));
        const auto public_receipt=store.information(category,"commit-first").get();require(public_receipt.find("synthetic-rotated-access")==std::string::npos&&public_receipt.find("synthetic rotated refresh")==std::string::npos);
        auto corrupted=Json::parse(public_receipt);corrupted["published_revision"]=0;inspect.execute("UPDATE information SET payload=? WHERE category=? AND id='commit-first'",{corrupted.dump(),std::string(category)});
        rejects<DatabaseError>([&]{grants.prepare_refresh(config,"corrupt-receipt",3);});inspect.execute("UPDATE information SET payload=? WHERE category=? AND id='commit-first'",{public_receipt,std::string(category)});
        auto cancelled=grants.prepare_refresh(configs.at(1),"cancel-before-dispatch",1);require(cancelled.record.binding.token_endpoint=="HTTPS://issuer.example.test/token");require(grants.abandon_refresh(cancelled.record).state=="cancelled");require(!grants.prepare_refresh(configs.at(1),"cancel-before-dispatch",1).grant);
        auto after_cancel=grants.prepare_refresh(configs.at(1),"after-cancel",1);require(bool(after_cancel.grant));grants.abandon_refresh(after_cancel.record);
        prepared_restart=grants.prepare_refresh(configs.at(2),"prepared-restart",1).record;
        dispatched_restart=grants.dispatch_refresh(grants.prepare_refresh(configs.at(3),"dispatched-restart",1).record);
        auto changed=grants.dispatch_refresh(grants.prepare_refresh(configs.at(4),"changed-during-exchange",1).record);definitions.at(4)["endpoint"]="https://resource.example.test/changed";McpConfigurationStore(store).apply(Json{{"servers",definitions}}.dump());
        rejects<Conflict>([&]{grants.publish_refresh(configs.at(4),changed,tokens(),now());});require(grants.load(configs.at(4)).revision==1);require(grants.abandon_refresh(changed).state=="uncertain");
        const auto& competing=configs.at(5);std::vector<std::future<McpOAuthRefreshClaim>> requests;
        for(int n=0;n<8;++n)requests.push_back(store.claim_mcp_oauth_refresh({"concurrent-"+std::to_string(n),competing.id,competing.oauth->scope,competing.oauth->id,mcp_credential_purpose(competing,"OAUTH"),mcp_server_setting_json(competing),endpoint,1,1}));
        int winners=0,conflicts=0;McpOAuthRefreshRecord winner;for(auto& request:requests)try{auto result=request.get();require(bool(result.grant));++winners;winner=result.record;}catch(const Conflict&){++conflicts;}require(winners==1&&conflicts==7);grants.abandon_refresh(winner);
        store.close();
    }
    {
        PersistenceService store(database,imports);McpOAuthCredentialStore grants(store);require(grants.load(configs.at(0)).revision==3&&same(grants.load(configs.at(0)).tokens.access_token,"synthetic-retained-access"));require(store.mcp_oauth_refresh("commit-first").get().published_revision==2);
        require(store.mcp_oauth_refresh("prepared-restart").get().state=="cancelled");require(store.mcp_oauth_refresh("dispatched-restart").get().state=="uncertain");
        require(!grants.prepare_refresh(configs.at(2),"prepared-restart",1).grant);auto new_claim=grants.prepare_refresh(configs.at(2),"new-after-prepared-restart",1);require(bool(new_claim.grant));grants.abandon_refresh(new_claim.record);
        require(!grants.prepare_refresh(configs.at(3),"dispatched-restart",1).grant);rejects<Conflict>([&]{grants.prepare_refresh(configs.at(3),"no-replay-after-restart",1);});rejects<Conflict>([&]{grants.save(configs.at(3),endpoint,tokens(),now(),1);});rejects<Conflict>([&]{grants.remove(configs.at(3),1);});
        rejects<Conflict>([&]{grants.dispatch_refresh(prepared_restart);});rejects<Conflict>([&]{grants.publish_refresh(configs.at(3),dispatched_restart,tokens(),now());});
        require(store.backend_owner().get().generation!=dispatched_restart.generation);store.close();
    }
    directory.passed=true;std::cout<<"Native durable refresh storage passed actual xlang3 SQLite exclusive concurrent claims, no private snapshot on duplicate identity, dispatch retirement, generic credential and quiescence fences, exact configuration checks, complete ciphertext/receipt atomic rollback/publication, scope restriction, refresh retention, corruption rejection and new-generation prepared cancellation/dispatched uncertainty without replay. Synthetic grants; no network refresh, automatic renewal or client recovery verified\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
