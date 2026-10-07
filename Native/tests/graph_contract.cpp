// Native domain and real embedded-xlang3 storage contract. Completion/input
// values below are labeled fixtures, not model/tool execution evidence.
#include "agentflow/graph.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected graph rejection did not occur");}
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    const Json spec={{"nodes",Json::array({
        {{"id","left"},{"type","agent"},{"prompt","Synthetic branch 🌍"}},
        {{"id","right"},{"type","tool"},{"tool","read_file"},{"arguments",{{"path","README.md"}}}},
        {{"id","gate"},{"type","human"},{"prompt","Synthetic decision"},{"depends_on",{"left","right"}}},
        {{"id","accepted"},{"type","tool"},{"tool","read_file"},{"depends_on",{"left","gate"}},{"arguments",{{"path",{{"$ref",{{"node","left"},{"path",{"items",0,"path"}}}}}}}},{"when",{{"node","gate"},{"path",{"approved"}},{"equals",true}}}},
        {{"id","rejected"},{"type","agent"},{"prompt","Synthetic rejected branch"},{"depends_on",{"gate"}},{"when",{{"node","gate"},{"path",{"approved"}},{"equals",false}}}},
        {{"id","after-rejected"},{"type","agent"},{"prompt","Synthetic skip descendant"},{"depends_on",{"rejected"}}}
    })}};
    const GraphPlan plan(spec.dump());require(plan.nodes().size()==6 && plan.order().size()==6,"Native plan must include all nodes");
    for(const auto source:{R"({"nodes":[]})",R"({"nodes":[{"id":"a","type":"agent","prompt":"x","depends_on":["missing"]}]})",R"({"nodes":[{"id":"a","type":"agent","prompt":"x","depends_on":["b"]},{"id":"b","type":"agent","prompt":"x","depends_on":["a"]}]})",R"({"nodes":[{"id":"a","type":"agent","prompt":"x","permissions":"allow"}]})",R"({"nodes":[{"id":"a","type":"agent","prompt":"x","prompt":"spoof"}]})",R"({"nodes":[{"id":"a","type":"tool","tool":"read_file","arguments":{"path":{"$ref":{"node":"undeclared"}}}}]})"})rejects<std::invalid_argument>([&]{GraphPlan invalid(source);});
    GraphCoordinator coordinator(plan);require(coordinator.inspect().ready==std::vector<std::string>({"left","right"}),"Independent branches must both be ready");
    rejects<Conflict>([&]{coordinator.start("gate");});coordinator.start("left");coordinator.start("right");require(coordinator.inspect().running.size()==2,"Coordinator must represent both owned branches");
    coordinator.complete("left",R"({"items":[{"path":"src/fixture.txt"}],"content":"Synthetic owned output"})");require(coordinator.inspect().ready.empty(),"Join must wait for every dependency");
    coordinator.complete("right",R"({"content":"Synthetic read output"})");require(coordinator.inspect().ready==std::vector<std::string>({"gate"}),"Join should admit the human node");
    coordinator.start("gate");rejects<Conflict>([&]{coordinator.complete("gate","true");});const auto paused=coordinator.checkpoint();GraphCoordinator resumed(plan,paused);require(resumed.state("gate")==GraphNodeState::waiting_human && resumed.inspect().ready.empty(),"Human wait and completed branches must survive checkpoint restore");
    resumed.provide_human("gate",R"({"approved":true})");require(resumed.inspect().ready==std::vector<std::string>({"accepted"}) && resumed.inspect().skippable==std::vector<std::string>({"rejected"}),"Typed human output must route conditions");
    const auto prepared=resumed.start("accepted");require(Json::parse(prepared.definition.arguments_json).at("path")=="src/fixture.txt","Nested array/object references must resolve actual owned outputs");
    resumed.skip("rejected");resumed.skip("after-rejected");resumed.complete("accepted",R"({"content":"Synthetic final read"})");require(resumed.inspect().finished,"Completion plus propagated skips must finish the coordinator");
    GraphCoordinator finished(plan,resumed.checkpoint());require(finished.inspect().finished,"Completed checkpoints must remain completed");rejects<Conflict>([&]{finished.start("left");});
    GraphCoordinator interrupted(plan);interrupted.start("left");GraphCoordinator recovered(plan,interrupted.checkpoint());require(recovered.state("left")==GraphNodeState::uncertain && recovered.inspect().halted && recovered.inspect().ready.empty(),"Interrupted work must be uncertain and never replayed");rejects<Conflict>([&]{recovered.start("left");});
    auto corrupt=Json::parse(paused);corrupt["nodes"][0]["state"]="pending";corrupt["nodes"][0].erase("output");rejects<std::invalid_argument>([&]{GraphCoordinator invalid(plan,corrupt.dump());});
    GraphCoordinator bounds(plan);bounds.start("left");rejects<std::invalid_argument>([&]{bounds.complete("left",R"(1,"spoof":2)");});rejects<std::invalid_argument>([&]{bounds.complete("left",Json(std::string(65536,'x')).dump());});require(bounds.state("left")==GraphNodeState::running,"Rejected output must preserve the actual owner state");bounds.cancel_pending();require(bounds.state("left")==GraphNodeState::running && bounds.inspect().halted,"Cancel request must not fabricate retirement of running work");
    const auto database=(std::filesystem::u8path(argv[1])/"graphs.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};
    const auto input=Json{{"graphs",Json::array({{{"id","workflow"},{"spec",spec}}})}}.dump();
    {PersistenceService store(database,imports);GraphCatalogStore catalog(store);require(catalog.load().revision==0,"Fresh storage must not invent a graph");const auto first=catalog.apply(input);require(first.revision==1 && first.entries[0].revision==1,"Graph catalog must assign backend revisions");require(catalog.apply(input).revision==1,"Canonical unchanged graph must preserve revisions");const auto saved=store.information("native-graphs","catalog").get();
        for(const auto source:{R"({"graphs":[],"revision":99})",R"({"graphs":[],"graphs":[]})",R"({"graphs":[{"id":"x","revision":9,"spec":{"nodes":[]}}]})"}){rejects<std::invalid_argument>([&]{catalog.apply(source);});require(store.information("native-graphs","catalog").get()==saved,"Rejected imports must preserve saved graph definitions");}
        auto changed=spec;changed["nodes"][0]["prompt"]="Changed synthetic branch";const auto update=Json{{"graphs",Json::array({{{"id","workflow"},{"spec",changed}}})}}.dump();
        {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_graph_update BEFORE UPDATE ON information WHEN NEW.category='native-graphs' BEGIN SELECT RAISE(ABORT,'actual graph storage fixture failure'); END");}
        rejects<DatabaseError>([&]{catalog.apply(update);});require(store.information("native-graphs","catalog").get()==saved,"Actual failed SQLite update must retain the earlier graph");
        {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_graph_update");}
        const auto second=catalog.apply(update);require(second.revision==2 && second.entries[0].revision==2,"Changed specification must rotate both revisions");store.close();}
    {PersistenceService store(database,imports);GraphCatalogStore catalog(store);require(catalog.load().entries[0].revision==2,"Definitions must reopen through real SQLite");const auto removed=catalog.apply(R"({"graphs":[]})");require(removed.revision==3 && removed.entries.empty() && removed.retired_ids==std::vector<std::string>({"workflow"}),"Removed graph identities must be retired");rejects<std::invalid_argument>([&]{catalog.apply(input);});
        store.put_information("native-graphs","catalog",R"({"version":2,"revision":3,"graphs":[],"retired_ids":[]})").get();const auto corrupt=store.information("native-graphs","catalog").get();rejects<std::invalid_argument>([&]{catalog.apply(R"({"graphs":[]})");});require(store.information("native-graphs","catalog").get()==corrupt,"Corrupt graph configuration must not be overwritten");store.close();}
    std::cout<<"Native graph contracts passed planning, joins, conditions, references, human checkpoint restore, uncertain non-replay and actual xlang3/SQLite configuration revisions/fault/reopen. Coordinator outputs are synthetic; no agent/tool graph execution claimed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
