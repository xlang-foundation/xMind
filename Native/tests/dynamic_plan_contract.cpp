// Pure model-schema/revision contracts. Snapshots below are explicitly synthetic
// repository observations; no child/model/tool execution or SQL pass is claimed.
#include "agentflow/dynamic_plan.hpp"
#include "agentflow/json_schema.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iostream>

using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected dynamic plan rejection did not occur");}
DynamicPlanCapabilities capabilities(){
    DynamicPlanCapabilities result;result.backend_identity=std::string(64,'a');result.workspace_identity="PRIVATE_WORKSPACE_BINDING";
    result.provider_identity_json=R"({"wire":"responses","model_id":"synthetic-dynamic-fixture"})";
    result.tool_catalog_json=Json{{"tools",Json::array({
        {{"name","read_file"},{"input_schema_json",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false})"}},
        {{"name","run_process"},{"input_schema_json",R"({"type":"object","properties":{"profile":{"type":"string","enum":["xlang3"]},"arguments":{"type":"array","items":{"type":"string","maxLength":4096},"maxItems":32},"workdir":{"type":"string","maxLength":4096},"folder_id":{"type":"string"},"timeout_ms":{"type":"integer","minimum":1,"maximum":5000},"background":{"type":"boolean"}},"required":["profile","arguments"],"additionalProperties":false})"}},
        {{"name","a2a_delegate_0123456789abcdef01234567"},{"input_schema_json",R"({"type":"object","properties":{"task":{"type":"string","minLength":1,"maxLength":32768}},"required":["task"],"additionalProperties":false})"},
         {"a2a_binding",Json{{"kind","a2a_remote_agent"},{"alias","a2a_delegate_0123456789abcdef01234567"},{"input_schema_json",R"({"type":"object","properties":{"task":{"type":"string","minLength":1,"maxLength":32768}},"required":["task"],"additionalProperties":false})"},{"agent_id","research.fixture"},{"agent_revision",1},{"card_url","https://fixture.invalid/.well-known/agent-card.json"},{"protocol_version","1.0"},{"endpoint","https://fixture.invalid/a2a"},{"agent_name","Fixture"},{"agent_version","1"},{"skills_json","[]"},{"has_bearer",false}}}}
        ,{{"name","mcp_resource_fixture"},{"input_schema_json",R"({"type":"object","properties":{},"additionalProperties":false})"},
         {"mcp_binding",Json{{"server_config_id","fixture-server"},{"config_revision",1},{"kind","resource"},{"peer_name","fixture/readme"},{"alias","mcp_resource_fixture"},{"catalogue_fingerprint",std::string(64,'1')},{"protocol_version","2025-11-25"},{"input_schema_json",R"({"type":"object","properties":{},"additionalProperties":false})"},{"output_schema_json",nullptr},{"annotations_json","{}"},{"metadata_json",R"({"uri":"fixture://readme","name":"fixture/readme"})"},{"resource_uri","fixture://readme"}}}},
        {{"name","mcp_prompt_fixture"},{"input_schema_json",R"({"type":"object","properties":{"topic":{"type":"string"}},"required":["topic"],"additionalProperties":false})"},
         {"mcp_binding",Json{{"server_config_id","fixture-server"},{"config_revision",1},{"kind","prompt"},{"peer_name","fixture/review"},{"alias","mcp_prompt_fixture"},{"catalogue_fingerprint",std::string(64,'2')},{"protocol_version","2025-11-25"},{"input_schema_json",R"({"type":"object","properties":{"topic":{"type":"string"}},"required":["topic"],"additionalProperties":false})"},{"output_schema_json",nullptr},{"annotations_json","{}"},{"metadata_json",R"({"name":"fixture/review","arguments":[{"name":"topic","required":true}]})"}}}}
    })},{"private_marker","PRIVATE_CATALOG_BINDING"}}.dump();result.catalogue_finalized=true;
    DynamicPresetCapability inspect;inspect.id="workspace.inspect";inspect.backend_identity=std::string(64,'b');inspect.tools={"read_repository_instructions","read_file","list_files","search_files"};
    DynamicPresetCapability coding=inspect;coding.id="workspace.coding";coding.backend_identity=std::string(64,'c');coding.readonly=false;coding.tools.push_back("edit_file");coding.tools.push_back("create_file");coding.tools.push_back("run_process");coding.tools.push_back("a2a_delegate_0123456789abcdef01234567");coding.tools.push_back("mcp_resource_fixture");coding.tools.push_back("mcp_prompt_fixture");
    result.presets={inspect,coding};return result;
}
Json task(const char* id,const char* preset="workspace.inspect",Json dependencies=Json::array()){
    return {{"id",id},{"type","agent"},{"objective","Synthetic metadata objective"},{"preset",preset},{"depends_on",std::move(dependencies)}};
}
Json dependency(const char* id,const char* requirement="success"){return {{"task",id},{"require",requirement}};}
DynamicPlanRecord initial_fixture(const Json& definitions){
    DynamicPlanRecord seed;seed.root_run_id="synthetic_root";seed.capabilities=capabilities();
    const auto input=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",definitions}}.dump();
    const auto candidate=reduce_dynamic_plan(seed,parse_dynamic_plan_change(input,true),seed.capabilities);
    DynamicPlanRecord observed=seed;observed.id="synthetic_plan";observed.revision=1;observed.state_sequence=1;observed.nodes=candidate.nodes;
    observed.retired_labels=candidate.retired_labels;observed.planned_children_reserved=candidate.planned_children_reserved;observed.planned_humans_reserved=candidate.planned_humans_reserved;
    for(auto& node:observed.nodes)node.backend_node_id="synthetic_node_"+node.definition.label;return observed;
}
DynamicNodeRecord& at(DynamicPlanRecord& plan,const char* id){return *std::find_if(plan.nodes.begin(),plan.nodes.end(),[&](const auto& node){return node.definition.label==id;});}
void observed_settlement_fixture(DynamicPlanRecord& plan,const char* id,const char* child_state="completed",std::string outcome=R"({"content":"Synthetic durable observation"})"){
    auto& node=at(plan,id);node.state=DynamicNodeState::settled;node.claim_revision=node.definition_revision;node.claim_id="synthetic_claim_"+node.definition.label;node.protected_definition=true;
    node.outcome_json=std::move(outcome);node.settled_event_seq=100+plan.state_sequence;++plan.state_sequence;
    if(node.definition.kind==DynamicNodeKind::agent){node.child_run_id="synthetic_child_"+node.definition.label;node.child_state=child_state;--plan.planned_children_reserved;}
    else{node.human_request_id="synthetic_question_"+node.definition.label;++plan.humans_published;--plan.planned_humans_reserved;}
}
Json revision(const DynamicPlanRecord& plan){return {{"expected_revision",plan.revision},{"expected_state_sequence",plan.state_sequence}};}
DynamicPlanRecord persisted_candidate_fixture(const DynamicPlanRecord& old,const DynamicPlanCandidate& candidate){
    auto result=old;++result.revision;++result.state_sequence;result.nodes=candidate.nodes;result.retired_labels=candidate.retired_labels;
    result.planned_children_reserved=candidate.planned_children_reserved;result.planned_humans_reserved=candidate.planned_humans_reserved;
    for(auto& node:result.nodes)if(node.backend_node_id.empty())node.backend_node_id="synthetic_node_"+node.definition.label;return result;
}
}
int main(){try{
    const auto caps=capabilities();
    require(caps.max_parent_turns==32,"Default planning availability must retain the ordinary coding allowance");
    for(const auto turns:{0,33}){auto invalid=caps;invalid.max_parent_turns=turns;rejects<std::invalid_argument>([&]{dynamic_plan_tool_definitions(invalid);});}
    const auto initial=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({task("left"),task("right"),
        {{"id","gate"},{"type","human"},{"question","Synthetic user choice 🌍"},{"depends_on",Json::array({dependency("left"),dependency("right")})}},
        task("repair","workspace.coding",Json::array({dependency("gate")})),task("verify","workspace.inspect",Json::array({dependency("repair")})),
        task("blocked","workspace.inspect",Json::array({dependency("verify")})),task("observer","workspace.inspect",Json::array({dependency("verify","observed")}))})}};
    const auto source=" \n"+initial.dump()+" \t";const auto parsed=parse_dynamic_plan_change(source,true);
    require(parsed.arguments_json==source,"Exact model request spelling must remain available for durable dedup and byte limits");
    auto plan=initial_fixture(initial["add"]);require(inspect_dynamic_plan(plan).ready==std::vector<std::string>({"left","right"}),"Independent actual dependencies must be ready together");
    rejects<Conflict>([&]{prepare_dynamic_node(plan,"gate");});
    const auto schemas=dynamic_plan_tool_definitions(caps);require(schemas.size()==3&&schemas[0].name=="plan_tasks"&&schemas[1].name=="revise_plan"&&schemas[2].name=="inspect_plan","Only implemented planning tools must be offered");
    JsonSchema initial_schema(schemas[0].input_schema_json),revision_schema(schemas[1].input_schema_json),inspect_schema(schemas[2].input_schema_json);
    initial_schema.validate_object(initial.dump());inspect_schema.validate_object("{}");rejects<SchemaArgumentsInvalid>([&]{inspect_schema.validate_object(R"({"root_run_id":"another"})");});
    for(const auto& schema:schemas){require(schema.input_schema_json.find("PRIVATE_")==std::string::npos&&schema.input_schema_json.find(caps.backend_identity)==std::string::npos&&schema.input_schema_json.find("tool_catalog_json")==std::string::npos,"Private capability material must not appear in model tool schemas");}
    const std::vector<std::string> invalid={
        R"({"expected_revision":0,"expected_revision":0,"expected_state_sequence":0,"add":[]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[],"owner":"injected"})",
        R"({"expected_revision":0.0,"expected_state_sequence":0,"add":[]})",
        R"({"expected_revision":18446744073709551615,"expected_state_sequence":0,"add":[]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[],"replace":[]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"tool","tool":"read_file","arguments_json":"{","depends_on":[]}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"agent","objective":"x","preset":"workspace.inspect","depends_on":[],"permissions":"allow"}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"human","question":"x","depends_on":[],"actor":"local-owner"}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"agent","objective":"x","preset":"workspace.inspect","depends_on":[{"task":"a","require":"success"}]}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"agent","objective":"x","preset":"workspace.inspect","depends_on":[{"task":"b","require":"success"},{"task":"b","require":"observed"}]}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"agent","objective":"x","preset":"workspace.coding","depends_on":[],"preset_revision":99}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","type":"agent","objective":"x","preset":"unknown","depends_on":[]}]})",
        R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"a","i\u0064":"b","type":"agent","objective":"x","preset":"workspace.inspect","depends_on":[]}]})"
    };
    for(const auto& value:invalid)rejects<std::invalid_argument>([&]{parse_dynamic_plan_change(value,true);});
    const auto raw_arguments=R"({"pa\u0074h":"file.txt"})";
    const auto deterministic=Json{{"id","read"},{"type","tool"},{"tool","read_file"},{"arguments_json",raw_arguments},{"depends_on",Json::array()}};
    auto deterministic_plan=initial_fixture(Json::array({deterministic}));
    require(deterministic_plan.planned_children_reserved==1&&inspect_dynamic_plan(deterministic_plan).ready==std::vector<std::string>({"read"}),"A deterministic read node must reserve and expose one dependency-ready child slot");
    require(at(deterministic_plan,"read").definition.arguments_json==raw_arguments,"Deterministic tool argument JSON must preserve escaped key spellings exactly");
    const auto process_call=Json{{"profile","xlang3"},{"arguments",Json::array({"-c","print('actual process result')"})},{"workdir","."},{"background",false}}.dump();
    const auto process_definition=Json{{"id","build"},{"type","tool"},{"tool","run_process"},{"arguments_json",process_call},{"depends_on",Json::array()}};
    const auto process_plan=initial_fixture(Json::array({process_definition}));require(process_plan.nodes.front().definition.tool=="run_process"&&inspect_dynamic_plan(process_plan).ready==std::vector<std::string>({"build"}),"Synchronous process node must be admitted through the sealed coding profile schema");
    for(const auto* background:{"true","\"false\""}){auto args=Json{{"profile","xlang3"},{"arguments",Json::array({"-c","print('no')"})},{"background",Json::parse(background)}};auto blocked=process_definition;blocked["arguments_json"]=args.dump();rejects<std::invalid_argument>([&]{initial_fixture(Json::array({blocked}));});}
    auto wrong_folder=process_definition;wrong_folder["arguments_json"]=Json{{"profile","xlang3"},{"arguments",Json::array({"-c","print('no')"})},{"folder_id","another-root"}}.dump();rejects<std::invalid_argument>([&]{initial_fixture(Json::array({wrong_folder}));});
    const std::string a2a_alias="a2a_delegate_0123456789abcdef01234567";const auto a2a_call=Json{{"task","Inspect the requested fixture"}}.dump();const auto a2a_definition=Json{{"id","remote"},{"type","tool"},{"tool",a2a_alias},{"arguments_json",a2a_call},{"depends_on",Json::array()}};
    const auto a2a_plan=initial_fixture(Json::array({a2a_definition}));require(a2a_plan.nodes.front().definition.tool==a2a_alias,"Registered A2A alias must be admitted through its sealed coding binding and exact schema");
    const auto resource_definition=Json{{"id","resource"},{"type","tool"},{"tool","mcp_resource_fixture"},{"arguments_json","{}"},{"depends_on",Json::array()}};
    const auto prompt_definition=Json{{"id","prompt"},{"type","tool"},{"tool","mcp_prompt_fixture"},{"arguments_json",R"({"topic":"review the changes"})"},{"depends_on",Json::array({dependency("resource")})}};
    const auto mcp_read_plan=initial_fixture(Json::array({resource_definition,prompt_definition}));require(mcp_read_plan.planned_children_reserved==2&&mcp_read_plan.nodes[0].definition.tool=="mcp_resource_fixture"&&mcp_read_plan.nodes[1].definition.tool=="mcp_prompt_fixture","Registered MCP resources and prompts must be admitted as exact-bound dependency nodes");
    const auto plan_schema=Json::parse(dynamic_plan_tool_definitions(caps)[0].input_schema_json);const auto& offered=plan_schema.at("properties").at("add").at("items").at("oneOf").at(1).at("properties").at("tool").at("enum");require(std::find(offered.begin(),offered.end(),"mcp_resource_fixture")!=offered.end()&&std::find(offered.begin(),offered.end(),"mcp_prompt_fixture")!=offered.end(),"Registered read-only MCP aliases must be exposed in the model plan schema");
    auto forged_resource=resource_definition;auto& forged_binding=mcp_read_plan.capabilities.tool_catalog_json;Json altered_catalog=Json::parse(forged_binding);for(auto& item:altered_catalog["tools"])if(item.value("name",std::string{})=="mcp_resource_fixture")item["mcp_binding"]["kind"]="tool";auto altered_caps=caps;altered_caps.tool_catalog_json=altered_catalog.dump();DynamicPlanRecord altered_seed;altered_seed.root_run_id="synthetic_root";altered_seed.capabilities=altered_caps;const auto altered_input=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({forged_resource})}}.dump();rejects<std::invalid_argument>([&]{reduce_dynamic_plan(altered_seed,parse_dynamic_plan_change(altered_input,true),altered_caps);});
    auto no_a2a=caps;no_a2a.presets.back().tools.erase(std::remove(no_a2a.presets.back().tools.begin(),no_a2a.presets.back().tools.end(),a2a_alias),no_a2a.presets.back().tools.end());const auto no_a2a_schema=Json::parse(dynamic_plan_tool_definitions(no_a2a)[0].input_schema_json);const auto& no_a2a_aliases=no_a2a_schema.at("properties").at("add").at("items").at("oneOf").at(1).at("properties").at("tool").at("enum");require(std::find(no_a2a_aliases.begin(),no_a2a_aliases.end(),a2a_alias)==no_a2a_aliases.end(),"A2A aliases must disappear from the plan schema when the coding preset does not admit them");rejects<std::invalid_argument>([&]{const auto input=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({a2a_definition})}}.dump();DynamicPlanRecord root;root.root_run_id="synthetic_root";root.capabilities=no_a2a;reduce_dynamic_plan(root,parse_dynamic_plan_change(input,true),no_a2a);});
    auto no_process=caps;no_process.presets.back().tools.erase(std::remove(no_process.presets.back().tools.begin(),no_process.presets.back().tools.end(),"run_process"),no_process.presets.back().tools.end());const auto no_process_schema=dynamic_plan_tool_definitions(no_process);const auto model_schema=Json::parse(no_process_schema[0].input_schema_json);const auto& aliases=model_schema.at("properties").at("add").at("items").at("oneOf").at(1).at("properties").at("tool").at("enum");require(std::find(aliases.begin(),aliases.end(),"run_process")==aliases.end(),"Process aliases must disappear from the plan tool schema when no coding profile is admitted");rejects<std::invalid_argument>([&]{const auto input=Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({process_definition})}}.dump();DynamicPlanRecord root;root.root_run_id="synthetic_root";root.capabilities=no_process;reduce_dynamic_plan(root,parse_dynamic_plan_change(input,true),no_process);});
    for(const auto* field:{"workspace","endpoint","credential","model_id","approval","server_id","config_revision","child_run_id"}){auto value=initial;value["add"][0][field]="injected";rejects<std::invalid_argument>([&]{parse_dynamic_plan_change(value.dump(),true);});rejects<SchemaArgumentsInvalid>([&]{initial_schema.validate_object(value.dump());});}
    std::string bad_utf8=initial.dump();bad_utf8.insert(bad_utf8.find("Synthetic"),1,static_cast<char>(0xff));rejects<std::invalid_argument>([&]{parse_dynamic_plan_change(bad_utf8,true);});
    rejects<std::invalid_argument>([&]{parse_dynamic_plan_change(std::string(262145,' ')+initial.dump(),true);});
    auto duplicate=initial;duplicate["add"].push_back(duplicate["add"][0]);rejects<std::invalid_argument>([&]{parse_dynamic_plan_change(duplicate.dump(),true);});
    for(const auto& definitions:{Json::array({task("a","workspace.inspect",Json::array({dependency("missing")}))}),Json::array({task("a","workspace.inspect",Json::array({dependency("b")})),task("b","workspace.inspect",Json::array({dependency("a")}))})})rejects<std::invalid_argument>([&]{initial_fixture(definitions);});
    observed_settlement_fixture(plan,"left");require(inspect_dynamic_plan(plan).ready==std::vector<std::string>({"right"}),"Join must still wait for its other dependency");
    observed_settlement_fixture(plan,"right");require(inspect_dynamic_plan(plan).ready==std::vector<std::string>({"gate"}),"Human publication readiness must be derived from both actual outcomes");
    observed_settlement_fixture(plan,"gate","completed",R"({"human_state":"answered","input_json":"{\"decimal\":1.00000000000000000001}","actor":"synthetic-controller"})");
    const auto prepared=prepare_dynamic_node(plan,"repair");require(Json::parse(prepared.dependency_outputs_json)["gate"]["outcome_json"]==at(plan,"gate").outcome_json,"Dependency preparation must retain exact observed JSON as data");
    observed_settlement_fixture(plan,"repair");at(plan,"repair").effect_state="succeeded";
    observed_settlement_fixture(plan,"verify","failed",R"({"error":{"code":"synthetic_actual_protocol_failure"}})");
    const auto after_failure=inspect_dynamic_plan(plan);require(after_failure.ready==std::vector<std::string>({"observer"})&&after_failure.blocked==std::vector<std::string>({"blocked"}),"Observed failures may be inspected but cannot satisfy successful dependencies");
    const auto retained=at(plan,"repair"),failed=at(plan,"verify");
    auto revised=revision(plan);auto replacement=task("blocked","workspace.inspect",Json::array({dependency("fresh")}));replacement["objective"]="Synthetic revised verification";
    revised["replace"]=Json::array({replacement});revised["add"]=Json::array({task("fresh","workspace.inspect",Json::array({dependency("verify","observed")}))});revised["skip"]={"observer"};
    revision_schema.validate_object(revised.dump());const auto change=parse_dynamic_plan_change(revised.dump(),false);const auto candidate=reduce_dynamic_plan(plan,change,caps);
    require(candidate.planned_children_reserved==2&&candidate.planned_humans_reserved==0&&candidate.newhuman_count==0,"Replacement and skips must reserve only actual remaining unpublished work");
    auto next=persisted_candidate_fixture(plan,candidate);const auto& kept=at(next,"repair");
    require(kept.backend_node_id==retained.backend_node_id&&kept.definition_revision==retained.definition_revision&&kept.claim_id==retained.claim_id&&kept.child_run_id==retained.child_run_id&&kept.effect_state==retained.effect_state&&kept.outcome_json==retained.outcome_json&&kept.settled_event_seq==retained.settled_event_seq,"A revision must retain completed effect-linked identity and outcome exactly");
    require(at(next,"verify").child_state==failed.child_state&&at(next,"verify").outcome_json==failed.outcome_json&&inspect_dynamic_plan(next).ready==std::vector<std::string>({"fresh"}),"Same-plan replanning must retain failed execution and dispatch only revised dependency-ready work");
    auto stale=plan;++stale.state_sequence;rejects<Conflict>([&]{reduce_dynamic_plan(stale,change,caps);});
    for(const auto* protected_label:{"left","gate","repair","verify"}){auto illegal=revision(plan);auto value=task(protected_label);illegal["replace"]=Json::array({value});rejects<Conflict>([&]{reduce_dynamic_plan(plan,parse_dynamic_plan_change(illegal.dump(),false),caps);});}
    auto reused=revision(next);reused["add"]=Json::array({task("observer")});rejects<Conflict>([&]{reduce_dynamic_plan(next,parse_dynamic_plan_change(reused.dump(),false),caps);});
    auto conflicting=revised;conflicting["skip"].push_back("blocked");rejects<std::invalid_argument>([&]{parse_dynamic_plan_change(conflicting.dump(),false);});
    auto forged=change;forged.replace[0].question="hidden authority data";rejects<std::invalid_argument>([&]{reduce_dynamic_plan(plan,forged,caps);});
    auto changed_caps=caps;changed_caps.presets[1].backend_identity=std::string(64,'d');rejects<Conflict>([&]{reduce_dynamic_plan(plan,change,changed_caps);});
    auto unavailable=caps;unavailable.presets.pop_back();DynamicPlanRecord seed;seed.root_run_id="synthetic_root";seed.capabilities=unavailable;rejects<std::invalid_argument>([&]{reduce_dynamic_plan(seed,parsed,unavailable);});
    auto unsafe=caps;unsafe.presets[0].tools.push_back("edit_file");rejects<std::invalid_argument>([&]{dynamic_plan_tool_definitions(unsafe);});
    auto uncertain=plan;at(uncertain,"verify").state=DynamicNodeState::uncertain;at(uncertain,"verify").effect_state="uncertain";
    require(inspect_dynamic_plan(uncertain).halted&&inspect_dynamic_plan(uncertain).ready.empty(),"Actual uncertainty must stop every automatic frontier");
    auto bad_ledger=plan;++bad_ledger.planned_children_reserved;rejects<std::invalid_argument>([&]{inspect_dynamic_plan(bad_ledger);});
    for(const auto* pending_effect:{"awaiting_approval","ready","executing"}){auto impossible=plan;at(impossible,"repair").effect_state=pending_effect;rejects<std::invalid_argument>([&]{inspect_dynamic_plan(impossible);});}
    auto invalid_lifecycle=plan;invalid_lifecycle.state="invented";rejects<std::invalid_argument>([&]{inspect_dynamic_plan(invalid_lifecycle);});
    invalid_lifecycle=plan;invalid_lifecycle.state="completed";rejects<std::invalid_argument>([&]{inspect_dynamic_plan(invalid_lifecycle);});
    auto unsealed=caps;unsealed.catalogue_finalized=false;rejects<std::invalid_argument>([&]{dynamic_plan_tool_definitions(unsealed);});
    DynamicPlanRecord unsealed_seed;unsealed_seed.root_run_id="synthetic_root";unsealed_seed.capabilities=unsealed;rejects<std::invalid_argument>([&]{reduce_dynamic_plan(unsealed_seed,parsed,unsealed);});
    auto unsealed_plan=plan;unsealed_plan.capabilities.catalogue_finalized=false;rejects<std::invalid_argument>([&]{inspect_dynamic_plan(unsealed_plan);});
    auto report=dynamic_plan_report(next);for(const auto* private_field:{"backend_identity","PRIVATE_WORKSPACE_BINDING","PRIVATE_CATALOG_BINDING","tool_catalog_json","provider_identity_json"})require(report.find(private_field)==std::string::npos,"Owned reports must exclude private authority fields");
    auto oversized=initial_fixture(Json::array({task("a"),task("b"),task("c"),task("d")}));
    for(const auto* id:{"a","b","c","d"})observed_settlement_fixture(oversized,id,"completed",Json{{"content",std::string(8000,'"')}}.dump());
    const auto preserved=at(oversized,"a").outcome_json;report=dynamic_plan_report(oversized);
    require(report.size()<=65536&&Json::parse(report)["error"]["code"]=="result_limit_exceeded"&&at(oversized,"a").outcome_json==preserved&&Json::parse(report)["nodes"].size()==4,"Actual escaped envelope bytes must trigger an identity-only report while preserving every durable outcome");
    auto over_output=oversized;at(over_output,"a").outcome_json=Json{{"content",std::string(17000,'"')}}.dump();rejects<std::invalid_argument>([&]{dynamic_plan_report(over_output);});
    auto human_only=initial_fixture(Json::array({{{"id","answer"},{"type","human"},{"question","Synthetic final input"},{"depends_on",Json::array()}}}));
    auto& question=at(human_only,"answer");question.state=DynamicNodeState::waiting_human;question.claim_revision=1;question.claim_id="synthetic_publication";question.human_request_id="synthetic_request";question.protected_definition=true;human_only.planned_humans_reserved=0;human_only.humans_published=1;++human_only.state_sequence;
    require(!inspect_dynamic_plan(human_only).report_ready&&inspect_dynamic_plan(human_only).waiting_human.size()==1,"Published human request must wait without executable children");
    question.state=DynamicNodeState::settled;question.outcome_json=R"({"human_state":"answered","input_json":"{\"answer\":true}"})";question.settled_event_seq=1000;++human_only.state_sequence;
    const auto final_answer=inspect_dynamic_plan(human_only);require(final_answer.ready.empty()&&final_answer.finished&&final_answer.report_ready,"A final human answer must make same-owner report/continuation readiness without requiring a child frontier");
    auto edit_question=revision(human_only);edit_question["skip"]={"answer"};rejects<Conflict>([&]{reduce_dynamic_plan(human_only,parse_dynamic_plan_change(edit_question.dump(),false),caps);});
    auto expired=initial_fixture(Json::array({{{"id","answer"},{"type","human"},{"question","Synthetic expiring input"},{"depends_on",Json::array()}},task("requires-answer","workspace.inspect",Json::array({dependency("answer")})),task("observe-expiry","workspace.inspect",Json::array({dependency("answer","observed")}))}));
    observed_settlement_fixture(expired,"answer","completed",R"({"human_state":"expired"})");const auto expiry=inspect_dynamic_plan(expired);
    require(expiry.ready==std::vector<std::string>({"observe-expiry"})&&expiry.blocked==std::vector<std::string>({"requires-answer"}),"A durable expired human question may be observed but cannot satisfy an answered-success dependency");
    // A 32-lifetime-label snapshot with 16 retired, never-dispatched labels and
    // a complete DAG of 16 pending tasks respects the actual 8 agent + 8 human
    // reservations. It exercises the worst permitted recursive dependency
    // shape without claiming admission/execution of 32 live children.
    DynamicPlanRecord dense;dense.id="synthetic_dense_plan";dense.root_run_id="synthetic_root";dense.capabilities=caps;dense.revision=3;dense.state_sequence=20;
    dense.planned_children_reserved=8;dense.planned_humans_reserved=8;
    for(int i=0;i<32;++i){DynamicNodeRecord item;item.definition.label="n"+std::to_string(i);item.backend_node_id="synthetic_node_"+item.definition.label;
        item.definition_revision=i<16?1:3;
        if(i>=16&&i%2){item.definition.kind=DynamicNodeKind::human;item.definition.question="Synthetic dense question";item.definition.preset_revision=0;}
        else{item.definition.kind=DynamicNodeKind::agent;item.definition.objective="Synthetic dense investigation";item.definition.preset_id="workspace.inspect";}
        if(i<16){item.state=DynamicNodeState::skipped;dense.retired_labels.push_back(item.definition.label);}
        else for(int predecessor=16;predecessor<i;++predecessor)item.definition.dependencies.push_back({"n"+std::to_string(predecessor),DynamicDependencyRequirement::success});
        dense.nodes.push_back(std::move(item));
    }
    auto dense_decision=inspect_dynamic_plan(dense);require(dense_decision.ready==std::vector<std::string>({"n16"})&&dense_decision.blocked.empty()&&!dense_decision.finished&&!dense_decision.report_ready,"Dense pending DAG must memoize readiness and wait for the one real predecessor frontier");
    observed_settlement_fixture(dense,"n16","failed",R"({"error":{"code":"synthetic_failed_dense_observation"}})");dense_decision=inspect_dynamic_plan(dense);
    require(dense_decision.ready.empty()&&dense_decision.blocked.size()==15&&dense_decision.report_ready&&!dense_decision.finished,"Dense success-dependency failure must propagate once to every blocked descendant and return a replanning report");
    std::cout<<"Native pure dynamic-plan contracts passed strict UTF-8/duplicate/authority schemas, dependency joins, success/observed failure semantics, immutable same-plan replanning, stale state and protected-work rejection, exact data-string observations, private-field exclusion, escaped byte limits and final-human report readiness. Snapshot values are synthetic; runtime/SQLite/execution acceptance is separate.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
