// Real authenticated SDK IPC with deliberately synthetic journal replies.
// This proves client scope/role decoding, not repository child admission.
#pragma once
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/agent_worker_run_journal.hpp"
#include "agentflow/process_executor.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
namespace worker_journal_role_fixture {
using namespace agentflow;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
inline void check(bool value,const char* message){if(!value){std::cerr<<message<<std::endl;throw std::runtime_error(message);}}
inline std::string child(AgentWorkerRpcPeer& peer,const std::string& method){
    if(method!="fixture.run")throw AgentWorkerRpcRemoteError("unsupported");
    const auto& scope=peer.scope();const auto role=scope.root_id==scope.run_id?ModelCallRole::parent:ModelCallRole::leaf;
    AgentWorkerJournalClient journal(peer);unsigned rejected=0;const bool graph=scope.root_id=="journal-graph-root";
    if(graph){const auto owned=make_agent_worker_ordinary_journal(peer,{});check(owned->child_admission(scope.run_id).get().graph_node_path=="outer/reader","Graph journal must retain its scoped nested path");bool denied=false;try{(void)make_agent_worker_coding_journal(peer,{});}catch(const Conflict&){denied=true;}check(denied,"Graph journal cannot acquire coding assembly");}
    for(const auto* phase:{"reserve","start","finish"}){
        auto invoke=[&](const std::string& attempt){if(std::string(phase)=="reserve")return journal.reserve_model_call(attempt);if(std::string(phase)=="start")return journal.start_model_call(attempt);return journal.finish_model_call(attempt);};
        const auto actual=invoke("correct");
        if(graph&&std::string(phase)=="reserve"){const auto remapped=invoke("remapped");check(remapped.attempt_id=="canonical-attempt"&&remapped.inference_step_id=="synthetic-step"&&remapped.role==ModelCallRole::leaf,"Typed graph reserve must accept its backend-funded canonical attempt handoff");}
        check(actual.root_run_id==scope.root_id&&actual.owner_run_id==scope.run_id&&actual.role==role&&actual.attempt_id=="correct","Journal receipt must retain its exact captured root, owner and derived role");
        std::vector<std::string> invalid_attempts={"wrong-role","wrong-root","wrong-owner","wrong-attempt","wrong-state","wrong-purpose","wrong-step","wrong-assistant"};if(graph)for(const auto* value:{"wrong-graph-path","wrong-graph-root","wrong-graph-extra","wrong-graph-kind"})invalid_attempts.push_back(value);
        for(const auto& attempt:invalid_attempts){
            bool invalid=false;try{(void)invoke(attempt);}catch(const AgentWorkerProtocolError&){invalid=true;}
            check(invalid,"Forged journal receipt must be rejected before constructing a reservation");++rejected;
        }
    }
    return Json{{"pid",GetCurrentProcessId()},{"role",role==ModelCallRole::parent?"parent":"leaf"},{"rejected",rejected}}.dump();
}
inline void parent(std::shared_ptr<const BoundProcessExecutable> image){
    for(unsigned mode=0;mode<3;++mode){const bool leaf=mode!=0,graph=mode==2;
        const auto deadline=Clock::now()+15s;
        AgentWorkerScope scope{graph?"journal-graph-root":"journal-role-root",leaf?"journal-role-leaf":"journal-role-root","workspace:none",leaf?"journal-leaf-generation":"journal-parent-generation",1};
        auto worker=AgentWorkerProcess::launch(image,scope,deadline);
        unsigned calls=0;std::string active_attempt;
        AgentWorkerRpcPeer peer(worker->channel(),[&](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token)->std::string{
            if(graph&&method=="journal.workspace_mode")return R"({"coding":false})";
            if(graph&&method=="journal.run")return Json{{"id",scope.run_id},{"session_id","synthetic-graph-session"},{"state","queued"},{"parent_id","nested-graph-invocation"},{"node_id","reader"},{"graph_root",false},{"provider_context_json",""}}.dump();
            if(graph&&method=="journal.root_budget")return Json{{"root_run_id",scope.root_id},{"spec",{{"policy_id","native.graph-context"},{"policy_revision",1},{"workspace_identity","workspace:none"},{"provider_identity_json",R"({"wire":"responses","model_id":"synthetic-graph-model"})"},{"max_children",8},{"max_parallel",2},{"max_model_calls",32},{"wall_limit_ms",20000}}},{"children_admitted",1},{"model_calls_reserved",0},{"parent_calls_held",0},{"revision",1},{"planned_children_reserved",0},{"parent_model_calls_reserved",0}}.dump();
            if(graph&&method=="journal.child_admission"){Json value={{"kind","graph_agent"},{"root_run_id",scope.root_id},{"graph_node_path","outer/reader"}};if(active_attempt=="wrong-graph-path")value["graph_node_path"]="outer//reader";if(active_attempt=="wrong-graph-root")value["root_run_id"]="foreign-root";if(active_attempt=="wrong-graph-extra")value["backend_identity"]="must-not-be-public";if(active_attempt=="wrong-graph-kind")value["kind"]="dynamic_agent";return value.dump();}
            if(method!="journal.reserve_model"&&method!="journal.start_model"&&method!="journal.finish_model")throw AgentWorkerRpcRemoteError("unsupported");
            ++calls;const auto attempt=Json::parse(payload).at("attempt_id").get<std::string>();active_attempt=attempt;
            const std::string state=method=="journal.reserve_model"?"reserved":method=="journal.start_model"?"started":"finished";
            Json value={{"root_run_id",scope.root_id},{"owner_run_id",scope.run_id},{"attempt_id",attempt},{"role",leaf?"leaf":"parent"},{"state",state},{"purpose","inference"},{"inference_step_id",graph?"synthetic-step":""},{"actual_assistant_json",state=="finished"?R"({"role":"assistant","content":"synthetic receipt"})":""}};
            if(attempt=="wrong-role")value["role"]=leaf?"parent":"leaf";
            if(attempt=="wrong-root")value["root_run_id"]="foreign-root";
            if(attempt=="wrong-owner")value["owner_run_id"]="foreign-owner";
            if(attempt=="remapped")value["attempt_id"]="canonical-attempt";
            if(attempt=="wrong-attempt")value["attempt_id"]=graph&&state=="reserved"?"invalid attempt":"foreign-attempt";
            if(attempt=="wrong-state")value["state"]="interrupted";
            if(attempt=="wrong-purpose")value["purpose"]="context_compaction";
            if(attempt=="wrong-step")value["inference_step_id"]="invalid step";
            if(attempt=="wrong-assistant")value["actual_assistant_json"]=state=="finished"?"":"invented premature assistant";
            return value.dump();
        },deadline);
        worker->channel().send({"fixture.boot","journal_role"},deadline);peer.start();
        const auto result=Json::parse(peer.call("fixture.run","{}"));
        check(result.at("pid")==worker->pid()&&result.at("role")== (leaf?"leaf":"parent")&&result.at("rejected")== (graph?36:24)&&calls==(graph?40:27),"Actual scoped child must validate all journal lifecycle replies and reject 24 forged receipts");
        peer.stop();check(worker->wait(Clock::now()+5s)==0,"Journal role fixture child must physically retire");
    }
    std::cout<<"Actual SDK IPC journal role codec passed parent/leaf/typed-graph reserve/start/finish and 84 forged receipt rejections; synthetic replies only, no child admission or funding."<<std::endl;
}
}
