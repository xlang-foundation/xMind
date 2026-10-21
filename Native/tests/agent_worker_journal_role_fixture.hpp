// Real authenticated SDK IPC with deliberately synthetic journal replies.
// This proves client scope/role decoding, not repository child admission.
#pragma once
#include "agentflow/agent_worker_journal_rpc.hpp"
#include "agentflow/process_executor.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
namespace worker_journal_role_fixture {
using namespace agentflow;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline std::string child(AgentWorkerRpcPeer& peer,const std::string& method){
    if(method!="fixture.run")throw AgentWorkerRpcRemoteError("unsupported");
    const auto& scope=peer.scope();const auto role=scope.root_id==scope.run_id?ModelCallRole::parent:ModelCallRole::leaf;
    AgentWorkerJournalClient journal(peer);unsigned rejected=0;
    for(const auto* phase:{"reserve","start","finish"}){
        auto invoke=[&](const std::string& attempt){if(std::string(phase)=="reserve")return journal.reserve_model_call(attempt);if(std::string(phase)=="start")return journal.start_model_call(attempt);return journal.finish_model_call(attempt);};
        const auto actual=invoke("correct");
        check(actual.root_run_id==scope.root_id&&actual.owner_run_id==scope.run_id&&actual.role==role&&actual.attempt_id=="correct","Journal receipt must retain its exact captured root, owner and derived role");
        for(const auto* attempt:{"wrong-role","wrong-root","wrong-owner","wrong-attempt","wrong-state","wrong-purpose","wrong-step","wrong-assistant"}){
            bool invalid=false;try{(void)invoke(attempt);}catch(const AgentWorkerProtocolError&){invalid=true;}
            check(invalid,"Forged journal receipt must be rejected before constructing a reservation");++rejected;
        }
    }
    return Json{{"pid",GetCurrentProcessId()},{"role",role==ModelCallRole::parent?"parent":"leaf"},{"rejected",rejected}}.dump();
}
inline void parent(std::shared_ptr<const BoundProcessExecutable> image){
    for(const bool leaf:{false,true}){
        const auto deadline=Clock::now()+15s;
        AgentWorkerScope scope{"journal-role-root",leaf?"journal-role-leaf":"journal-role-root","workspace:none",leaf?"journal-leaf-generation":"journal-parent-generation",1};
        auto worker=AgentWorkerProcess::launch(image,scope,deadline);
        unsigned calls=0;
        AgentWorkerRpcPeer peer(worker->channel(),[&](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token)->std::string{
            if(method!="journal.reserve_model"&&method!="journal.start_model"&&method!="journal.finish_model")throw AgentWorkerRpcRemoteError("unsupported");
            ++calls;const auto attempt=Json::parse(payload).at("attempt_id").get<std::string>();
            const std::string state=method=="journal.reserve_model"?"reserved":method=="journal.start_model"?"started":"finished";
            Json value={{"root_run_id",scope.root_id},{"owner_run_id",scope.run_id},{"attempt_id",attempt},{"role",leaf?"leaf":"parent"},{"state",state},{"purpose","inference"},{"inference_step_id",""},{"actual_assistant_json",state=="finished"?R"({"role":"assistant","content":"synthetic receipt"})":""}};
            if(attempt=="wrong-role")value["role"]=leaf?"parent":"leaf";
            if(attempt=="wrong-root")value["root_run_id"]="foreign-root";
            if(attempt=="wrong-owner")value["owner_run_id"]="foreign-owner";
            if(attempt=="wrong-attempt")value["attempt_id"]="foreign-attempt";
            if(attempt=="wrong-state")value["state"]="interrupted";
            if(attempt=="wrong-purpose")value["purpose"]="context_compaction";
            if(attempt=="wrong-step")value["inference_step_id"]="invalid step";
            if(attempt=="wrong-assistant")value["actual_assistant_json"]=state=="finished"?"":"invented premature assistant";
            return value.dump();
        },deadline);
        worker->channel().send({"fixture.boot","journal_role"},deadline);peer.start();
        const auto result=Json::parse(peer.call("fixture.run","{}"));
        check(result.at("pid")==worker->pid()&&result.at("role")== (leaf?"leaf":"parent")&&result.at("rejected")==24&&calls==27,"Actual scoped child must validate all journal lifecycle replies and reject 24 forged receipts");
        peer.stop();check(worker->wait(Clock::now()+5s)==0,"Journal role fixture child must physically retire");
    }
    std::cout<<"Actual SDK IPC journal role codec passed parent/leaf reserve/start/finish and 48 forged receipt rejections; synthetic replies only, no child admission or funding."<<std::endl;
}
}
