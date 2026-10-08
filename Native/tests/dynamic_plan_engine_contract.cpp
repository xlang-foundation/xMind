// Synthetic signed provider replies/key; actual AgentService, child engines,
// workspace approvals/effects and xlang3 SQLite. No ledger state shortcuts.
#include "agentflow/agent_service.hpp"
#include "agentflow/dynamic_plan.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
using Json=nlohmann::json;
constexpr const char* fixture_key="synthetic-dynamic-engine-key-not-live";
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action>void rejects(Action action,const char* reason){try{action();}catch(const Error&){return;}throw std::runtime_error(reason);}
template<class Action>void eventually(Action action,const char* reason){const auto until=std::chrono::steady_clock::now()+12s;while(std::chrono::steady_clock::now()<until){if(action())return;std::this_thread::sleep_for(5ms);}throw std::runtime_error(reason);}
std::string read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
void write(const std::filesystem::path& path,const std::string& value){std::ofstream out(path,std::ios::binary);out<<value;if(!out)throw std::runtime_error("Fixture control marker write failed");}
std::size_t count(const std::vector<Event>& events,const std::string& kind){return std::count_if(events.begin(),events.end(),[&](const auto& event){return event.kind==kind;});}
const DynamicNodeRecord& node(const DynamicPlanRecord& plan,const std::string& label){const auto found=std::find_if(plan.nodes.begin(),plan.nodes.end(),[&](const auto& value){return value.definition.label==label;});if(found==plan.nodes.end())throw std::runtime_error("Owned task missing");return *found;}
DynamicPlanRecord paused(PersistenceService& store,const std::string& root){
    std::optional<DynamicPlanRecord> result;eventually([&]{if(store.run(root).get().state!=RunState::paused)return false;result=store.dynamic_plan_for_root(root).get();return result.has_value();},"Actual ordinary Agent did not enter a typed human pause");return *result;
}
DynamicHumanRequest question(PersistenceService& store,const DynamicPlanRecord& plan){const auto requests=store.dynamic_human_requests(plan.id).get();require(requests.size()==1&&requests[0].state=="waiting","One actual owned human question must be published");return requests[0];}
void metrics(const Message& row,std::int64_t input,std::int64_t output){const auto data=Json::parse(row.json);require(data.at("usage").at("input_tokens")==input&&data.at("usage").at("output_tokens")==output,"Each actual response retains its own supplied usage");require(data.contains("elapsed_ms")&&data.contains("first_token_ms"),"Measured native response timing must remain present");require(!data.at("usage").contains("total_tokens"),"Absent provider totals must not be invented");}
AgentSettings settings(const std::string& endpoint,const std::string& workspace){
    AgentSettings result;result.provider={endpoint,"synthetic-dynamic-engine",Capability::supported,Capability::supported,Capability::supported};result.provider.wire=ProviderWire::anthropic_messages;result.provider.deadline=8s;result.provider.idle_timeout=6s;
    result.workspace=workspace;result.approved_edits=true;result.max_turns=8;result.max_output_tokens=256;result.run_timeout=30s;
    result.instructions="Explicit synthetic dynamic-plan provider fixture; only actual native outcomes are evidence.";
    result.credential=CredentialReference{"fixture","dynamic-engine-provider","provider:synthetic-dynamic-engine"};return result;
}
void private_absent(PersistenceService& store,const std::string& root){for(const auto& event:store.tree_events(root,0,256).get())require(event.json.find(fixture_key)==std::string::npos,"Provider credential must not enter owned events");for(const auto& row:store.run_history(root).get())require(row.json.find(fixture_key)==std::string::npos,"Provider credential must not enter history");}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    try{
        const std::filesystem::path folder=argv[1],workspace=argv[5];const auto database=(folder/"state.sqlite").string();const std::vector<std::string> imports={argv[2],argv[3]};const auto frozen=settings(argv[4],workspace.string());
        DynamicPlanRecord before;std::vector<Message> paused_history;std::vector<OwnedChildRecord> original_children;
        {
            PersistenceService store(database,imports);const std::string key=fixture_key;store.put_credential("fixture","dynamic-engine-provider","provider:synthetic-dynamic-engine","Synthetic fixture credential",SecretBytes({reinterpret_cast<const std::uint8_t*>(key.data()),key.size()}),0).get();
            store.create_session("main-session","Actual same-plan revision and independently approved coding").get();AgentService service(store,frozen,2,8);
            require(service.supports_dynamic_planning()&&!service.supports_delegation(),"Eligible ordinary Agent offers planning without silently enabling legacy delegation");
            service.submit("main-root","main-session","fixture-main-parent: Investigate both files, observe failure, revise the same plan, ask a human, then apply a separately reviewed edit and verify actual bytes.");
            before=paused(store,"main-root");require(before.revision==2&&before.state=="waiting_human","Actual parent revision must pause the same plan");
            original_children=store.owned_children("main-root").get();require(original_children.size()==2,"Exactly two inspectors run before the published question");
            require(node(before,"left").child_state=="completed"&&node(before,"right").child_state=="failed"&&node(before,"code").claim_revision==0&&node(before,"code").definition_revision==2,"Actual failure remains protected; only never-claimed coding definition changes");
            require(read(workspace/"target.txt")=="original\n"&&store.operations("main-root").get().empty(),"Planning and a human question do not authorize a file effect");
            paused_history=store.run_history("main-root").get();require(paused_history.size()==3,"Pending signed revision is not a fabricated committed tool result");
            const auto segment=store.dynamic_budget_segment("main-root").get();require(segment.state=="closed"&&segment.closed_event_seq&&segment.remaining_active_ms>0,"Actual human pause closes measured active time");
            require(store.root_budget("main-root").get().model_calls_reserved==6&&store.root_budget("main-root").get().parent_calls_held==1,"Two parent and four inspector calls retain one held continuation");
            service.close();require(store.run("main-root").get().state==RunState::paused,"Clean shutdown preserves the typed human owner");store.close();
        }
        {
            PersistenceService store(database,imports);AgentService service(store,frozen,2,8);
            require(store.run_history("main-root").get().size()==paused_history.size(),"Paused adoption preserves actual signed history without replay");
            const auto current=store.dynamic_plan_for_root("main-root").get();require(current&&current->id==before.id&&current->revision==before.revision,"Reopen adopts the same durable plan identity");
            const auto request=question(store,*current);const std::string answer=R"({"decision":"review coding","quantity":1.00000000000000000001})";
            rejects<DynamicPlanChanged>([&]{service.plan_input("main-root",request.id,answer,"fixture-controller",current->revision,current->state_sequence-1);},"Stale input cannot consume a wake or mutate the question");
            rejects<NotFound>([&]{service.plan_input("main-root","unowned-request",answer,"fixture-controller",current->revision,current->state_sequence);},"Human input must belong to the exact owned plan");
            service.plan_input("main-root",request.id,answer,"fixture-controller",current->revision,current->state_sequence);
            std::optional<Operation> proposed;eventually([&]{for(const auto& child:store.owned_children("main-root").get())for(const auto& operation:store.operations(child.run.id).get())if(operation.state==OperationState::awaiting_approval){proposed=operation;return true;}return false;},"Actual coding child did not propose its own exact edit approval");
            const auto at_approval=store.dynamic_plan_for_root("main-root").get();const auto coding=node(*at_approval,"code");const auto proposal=Json::parse(proposed->spec.arguments_json);
            require(proposed->spec.run_id==coding.child_run_id&&coding.child_run_id!="main-root"&&proposed->spec.tool=="replace_file"&&proposal.at("before_content")=="original\n"&&proposal.at("after_content")=="reviewed dynamic plan\n","Effect approval binds actual coding child and exact file snapshots");
            require(read(workspace/"target.txt")=="original\n"&&node(*at_approval,"verify").claim_revision==0,"Human answer cannot bypass edit approval or admit success-dependent verification early");
            const auto human=store.dynamic_human_request(current->id,request.id).get();require(human.state=="answered"&&human.input_json==answer&&human.actor=="fixture-controller","Human answer retains exact JSON lexemes and authenticated actor");
            store.decide_operation(proposed->id,OperationDecision::allow,"fixture-controller").get();
            eventually([&]{return store.run("main-root").get().state==RunState::completed;},"Actual approved coding, dependent verification and signed parent continuation did not finish");
            require(read(workspace/"target.txt")=="reviewed dynamic plan\n"&&store.operation(proposed->id).get().state==OperationState::succeeded,"Actual edited bytes and succeeded operation must agree");
            const auto final=store.dynamic_plan_for_root("main-root").get();require(final&&final->id==before.id&&final->revision==2&&final->state=="completed","Successful execution completes the same revisioned plan");
            for(const auto* label:{"left","right"})require(node(*final,label).backend_node_id==node(before,label).backend_node_id&&node(*final,label).child_run_id==node(before,label).child_run_id&&node(*final,label).definition_revision==1,"Claimed and settled investigations are never replaced or replayed");
            const auto children=store.owned_children("main-root").get();require(children.size()==4,"Only two inspectors, the approved coding child and its dependent verifier were actually admitted");
            for(const auto& child:children){require(child.kind=="dynamic_agent"&&child.plan_id==before.id,"Each actual child has typed dynamic ownership");const auto rows=store.owned_child_history("main-root",child.run.id).get();
                if(child.node_label=="right"){require(child.run.state==RunState::failed&&rows.size()==3&&store.operations(child.run.id).get().empty(),"Protocol-failed inspector preserves its actual read turn without fake final answer");metrics(rows[1],13,4);}
                else{require(child.run.state==RunState::completed&&rows.size()==4,"Each successful real child owns an independent actual tool loop");const auto last=Json::parse(rows.back().json);require(last.at("provider_items")[0].at("content_json").get<std::string>().find("opaque-child-"+child.node_label)!=std::string::npos,"Signed child reasoning stays in its own history");}
            }
            const auto root_history=store.run_history("main-root").get();require(root_history.size()==6&&root_history[1].role=="assistant"&&root_history[2].role=="tool"&&root_history[3].role=="assistant"&&root_history[4].role=="tool"&&root_history[5].role=="assistant","Each actual signed parent plan call and joined result commits once together");
            const auto first=Json::parse(Json::parse(root_history[2].json).at("content").get<std::string>()),joined=Json::parse(Json::parse(root_history[4].json).at("content").get<std::string>());
            require(first.at("plan_id")==joined.at("plan_id")&&first.at("revision")==1&&joined.at("revision")==2&&joined.at("finished")==true,"Actual model reports show failure/revision/completion without inventing new plan identity");
            for(const auto& row:root_history)require(row.json.find("opaque-child-")==std::string::npos,"Child continuation receipts never leak into the parent's signed history");metrics(root_history.back(),17,6);
            const auto budget=store.root_budget("main-root").get();require(budget.children_admitted==4&&budget.model_calls_reserved==11&&budget.parent_model_calls_reserved==3&&budget.parent_calls_held==0,"Actual eleven requests consume one shared durable budget and held continuations once");
            require(count(store.events("main-root").get(),"conversation.tool_turn")==2&&count(store.events("main-root").get(),"plan.call.accepted")==2,"Two accepted revisions and signed tool turns must be once-only");
            private_absent(store,"main-root");
            // The final dependency is a human node, with no child dispatch. Hold
            // the synthetic final provider response to test duplicate input.
            store.create_session("final-human-session","Actual final human-only wake").get();service.submit("final-human-root","final-human-session","fixture-final-human-parent: Ask one human question, then consume the held continuation without children.");
            const auto human_plan=paused(store,"final-human-root");const auto final_question=question(store,human_plan);const std::string final_answer=R"({"answer":"confirmed"})";
            service.plan_input("final-human-root",final_question.id,final_answer,"fixture-controller",human_plan.revision,human_plan.state_sequence);
            eventually([&]{return store.run("final-human-root").get().state==RunState::running&&count(store.events("final-human-root").get(),"budget.model_call.started")==2;},"Final human answer must wake the same ordinary root's held model continuation");
            service.plan_input("final-human-root",final_question.id,final_answer,"fixture-controller",human_plan.revision,human_plan.state_sequence);
            rejects<Conflict>([&]{service.resume_plan("final-human-root","fixture-controller",human_plan.revision,human_plan.state_sequence);},"An already-working owner cannot acquire a duplicate resume");
            write(workspace/"release-final-human", "release");eventually([&]{return store.run("final-human-root").get().state==RunState::completed;},"Human-only signed continuation did not retire");
            require(store.owned_children("final-human-root").get().empty()&&store.root_budget("final-human-root").get().model_calls_reserved==2&&store.run_history("final-human-root").get().size()==4,"Human-only answer plus duplicate input consumes exactly origin and one continuation, with no child");
            store.create_session("cancel-session","Actual paused owner cancellation").get();service.submit("cancel-root","cancel-session","fixture-cancel-parent: Publish a question and remain paused until controller cancellation.");paused(store,"cancel-root");service.cancel("cancel-root");
            eventually([&]{return store.run("cancel-root").get().state==RunState::cancelled;},"Explicit cancellation must retire even if worker phase still precedes durable pause observation");
            require(store.run("cancel-root").get().state==RunState::cancelled&&store.root_budget("cancel-root").get().model_calls_reserved==1&&store.owned_children("cancel-root").get().empty(),"Paused cancellation retires without provider continuation or fabricated children");
            service.close();store.close();
        }
        {
            PersistenceService store(database,imports);auto expiry=frozen;expiry.planning=DynamicPlanningPolicy{};expiry.planning->human_expiry_ms=500;AgentService service(store,expiry,1,8);
            store.create_session("expiry-session","Actual native human expiry").get();service.submit("expiry-root","expiry-session","fixture-expiry-parent: Publish a question and let native policy expire it without another provider request.");
            const auto plan=paused(store,"expiry-root");const auto request=question(store,plan);
            eventually([&]{return store.run("expiry-root").get().state==RunState::failed;},"Native waiting owner expiry did not wake and retire");
            require(store.dynamic_human_request(plan.id,request.id).get().state=="expired"&&store.root_budget("expiry-root").get().model_calls_reserved==1&&store.root_budget("expiry-root").get().parent_calls_held==0&&store.run_history("expiry-root").get().size()==1,"Expiry must retain the actual failed question and never dispatch the held continuation");
            private_absent(store,"expiry-root");service.close();store.close();
        }
        std::cout<<"Native dynamic engine contract passed: actual dependency children/failure, same-plan revision, closed-pause reopen, same-owner human wake, child edit approval/effect, dependent read, independent signed histories/usage, human-only duplicate wake, paused cancel and native expiry. Provider replies/key are synthetic.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
