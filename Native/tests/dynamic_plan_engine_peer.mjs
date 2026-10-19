// Independent synthetic signed Anthropic wire. Actual native Agents, approved
// workspace mutation and xlang3 SQLite are exercised by the compiled contract.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const [executable,modules,stdlib,cliExecutable,runtimeExecutable]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib&&cliExecutable&&runtimeExecutable,'Expected engine contract, actual xmind_cli and runtime module/source roots');
const folder=await mkdtemp(join(tmpdir(),'xmind-dynamic-engine-')),workspace=join(folder,'workspace');
const key='synthetic-dynamic-engine-key-not-live',counts=new Map(),requests=[],barrier=[],planningBarrier=[],held=[];
let failure;let completed=false;
const callableCounts=new Map();
const serviceControlCounts={start:0,stop:0};let serviceControlJob;
const event=value=>'event: '+value.type+'\ndata: '+JSON.stringify(value)+'\n\n';
const start=input=>event({type:'message_start',message:{id:'synthetic-dynamic-message',type:'message',role:'assistant',model:'synthetic-dynamic-engine',content:[],stop_reason:null,stop_sequence:null,usage:{input_tokens:input,output_tokens:0}}});
function thought(signature){return event({type:'content_block_start',index:0,content_block:{type:'thinking',thinking:'',signature:''}})+event({type:'content_block_delta',index:0,delta:{type:'thinking_delta',thinking:'Explicit synthetic reasoning for '+signature}})+event({type:'content_block_delta',index:0,delta:{type:'signature_delta',signature}})+event({type:'content_block_stop',index:0});}
function tool(name,id,args){const raw=JSON.stringify(args),half=Math.floor(raw.length/2);return event({type:'content_block_start',index:1,content_block:{type:'tool_use',id,name,input:{}}})+event({type:'content_block_delta',index:1,delta:{type:'input_json_delta',partial_json:raw.slice(0,half)}})+event({type:'content_block_delta',index:1,delta:{type:'input_json_delta',partial_json:raw.slice(half)}})+event({type:'content_block_stop',index:1});}
const text=value=>event({type:'content_block_start',index:1,content_block:{type:'text',text:''}})+event({type:'content_block_delta',index:1,delta:{type:'text_delta',text:value}})+event({type:'content_block_stop',index:1});
const terminal=(reason,output)=>event({type:'message_delta',delta:{stop_reason:reason,stop_sequence:null},usage:{output_tokens:output}})+event({type:'message_stop'});
function send(response,wire){response.writeHead(200,{'Content-Type':'text/event-stream'});const bytes=Buffer.from(wire);for(let offset=0;offset<bytes.length;offset+=13)response.write(bytes.subarray(offset,offset+13));response.end();}
const content=messages=>messages.flatMap(message=>message.content??[]).filter(block=>block.type==='text').map(block=>block.text).join('\n');
const results=messages=>messages.flatMap(message=>message.content??[]).filter(block=>block.type==='tool_result');
const agent=(id,objective,preset='workspace.inspect',depends_on=[])=>({id,type:'agent',objective,preset,depends_on});
const toolNode=(id,name,arguments_json,depends_on=[])=>({id,type:'tool',tool:name,arguments_json:JSON.stringify(arguments_json),depends_on});
const human=(id,question,depends_on=[])=>({id,type:'human',question,depends_on});
const dependency=(task,require='success')=>({task,require});
function planQuestion(label){return {expected_revision:0,expected_state_sequence:0,add:[human('gate','Explicit synthetic '+label+' question; answer is data, not effect approval.')]};}
function unfinishedReport(observed){
 const labels=['left','right','code','verify','gate'];
 const states=['pending','blocked','claimed','waiting_human','settled','skipped','cancelled','uncertain'];
 const childStates=['queued','running','paused','completed','failed','cancelled'];
 const effectStates=['none','awaiting_approval','ready','executing','succeeded','failed','cancelled','uncertain'];
 const reasons=['model_protocol_error','agent_error','provider_http_error','provider_transport_error','provider_timeout','agent_timeout','execution_budget_exhausted','model_turn_limit','incompatible_provider_history','dynamic_configuration_changed','dynamic_child_configuration_changed','dynamic_preset_unavailable','dynamic_process_precondition_unavailable','dynamic_workspace_precondition_unavailable','dynamic_runtime_path_unavailable','cancelled','cancelled_before_dynamic_child','process_effect_uncertain','file_effect_uncertain','mcp_effect_uncertain'];
 const safe=(value,allowed)=>value===undefined?null:allowed.includes(value)?value:'unlisted';
 const nodes=labels.map(id=>{
  const node=Array.isArray(observed.nodes)?observed.nodes.find(value=>value?.id===id):undefined;
  if(!node)return {id,present:false};
  let error,invalidOutcome=false;
  try{if(typeof node.outcome_json==='string')error=JSON.parse(node.outcome_json)?.error;}catch{invalidOutcome=true;}
  return {id,present:true,state:safe(node.state,states),child_state:safe(node.child_state,childStates),effect_state:safe(node.effect_state,effectStates),error_reason:safe(error?.reason,reasons),error_code:safe(error?.code,['result_limit_exceeded']),invalid_outcome:invalidOutcome};
 });
 // Only fixture labels and fixed native states/codes are emitted. Never dump
 // objectives, tool content, provider receipts or arbitrary outcome members.
 process.stderr.write('Synthetic dynamic-engine unfinished nodes: '+JSON.stringify(nodes)+'\n');
}
const release=setInterval(()=>{for(let i=held.length-1;i>=0;i--){const item=held[i];if(existsSync(join(workspace,item.marker))){held.splice(i,1);send(item.response,item.wire);}}},10);
const peer=createServer((request,response)=>{
 const chunks=[];let size=0;let owner='unknown',count=0;request.on('data',bytes=>{size+=bytes.length;if(size>1024*1024){request.destroy();return;}chunks.push(bytes);});
 request.on('end',()=>{try{
  assert.equal(request.method,'POST');assert.equal(request.url,'/messages');assert.equal(request.headers['x-api-key'],key);assert.equal(request.headers.authorization,undefined);
  const raw=Buffer.concat(chunks).toString('utf8');assert.ok(!raw.includes(key));const body=JSON.parse(raw);assert.equal(body.model,'synthetic-dynamic-engine');assert.equal(body.stream,true);
  const names=body.tools.map(tool=>tool.name).sort(),source=content(body.messages),canPlan=names.includes('plan_tasks');
  const serviceControl=source.includes('fixture-service-stop-parent')?'stop':source.includes('fixture-service-start-parent')?'start':undefined;
  if(serviceControl){
   assert.ok(canPlan&&names.includes('run_process')&&names.includes('wait_process')&&names.includes('stop_process'),'Planned coding roots must advertise the complete persistent service lifecycle');
   const turn=++serviceControlCounts[serviceControl],last=results(body.messages).at(-1),signature='opaque-parent-service-control-'+serviceControl+'-'+turn;
   if(serviceControl==='start'){
    if(turn===1){send(response,start(11)+thought(signature)+tool('read_repository_instructions','service-guidance-call',{directory:'.'})+terminal('tool_use',3));return;}
    if(turn===2){assert.equal(last.tool_use_id,'service-guidance-call');send(response,start(11)+thought(signature)+tool('run_process','service-start-call',{profile:'fixture-service',arguments:[],background:true,persistent:true,timeout_ms:30000})+terminal('tool_use',3));return;}
    assert.equal(turn,3);assert.equal(last.tool_use_id,'service-start-call');const started=JSON.parse(last.content);assert.equal(started.state,'running',JSON.stringify(started.error??{keys:Object.keys(started)}));assert.ok(started.job_id);serviceControlJob=started.job_id;send(response,start(11)+thought(signature)+text('Actual native fixture service remains running after this response.')+terminal('end_turn',3));return;
   }
   if(turn===1){assert.ok(serviceControlJob);send(response,start(11)+thought(signature)+tool('stop_process','service-stop-call',{job_id:serviceControlJob})+terminal('tool_use',3));return;}
   assert.equal(turn,2);assert.equal(last.tool_use_id,'service-stop-call');const stopped=JSON.parse(last.content);assert.equal(stopped.state,'succeeded');assert.equal(stopped.result.service_stopped,true);assert.equal(stopped.result.process_tree_retired,true);send(response,start(11)+thought(signature)+text('Actual prior native service tree is stopped.')+terminal('end_turn',3));return;
  }
  const callable=source.match(/fixture-callable-(root|inspect|code)-(allow|deny|worker):/);
  if(callable){
   const [,kind,mode]=callable,identity=kind+'-'+mode,turn=(callableCounts.get(identity)??0)+1;callableCounts.set(identity,turn);
   const last=results(body.messages).at(-1),signature='opaque-callable-'+identity+'-'+turn;
   if(kind==='inspect'){assert.equal(turn,1);assert.ok(!names.includes('trusted_graph_probe'),'Inspect preset must not advertise custom callable effects');send(response,start(5)+thought(signature)+text('Actual inspect child has no custom callable effect.')+terminal('end_turn',2));return;}
   assert.ok(names.includes('trusted_graph_probe'),'Frozen coding catalogue must retain its actual registered callable');
   if(kind==='root'){
    if(turn===1){send(response,start(7)+thought(signature)+tool('plan_tasks','callable-plan-'+mode,{expected_revision:0,expected_state_sequence:0,add:[agent('inspect','fixture-callable-inspect-'+mode+': report your inspect tool catalogue.'),agent('code','fixture-callable-code-'+mode+': invoke the registered graph callable once.','workspace.coding',[dependency('inspect')])]})+terminal('tool_use',3));return;}
    assert.equal(turn,2);assert.equal(last.tool_use_id,'callable-plan-'+mode);const actual=JSON.parse(last.content);assert.equal(actual.finished,true);assert.equal(actual.nodes.length,2);for(const node of actual.nodes)assert.equal(node.child_state,'completed');send(response,start(9)+thought(signature)+text('Observed the completed native callable graph.')+terminal('end_turn',3));return;
   }
   if(turn===1){send(response,start(5)+thought(signature)+tool('trusted_graph_probe','callable-effect-'+mode,{marker:join(folder,'callable-code',mode+'.txt'),value:mode})+terminal('tool_use',2));return;}
   assert.equal(turn,2);assert.equal(last.tool_use_id,'callable-effect-'+mode);const actual=JSON.parse(last.content);if(mode==='deny')assert.equal(actual.error.code,'permission_denied');else assert.equal(actual.value,mode.toUpperCase());send(response,start(6)+thought(signature)+text('Observed actual callable '+mode+' result.')+terminal('end_turn',2));return;
  }
  const isRoot=['empty-inspect','main','nested','nested-restart','nested-parallel','nested-multihuman','final-human','restart-human','cancel','expiry'].some(label=>source.includes('fixture-'+label+'-parent'));
  const readTools=['read_repository_instructions','read_file','list_files','search_files','find_files','glob_files','list_skills','load_skill'];
  if(isRoot)assert.deepEqual(names,[...readTools,'language_server','edit_file','create_file','apply_patch','restore_operation','plan_tasks','revise_plan','inspect_plan'].sort(),'Ordinary Agent planning retains its configured coding authority');
  owner=isRoot?(['empty-inspect','main','nested','nested-restart','nested-parallel','nested-multihuman','final-human','restart-human','cancel','expiry'].find(label=>source.includes('fixture-'+label+'-parent'))):(['nested-multihuman-left','nested-multihuman-right','nested-parallel-left-reader','nested-parallel-right-reader','nested-parallel-left-planner','nested-parallel-right-planner','left','right','code','verify','nested-outer','nested-restart-outer','nested-inner','nested-deep'].find(label=>source.includes('fixture-'+(label.startsWith('nested-')?'nested-':'main-')+label.replace(/^nested-/,'')+':')));
  assert.ok(owner,'Unknown actual execution objective');count=(counts.get(owner)??0)+1;counts.set(owner,count);requests.push({owner,count});
  const last=results(body.messages).at(-1),signature=(isRoot?'opaque-parent-':'opaque-child-')+owner+'-'+count;
  const providerThinking=body.messages.filter(message=>message.role==='assistant').flatMap(message=>message.content??[]).filter(block=>block.type==='thinking');
  for(const block of providerThinking)assert.ok(block.signature.startsWith((isRoot?'opaque-parent-':'opaque-child-')+owner+'-'),'Each signed continuation belongs only to this execution');
  if(isRoot){
   assert.ok(!raw.includes('opaque-child-'),'Parent cannot acquire child signed reasoning');
   if(owner==='empty-inspect'){
    if(count===1){assert.equal(last,undefined);const empty=event({type:'content_block_start',index:1,content_block:{type:'tool_use',id:'empty-inspect-origin',name:'inspect_plan',input:{}}})+event({type:'content_block_delta',index:1,delta:{type:'input_json_delta',partial_json:''}})+event({type:'content_block_stop',index:1});send(response,start(13)+thought(signature)+empty+terminal('tool_use',4));return;}
    assert.equal(count,2,'No-argument inspection needs only its actual continuation');assert.equal(last.tool_use_id,'empty-inspect-origin');assert.deepEqual(JSON.parse(last.content),{source:'native_dynamic_plan',plan:null});const original=body.messages.filter(message=>message.role==='assistant').flatMap(message=>message.content??[]).filter(block=>block.type==='tool_use');assert.equal(original.length,1);assert.deepEqual(original[0].input,{});send(response,start(17)+thought(signature)+text('Explicit synthetic parent observed that no native plan exists; inspection created no work.')+terminal('end_turn',6));return;
   }
   if(owner==='main'){
      if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('left','fixture-main-left: Read left.txt and report actual bytes.'),agent('right','fixture-main-right: Read right.txt and report actual bytes before the labelled protocol failure.'),agent('code','fixture-main-code-neverclaimed: Initial coding depends on both successful investigations.','workspace.coding',[dependency('left'),dependency('right')])]};send(response,start(17)+thought(signature)+tool('plan_tasks','main-plan-origin',args)+terminal('tool_use',6));return;}
    const observed=JSON.parse(last.content);assert.equal(observed.source,'native_dynamic_plan');assert.ok(!last.content.includes('backend_identity')&&!last.content.includes('provider_items')&&!last.content.includes('encrypted_content'));
    if(count===2){
     assert.equal(last.tool_use_id,'main-plan-origin');assert.equal(observed.revision,1);assert.equal(observed.finished,false);assert.ok(observed.blocked.includes('code'));
     const left=observed.nodes.find(node=>node.id==='left'),right=observed.nodes.find(node=>node.id==='right'),code=observed.nodes.find(node=>node.id==='code');
     assert.equal(left.child_state,'completed',JSON.stringify(left));assert.equal(right.child_state,'failed',JSON.stringify(right));assert.equal(left.protected,true);assert.equal(right.protected,true);assert.equal(code.child_run_id,undefined);assert.equal(code.protected,false);
     assert.equal(JSON.parse(left.outcome_json).content,'Observed actual left bytes: Actual dynamic left file bytes\n');
      const args={expected_revision:observed.revision,expected_state_sequence:observed.state_sequence,replace:[agent('code','fixture-main-code: Change target.txt only after independent exact edit approval, then report actual effect.','workspace.coding',[dependency('left'),dependency('right','observed'),dependency('gate')])],add:[human('gate','Review this investigation before a separately approved coding operation.',[dependency('right','observed')]),toolNode('verify','read_file',{path:'target.txt'},[dependency('code')])]};
     send(response,start(17)+thought(signature)+tool('revise_plan','main-revise-origin',args)+terminal('tool_use',6));return;
    }
    assert.equal(count,3,'The two accepted plan calls have exactly one each continuation');assert.equal(last.tool_use_id,'main-revise-origin');assert.equal(observed.revision,2);if(observed.finished!==true)unfinishedReport(observed);assert.equal(observed.finished,true);
     assert.equal(observed.nodes.find(node=>node.id==='right').child_state,'failed');assert.equal(observed.nodes.find(node=>node.id==='code').child_state,'completed');assert.equal(observed.nodes.find(node=>node.id==='verify').type,'tool');assert.equal(observed.nodes.find(node=>node.id==='verify').child_state,'completed');
     assert.equal(JSON.parse(observed.nodes.find(node=>node.id==='verify').outcome_json).data.content,'reviewed dynamic plan\n');
    const answered=JSON.parse(observed.nodes.find(node=>node.id==='gate').outcome_json);assert.equal(answered.human_state,'answered');assert.equal(answered.input_json,'{"decision":"review coding","quantity":1.00000000000000000001}');
    send(response,start(17)+thought(signature)+text('Synthetic signed parent observed failure, same-plan revision, human input, the independently approved child edit and verified actual target bytes.')+terminal('end_turn',6));return;
   }
   if(owner==='nested'){
    if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('outer','fixture-nested-outer: Plan the work and delegate one read to an inner agent, then summarize its actual result.') ]};send(response,start(17)+thought(signature)+tool('plan_tasks','nested-root-plan',args)+terminal('tool_use',6));return;}
     assert.equal(count,2);assert.equal(last.tool_use_id,'nested-root-plan');const report=JSON.parse(last.content);assert.equal(report.finished,true);const outer=report.nodes.find(node=>node.id==='outer');assert.equal(outer.child_state,'completed',JSON.stringify(outer));assert.ok(outer.outcome_json.includes('inner'));send(response,start(17)+thought(signature)+text('Nested dynamic plan joined its child agent and returned the actual read result.')+terminal('end_turn',6));return;
   }
   if(owner==='nested-restart'){
    if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('outer','fixture-nested-restart-outer: Ask for human confirmation, then continue after backend restart.')]};send(response,start(17)+thought(signature)+tool('plan_tasks','nested-restart-root-plan',args)+terminal('tool_use',6));return;}
    assert.equal(count,2);assert.equal(last.tool_use_id,'nested-restart-root-plan');const report=JSON.parse(last.content);assert.equal(report.finished,true);assert.equal(report.nodes[0].child_state,'completed');send(response,start(17)+thought(signature)+text('Recovered nested owner completed after the native service restarted.')+terminal('end_turn',6));return;
   }
   if(owner==='nested-parallel'){
    if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('left-planner','fixture-nested-parallel-left-planner: Create and join a nested plan that reads the left nested file.'),agent('right-planner','fixture-nested-parallel-right-planner: Create and join a nested plan that reads the right nested file.')]};send(response,start(17)+thought(signature)+tool('plan_tasks','nested-parallel-root-plan',args)+terminal('tool_use',6));return;}
    assert.equal(count,2);assert.equal(last.tool_use_id,'nested-parallel-root-plan');const report=JSON.parse(last.content);assert.equal(report.finished,true);for(const id of ['left-planner','right-planner']){const planned=report.nodes.find(node=>node.id===id);assert.equal(planned.child_state,'completed',JSON.stringify(planned));assert.ok(planned.outcome_json.includes('reader'));}send(response,start(17)+thought(signature)+text('Both parallel planner children completed their independent nested read plans under the shared root budget.')+terminal('end_turn',6));return;
   }
   if(owner==='nested-multihuman'){
    if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('left-questioner','fixture-nested-multihuman-left: Ask the left branch question and continue only after its answer.'),agent('right-questioner','fixture-nested-multihuman-right: Ask the right branch question and continue only after its answer.')]};send(response,start(17)+thought(signature)+tool('plan_tasks','nested-multihuman-root-plan',args)+terminal('tool_use',6));return;}
    assert.equal(count,2);assert.equal(last.tool_use_id,'nested-multihuman-root-plan');const report=JSON.parse(last.content);assert.equal(report.finished,true);for(const id of ['left-questioner','right-questioner'])assert.equal(report.nodes.find(node=>node.id===id).child_state,'completed');send(response,start(17)+thought(signature)+text('Both independently answered nested branches completed under one durable root wait set.')+terminal('end_turn',6));return;
   }
   if(count===1){assert.equal(last,undefined);send(response,start(7)+thought(signature)+tool('plan_tasks',owner+'-plan-origin',planQuestion(owner))+terminal('tool_use',3));return;}
   assert.ok(owner==='final-human'||owner==='restart-human','Expiry/cancel may not perform any provider continuation');assert.equal(count,2);assert.equal(last.tool_use_id,owner+'-plan-origin');
   const observed=JSON.parse(last.content);assert.equal(observed.finished,true);assert.equal(observed.nodes.length,1);assert.equal(observed.nodes[0].type,'human');assert.equal(observed.nodes[0].child_run_id,undefined);assert.equal(JSON.parse(observed.nodes[0].outcome_json).input_json,owner==='final-human'?'{"answer":"confirmed"}':'{"answer":"review after restart"}');
   held.push({response,marker:owner==='final-human'?'release-final-human':'release-restart-human',wire:start(7)+thought(signature)+text('Synthetic human-only continuation completed without a child.')+terminal('end_turn',3)});return;
  }
  assert.ok(!names.includes('delegate_tasks'),'Dynamic child cannot delegate outside the shared dynamic planner');
  assert.equal(canPlan,true,'Planning-enabled Agent exposes its owner-scoped dynamic plan tools');
  assert.deepEqual(names,[...(owner==='code'?[...readTools,'language_server','edit_file','create_file','apply_patch','restore_operation']:readTools),'plan_tasks','revise_plan','inspect_plan'].sort(),'Registered preset exposes exact inherited or read-only authority plus bounded planning');
  if(owner==='nested-outer'){
   if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[human('approval','Confirm that this nested owner may continue to its inner reader.')]};send(response,start(11)+thought(signature)+tool('plan_tasks','nested-outer-plan',args)+terminal('tool_use',3));return;}
   if(count===2){assert.equal(last.tool_use_id,'nested-outer-plan');const report=JSON.parse(last.content);assert.equal(report.finished,true);assert.equal(report.nodes[0].id,'approval');assert.equal(JSON.parse(report.nodes[0].outcome_json).input_json,'{"approved":true}');const args={expected_revision:report.revision,expected_state_sequence:report.state_sequence,add:[agent('inner','fixture-nested-inner: Read nested-inner.txt and report the actual bytes.')]};send(response,start(11)+thought(signature)+tool('revise_plan','nested-outer-revise',args)+terminal('tool_use',3));return;}
   assert.equal(count,3);assert.equal(last.tool_use_id,'nested-outer-revise');const report=JSON.parse(last.content);assert.equal(report.finished,true);const inner=report.nodes.find(node=>node.id==='inner');assert.equal(inner.child_state,'completed');send(response,start(11)+thought(signature)+text('Outer agent observed the completed inner plan and actual file result.')+terminal('end_turn',3));return;
  }
  if(owner==='nested-restart-outer'){
   if(count===1){assert.equal(last,undefined);send(response,start(11)+thought(signature)+tool('plan_tasks','nested-restart-human-origin',planQuestion(owner))+terminal('tool_use',3));return;}
   assert.equal(count,2);assert.equal(last.tool_use_id,'nested-restart-human-origin');const report=JSON.parse(last.content);assert.equal(report.finished,true);assert.equal(JSON.parse(report.nodes[0].outcome_json).input_json,'{"approved_after_restart":true}');send(response,start(11)+thought(signature)+text('Recovered nested accepted call observed its exact human answer.')+terminal('end_turn',3));return;
  }
  if(owner==='nested-multihuman-left'||owner==='nested-multihuman-right'){
   const answer=owner.endsWith('left')?'left':'right';
   if(count===1){assert.equal(last,undefined);const args=planQuestion(owner);const wire=start(11)+thought(signature)+tool('plan_tasks',owner+'-plan-origin',args)+terminal('tool_use',3);planningBarrier.push({response,wire});if(planningBarrier.length===2)for(const item of planningBarrier.splice(0))send(item.response,item.wire);return;}
   assert.equal(count,2);assert.equal(last.tool_use_id,owner+'-plan-origin');const report=JSON.parse(last.content);assert.equal(report.finished,true);assert.equal(report.nodes[0].type,'human');assert.equal(JSON.parse(report.nodes[0].outcome_json).input_json,JSON.stringify({answer}));send(response,start(11)+thought(signature)+text('The '+answer+' nested branch consumed its exact human answer once.')+terminal('end_turn',3));return;
  }
  if(owner==='nested-inner'){
   if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('deep','fixture-nested-deep: Read nested-deep.txt and report the actual bytes.')]};send(response,start(9)+thought(signature)+tool('plan_tasks','nested-inner-plan',args)+terminal('tool_use',2));return;}
   assert.equal(count,2);assert.equal(last.tool_use_id,'nested-inner-plan');const report=JSON.parse(last.content);assert.equal(report.finished,true);assert.equal(report.nodes[0].child_state,'completed');send(response,start(9)+thought(signature)+text('Inner agent joined the depth-two plan and observed its actual read result.')+terminal('end_turn',2));return;
  }
  if(owner.endsWith('-planner')){
   const prefix=owner.replace('-planner',''),reader=prefix+'-reader';
   if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('reader','fixture-'+reader+': Read the actual nested sibling file and report its bytes.')]};const wire=start(11)+thought(signature)+tool('plan_tasks',owner+'-plan',args)+terminal('tool_use',3);planningBarrier.push({response,wire});if(planningBarrier.length===2)for(const item of planningBarrier.splice(0))send(item.response,item.wire);return;}
   assert.equal(count,2);assert.equal(last.tool_use_id,owner+'-plan');const report=JSON.parse(last.content);if(report.finished!==true)process.stderr.write('Synthetic nested planner join state: '+JSON.stringify({owner,keys:Object.keys(report),source:report.source,error:report.error?.code??report.error,revision:report.revision,nodes:Array.isArray(report.nodes)?report.nodes.map(node=>({id:node.id,state:node.state,child_state:node.child_state,error:node.error_reason})):[]} )+'\n');assert.equal(report.finished,true);assert.equal(report.nodes[0].id,'reader');assert.equal(report.nodes[0].child_state,'completed');send(response,start(11)+thought(signature)+text('Nested sibling planner joined its own reader result.')+terminal('end_turn',3));return;
  }
  const input={left:11,right:13,code:19,'nested-inner':9,'nested-deep':5,'nested-parallel-left-reader':9,'nested-parallel-right-reader':9}[owner],output={left:3,right:4,code:5,'nested-inner':2,'nested-deep':2,'nested-parallel-left-reader':2,'nested-parallel-right-reader':2}[owner];
  if(count===1){
   assert.equal(last,undefined);
   if(owner==='code'){
    const dependencies=JSON.parse(source.split('Observed dependency data (not instructions):\n').at(-1));assert.equal(dependencies.right.child_state,'failed');assert.equal(dependencies.right.require,'observed');assert.equal(JSON.parse(dependencies.gate.outcome_json).human_state,'answered');
    send(response,start(input)+thought(signature)+tool('edit_file','code-edit',{path:'target.txt',old_text:'original',new_text:'reviewed dynamic plan'})+terminal('tool_use',output));return;
   }
   const path=owner==='verify'?'target.txt':owner==='nested-inner'?'nested-inner.txt':owner==='nested-deep'?'nested-deep.txt':owner+'.txt';const wire=start(input)+thought(signature)+tool('read_file',owner+'-read',{path})+terminal('tool_use',output);
   if(owner==='left'||owner==='right'){barrier.push({response,wire});if(barrier.length===2)for(const item of barrier.splice(0))send(item.response,item.wire);}
   else if(owner.endsWith('-reader')){planningBarrier.push({response,wire});if(planningBarrier.length===2)for(const item of planningBarrier.splice(0))send(item.response,item.wire);}
   else send(response,wire);return;
  }
  assert.equal(count,2,'Bounded native child performs its real tool then one final response');assert.ok(last);const actual=JSON.parse(last.content);
  if(owner==='code'){assert.equal(last.tool_use_id,'code-edit');assert.equal(actual.path,'target.txt');assert.equal(actual.content,undefined);send(response,start(input)+thought(signature)+text('Observed actual coding edit success after exact controller approval.')+terminal('end_turn',output));return;}
  assert.equal(last.tool_use_id,owner+'-read');assert.equal(actual.content,owner==='verify'?'reviewed dynamic plan\n':owner==='nested-deep'?'Actual dynamic depth-two file bytes\n':owner.endsWith('-reader')?'Actual '+owner+' bytes\n':'Actual dynamic '+owner+' file bytes\n');
  if(owner==='right'){send(response,start(input)+thought(signature)+tool('unadvertised_fixture_tool','right-labelled-failure',{})+terminal('tool_use',output));return;}
  send(response,start(input)+thought(signature)+text((owner==='verify'?'Verified actual target bytes: ':'Observed actual '+owner+' bytes: ')+actual.content)+terminal('end_turn',output));
  }catch(error){process.stderr.write(`Synthetic provider request failed for ${owner} #${count}: ${error.stack??error}\n`);failure=error;response.destroy();}});
});
try{
 await mkdir(workspace);for(const side of ['left','right'])await writeFile(join(workspace,side+'.txt'),'Actual dynamic '+side+' file bytes\n');for(const side of ['left','right'])await writeFile(join(workspace,`nested-parallel-${side}-reader.txt`),`Actual nested-parallel-${side}-reader bytes\n`);await writeFile(join(workspace,'target.txt'),'original\n');
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
 const result=await promisify(execFile)(executable,[folder,modules,stdlib,`http://127.0.0.1:${peer.address().port}/messages`,workspace,cliExecutable,runtimeExecutable],{windowsHide:true,timeout:60000,maxBuffer:1024*1024});
  if(failure)throw failure;for(const mode of ['allow','deny','worker']){assert.equal(callableCounts.get('root-'+mode),2);assert.equal(callableCounts.get('inspect-'+mode),1);assert.equal(callableCounts.get('code-'+mode),2);}assert.equal(callableCounts.size,9);assert.deepEqual(serviceControlCounts,{start:3,stop:2});assert.deepEqual(Object.fromEntries(counts),{'empty-inspect':2,main:3,left:2,right:2,code:2,nested:2,'nested-outer':3,'nested-restart':2,'nested-restart-outer':2,'nested-inner':2,'nested-deep':2,'nested-parallel':2,'nested-parallel-left-planner':2,'nested-parallel-right-planner':2,'nested-parallel-left-reader':2,'nested-parallel-right-reader':2,'nested-multihuman':2,'nested-multihuman-left':2,'nested-multihuman-right':2,'final-human':2,'restart-human':2,cancel:1,expiry:1});assert.equal(requests.length,46);assert.equal(barrier.length,0);assert.equal(planningBarrier.length,0);assert.equal(held.length,0);assert.equal(await readFile(join(workspace,'target.txt'),'utf8'),'reviewed dynamic plan\n');completed=true;process.stdout.write(result.stdout);
}catch(error){if(failure)throw new Error((failure.stack??String(failure))+'; actual native engine: '+(error.stderr??error.message));throw error;}
finally{clearInterval(release);peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));if(completed)await rm(folder,{recursive:true,force:true});else process.stderr.write("Dynamic engine failure evidence retained at "+folder+"\n");}
