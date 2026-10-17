'use strict';
// Synthetic public DTOs test the thin client boundary, not native execution.
const test=require('node:test'),assert=require('node:assert/strict');
const {BackendClient,validatePlanning,validatePlanObservation,observeOwnedRun,validatePlanInputText}=require('../client');
const {planFixture,nestedPlanFixture,nestedDepthTwoPlanFixture,nestedWaitPlanFixture}=require('./plan-fixture');
const token='synthetic-planning-access-'.padEnd(64,'x');
function snapshot(){
 const definition={id:'coding',type:'agent',objective:'Synthetic coding objective',preset:'workspace.coding',depends_on:[]};
 return {run:{id:'owner',session_id:'session',state:'paused',parent_id:'',node_id:'',graph_root:false},enabled:true,
  plan:{id:'plan',root_run_id:'owner',revision:1,state_sequence:4,state:'waiting_human',ready:[],blocked:[],claimed:[],waiting_human:['answer'],finished:false,report_ready:false,halted:false,retired_labels:[],planned_children_reserved:0,planned_humans_reserved:0,humans_published:1,nodes:[
   {...definition,backend_node_id:'node_coding',definition_revision:1,state:'settled',protected:true,effect_state:'succeeded',preset_revision:1,claim_revision:1,claim_id:'claim',child_run_id:'coding_child',child_state:'completed',outcome_json:'{"content":"Synthetic public result"}',settled_event_seq:11},
   {id:'answer',type:'human',question:'Synthetic question',depends_on:[{task:'coding',require:'success'}],backend_node_id:'node_answer',definition_revision:1,state:'waiting_human',protected:true,effect_state:'none',claim_revision:1,claim_id:'question_claim',human_request_id:'question'}]},
  questions:[{id:'question',plan_id:'plan',root_run_id:'owner',label:'answer',backend_node_id:'node_answer',question:'Synthetic question',state:'waiting',definition_revision:1,published_event_seq:12,expires_unix_ms:9999999999999,input_json:'',input_event_seq:null}],
  revisions:[{plan_id:'plan',revision:1,accepted_event_seq:7,call_id:'call',spec:{nodes:[definition,{id:'answer',type:'human',question:'Synthetic question',depends_on:[{task:'coding',require:'success'}]}]}}],
  calls:[{id:'call',plan_id:'plan',root_run_id:'owner',provider_tool_call_id:'provider_call',origin_attempt_id:'attempt',name:'plan_tasks',state:'accepted',accepted_revision:1,accepted_event_seq:7,result_event_seq:null,conversation_commit_seq:null,continuation_attempt_id:'',pending:true,response:{model:'synthetic-model',usage:{input_tokens:17,output_tokens:3,output_tokens_details:{reasoning_tokens:0}},elapsed_ms:29,first_token_ms:11}}],nested_plans:[],
  policy:{revision:1,catalogue_finalized:true,limits:{max_nodes:32,max_revisions:16,max_humans:8,change_bytes:262144,human_expiry_ms:900000,max_parent_turns:16},presets:[{id:'workspace.inspect',revision:1,readonly:true,turn_limit:4},{id:'workspace.coding',revision:1,readonly:false,turn_limit:16}]},
  budget:{policy_id:'native.dynamic-plan',policy_revision:1,revision:7,max_children:8,max_parallel:2,max_model_calls:32,wall_limit_ms:600000,children_admitted:1,planned_children_reserved:0,model_calls_reserved:3,parent_calls_held:1,parent_model_calls_reserved:1}};
}
test('planning capabilities are truthful and do not invent presets or limits',()=>{
 assert.deepEqual(validatePlanning({enabled:false,tools:[]}),{enabled:false,tools:[]});
 assert.equal(validatePlanning({enabled:true,tools:['inspect_plan','plan_tasks','revise_plan']}).enabled,true);
 for(const changed of [{enabled:false,tools:['plan_tasks']},{enabled:true,tools:['spawn']},{enabled:true,tools:['inspect_plan','plan_tasks','revise_plan'],limits:{max_children:8}}])assert.throws(()=>validatePlanning(changed));
});
test('client preserves the displayed head and exact raw input at its authenticated loopback origin',async()=>{
 const observed=[],raw='{"count":18446744073709551617,"decimal":1.00000000000000000001}';
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{observed.push({url,options});return {ok:true,json:async()=>url.endsWith('/planning')?{enabled:true,tools:['inspect_plan','plan_tasks','revise_plan']}:url.endsWith('/plan')?snapshot():snapshot().run};});
 await client.planning();const value=await client.plan('owner');await client.planInput('owner','question',raw,value.plan.revision,value.plan.state_sequence);await client.resumePlan('owner',1,5);
 assert.deepEqual(observed.map(item=>[item.url.replace(client.baseUrl,''),item.options.method]),[['/v1/agent/planning','GET'],['/v1/runs/owner/plan','GET'],['/v1/runs/owner/plan/human/question','POST'],['/v1/runs/owner/plan/resume','POST']]);
 assert.deepEqual(JSON.parse(observed[2].options.body),{input_json:raw,expected_revision:1,expected_state_sequence:4});assert.deepEqual(JSON.parse(observed[3].options.body),{expected_revision:1,expected_state_sequence:5});
 assert.ok(observed.every(item=>item.options.headers.Authorization==='Bearer '+token&&item.options.redirect==='error'));assert.equal(value.calls[0].response.usage.total_tokens,undefined,'Do not fabricate or aggregate supplied usage');
});
test('invalid input identities, types and preconditions never reach the backend',async()=>{
 let requests=0;const client=new BackendClient('http://127.0.0.1:8765',()=>token,async()=>{requests++;return {ok:true,json:async()=>snapshot().run};});
 for(const [root,question,raw,rev,seq] of [['../owner','question','{}',1,1],['owner','foreign/id','{}',1,1],['owner','question','[]',1,1],['owner','question','{}',0,1],['owner','question','{}',1,1.2],['owner','question','{"x":"'+'a'.repeat(16384)+'"}',1,1],['owner','question','{}',1,Number.MAX_SAFE_INTEGER+1]])await assert.rejects(client.planInput(root,question,raw,rev,seq));
 await assert.rejects(client.resumePlan('owner',1,0));assert.equal(requests,0);
});
test('MCP form client binds pending requests and answers to one exact run-owned identity',async()=>{
 const observed=[],request={method:'elicitation/create',params:{mode:'form',message:'Choose a target',requestedSchema:{type:'object',properties:{target:{type:'string'}},required:['target']}}},pending={id:'input-1',run_id:'owner',operation_id:'operation-1',server_config_id:'tools.peer',config_revision:2,peer_tool:'ask_user',request_json:JSON.stringify(request),state:'waiting',expires_unix_ms:Date.now()+30000,published_event_seq:9};
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{observed.push({url,options});return {ok:true,json:async()=>url.endsWith('/mcp-inputs')?{requests:[pending]}:{...pending,state:'answered'}};});
 assert.deepEqual(await client.mcpInputs('owner'),[pending]);const raw=JSON.stringify({action:'accept',content:{target:'TestProj'}});await client.answerMcpInput('owner','input-1',raw);pending.request_json=JSON.stringify({method:'elicitation/create',params:{message:'Choose a target',requestedSchema:{type:'object',properties:{target:{type:'string'}}}}});assert.deepEqual(await client.mcpInputs('owner'),[pending],'omitted mode uses the native form default');
 assert.deepEqual(observed.map(item=>[item.url.replace(client.baseUrl,''),item.options.method]),[['/v1/runs/owner/mcp-inputs','GET'],['/v1/runs/owner/mcp-inputs/input-1','POST'],['/v1/runs/owner/mcp-inputs','GET']]);assert.deepEqual(JSON.parse(observed[1].options.body),{response_json:raw});assert.ok(observed.every(item=>item.options.headers.Authorization==='Bearer '+token&&item.options.redirect==='error'));
 let dispatched=false;const invalid=new BackendClient('http://127.0.0.1:8765',()=>token,async()=>{dispatched=true;return {ok:true,json:async()=>({requests:[]})};});await assert.rejects(invalid.mcpInputs('owner/foreign'));await assert.rejects(invalid.answerMcpInput('owner','input-1','[]'));assert.equal(dispatched,false);
});
test('human input validates decoded duplicate keys, Unicode and nested JSON without reserializing',()=>{
 const raw=' {"count":18446744073709551617,"decimal":1.00000000000000000001,"nested":[{"answer":"\\uD83D\\uDE00"},null,true]}\n';assert.equal(validatePlanInputText(raw),raw);
 for(const source of ['{"a":1,"\\u0061":2}','{"nested":{"a":1,"a":2}}','{"nested":[{"a":1,"a":2}]}','{"x":"\\uD800"}','{"x":"\\uDC00"}','{"x":"\ud800"}','{"x":01}','{"x":true,}','{} false','{"x":'+ '['.repeat(65)+'0'+']'.repeat(65)+'}'])assert.throws(()=>validatePlanInputText(source));
 assert.equal(validatePlanInputText('{"__proto__":{"safe":true},"constructor":null}'),'{"__proto__":{"safe":true},"constructor":null}');
});
test('strict public planning DTOs reject foreign ownership, stale claims and private projection fields',()=>{
 assert.equal(validatePlanObservation(snapshot(),'owner').plan.nodes[0].preset,'workspace.coding');
 const mutations=[s=>s.run.id='other',s=>s.run.parent_id='other',s=>s.plan.root_run_id='other',s=>s.plan.nodes[0].claim_revision=2,s=>s.plan.nodes[0].backend_identity='private',s=>s.policy.backend_identity='private',s=>s.policy.presets[0].tools=['private'],s=>s.budget.provider_identity_json='private',s=>s.questions[0].root_run_id='other',s=>s.questions[0].definition_revision=2,s=>s.questions[0].actor='private',s=>s.calls[0].parent_assistant_json='private',s=>s.calls[0].response.provider_items_json='private',s=>s.calls[0].response.usage.private_value=7,s=>s.calls[0].response.usage.output_tokens_details.encrypted_content='private',s=>s.calls[0].accepted_revision=2,s=>s.revisions[0].accepted_event_seq=8,s=>s.revisions[0].spec.nodes[0].backend_identity='private',s=>s.plan.ready=['foreign'],s=>s.plan.nodes[1].depends_on[0].task='foreign',s=>s.calls[0].pending=false];
 for(const change of mutations){const value=snapshot();change(value);assert.throws(()=>validatePlanObservation(value,'owner'));}
 const old=snapshot();old.plan=null;old.questions=[];old.calls=[];old.revisions=[];old.policy=null;old.budget=null;assert.equal(validatePlanObservation(old,'owner').plan,null,'Historical ordinary runs remain inspectable');
  const committed=snapshot();committed.calls[0].state='turn_committed';committed.calls[0].pending=false;committed.calls[0].conversation_commit_seq=15;delete committed.calls[0].response;assert.equal(validatePlanObservation(committed,'owner').calls[0].response,undefined);
});
test('provider call correlation retains the exact native 256-byte dotted/colon identity domain',()=>{
 const value=snapshot();value.calls[0].provider_tool_call_id='provider.call:'.padEnd(256,'x');assert.equal(validatePlanObservation(value,'owner').calls[0].provider_tool_call_id.length,256);
 for(const id of ['', 'x'.repeat(257),'provider/call','provider call']){const changed=snapshot();changed.calls[0].provider_tool_call_id=id;assert.throws(()=>validatePlanObservation(changed,'owner'));}
});
test('nested plan observations bind the actual child owner and expose its call/revision ledger',()=>{
 const value=planFixture();value.nested_plans.push(nestedPlanFixture(value));assert.equal(validatePlanObservation(value,'plan-root').nested_plans[0].plan.nodes[0].backend_node_id,'nested-node-inner');
 for(const change of [v=>v.nested_plans[0].owner_run_id='foreign',v=>v.nested_plans[0].parent_node_label='gate',v=>v.nested_plans[0].plan.root_run_id='foreign',v=>v.nested_plans[0].calls[0].parent_assistant_json='private',v=>v.nested_plans[0].revisions[0].spec.nodes[0].backend_node_id='private']){const invalid=planFixture();invalid.nested_plans.push(nestedPlanFixture(invalid));change(invalid);assert.throws(()=>validatePlanObservation(invalid,'plan-root'));}
});
test('nested plan observations accept bounded human questions and enforce the root-wide limit',()=>{
 const value=planFixture();value.nested_plans.push(nestedWaitPlanFixture(value));assert.equal(validatePlanObservation(value,'plan-root').nested_plans[0].questions[0].state,'waiting');
 const invalid=planFixture();invalid.nested_plans.push(nestedWaitPlanFixture(invalid));invalid.policy.limits.max_humans=1;assert.throws(()=>validatePlanObservation(invalid,'plan-root'),/human-question limit/);
 const foreign=planFixture();const nested=nestedWaitPlanFixture(foreign);nested.questions[0].plan_id='plan-root';foreign.nested_plans.push(nested);assert.throws(()=>validatePlanObservation(foreign,'plan-root'),/question ownership/);
});
test('recursive plan observations accept only exact parent-linked depth-two owners',()=>{
 const value=planFixture(),parent=nestedPlanFixture(value),node=parent.plan.nodes[0];Object.assign(node,{state:'claimed',protected:true,claim_revision:1,claim_id:'nested-claim',child_run_id:'inner-leaf',child_state:'running'});parent.plan.ready=[];parent.plan.claimed=['inner'];value.nested_plans.push(parent,nestedDepthTwoPlanFixture(value));assert.equal(validatePlanObservation(value,'plan-root').nested_plans[1].depth,2);
 for(const change of [v=>v.nested_plans[1].parent_plan_id='plan-owned',v=>v.nested_plans[1].parent_node_label='missing',v=>v.nested_plans[1].depth=3,v=>v.nested_plans[1].owner_run_id='leaf-read']){const invalid=planFixture(),first=nestedPlanFixture(invalid);Object.assign(first.plan.nodes[0],{state:'claimed',protected:true,claim_revision:1,claim_id:'nested-claim',child_run_id:'inner-leaf',child_state:'running'});first.plan.ready=[];first.plan.claimed=['inner'];invalid.nested_plans.push(first,nestedDepthTwoPlanFixture(invalid));change(invalid);assert.throws(()=>validatePlanObservation(invalid,'plan-root'));}
});
test('coding children are observed through actual dynamic ownership without a read-only label',async()=>{
 const run=snapshot().run,child={run:{id:'coding_child',parent_id:'owner',session_id:'session',node_id:'node_coding',graph_root:false,state:'completed'},kind:'dynamic_agent',batch_id:'',task_id:'',preset_id:'workspace.coding',preset_revision:1,plan_id:'plan',node_label:'coding',claim_id:'claim',definition_revision:1,claim_revision:1};let histories=0;
 const client={treeEvents:async()=>[{seq:1,run_id:'coding_child',kind:'tool.completed',data:{}}],ownedChildren:async()=>[child],ownedChildHistory:async()=>{histories++;return [{seq:2,role:'assistant',data:{content:'Synthetic child response',usage:{input_tokens:5,output_tokens:2}}}];},operations:async()=>[]};
 const value=await observeOwnedRun(client,run,'session',0);assert.equal(value.children[0].preset_id,'workspace.coding');assert.equal(value.children[0].readonly,undefined);assert.equal(value.histories.coding_child[0].data.usage.input_tokens,5);assert.equal(histories,1);
 for(const changed of [{claim_revision:2},{plan_id:'../foreign'},{run:{...child.run,session_id:'foreign'}},{kind:'graph_agent'},{backend_identity:'private'},{run:{...child.run,provider_items_json:'private'}},{run:{...child.run,backend_identity:'private'}},{run:{...child.run,graph_root:undefined}},{run:{...child.run,state:'invented'}}])await assert.rejects(observeOwnedRun({...client,ownedChildren:async()=>[{...child,...changed}]},run,'session',0));assert.equal(histories,1,'Reject foreign or malformed claims before fetching history');
 const context={profile_id:'public_profile',route_id:'openai.responses',provider:'openai',wire:'responses',model_id:'synthetic-model',profile_revision:1};const profileChild={...child,run:{...child.run,provider_context:context}};await observeOwnedRun({...client,ownedChildren:async()=>[profileChild]},run,'session',0);assert.equal(histories,2);
 await assert.rejects(observeOwnedRun({...client,ownedChildren:async()=>[{...profileChild,run:{...profileChild.run,provider_context:{...context,credential_id:'private'}}}]},run,'session',0));assert.equal(histories,2);
});
test('deterministic dynamic tool nodes and owned runs keep exact model-free metadata',async()=>{
 const record=snapshot(),definition={id:'read-bytes',type:'tool',tool:'read_file',arguments_json:' {"path":"README.md"} ',depends_on:[]};
 const runtime={...definition,backend_node_id:'node-read-bytes',definition_revision:1,state:'settled',protected:true,effect_state:'none',claim_revision:1,claim_id:'tool-claim',child_run_id:'tool-child',child_state:'completed',outcome_json:'{"content":"actual bytes"}',settled_event_seq:13};
 record.plan.nodes.push(runtime);record.revisions[0].spec.nodes.push(definition);
 assert.equal(validatePlanObservation(record,'owner').plan.nodes.at(-1).arguments_json,definition.arguments_json,'Argument JSON is preserved byte for byte');
 const child={run:{id:'tool-child',parent_id:'owner',session_id:'session',node_id:'node-read-bytes',graph_root:false,state:'completed'},kind:'dynamic_tool',batch_id:'',task_id:'',preset_id:'',preset_revision:0,plan_id:'plan',node_label:'read-bytes',claim_id:'tool-claim',definition_revision:1,claim_revision:1};
 const client={treeEvents:async()=>[{seq:1,run_id:'tool-child',kind:'tool.completed',data:{}}],ownedChildren:async()=>[child],ownedChildHistory:async()=>[{role:'assistant',data:{content:'actual bytes'}}],operations:async()=>[]};
 const observed=await observeOwnedRun(client,record.run,'session',0);assert.equal(observed.children[0].kind,'dynamic_tool');assert.equal(observed.histories['tool-child'][0].data.content,'actual bytes');
 for(const changed of [{preset_id:'workspace.inspect'},{preset_revision:1},{claim_revision:2},{node_label:'../foreign'}])await assert.rejects(observeOwnedRun({...client,ownedChildren:async()=>[{...child,...changed}]},record.run,'session',0));
  for(const tool of ['run_process','mcp_'+'a'.repeat(48),'a2a_delegate_'+'b'.repeat(24)]){const accepted=snapshot(),call={...definition,tool,arguments_json:'{}'},node={...runtime,...call};accepted.plan.nodes.push(node);accepted.revisions[0].spec.nodes.push(call);assert.doesNotThrow(()=>validatePlanObservation(accepted,'owner'),`Valid registered tool alias rejected: ${tool}`);}
  for(const tool of ['mcp_tool','mcp_'+'z'.repeat(48),'a2a_delegate_bad','edit_file']){const invalid=snapshot(),bad={...definition,tool};invalid.plan.nodes.push({...runtime,tool});invalid.revisions[0].spec.nodes.push(bad);assert.throws(()=>validatePlanObservation(invalid,'owner'));}
});
