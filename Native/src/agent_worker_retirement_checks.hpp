#pragma once
#include "agentflow/agent_worker_retirement.hpp"
#include "agentflow/persistence_service.hpp"
#include <set>
#include <algorithm>
namespace agentflow::worker_retirement_detail {
inline void process_identity(AgentWorkerProcess& worker,const AgentWorkerScope& scope,const AgentWorkerLeaseSpec& lease){
    const AgentWorkerScope expected{lease.root_run_id,lease.owner_run_id,lease.workspace_id,lease.generation,lease.workspace_revision};
    if(worker.pid()!=lease.pid||std::to_string(worker.creation_time())!=lease.creation_time||worker.executable_identity()!=lease.executable_identity||
       worker.channel().scope()!=expected||scope!=expected)
        throw Conflict("Worker drain differs from its captured process and channel");
}
inline bool dynamic_completed(PersistenceService& store,const AgentWorkerLeaseSpec& lease,const Run& run,const ChildAdmissionRecord& child){
    if(child.kind!=ChildAdmissionKind::dynamic_agent||store.root_budget(lease.root_run_id).get().spec.policy_id!="native.dynamic-plan")return false;const auto plan=store.dynamic_plan(child.plan_id).get();const auto node=std::find_if(plan.nodes.begin(),plan.nodes.end(),[&](const auto& n){return n.child_run_id==run.id;});const auto preset=std::find_if(plan.capabilities.presets.begin(),plan.capabilities.presets.end(),[&](const auto& p){return p.id==child.preset_id&&p.revision==child.preset_revision;});// Descendants retain the ultimate budget root as parent; their plan owner
    // may be another admitted descendant. Keep both lineage checks distinct.
    const auto owner=store.run(plan.owner_run_id).get();const bool owner_scope=owner.id==lease.root_run_id?owner.parent_id.empty():(owner.parent_id==lease.root_run_id&&store.child_admission(owner.id).get().root_run_id==lease.root_run_id);
    return owner_scope&&plan.root_run_id==lease.root_run_id&&run.parent_id==lease.root_run_id&&node!=plan.nodes.end()&&preset!=plan.capabilities.presets.end()&&(node->state==DynamicNodeState::claimed||node->state==DynamicNodeState::settled)&&node->claim_id==child.claim_id&&node->definition_revision==child.definition_revision&&node->claim_revision==child.claim_revision&&node->definition.label==child.node_label&&node->definition.preset_id==preset->id&&node->definition.preset_revision==preset->revision&&child.backend_identity==preset->backend_identity;
}
inline Run completed(PersistenceService& store,const AgentWorkerLeaseSpec& lease){
    const auto captured=store.agent_worker_lease(lease.generation).get();
    const auto run=store.run(lease.owner_run_id).get();
    if(captured.spec!=lease||captured.state!="retired"||captured.retirement_reason!="completed"||
       run.id!=lease.owner_run_id||run.state!=RunState::completed||run.graph_root||(lease.root_run_id==lease.owner_run_id?!run.parent_id.empty():(run.parent_id.empty()||store.child_admission(run.id).get().root_run_id!=lease.root_run_id)))
        throw Conflict("Worker drain has no captured canonical completion");
    if(lease.root_run_id!=lease.owner_run_id){const auto child=store.child_admission(run.id).get();if(child.root_run_id!=lease.root_run_id||!((child.kind==ChildAdmissionKind::delegated_leaf&&child.preset_id=="workspace.inspect"&&child.preset_revision==1)||(child.kind==ChildAdmissionKind::graph_agent&&!child.graph_node_path.empty()&&store.root_budget(lease.root_run_id).get().spec.policy_id=="native.graph-context")||dynamic_completed(store,lease,run,child)))throw Conflict("Completed worker differs from captured child admission");}
    return run;
}
}
