#include "agentflow/graph_service.hpp"
#include "agentflow/mcp_tool_registry.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
const std::string body="Actual native graph MCP bytes\n";
void require(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action,const char* reason){
    try{action();}catch(const Error&){return;}throw std::runtime_error(reason);
}
std::string read(const std::filesystem::path& file){
    std::ifstream stream(file,std::ios::binary);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
template<class Predicate>void eventually(Predicate predicate,const std::string& reason){
    const auto deadline=std::chrono::steady_clock::now()+8s;
    while(std::chrono::steady_clock::now()<deadline){if(predicate())return;std::this_thread::sleep_for(5ms);}
    throw std::runtime_error(reason);
}
std::size_t peer_events(const std::filesystem::path& root,const std::string& kind){
    std::ifstream stream(root/"peer-audit.jsonl");std::string line;std::size_t count=0;
    while(std::getline(stream,line))if(Json::parse(line).at("event")==kind)++count;
    return count;
}
std::size_t calls(const std::filesystem::path& root){return peer_events(root,"called");}
std::size_t starts(const std::filesystem::path& root){return peer_events(root,"started");}
Operation approval(PersistenceService& store,const std::string& root){
    std::optional<Operation> found;
    eventually([&]{for(const auto& child:store.children(root).get())for(const auto& operation:store.operations(child.id).get())
        if(operation.state==OperationState::awaiting_approval){found=operation;return true;}return false;},"Actual graph MCP proposal did not arrive");
    return *found;
}
Run retired(PersistenceService& store,GraphService& service,const std::string& id){
    eventually([&]{const auto state=store.run(id).get().state;
        return state==RunState::completed || state==RunState::failed || state==RunState::cancelled;},id+": graph did not retire");
    eventually([&]{return service.idle();},id+": graph service did not relinquish ownership");
    return store.run(id).get();
}
void no_operations(PersistenceService& store,const std::string& root){
    for(const auto& child:store.children(root).get())require(store.operations(child.id).get().empty(),"Rejected graph must have no durable MCP approval");
}
struct Fixture {
    std::filesystem::path root;std::string database,node,peer,mode;
    std::vector<std::string> imports;
    Fixture(char** argv,std::string name):root(std::filesystem::u8path(argv[1])/name),database((root/"state.sqlite").string()),
        node(argv[2]),peer(argv[3]),mode(std::move(name)),imports{argv[4],argv[5]}{
        require(std::filesystem::create_directory(root),"Each graph MCP workspace must be newly created");
    }
    Json configuration()const{
        return Json{{"servers",Json::array({{{"id","graph-peer"},{"transport","stdio"},{"executable",node},
            {"working_directory",root.string()},{"arguments",Json::array({peer,"--peer",mode,root.string()})}}})}};
    }
    AgentSettings settings(PersistenceService& store){
        AgentSettings result;result.workspace=root.string();result.run_timeout=10s;
        result.mcp_servers=McpConfigurationStore(store).apply(configuration().dump());
        require(result.provider.model.empty() && result.mcp_servers.size()==1,"MCP-only graph must use an actual configured peer with no provider/model");
        return result;
    }
    std::string discover(PersistenceService& store,const AgentSettings& settings){
        const auto& server=settings.mcp_servers.front();
        McpStdioClient client({server.executable,server.working_directory,server.arguments,{}});
        const auto deadline=std::chrono::steady_clock::now()+5s;client.connect(deadline);
        WorkspaceTools workspace(root.string());McpToolRegistry registry(client,store,workspace,server.id,server.revision,deadline);
        const auto tools=registry.definitions();require(tools.size()==1 && tools.front().name.starts_with("mcp_") && tools.front().name.size()==52,
            "Trusted enrollment must derive the alias from actual owned peer discovery");
        const auto alias=tools.front().name;client.shutdown();require(client.status().exit_code==0,"Owned discovery process must shut down cleanly");
        require(starts(root)==1,"Explicit enrollment must start exactly one owned peer");return alias;
    }
};
std::string literal_arguments(){
    return "{\"pa\\u0074h\":\"effect.txt\",\"body\":"+Json(body).dump()+
        ",\"decimal\":1.00000000000000000001,\"literal_integer\":18446744073709551617,\"quantity\":7}";
}
std::string referenced_arguments(){
    return R"({"pa\u0074h":{"$ref":{"node":"gate","path":["path"]}},"body":{"$ref":{"node":"gate","path":["body"]}},"decimal":1.00000000000000000001,"literal_integer":18446744073709551617,"quantity":{"$ref":{"node":"gate","path":["quantity"]}}})";
}
Json tool_node(const std::string& alias,const std::string& arguments,std::int64_t revision=1){
    return Json{{"id","write"},{"type","tool"},{"tool",alias},{"arguments_json",arguments},
        {"mcp",{{"server_id","graph-peer"},{"config_revision",revision}}}};
}
Json plan(const std::string& alias,bool human=false,bool dependent_read=false){
    auto nodes=Json::array();
    if(human)nodes.push_back({{"id","gate"},{"type","human"},{"prompt","Supply the actual graph fixture file and bytes"}});
    auto write=tool_node(alias,human?referenced_arguments():literal_arguments());
    if(human)write["depends_on"]=Json::array({"gate"});nodes.push_back(std::move(write));
    if(dependent_read){
        const Json reference{{"$ref",{{"node","gate"},{"path",Json::array({"path"})}}}};
        nodes.push_back({{"id","read"},{"type","tool"},{"tool","read_file"},
            {"depends_on",Json::array({"write","gate"})},{"arguments",{{"path",reference}}}});
    }
    return Json{{"nodes",std::move(nodes)}};
}
void catalog(PersistenceService& store,const Json& specification,bool read=false){
    auto graphs=Json::array({{{"id","fixture"},{"spec",specification}}});
    if(read){
        const Json node{{"id","read"},{"type","tool"},{"tool","read_file"},{"arguments",{{"path","effect.txt"}}}};
        graphs.push_back({{"id","read"},{"spec",{{"nodes",Json::array({node})}}}});
    }
    GraphCatalogStore(store).apply(Json{{"graphs",std::move(graphs)}}.dump());
}
void submit(PersistenceService& store,GraphService& service,const std::string& id,const std::string& graph="fixture"){
    store.create_session(id+"-session","Real graph MCP execution; labeled synthetic peer protocol").get();
    service.submit_graph(id,id+"-session",graph,1,"Exercise actual approved MCP file execution");
}
void paused(PersistenceService& store,const std::string& id){
    eventually([&]{return store.run(id).get().state==RunState::paused;},id+": durable human pause did not arrive");
}
std::string human_input(std::string quantity="7"){
    return "{\"path\":\"effect.txt\",\"body\":"+Json(body).dump()+",\"quantity\":"+quantity+"}";
}
void resume(PersistenceService& store,GraphService& service,const std::string& id,const std::string& input=human_input()){
    const auto root=store.graph_run(id).get();service.human_input(id,"gate",input,"fixture-controller",root.checkpoint_revision);
}
void exact_proposal(const Operation& proposal,const std::string& alias){
    const auto binding=Json::parse(proposal.spec.arguments_json);
    require(proposal.spec.tool=="mcp_tool" && proposal.state==OperationState::awaiting_approval && binding["server_config_id"]=="graph-peer" &&
        binding["config_revision"]==1 && binding["alias"]==alias && binding["peer_tool"]=="fixture.write_file" &&
        binding["catalogue_fingerprint"].get<std::string>().size()==64,"Exact permission must bind real configuration/schema/alias identity");
    const auto raw=binding.at("arguments_json").get<std::string>();
    require(raw.find("1.00000000000000000001")!=std::string::npos && raw.find("18446744073709551617")!=std::string::npos &&
        raw.find("\"pa\\u0074h\"")!=std::string::npos && raw.find("$ref")==std::string::npos,"Durable permission must retain raw literal tokens and actual resolved reference data");
    require(Json::parse(raw).at("body")==body && Json::parse(raw).at("quantity")==7,"Reference resolution must use the authenticated human data");
}
void success(char** argv){
    Fixture fixture(argv,"success");std::string alias,operation;
    {
        PersistenceService store(fixture.database,fixture.imports);const auto settings=fixture.settings(store);alias=fixture.discover(store,settings);
        catalog(store,plan(alias,true,true));const auto persisted=GraphCatalogStore(store).load();
        require(persisted.entries.front().plan.nodes()[1].arguments_json==referenced_arguments(),"Catalog apply/load must preserve the exact MCP argument source string");
        {GraphService service(store,settings,1,4);
            require(service.graphs().size()==1 && service.graphs()[0].executable && starts(fixture.root)==1,"Service construction/catalogue metadata must validate without launching a peer");
            submit(store,service,"success-root");paused(store,"success-root");
            require(starts(fixture.root)==1,"Graph admission and durable human pause must not launch another peer");service.close();}
        require(store.children("success-root").get().empty() && calls(fixture.root)==0,"Human pause must precede any tool worker/effect");store.close();
    }
    {
        PersistenceService store(fixture.database,fixture.imports);AgentSettings settings;settings.workspace=fixture.root.string();settings.run_timeout=10s;
        settings.mcp_servers=McpConfigurationStore(store).load();GraphService service(store,settings,1,4);
        require(!service.idle() && store.run("success-root").get().state==RunState::paused,"Reopened native service must adopt the real durable pause");
        require(service.graphs().size()==1 && service.graphs()[0].executable && starts(fixture.root)==1,"Pause adoption and catalogue metadata must not relaunch the enrolled peer");
        const auto saved=GraphPlan(store.graph_run("success-root").get().specification_json);
        require(saved.nodes()[1].arguments_json==referenced_arguments(),"Actual SQLite pause/reopen must preserve literal decimals/large integers/escaped keys");
        resume(store,service,"success-root");const auto proposal=approval(store,"success-root");operation=proposal.id;exact_proposal(proposal,alias);
        require(starts(fixture.root)==2,"Actual resumed tool execution must own exactly one fresh rediscovery peer");
        require(!std::filesystem::exists(fixture.root/"effect.txt") && calls(fixture.root)==0,"Untrusted readOnlyHint must never grant graph permission");
        store.decide_operation(operation,OperationDecision::allow,"fixture-controller").get();
        require(retired(store,service,"success-root").state==RunState::completed,"Approved MCP effect and dependent native read must finish the graph");
        require(read(fixture.root/"effect.txt")==body && calls(fixture.root)==1,"Observed real file bytes must agree with exactly one wire dispatch");
        const auto checkpoint=Json::parse(store.graph_run("success-root").get().checkpoint_json);
        const auto& output=checkpoint["nodes"][1]["output"];
        require(output["acknowledged_by_peer"]==true && output["independently_verified"]==false && output["source"]=="graph_tool" &&
            checkpoint["nodes"][2]["output"]["content"]==body,"Peer acknowledgement and independent native read must retain distinct evidence");
        require(store.children("success-root").get().size()==2 && store.history("success-root-session").get().size()==2 &&
            store.operation(operation).get().state==OperationState::succeeded,"Actual root/child history and approved operation must settle durably");
        service.close();store.close();
    }
    {
        PersistenceService store(fixture.database,fixture.imports);AgentSettings settings;settings.workspace=fixture.root.string();settings.mcp_servers=McpConfigurationStore(store).load();
        GraphService service(store,settings,1,4);require(service.idle() && store.run("success-root").get().state==RunState::completed &&
            store.operation(operation).get().state==OperationState::succeeded && calls(fixture.root)==1 && starts(fixture.root)==2,"Completed acknowledged graph must not replay or launch a peer when reopened");service.close();store.close();
    }
}
void blocked(char** argv,const std::string& mode){
    Fixture fixture(argv,mode);PersistenceService store(fixture.database,fixture.imports);auto settings=fixture.settings(store);const auto enrolled=fixture.discover(store,settings);
    auto alias=enrolled;if(mode=="unknown-alias")alias[4]=alias[4]=='0'?'1':'0';
    auto specification=plan(alias);if(mode=="invalid-arguments")specification["nodes"][0]["arguments_json"]=R"({"path":"effect.txt","body":3,"decimal":1,"literal_integer":1,"quantity":7})";
    catalog(store,specification);GraphService service(store,settings,1,4);submit(store,service,mode+"-root");
    if(mode=="denied" || mode=="cancel-wait"){
        const auto proposal=approval(store,mode+"-root");exact_proposal(proposal,enrolled);
        if(mode=="denied")store.decide_operation(proposal.id,OperationDecision::deny,"fixture-controller").get();else service.cancel(mode+"-root","fixture-controller");
        require(retired(store,service,mode+"-root").state==(mode=="denied"?RunState::failed:RunState::cancelled),"Rejected/cancelled graph must retire without dispatch");
        require(store.operation(proposal.id).get().state==(mode=="denied"?OperationState::denied:OperationState::cancelled),"Actual permission outcome must survive graph retirement");
    }else{
        require(retired(store,service,mode+"-root").state==RunState::failed,"Invalid schema or catalogue identity must explicitly fail the graph");
        no_operations(store,mode+"-root");
    }
    require(calls(fixture.root)==0 && !std::filesystem::exists(fixture.root/"effect.txt"),"Blocked graph must never dispatch the real MCP tool");service.close();store.close();
}
void unsafe_reference(char** argv,const std::string& mode,const std::string& quantity){
    Fixture fixture(argv,mode);PersistenceService store(fixture.database,fixture.imports);const auto settings=fixture.settings(store);const auto alias=fixture.discover(store,settings);
    catalog(store,plan(alias,true));GraphService service(store,settings,1,4);submit(store,service,mode+"-root");paused(store,mode+"-root");
    resume(store,service,mode+"-root",human_input(quantity));require(retired(store,service,mode+"-root").state==RunState::failed,
        "Unsafe referenced numeric data must fail explicitly instead of silently rounding");
    require(store.children(mode+"-root").get().empty() && calls(fixture.root)==0,"Unsafe numeric references must reject before child admission, approval or dispatch");service.close();store.close();
}
void stale_pause(char** argv,const std::string& mode){
    Fixture fixture(argv,mode);
    {
        PersistenceService store(fixture.database,fixture.imports);const auto settings=fixture.settings(store);const auto alias=fixture.discover(store,settings);
        catalog(store,plan(alias,true));GraphService service(store,settings,1,4);submit(store,service,mode+"-root");paused(store,mode+"-root");service.close();store.close();
    }
    {
        PersistenceService store(fixture.database,fixture.imports);auto desired=fixture.configuration();
        if(mode=="stale-disabled")desired["servers"][0]["enabled"]=false;
        else desired["servers"][0]["arguments"].push_back("--configuration-revision-two");
        AgentSettings settings;settings.workspace=fixture.root.string();settings.mcp_servers=McpConfigurationStore(store).apply(desired.dump());
        require(settings.mcp_servers[0].revision==2,"Actual configuration change must persist a new backend revision");
        GraphService service(store,settings,1,4);const auto before=store.graph_run(mode+"-root").get();const auto events=store.graph_events(mode+"-root").get().size();
        require(service.healthy() && !service.idle() && service.graphs().size()==1 && !service.graphs()[0].executable,
            "Stale paused graph must remain owned/inspectable without taking down unrelated backend services");
        require(starts(fixture.root)==1,"Stale pause adoption and catalogue metadata must not launch a configured peer");
        rejects<RunUnavailable>([&]{resume(store,service,mode+"-root");},"Stale human resume must reject before data commit or worker scheduling");
        const auto after=store.graph_run(mode+"-root").get();
        require(after.run.state==RunState::paused && after.checkpoint_revision==before.checkpoint_revision && after.checkpoint_json==before.checkpoint_json &&
            store.graph_events(mode+"-root").get().size()==events && store.children(mode+"-root").get().empty() && calls(fixture.root)==0 && starts(fixture.root)==1,
            "Rejected stale resume must leave the exact committed checkpoint/events and peer untouched");
        service.cancel(mode+"-root","fixture-controller");require(store.run(mode+"-root").get().state==RunState::cancelled && service.idle(),
            "Controller must still cancel an inspectable stale human pause");service.close();store.close();
    }
}
void static_validation(char** argv){
    Fixture fixture(argv,"static-validation");PersistenceService store(fixture.database,fixture.imports);auto settings=fixture.settings(store);const auto alias=fixture.discover(store,settings);
    auto valid=plan(alias);GraphRunner runner(store,settings);runner.validate(GraphPlan(valid.dump()));
    auto invalid=valid;invalid["nodes"][0].erase("mcp");rejects<std::invalid_argument>([&]{GraphPlan(invalid.dump());},"MCP alias must require a trusted binding");
    invalid=valid;invalid["nodes"][0]["arguments"]={{"path","effect.txt"}};rejects<std::invalid_argument>([&]{GraphPlan(invalid.dump());},"MCP argument source must not accept a lossy alternate object");
    invalid=valid;invalid["nodes"][0]["arguments_json"]="{\"path\":1,\"path\":2}";rejects<std::invalid_argument>([&]{GraphPlan(invalid.dump());},"Duplicate raw MCP argument fields must reject");
    invalid=valid;invalid["nodes"][0]["tool"]="mcp_not-a-bounded-alias";rejects<std::invalid_argument>([&]{GraphPlan(invalid.dump());},"Malformed MCP alias must reject");
    invalid=valid;invalid["nodes"][0]["mcp"]["config_revision"]=2;rejects<RunUnavailable>([&]{runner.validate(GraphPlan(invalid.dump()));},"Stale binding must reject without peer start");
    invalid=valid;invalid["nodes"][0]["mcp"]["server_id"]="absent-server";rejects<RunUnavailable>([&]{runner.validate(GraphPlan(invalid.dump()));},"Missing configured server must reject");
    auto disabled=settings;disabled.mcp_servers[0].enabled=false;GraphRunner unavailable(store,disabled);
    rejects<RunUnavailable>([&]{unavailable.validate(GraphPlan(valid.dump()));},"Disabled bound server must reject");
    auto duplicate=settings;duplicate.mcp_servers.push_back(duplicate.mcp_servers.front());
    rejects<std::invalid_argument>([&]{GraphRunner invalid_snapshot(store,duplicate);},"MCP-only owner must reject duplicate trusted server identities");
    auto bad_revision=settings;bad_revision.mcp_servers[0].revision=0;
    rejects<std::invalid_argument>([&]{GraphRunner invalid_snapshot(store,bad_revision);},"MCP-only owner must reject a zero trusted revision");
    bad_revision.mcp_servers[0].revision=9007199254740992LL;
    rejects<std::invalid_argument>([&]{GraphRunner invalid_snapshot(store,bad_revision);},"MCP-only owner must reject an unsafe trusted revision");
    auto too_many=settings;too_many.mcp_servers.clear();
    for(int index=0;index<17;++index){auto server=settings.mcp_servers.front();server.id="fixture-server-"+std::to_string(index);too_many.mcp_servers.push_back(std::move(server));}
    rejects<std::invalid_argument>([&]{GraphRunner invalid_snapshot(store,too_many);},"MCP-only owner must enforce the configured server bound");
    require(calls(fixture.root)==0 && starts(fixture.root)==1,"Validation must never launch another peer or dispatch an external effect");store.close();
}
void post_effect(char** argv,const std::string& mode){
    Fixture fixture(argv,mode);std::string operation;
    {
        PersistenceService store(fixture.database,fixture.imports);const auto settings=fixture.settings(store);const auto alias=fixture.discover(store,settings);
        catalog(store,plan(alias));GraphService service(store,settings,1,4);submit(store,service,mode+"-root");const auto proposal=approval(store,mode+"-root");operation=proposal.id;
        store.decide_operation(operation,OperationDecision::allow,"fixture-controller").get();require(retired(store,service,mode+"-root").state==RunState::failed,
            "Uncertain or oversized observed MCP output must fail its real graph child");
        const auto expected=mode=="lost-reply"?OperationState::uncertain:OperationState::succeeded;
        require(store.operation(operation).get().state==expected && read(fixture.root/"effect.txt")==body && calls(fixture.root)==1,
            "Graph failure must preserve the actual file effect and its distinct durable operation outcome");
        if(mode=="oversized-output"){
            require(store.operation(operation).get().result_json.size()>65536,"Oversized test must exercise the final acknowledged registry envelope");
            bool explicit_failure=false;for(const auto& event:store.graph_events(mode+"-root").get())
                if(event.json.find("graph_mcp_output_exceeds_limit")!=std::string::npos)explicit_failure=true;
            require(explicit_failure,"Acknowledged oversized output must have the specific non-replayable child failure reason");
            for(const auto& child:store.children(mode+"-root").get())for(const auto& event:store.events(child.id).get())
                require(event.kind!="tool.completed","Oversized acknowledgement must not invent a completed graph-tool event");
        }
        service.close();store.close();
    }
    {
        PersistenceService store(fixture.database,fixture.imports);AgentSettings settings;settings.workspace=fixture.root.string();settings.mcp_servers=McpConfigurationStore(store).load();
        GraphService service(store,settings,1,4);require(service.idle() && store.run(mode+"-root").get().state==RunState::failed && calls(fixture.root)==1 &&
            read(fixture.root/"effect.txt")==body,"Actual restart must not replay failed graphs with observed/uncertain MCP effects");
        require(store.operation(operation).get().state==(mode=="lost-reply"?OperationState::uncertain:OperationState::succeeded),"Restart must retain uncertain versus succeeded acknowledgement");
        if(mode=="lost-reply"){
            submit(store,service,"quarantined-new-root");const auto next=approval(store,"quarantined-new-root");
            store.decide_operation(next.id,OperationDecision::allow,"fixture-controller").get();
            require(retired(store,service,"quarantined-new-root").state==RunState::failed && store.operation(next.id).get().state==OperationState::cancelled &&
                calls(fixture.root)==1 && read(fixture.root/"effect.txt")==body,"Persisted uncertainty must block even a separately approved new root from replaying the configured peer effect");
        }
        service.close();store.close();
    }
}
void journal_fault(char** argv){
    Fixture fixture(argv,"journal-fault");std::string operation;
    {
        PersistenceService store(fixture.database,fixture.imports);const auto settings=fixture.settings(store);const auto alias=fixture.discover(store,settings);
        catalog(store,plan(alias),true);GraphService service(store,settings,1,4);submit(store,service,"journal-root");const auto proposal=approval(store,"journal-root");operation=proposal.id;
        submit(store,service,"queued-after-fault","read");
        {XlangSqlite inject(fixture.database,fixture.imports);inject.execute("CREATE TRIGGER reject_graph_mcp_ack BEFORE UPDATE OF state ON operations WHEN NEW.state='succeeded' BEGIN SELECT RAISE(ABORT,'labeled real MCP journal fixture failure'); END");}
        store.decide_operation(operation,OperationDecision::allow,"fixture-controller").get();eventually([&]{return !service.healthy();},"Unrecorded actual MCP acknowledgement must fail-stop the owner");
        require(read(fixture.root/"effect.txt")==body && calls(fixture.root)==1 && store.operation(operation).get().state==OperationState::executing &&
            store.run("journal-root").get().state==RunState::running && store.children("queued-after-fault").get().empty(),
            "Service fault must retain actual bytes/unfinished claim and stop queued dispatch without inventing retirement");
        rejects<RunUnavailable>([&]{service.submit_graph("forbidden-admission","queued-after-fault-session","read",1,"must reject");},"Faulted service must reject new roots");
        for(const auto& graph:service.graphs())require(!graph.executable,"Faulted service must stop advertising executable graphs");
        service.close();require(store.run("queued-after-fault").get().state==RunState::cancelled,"Owned shutdown must retire undispatched queued work");
        {XlangSqlite inject(fixture.database,fixture.imports);inject.execute("DROP TRIGGER reject_graph_mcp_ack");}store.close();
    }
    {
        PersistenceService store(fixture.database,fixture.imports);AgentSettings settings;settings.workspace=fixture.root.string();settings.mcp_servers=McpConfigurationStore(store).load();
        GraphService service(store,settings,1,4);require(service.idle() && store.operation(operation).get().state==OperationState::uncertain &&
            store.run("journal-root").get().state==RunState::failed && calls(fixture.root)==1 && read(fixture.root/"effect.txt")==body,
            "Restart must quarantine the unfinished acknowledged MCP claim without replay");service.close();store.close();
    }
}
void contention(char** argv){
    Fixture fixture(argv,"contention");PersistenceService store(fixture.database,fixture.imports);const auto settings=fixture.settings(store);const auto alias=fixture.discover(store,settings);
    catalog(store,plan(alias));GraphService service(store,settings,2,4);submit(store,service,"first-contender");const auto first=approval(store,"first-contender");
    store.decide_operation(first.id,OperationDecision::allow,"fixture-controller").get();eventually([&]{return std::filesystem::exists(fixture.root/"effect.marker");},"First actual peer effect did not occur");
    submit(store,service,"second-contender");const auto second=approval(store,"second-contender");store.decide_operation(second.id,OperationDecision::allow,"fixture-controller").get();
    std::this_thread::sleep_for(220ms);
    require(store.operation(first.id).get().state==OperationState::executing && store.operation(second.id).get().state==OperationState::ready && calls(fixture.root)==1,
        "Exact approved second root must wait for the shared configured-server/workspace claim before wire dispatch");
    {std::ofstream release(fixture.root/"release-ack",std::ios::binary);release<<"Fixture controller releases the held synthetic acknowledgement";}
    eventually([&]{return store.run("first-contender").get().state==RunState::completed && store.run("second-contender").get().state==RunState::completed;},"Serialized actual graph effects did not finish");
    eventually([&]{return service.idle();},"Completed contending graph roots did not retire");
    require(store.operation(first.id).get().state==OperationState::succeeded && store.operation(second.id).get().state==OperationState::succeeded &&
        calls(fixture.root)==2 && read(fixture.root/"effect.txt")==body+body,"Releasing one claim must permit exactly one later approved effect");service.close();store.close();
}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    try{
        success(argv);
        for(const auto* mode:{"denied","cancel-wait","invalid-arguments","unknown-alias","catalogue-drift"})blocked(argv,mode);
        unsafe_reference(argv,"float-reference","3.14");unsafe_reference(argv,"unsafe-integer-reference","9007199254740992");
        stale_pause(argv,"stale-disabled");stale_pause(argv,"stale-revision");static_validation(argv);
        post_effect(argv,"lost-reply");post_effect(argv,"oversized-output");journal_fault(argv);contention(argv);
        std::cout<<"Native graph MCP contract passed actual owned peer file writes, real GraphRunner/GraphService and embedded-xlang3 SQLite: exact enrollment/approval, raw literal precision, typed human references and pause/reopen, dependent native read, denied/cancelled/invalid/drift/stale paths, serialized root claims, lost-reply uncertainty, acknowledged output bounds, journal fail-stop and quarantine without replay. Peer descriptors/replies are synthetic; no live model, UI or independent peer acknowledgement verification claimed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
