// Real native parent/leaf engines and xlang3 SQLite. Provider replies and the
// direct repository lifecycle inputs below are explicitly synthetic fixtures.
#include "agentflow/agent_runner.hpp"
#include "agentflow/agent_service.hpp"
#include "agentflow/delegation_executor.hpp"
#include "agentflow/root_execution_budget.hpp"
#include "agentflow/schema_worker.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "graph_schema_fixture.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
constexpr const char* fixture_key="synthetic-native-delegation-key-not-live";
const std::string left_file="Actual left delegation file: \"quoted\" 雪\n";
const std::string right_file="Actual right delegation file bytes\n";
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action,const char* reason){try{action();}catch(const Error&){return;}throw std::runtime_error(reason);}
template<class Action>void eventually(Action action,const char* reason){const auto deadline=std::chrono::steady_clock::now()+8s;while(std::chrono::steady_clock::now()<deadline){if(action())return;std::this_thread::sleep_for(5ms);}throw std::runtime_error(reason);}
std::size_t count(const std::vector<Event>& events,const std::string& kind){std::size_t total=0;for(const auto& event:events)if(event.kind==kind)++total;return total;}
void same_history(const std::vector<Message>& a,const std::vector<Message>& b){require(a.size()==b.size(),"Reopen must preserve every history row");for(std::size_t i=0;i<a.size();++i)require(a[i].sequence==b[i].sequence&&a[i].role==b[i].role&&a[i].json==b[i].json,"Reopen must preserve exact conversation bytes/sequence");}
void private_absent(const std::vector<Message>& messages,const std::vector<Event>& events){for(const auto& message:messages)require(message.json.find(fixture_key)==std::string::npos,"Owned credential must not enter conversation");for(const auto& event:events)require(event.json.find(fixture_key)==std::string::npos,"Owned credential must not enter execution events");}
std::string read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
Operation approval(PersistenceService& store,const std::string& root){std::optional<Operation> result;eventually([&]{for(const auto& operation:store.operations(root).get())if(operation.state==OperationState::awaiting_approval){result=operation;return true;}return false;},"Actual parent edit approval did not arrive");return *result;}
struct Execution {
    std::stop_source stop;std::future<Run> result;
    Execution(AgentRunner& runner,const std::string& root):result(std::async(std::launch::async,[this,&runner,root]{return runner.execute(root,stop.get_token());})){}
    ~Execution(){stop.request_stop();if(result.valid())result.wait();}
};
AgentSettings settings(const std::string& endpoint,const std::string& workspace){
    AgentSettings value;value.provider={endpoint,"fixture-delegation",Capability::supported,Capability::supported,Capability::supported};
    value.provider.wire=ProviderWire::anthropic_messages;value.provider.deadline=8s;value.provider.idle_timeout=6s;
    value.workspace=workspace;value.approved_edits=true;value.max_output_tokens=256;value.max_turns=8;value.run_timeout=20s;
    value.instructions="Labeled synthetic delegation provider fixture: use only actual authorized tool outcomes.";
    value.credential=CredentialReference{"fixture","delegation-provider","provider:fixture-delegation"};value.delegation=AgentDelegationPolicy{};return value;
}
void metrics(const Json& answer){require(answer.contains("usage")&&answer["usage"].at("input_tokens").is_number_integer()&&answer["usage"].at("output_tokens").is_number_integer(),"Actual child must retain supplied per-response usage");require(!answer["usage"].contains("total_tokens"),"Missing provider totals must remain missing");require(answer.contains("elapsed_ms")&&answer["elapsed_ms"]>=0&&answer.contains("first_token_ms")&&answer["first_token_ms"]>=0,"Actual child must retain measured native timing");}
void actual_success(PersistenceService& store,const AgentSettings& value,const std::filesystem::path& workspace,std::vector<Message>& root_history,std::vector<OwnedChildRecord>& saved_children){
    auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("success-session","Actual native delegation and controller-approved edit").get();runner.start("success-root","success-session","fixture-parent-success: Investigate the actual files and apply one reviewed edit after observing findings.");
    require(store.root_budget("success-root").get().children_admitted==0,"Root budget must be admitted atomically before provider execution");
    Execution execution(runner,"success-root");const auto proposed=approval(store,"success-root");
    require(read(workspace/"target.txt")=="original\n"&&execution.result.wait_for(30ms)==std::future_status::timeout,"Delegation may not grant the parent's mutation permission");
    const auto review=Json::parse(proposed.spec.arguments_json);require(proposed.spec.run_id=="success-root"&&proposed.spec.tool=="replace_file"&&review["before_content"]=="original\n"&&review["after_content"]=="reviewed native delegation\n","Exact controller proposal must bind the actual parent and file snapshots");
    saved_children=store.owned_children("success-root").get();require(saved_children.size()==2,"Actual parent-selected batch must admit exactly two owned leaves");
    for(const auto& child:saved_children)require(child.run.state==RunState::completed&&child.kind=="delegated_leaf"&&child.preset_id=="workspace.inspect"&&child.preset_revision==1&&store.operations(child.run.id).get().empty(),"Read-only leaves must retire under their explicit immutable preset without mutation proposals");
    store.decide_operation(proposed.id,OperationDecision::allow,"fixture-controller").get();require(execution.result.get().state==RunState::completed,"Actual joined child findings and approved file effect must complete the parent");
    require(read(workspace/"target.txt")=="reviewed native delegation\n"&&store.operation(proposed.id).get().state==OperationState::succeeded,"Actual reviewed parent effect must be durably succeeded");
    root_history=store.history("success-session").get();require(root_history.size()==6,"Two coupled parent tool turns and final answer must retain exactly six history rows");
    const auto batches=store.delegation_batches("success-root").get();require(batches.size()==1&&batches.front().state=="completed"&&batches.front().tasks.size()==2,"Completed delegation ledger must describe the actual children");
    const auto original=Json::parse(batches.front().parent_assistant_json);
    require(original.at("tool_calls")[0].at("id")=="delegate-success"&&original.at("provider_items")[0].at("content_json").get<std::string>().find("opaque-parent-delegate")!=std::string::npos,"Accepted ledger must retain the actual parent signed tool turn");
    require(Json::parse(root_history[1].json)==original,"Replayable parent tool turn must retain the accepted actual signed receipt exactly");
    for(const auto& child:saved_children){
        const auto rows=store.owned_child_history("success-root",child.run.id).get();require(rows.size()==4&&rows[0].role=="user"&&rows[1].role=="assistant"&&rows[2].role=="tool"&&rows[3].role=="assistant","Each real leaf tool loop must own its independent four-row history");
        const bool left=child.task_id=="left";const auto tool=Json::parse(rows[1].json),result=Json::parse(Json::parse(rows[2].json).at("content").get<std::string>()),answer=Json::parse(rows.back().json);metrics(tool);metrics(answer);
        require(tool.at("tool_calls")[0].at("arguments")==std::string("{\"pa\\u0074h\":\"")+(left?"left.txt":"right.txt")+"\"}","Leaf signed replay must retain original escaped argument spelling");
        require(result==Json{{"path",left?"left.txt":"right.txt"},{"content",left?left_file:right_file}},"Actual native workspace bytes must reach the leaf's durable result");
        require(answer["usage"]["input_tokens"]==(left?29:31)&&answer["usage"]["output_tokens"]==(left?7:9),"Leaf metrics must retain its own supplied counters without sums or sibling substitution");
        for(const auto& row:rows)require(row.json.find("opaque-parent-")==std::string::npos&&row.json.find(left?"opaque-leaf-right":"opaque-leaf-left")==std::string::npos,"Signed provider histories must stay inside their actual execution");
        private_absent(rows,store.events(child.run.id).get());const auto admission=store.child_admission(child.run.id).get();require(admission.kind==ChildAdmissionKind::delegated_leaf&&admission.root_run_id=="success-root"&&admission.preset_id=="workspace.inspect","Typed child admission must bind actual owner/root/preset");
    }
    for(const auto& row:root_history)require(row.json.find("opaque-leaf-left")==std::string::npos&&row.json.find("opaque-leaf-right")==std::string::npos,"Observed parent join must not import signed leaf receipts");
    const auto budget=store.root_budget("success-root").get();require(budget.children_admitted==2&&budget.model_calls_reserved==7&&budget.parent_calls_held==0,"Parent and both leaves must consume one shared durable seven-call budget");
    const auto tree=store.tree_events("success-root",0,256).get();require(!tree.empty(),"Generic committed tree observation must include parent and children");std::int64_t cursor=0;bool child_seen=false;for(const auto& event:tree){require(event.sequence>cursor,"Committed tree cursor must remain globally monotonic");cursor=event.sequence;if(event.run_id!="success-root")child_seen=true;}
    require(child_seen&&store.tree_events("success-root",cursor,256).get().empty(),"Tree reconnect must observe identified child events without replay");
    rejects<Conflict>([&]{runner.execute("success-root");},"Completed parent cannot be executed twice");same_history(root_history,store.history("success-session").get());private_absent(root_history,tree);pool->close();
}
void actual_followup(PersistenceService& store,AgentSettings value){
    auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("followup-session","Observed failed leaf and real new investigation").get();runner.start("followup-root","followup-session","fixture-parent-followup: Observe the initial leaf failure and decide a different investigation.");
    require(runner.execute("followup-root").state==RunState::completed,"Parent must continue from an observed read-only leaf failure");const auto batches=store.delegation_batches("followup-root").get();require(batches.size()==2&&batches[0].id!=batches[1].id&&batches[0].provider_tool_call_id!=batches[1].provider_tool_call_id,"Follow-up must use a new immutable actual batch identity");
    const auto children=store.owned_children("followup-root").get();require(children.size()==2,"Follow-up must not replay or replace the earlier failed leaf");bool failed=false,finished=false;
    for(const auto& child:children){if(child.task_id=="forbidden"){failed=true;require(child.run.state==RunState::failed&&store.run_history(child.run.id).get().size()==1&&store.operations(child.run.id).get().empty(),"Unadvertised leaf mutation must fail before tool effect or conversation success");}else{finished=true;require(child.task_id=="followup"&&child.run.state==RunState::completed&&store.run_history(child.run.id).get().size()==4,"New actual read leaf must retain its own observed history");}}
    require(failed&&finished&&store.operations("followup-root").get().empty()&&store.root_budget("followup-root").get().model_calls_reserved==6,"Failed call and later actual investigation must consume the same root budget without invented effects");pool->close();
}
void actual_parallel(PersistenceService& store,const AgentSettings& value){
    store.create_session("parallel-a-session","First occupied actual parent worker").get();store.create_session("parallel-b-session","Second occupied actual parent worker").get();
    AgentService service(store,value,2,4);service.submit("parallel-a-root","parallel-a-session","fixture-parent-parallel-a: Investigate actual files through two independent leaves.");service.submit("parallel-b-root","parallel-b-session","fixture-parent-parallel-b: Investigate actual files through two independent leaves.");
    eventually([&]{return service.idle();},"Two occupied real parent workers must not deadlock their separate bounded leaf pool");
    for(const auto* root:{"parallel-a-root","parallel-b-root"}){require(store.run(root).get().state==RunState::completed&&store.owned_children(root).get().size()==2,"Each occupied parent must join only its two actual completed children");const auto budget=store.root_budget(root).get();require(budget.children_admitted==2&&budget.model_calls_reserved==6&&budget.parent_calls_held==0,"Concurrent roots must retain distinct shared parent/leaf call allowances");}
    service.close();
}
void actual_nested(PersistenceService& store,const AgentSettings& value){
    auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("nested-session","Actual leaf rejects unadvertised recursive delegation").get();runner.start("nested-root","nested-session","fixture-parent-nested: Observe a leaf attempting unsupported recursive delegation.");
    require(runner.execute("nested-root").state==RunState::completed,"Parent must observe the actual recursive-delegation rejection");const auto children=store.owned_children("nested-root").get();
    require(children.size()==1&&children.front().run.state==RunState::failed&&store.run_history(children.front().run.id).get().size()==1&&store.root_budget("nested-root").get().children_admitted==1&&store.root_budget("nested-root").get().model_calls_reserved==3,"Unadvertised recursive tool must reject before a grandchild, effect or extra provider call");pool->close();
}
void actual_budget_race(PersistenceService& store,AgentSettings value){
    value.delegation->max_model_calls=3;auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("budget-session","Actual two-leaf final shared-call allowance race").get();runner.start("budget-root","budget-session","fixture-parent-budget: Observe children exhausting their shared model allowance.");
    require(runner.execute("budget-root").state==RunState::completed,"Held parent continuation must perform its actual final provider request after leaf exhaustion");
    const auto children=store.owned_children("budget-root").get();require(children.size()==2,"Both typed leaves must be admitted before competing for the one unheld call");std::size_t actual_reads=0;
    for(const auto& child:children){require(child.run.state==RunState::failed&&store.operations(child.run.id).get().empty(),"Exhausted real leaf must fail explicitly without invented completion/effects");actual_reads+=count(store.events(child.run.id).get(),"tool.completed");const auto rows=store.run_history(child.run.id).get();require(rows.size()==1||rows.size()==3,"Call-race loser must retain its prompt; winner must retain the actual read turn");}
    const auto budget=store.root_budget("budget-root").get();require(actual_reads==1&&budget.children_admitted==2&&budget.model_calls_reserved==3&&budget.parent_calls_held==0&&store.history("budget-session").get().size()==4,"One actual extra leaf call and one actual held parent continuation must consume exactly the three shared attempts");pool->close();
}
void actual_cancel(PersistenceService& store,const AgentSettings& value){
    auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("cancel-session","Cancellation of actual held native leaf streams").get();runner.start("cancel-root","cancel-session","fixture-parent-cancel: Delegate the two held fixture investigations.");Execution execution(runner,"cancel-root");
    eventually([&]{const auto children=store.owned_children("cancel-root").get();if(children.size()!=2)return false;for(const auto& child:children)if(count(store.events(child.run.id).get(),"model.text")==0)return false;return true;},"Both actual leaf transports must stream before root cancellation");
    execution.stop.request_stop();require(execution.result.get().state==RunState::cancelled,"Parent cancellation must drain dispatched real leaves before retirement");
    for(const auto& child:store.owned_children("cancel-root").get())require(child.run.state==RunState::cancelled&&store.run_history(child.run.id).get().size()==1,"Cancelled partial leaf replies must not become fake persisted assistants");
    require(store.history("cancel-session").get().size()==1&&store.operations("cancel-root").get().empty()&&store.root_budget("cancel-root").get().model_calls_reserved==3,"Cancelled accepted delegation cannot leave dangling parent tool turns or unbudgeted retries");pool->close();
}
void actual_admission_fault(PersistenceService& store,const AgentSettings& value,const std::string& database,const std::vector<std::string>& imports){
    auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("admission-fault-session","Actual second-task SQL admission fault").get();runner.start("admission-fault-root","admission-fault-session","fixture-parent-admission-fault: Exercise atomic native admission.");
    {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_real_delegation_task BEFORE INSERT ON delegation_tasks WHEN NEW.task_id='right' BEGIN SELECT RAISE(ABORT,'labeled actual delegation admission fault'); END");}
    require(runner.execute("admission-fault-root").state==RunState::failed,"Actual batch admission SQL failure must fail before launching child providers");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_real_delegation_task");}
    const auto budget=store.root_budget("admission-fault-root").get();require(store.delegation_batches("admission-fault-root").get().empty()&&store.owned_children("admission-fault-root").get().empty()&&store.history("admission-fault-session").get().size()==1&&budget.children_admitted==0&&budget.parent_calls_held==0,"Failed admission must roll back batch/tasks/runs/private prompts/budget/held allowance together");pool->close();
}
void actual_service_fault(PersistenceService& store,const AgentSettings& value,const std::string& database,const std::vector<std::string>& imports,const std::filesystem::path& workspace,const std::string& mode){
    const std::string id=mode+"-root",queued=mode+"-queued",trigger=mode=="conversation-fault"?"reject_delegation_parent_turn":"reject_delegation_settlement";
    {XlangSqlite sql(database,imports);sql.execute(mode=="conversation-fault"?
        "CREATE TRIGGER reject_delegation_parent_turn BEFORE INSERT ON messages WHEN NEW.role='tool' AND NEW.execution_run_id IS NULL AND NEW.payload LIKE '%delegate-conversation-fault%' BEGIN SELECT RAISE(ABORT,'labeled actual coupled parent conversation fault'); END":
        "CREATE TRIGGER reject_delegation_settlement BEFORE INSERT ON events WHEN NEW.kind='delegation.child.settled' BEGIN SELECT RAISE(ABORT,'labeled actual child settlement event fault'); END");}
    store.create_session(id+"-session","Actual delegation owner fail-stop").get();store.create_session(queued+"-session","Queued owner must not dispatch after fault").get();
    AgentService service(store,value,1,4);service.submit(id,id+"-session","fixture-parent-"+mode+": Exercise actual result storage fault.");
    eventually([&]{const auto children=store.owned_children(id).get();return children.size()==(mode=="conversation-fault"?2:1);},"Fault fixture must own accepted children before queueing another root");
    service.submit(queued,queued+"-session","fixture-parent-undispatched: Must remain queued behind the owned parent.");
    {std::ofstream release(workspace/("release-"+mode),std::ios::binary);release<<"Fixture releases synthetic provider replies after actual queue admission";}
    eventually([&]{return !service.healthy();},"Unrecorded delegation outcome/parent turn must fail-stop the actual AgentService");
    require(store.run(id).get().state==RunState::running&&store.history(id+"-session").get().size()==1&&store.owned_children(queued).get().empty(),"Storage fault must retain the unfinished parent ledger and halt queued child/provider dispatch");
    for(const auto& child:store.owned_children(id).get())require(child.run.state==RunState::completed&&store.run_history(child.run.id).get().size()==4,"Unrecorded settlement/parent history must preserve earlier completed actual leaf histories");
    if(mode=="conversation-fault")require(store.delegation_batches(id).get().front().state=="completed","Coupled parent-row failure must retain the already completed actual batch outcome");
    service.close();require(store.run(queued).get().state==RunState::cancelled,"Fail-stop service must drain demonstrably undispatched queued roots");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER "+trigger);}
}
void actual_oversize(PersistenceService& store,const AgentSettings& value){
    auto pool=std::make_shared<DelegationExecutor>(store,4,64);AgentRunner runner(store,value,pool);store.create_session("oversize-session","Actual completed leaf exceeds encoded join bound").get();runner.start("oversize-root","oversize-session","fixture-parent-oversize: Observe the explicit leaf result-envelope bound.");
    require(runner.execute("oversize-root").state==RunState::completed,"Parent must observe the typed limit outcome without replaying completed child work");const auto children=store.owned_children("oversize-root").get();require(children.size()==1&&children.front().run.state==RunState::completed,"Oversized child must retain its actual completed status");
    const auto rows=store.run_history(children.front().run.id).get();require(rows.size()==4&&Json::parse(rows.back().json).at("content").get<std::string>()==std::string(40000,'"'),"Original full child answer/history must survive without silent truncation");
    const auto batch=store.delegation_batches("oversize-root").get().front();require(batch.result_json.size()<=65536&&batch.result_json.find("limit")!=std::string::npos&&batch.result_json.find(children.front().run.id)!=std::string::npos,"Bounded join must retain actual child identity and explicit encoded-envelope limit");pool->close();
}
RootBudgetSpec repository_budget(const std::string& workspace,std::int64_t calls=32,std::int64_t children=8){RootBudgetSpec spec;spec.policy_id="native.delegation";spec.policy_revision=1;spec.workspace_identity=workspace;spec.provider_identity_json=R"({"wire":"anthropic-messages","model_id":"fixture-delegation"})";spec.max_model_calls=calls;spec.max_children=children;spec.wall_limit_ms=20000;return spec;}
void repository_root(PersistenceService& store,const std::string& id,const RootBudgetSpec& budget){store.create_session(id+"-session","Synthetic repository lifecycle input; real xlang3 transaction").get();store.start_prompt_run(id,id+"-session",R"({"content":"Synthetic repository lifecycle input"})",budget).get();store.transition(id,RunState::queued,RunState::running).get();}
DelegationBatchSpec repository_batch(PersistenceService& store,const std::string& root,const std::string& id,const std::vector<std::string>& labels){
    DelegationBatchSpec spec;spec.id=id;spec.parent_run_id=root;spec.provider_tool_call_id="provider-"+id;spec.expected_budget_revision=store.root_budget(root).get().revision;
    auto tasks=Json::array();for(const auto& label:labels){const auto objective="Synthetic repository task "+label;tasks.push_back({{"id",label},{"objective",objective},{"preset","workspace.inspect"}});spec.tasks.push_back({label,id+"-"+label,id+"-child-"+label,Json{{"content",objective}}.dump(),objective});}
    spec.arguments_json=Json{{"tasks",std::move(tasks)}}.dump();spec.parent_assistant_json=Json{{"content",""},{"tool_calls",Json::array({{{"id",spec.provider_tool_call_id},{"name","delegate_tasks"},{"arguments",spec.arguments_json}}})}}.dump();return spec;
}
void requested_arguments(DelegationBatchSpec& spec,std::string arguments){
    spec.arguments_json=std::move(arguments);auto assistant=Json::parse(spec.parent_assistant_json);assistant["tool_calls"][0]["arguments"]=spec.arguments_json;spec.parent_assistant_json=assistant.dump();
}
void repository_contracts(PersistenceService& store,const std::string& database,const std::vector<std::string>& imports,const std::string& workspace){
    const auto budget=repository_budget(workspace);repository_root(store,"repository-root",budget);auto specification=repository_batch(store,"repository-root","repository-batch",{"left","right"});
    const auto definition=DelegationExecutor::definition();SchemaWorker().evaluate(definition.input_schema_json,specification.arguments_json,std::chrono::steady_clock::now()+3s);
    rejects<SchemaArgumentsInvalid>([&]{SchemaWorker().evaluate(definition.input_schema_json,R"({"tasks":[{"id":"one","objective":"inspect","preset":"workspace.inspect","actor":"model-injected-controller"}]})",std::chrono::steady_clock::now()+3s);},"Actual native schema worker must reject injected leaf authority");
    auto invalid=specification;invalid.preset_id="workspace.write";rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"Unknown execution preset cannot admit a more capable leaf");
    invalid=specification;invalid.preset_revision=2;rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"An unregistered preset revision cannot silently fall back");
    invalid=specification;invalid.provider_tool_call_id="not-the-actual-provider-call";rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"Batch must belong to its exact actual parent tool-call identity");
    invalid=specification;requested_arguments(invalid,R"({"tasks":[],"tasks":[]})");rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"Duplicate raw delegation fields cannot become trusted admission");
    invalid=specification;auto injected=Json::parse(invalid.arguments_json);injected["root_run_id"]="model-selected-owner";requested_arguments(invalid,injected.dump());rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"Model arguments cannot choose another execution root");
    invalid=specification;injected=Json::parse(invalid.arguments_json);injected["tasks"][1]["id"]="left";invalid.tasks[1].task_id="left";requested_arguments(invalid,injected.dump());rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"Duplicate task labels cannot admit multiple children for one task");
    invalid=specification;invalid.tasks[0].prompt_json=R"({"content":"changed objective","credential_id":"model-selected-key"})";rejects<std::invalid_argument>([&]{store.accept_delegation_batch(invalid).get();},"Private child prompt must bind only the accepted objective and immutable provider context");
    require(store.owned_children("repository-root").get().empty()&&store.delegation_batches("repository-root").get().empty()&&store.root_budget("repository-root").get().children_admitted==0,"All authority/schema negatives must reject before durable child admission");
    const auto before=store.root_budget("repository-root").get();const auto event_count=store.events("repository-root").get().size();
    {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_repo_admission_event BEFORE INSERT ON events WHEN NEW.kind='delegation.child.admitted' BEGIN SELECT RAISE(ABORT,'labeled repository admission event failure'); END");}
    rejects<DatabaseError>([&]{store.accept_delegation_batch(specification).get();},"Actual admission event failure must roll back the whole batch");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_repo_admission_event");}
    require(store.owned_children("repository-root").get().empty()&&store.delegation_batches("repository-root").get().empty()&&store.events("repository-root").get().size()==event_count&&store.root_budget("repository-root").get().revision==before.revision,"Event-fault rollback must preserve exact pre-admission ownership/budget revision");
    const auto admitted=store.accept_delegation_batch(specification).get();require(admitted.created&&admitted.tasks.size()==2&&store.root_budget("repository-root").get().children_admitted==2&&store.root_budget("repository-root").get().parent_calls_held==1,"Actual atomic typed admission must establish children and held parent continuation");
    const auto duplicate=store.accept_delegation_batch(specification).get();require(!duplicate.created&&duplicate.id==admitted.id&&store.owned_children("repository-root").get().size()==2,"Identical actual provider-call retry must observe existing children without readmission");
    auto changed_backend_task=specification;changed_backend_task.tasks[0].objective="Changed backend objective on the same provider call";changed_backend_task.tasks[0].prompt_json=Json{{"content",changed_backend_task.tasks[0].objective}}.dump();
    rejects<std::invalid_argument>([&]{store.accept_delegation_batch(changed_backend_task).get();},"Identical saved raw arguments cannot authorize changed backend objective/prompt through deduplication");
    auto colon_task=specification;auto colon_arguments=Json::parse(colon_task.arguments_json);colon_task.tasks[0].task_id="left:injected";colon_arguments["tasks"][0]["id"]=colon_task.tasks[0].task_id;requested_arguments(colon_task,colon_arguments.dump());
    rejects<std::invalid_argument>([&]{store.accept_delegation_batch(colon_task).get();},"A colon task identity must reject even when its typed and raw requested labels match");
    auto nested=specification;nested.id="injected-nested-batch";nested.parent_run_id=admitted.tasks.front().run.id;rejects<std::invalid_argument>([&]{store.accept_delegation_batch(nested).get();},"Typed admission must reject delegation underneath an ordinary leaf");
    const auto stale=repository_batch(store,"repository-root","stale-new-batch",{"later"});auto stale_revision=stale;stale_revision.expected_budget_revision=before.revision;rejects<Conflict>([&]{store.accept_delegation_batch(stale_revision).get();},"A new batch cannot admit against a stale root budget revision");
    auto changed=specification;changed.parent_assistant_json=Json{{"content","changed prior assistant turn"},{"tool_calls",Json::parse(specification.parent_assistant_json).at("tool_calls")}}.dump();
    rejects<Conflict>([&]{store.accept_delegation_batch(changed).get();},"Provider-call deduplication must bind the actual parent assistant turn");
    rejects<Conflict>([&]{store.complete_run("repository-root",R"({"content":"premature parent"})").get();},"Parent cannot complete while accepted owned children remain active");
    rejects<Conflict>([&]{store.transition("repository-root",RunState::running,RunState::paused).get();},"Parent cannot pause around active owned leaves");
    rejects<Conflict>([&]{store.transition("repository-root",RunState::running,RunState::cancelled).get();},"Parent cannot retire before draining active owned leaves");
    {XlangSqlite sql(database,imports);rejects<DatabaseError>([&]{sql.execute("INSERT INTO runs(id,session_id,state,parent_run_id,node_id) VALUES('unadmitted-child','repository-root-session','queued','repository-root','injected-node')");},"SQL ownership trigger must reject arbitrary ordinary-agent child insertion");rejects<DatabaseError>([&]{sql.execute("UPDATE delegation_tasks SET objective='changed authority' WHERE child_run_id=?",{admitted.tasks[0].run.id});},"Accepted child task identity must be immutable");rejects<DatabaseError>([&]{sql.execute("UPDATE agent_execution_budgets SET max_children=8 WHERE root_run_id='repository-root'");},"Admitted policy cannot be rewritten after ownership admission");}
    rejects<Conflict>([&]{store.settle_delegation_child(admitted.tasks[0].run.id).get();},"A queued child cannot acquire a fabricated terminal outcome");
    for(const auto& task:admitted.tasks){store.transition(task.run.id,RunState::queued,RunState::running).get();store.complete_run(task.run.id,Json{{"content","Synthetic repository terminal observation for "+task.task_id}}.dump()).get();}
    const auto child=admitted.tasks[0].run.id,other=admitted.tasks[1].run.id;const auto child_events=store.events(child).get().size();
    {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_repo_settlement_event BEFORE INSERT ON events WHEN NEW.kind='delegation.child.settled' BEGIN SELECT RAISE(ABORT,'labeled actual settlement event failure'); END");}
    rejects<DatabaseError>([&]{store.settle_delegation_child(child).get();},"Actual settlement event failure must roll back event/outcome together");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_repo_settlement_event");}
    require(store.events(child).get().size()==child_events&&store.delegation_batch(admitted.id).get().tasks[0].outcome_json.empty()&&store.run(child).get().state==RunState::completed,"Settlement rollback must preserve completed child/history without a fake outcome/event");
    auto a=std::async(std::launch::async,[&]{return store.settle_delegation_child(child).get();});auto b=std::async(std::launch::async,[&]{return store.settle_delegation_child(child).get();});const auto first=a.get(),second=b.get();
    require(first.outcome_json==second.outcome_json&&first.settled_event_seq==second.settled_event_seq&&store.events(child).get().size()==child_events+1,"Concurrent settlement must produce exactly one matching committed outcome/event");
    const auto unrelated_json=Json{{"child_run_id",other},{"child_state","completed"}}.dump();
    rejects<std::invalid_argument>([&]{store.append_event("repository-root","delegation.child.settled",unrelated_json).get();},"Generic events cannot mint reserved delegation settlement authority");
    std::int64_t unrelated_sequence=0;{XlangSqlite sql(database,imports);const auto injected=sql.execute("INSERT INTO events(run_id,kind,payload) VALUES('repository-root','delegation.child.settled',?)",{unrelated_json});require(injected.last_insert_id.has_value(),"Disposable unrelated SQL event must have an actual sequence");unrelated_sequence=*injected.last_insert_id;}
    {XlangSqlite sql(database,imports);rejects<DatabaseError>([&]{sql.execute("UPDATE delegation_tasks SET outcome_json=?,settled_event_seq=? WHERE child_run_id=?",{unrelated_json,unrelated_sequence,other});},"A different owner's event cannot authorize this child's settlement");rejects<DatabaseError>([&]{sql.execute("UPDATE delegation_tasks SET outcome_json=?,settled_event_seq=? WHERE child_run_id=?",{first.outcome_json,*first.settled_event_seq,child});},"Committed child outcome must remain write-once");}
    store.settle_delegation_child(other).get();
    rejects<Conflict>([&]{store.complete_run("repository-root",R"({"content":"premature unsettled batch"})").get();},"Parent cannot complete merely because children retired while the accepted batch is unsettled");
    rejects<Conflict>([&]{store.transition("repository-root",RunState::running,RunState::paused).get();},"Parent cannot pause with an uncommitted accepted batch outcome");
    const auto joined=store.settle_delegation_batch(admitted.id).get();const auto again=store.settle_delegation_batch(admitted.id).get();require(joined.state=="completed"&&joined.result_json==again.result_json,"Batch join must derive once from actual settled child outcomes");
    store.complete_run("repository-root",R"({"content":"Synthetic repository observed join"})").get();
    repository_root(store,"child-limit-root",repository_budget(workspace,32,1));const auto limited=repository_batch(store,"child-limit-root","child-limit-batch",{"left","right"});
    rejects<RootBudgetExhausted>([&]{store.accept_delegation_batch(limited).get();},"Total root child allowance must apply before creating a batch");require(store.owned_children("child-limit-root").get().empty()&&store.root_budget("child-limit-root").get().parent_calls_held==0,"Rejected child limit must not consume hidden admission or continuation state");store.transition("child-limit-root",RunState::running,RunState::failed).get();
    store.create_session("budget-atomic-session","Actual root-budget insertion SQL fault").get();
    {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_root_budget_admission BEFORE INSERT ON agent_execution_budgets WHEN NEW.root_run_id='budget-atomic-root' BEGIN SELECT RAISE(ABORT,'labeled root budget admission fault'); END");}
    rejects<DatabaseError>([&]{store.start_prompt_run("budget-atomic-root","budget-atomic-session",R"({"content":"Synthetic root budget atomicity"})",budget).get();},"Root prompt/run/budget insertion must form one actual transaction");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_root_budget_admission");}
    rejects<NotFound>([&]{store.run("budget-atomic-root").get();},"Faulted atomic root admission cannot leave a queued run");require(store.history("budget-atomic-session").get().empty(),"Faulted atomic budget admission cannot leave a parent prompt");
    repository_root(store,"reservation-fault-root",budget);const auto unused=store.root_budget("reservation-fault-root").get();
    {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_call_reservation_event BEFORE INSERT ON events WHEN NEW.kind='budget.model_call.reserved' BEGIN SELECT RAISE(ABORT,'labeled model reservation event fault'); END");}
    rejects<DatabaseError>([&]{store.reserve_model_call("reservation-fault-root","reservation-fault-root","actual-reservation",ModelCallRole::parent).get();},"Model-attempt counter/reservation/event must commit atomically");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_call_reservation_event");}
    const auto unchanged=store.root_budget("reservation-fault-root").get();require(unchanged.revision==unused.revision&&unchanged.model_calls_reserved==0,"Failed model reservation event cannot consume or invent an attempt");
    store.reserve_model_call("reservation-fault-root","reservation-fault-root","actual-reservation",ModelCallRole::parent).get();const auto duplicate_attempt=store.reserve_model_call("reservation-fault-root","reservation-fault-root","actual-reservation",ModelCallRole::parent).get();require(duplicate_attempt.state=="reserved"&&store.root_budget("reservation-fault-root").get().model_calls_reserved==1,"Identical attempt reservation must remain one consumed allowance");
    {XlangSqlite sql(database,imports);sql.execute("CREATE TRIGGER reject_call_start_event BEFORE INSERT ON events WHEN NEW.kind='budget.model_call.started' BEGIN SELECT RAISE(ABORT,'labeled model start event fault'); END");}
    rejects<DatabaseError>([&]{store.start_model_call("reservation-fault-root","reservation-fault-root","actual-reservation").get();},"Actual start event fault must roll back invocation ownership");
    {XlangSqlite sql(database,imports);sql.execute("DROP TRIGGER reject_call_start_event");}
    require(store.reserve_model_call("reservation-fault-root","reservation-fault-root","actual-reservation",ModelCallRole::parent).get().state=="reserved","Failed start cannot relabel a reservation as dispatched");
    store.start_model_call("reservation-fault-root","reservation-fault-root","actual-reservation").get();store.finish_model_call("reservation-fault-root","reservation-fault-root","actual-reservation").get();
    rejects<Conflict>([&]{store.start_model_call("reservation-fault-root","reservation-fault-root","actual-reservation").get();},"Finished exact attempt must remain unavailable to a second invocation");store.complete_run("reservation-fault-root",R"({"content":"Synthetic repository attempt audit completed"})").get();
    repository_root(store,"race-root",repository_budget(workspace,4));store.reserve_model_call("race-root","race-root","parent-first",ModelCallRole::parent).get();store.start_model_call("race-root","race-root","parent-first").get();store.finish_model_call("race-root","race-root","parent-first").get();
    const auto race=store.accept_delegation_batch(repository_batch(store,"race-root","race-batch",{"left","right"})).get();for(const auto& task:race.tasks)store.transition(task.run.id,RunState::queued,RunState::running).get();
    const auto left=race.tasks[0].run.id,right=race.tasks[1].run.id;store.reserve_model_call("race-root",left,"leaf-first",ModelCallRole::leaf).get();
    const auto attempt=[&](const std::string& owner,const std::string& id){try{store.reserve_model_call("race-root",owner,id,ModelCallRole::leaf).get();return true;}catch(const RootBudgetExhausted&){return false;}};
    auto left_attempt=std::async(std::launch::async,[&]{return attempt(left,"leaf-race-left");});auto right_attempt=std::async(std::launch::async,[&]{return attempt(right,"leaf-race-right");});const bool left_ok=left_attempt.get(),right_ok=right_attempt.get();
    require(left_ok!=right_ok&&store.root_budget("race-root").get().model_calls_reserved==3&&store.root_budget("race-root").get().parent_calls_held==1,"Two real repository contenders may reserve only the final unheld leaf allowance");
    store.reserve_model_call("race-root","race-root","parent-continuation",ModelCallRole::parent).get();require(store.root_budget("race-root").get().model_calls_reserved==4&&store.root_budget("race-root").get().parent_calls_held==0,"Held continuation must remain available to its actual parent");
    rejects<RootBudgetExhausted>([&]{store.reserve_model_call("race-root",left,"exhausted-leaf",ModelCallRole::leaf).get();},"Consumed root call limit cannot reset for another child/batch");
    rejects<Conflict>([&]{store.start_model_call("race-root","race-root","parent-first").get();},"A consumed model-call attempt cannot invoke transport again");
    rejects<Conflict>([&]{store.reserve_model_call("race-root",right,"parent-continuation",ModelCallRole::leaf).get();},"An attempt identity cannot be rebound to another owner/role");
    for(const auto& task:race.tasks){store.transition(task.run.id,RunState::running,RunState::cancelled).get();store.settle_delegation_child(task.run.id).get();}store.settle_delegation_batch(race.id).get();store.transition("race-root",RunState::running,RunState::cancelled).get();
    repository_root(store,"recovery-root",budget);const auto interrupted=store.accept_delegation_batch(repository_batch(store,"recovery-root","recovery-batch",{"done","pending"})).get();
    const auto done=interrupted.tasks[0].run.id;store.transition(done,RunState::queued,RunState::running).get();store.complete_run(done,R"({"content":"Synthetic observed repository completion retained on recovery"})").get();store.settle_delegation_child(done).get();
}
void migration_contract(const std::filesystem::path& database,const std::vector<std::string>& imports){
    const auto path=(database.parent_path()/"migration.sqlite").string();std::vector<Message> original;
    {
        PersistenceService store(path,imports);store.create_session("legacy-session","Synthetic legacy migration fixture").get();store.start_prompt_run("legacy-run","legacy-session",R"({"content":"Synthetic pre-v10 prompt"})").get();store.transition("legacy-run",RunState::queued,RunState::running).get();store.complete_run("legacy-run",R"({"content":"Synthetic pre-v10 stored answer"})").get();original=store.history("legacy-session").get();
        store.put_information("fixture","legacy",R"({"retained":true})").get();const std::string key=fixture_key;store.put_credential("fixture","legacy-key","fixture:migration","Synthetic legacy encrypted credential",SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()}),0).get();store.close();
    }
    {
        XlangSqlite sql(path,imports);sql.begin();
        remove_dynamic_schema_fixture(sql);
        for(const auto* trigger:{"owned_child_boundary","delegation_task_initial_outcome","delegation_task_identity_immutable","delegation_task_settlement_boundary","delegation_batch_identity_immutable","agent_budget_identity_immutable","model_call_identity_immutable"})sql.execute(std::string("DROP TRIGGER ")+trigger);
        for(const auto* table:{"agent_model_call_reservations","delegation_tasks","delegation_batches","agent_execution_budgets"})sql.execute(std::string("DROP TABLE ")+table);
        sql.execute("CREATE TRIGGER graph_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT EXISTS(SELECT 1 FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=NEW.parent_run_id AND r.parent_run_id IS NULL AND r.session_id=NEW.session_id AND r.state='running') THEN RAISE(ABORT,'invalid graph child boundary') END; END");
        sql.execute("PRAGMA user_version=9");sql.execute("CREATE TABLE delegation_tasks(labeled_conflict INTEGER)");sql.commit();
    }
    rejects<DatabaseError>([&]{PersistenceService migration(path,imports);},"Actual v10 DDL conflict must reject migration instead of hiding the schema failure");
    {
        XlangSqlite sql(path,imports);require(std::get<std::int64_t>(sql.execute("PRAGMA user_version").rows.at(0).at(0))==9,"Failed actual migration must retain schema version9");
        require(std::get<std::int64_t>(sql.execute("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('agent_execution_budgets','agent_model_call_reservations','delegation_batches')").rows.at(0).at(0))==0,"Migration DDL failure must roll back every earlier new relation");
        require(std::get<std::int64_t>(sql.execute("SELECT COUNT(*) FROM sqlite_master WHERE type='trigger' AND name='graph_child_boundary'").rows.at(0).at(0))==1,"Failed migration must preserve the previous graph ownership trigger");sql.execute("DROP TABLE delegation_tasks");
    }
    {
        PersistenceService migrated(path,imports);same_history(original,migrated.history("legacy-session").get());require(migrated.run("legacy-run").get().state==RunState::completed&&migrated.information("fixture","legacy").get()==R"({"retained":true})","Actual v9-to-v10 migration must retain existing terminal execution and information");
        const auto secret=migrated.resolve_credential("fixture","legacy-key","fixture:migration").get();const auto bytes=secret.view();require(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())==fixture_key,"Existing encrypted legacy credential must remain resolvable after migration");
        rejects<NotFound>([&]{migrated.root_budget("legacy-run").get();},"Migration must not backfill fictional historical budgets or model usage");
        {XlangSqlite sql(path,imports);require(std::get<std::int64_t>(sql.execute("PRAGMA user_version").rows.at(0).at(0))==11,"Corrected exact legacy schema must migrate through version10 to version11");require(std::get<std::int64_t>(sql.execute("SELECT COUNT(*) FROM sqlite_master WHERE type='trigger' AND name='owned_child_boundary'").rows.at(0).at(0))==1,"Successful migration must install the strict generic owned-child boundary");}migrated.close();
    }
}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    try{
        const std::string database=argv[1],endpoint=argv[5];const std::vector<std::string> imports{argv[2],argv[3]};const auto workspace=std::filesystem::u8path(argv[4]);std::vector<Message> saved;std::vector<OwnedChildRecord> saved_children;
        {
            PersistenceService store(database,imports);const std::string key=fixture_key;store.put_credential("fixture","delegation-provider","provider:fixture-delegation","Synthetic delegation credential",SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()}),0).get();
            actual_success(store,settings(endpoint+"/success",workspace.string()),workspace,saved,saved_children);
            actual_followup(store,settings(endpoint+"/followup",workspace.string()));actual_parallel(store,settings(endpoint+"/parallel",workspace.string()));actual_nested(store,settings(endpoint+"/nested",workspace.string()));actual_budget_race(store,settings(endpoint+"/budget",workspace.string()));actual_cancel(store,settings(endpoint+"/cancel",workspace.string()));
            actual_admission_fault(store,settings(endpoint+"/admission-fault",workspace.string()),database,imports);
            actual_service_fault(store,settings(endpoint+"/conversation-fault",workspace.string()),database,imports,workspace,"conversation-fault");
            actual_service_fault(store,settings(endpoint+"/settlement-fault",workspace.string()),database,imports,workspace,"settlement-fault");
            actual_oversize(store,settings(endpoint+"/oversize",workspace.string()));WorkspaceTools tools(workspace.string());repository_contracts(store,database,imports,tools.identity());store.close();
        }
        {
            PersistenceService store(database,imports);same_history(saved,store.history("success-session").get());
            for(const auto& child:saved_children)require(store.run(child.run.id).get().state==RunState::completed&&store.run_history(child.run.id).get().size()==4,"Reopen must preserve isolated completed actual leaf histories");
            for(const auto* root:{"conversation-fault-root","settlement-fault-root","recovery-root"})require(store.run(root).get().state==RunState::failed,"Recovery must retire interrupted owners without relaunch");
            const auto recovery=store.delegation_batch("recovery-batch").get();require(recovery.state=="interrupted"&&recovery.tasks[0].run.state==RunState::completed&&!recovery.tasks[0].outcome_json.empty()&&recovery.tasks[1].run.state==RunState::failed,"Recovery must preserve completed outcomes and retire unfinished leaves without replay");
            auto prior=repository_batch(store,"recovery-root","recovery-batch",{"done","pending"});rejects<Conflict>([&]{store.accept_delegation_batch(prior).get();},"Interrupted batch deduplication cannot resume execution after reopen");
            require(read(workspace/"target.txt")=="reviewed native delegation\n"&&store.credentials("fixture").get().size()==1,"Recovery must preserve the actual approved effect and encrypted owned credential without repeating either");store.close();
        }
        migration_contract(std::filesystem::u8path(database),imports);
        std::cout<<"Native delegation contract passed real signed parent/leaf provider transport, independent filesystem reads/history/metrics, one controller-approved parent edit, observed failure follow-up, cancellation/draining, atomic admission rollback, coupled conversation/settlement fail-stop, encoded output bounds, shared call allowances, exact deduplication/ownership and xlang3 SQLite recovery without replay. Provider replies/signatures/key and direct repository lifecycle inputs are synthetic; no live provider or rendered UI acceptance claimed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
