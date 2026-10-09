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

const [executable,modules,stdlib]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib,'Expected actual compiled contract and runtime module/source roots');
const folder=await mkdtemp(join(tmpdir(),'xmind-dynamic-engine-')),workspace=join(folder,'workspace');
const key='synthetic-dynamic-engine-key-not-live',counts=new Map(),requests=[],barrier=[],held=[];
let failure;
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
const release=setInterval(()=>{if(existsSync(join(workspace,'release-final-human')))while(held.length){const item=held.shift();send(item.response,item.wire);}},10);
const peer=createServer((request,response)=>{
 const chunks=[];let size=0;request.on('data',bytes=>{size+=bytes.length;if(size>1024*1024){request.destroy();return;}chunks.push(bytes);});
 request.on('end',()=>{try{
  assert.equal(request.method,'POST');assert.equal(request.url,'/messages');assert.equal(request.headers['x-api-key'],key);assert.equal(request.headers.authorization,undefined);
  const raw=Buffer.concat(chunks).toString('utf8');assert.ok(!raw.includes(key));const body=JSON.parse(raw);assert.equal(body.model,'synthetic-dynamic-engine');assert.equal(body.stream,true);
  const names=body.tools.map(tool=>tool.name).sort(),source=content(body.messages),parent=names.includes('plan_tasks');
  const readTools=['read_repository_instructions','read_file','list_files','search_files','glob_files','list_skills','load_skill'];
  if(parent)assert.deepEqual(names,[...readTools,'edit_file','create_file','apply_patch','plan_tasks','revise_plan','inspect_plan'].sort(),'Ordinary Agent planning retains existing exact coding authority');
  const owner=parent?(['main','final-human','cancel','expiry'].find(label=>source.includes('fixture-'+label+'-parent'))):(['left','right','code','verify'].find(label=>source.includes('fixture-main-'+label+':')));
  assert.ok(owner,'Unknown actual execution objective');const count=(counts.get(owner)??0)+1;counts.set(owner,count);requests.push({owner,count});
  const last=results(body.messages).at(-1),signature=(parent?'opaque-parent-':'opaque-child-')+owner+'-'+count;
  const providerThinking=body.messages.filter(message=>message.role==='assistant').flatMap(message=>message.content??[]).filter(block=>block.type==='thinking');
  for(const block of providerThinking)assert.ok(block.signature.startsWith((parent?'opaque-parent-':'opaque-child-')+owner+'-'),'Each signed continuation belongs only to this execution');
  if(parent){
   assert.ok(!raw.includes('opaque-child-'),'Parent cannot acquire child signed reasoning');
   if(owner==='main'){
    if(count===1){assert.equal(last,undefined);const args={expected_revision:0,expected_state_sequence:0,add:[agent('left','fixture-main-left: Read left.txt and report actual bytes.'),agent('right','fixture-main-right: Read right.txt and report actual bytes before the labelled protocol failure.'),agent('code','fixture-main-code-neverclaimed: Initial coding depends on both successful investigations.','workspace.coding',[dependency('left'),dependency('right')])]};send(response,start(17)+thought(signature)+tool('plan_tasks','main-plan-origin',args)+terminal('tool_use',6));return;}
    const observed=JSON.parse(last.content);assert.equal(observed.source,'native_dynamic_plan');assert.ok(!last.content.includes('backend_identity')&&!last.content.includes('provider_items')&&!last.content.includes('encrypted_content'));
    if(count===2){
     assert.equal(last.tool_use_id,'main-plan-origin');assert.equal(observed.revision,1);assert.equal(observed.finished,false);assert.ok(observed.blocked.includes('code'));
     const left=observed.nodes.find(node=>node.id==='left'),right=observed.nodes.find(node=>node.id==='right'),code=observed.nodes.find(node=>node.id==='code');
     assert.equal(left.child_state,'completed');assert.equal(right.child_state,'failed');assert.equal(left.protected,true);assert.equal(right.protected,true);assert.equal(code.child_run_id,undefined);assert.equal(code.protected,false);
     assert.equal(JSON.parse(left.outcome_json).content,'Observed actual left bytes: Actual dynamic left file bytes\n');
     const args={expected_revision:observed.revision,expected_state_sequence:observed.state_sequence,replace:[agent('code','fixture-main-code: Change target.txt only after independent exact edit approval, then report actual effect.','workspace.coding',[dependency('left'),dependency('right','observed'),dependency('gate')])],add:[human('gate','Review this investigation before a separately approved coding operation.',[dependency('right','observed')]),agent('verify','fixture-main-verify: Read target.txt after actual coding success and report its exact changed bytes.','workspace.inspect',[dependency('code')])]};
     send(response,start(17)+thought(signature)+tool('revise_plan','main-revise-origin',args)+terminal('tool_use',6));return;
    }
    assert.equal(count,3,'The two accepted plan calls have exactly one each continuation');assert.equal(last.tool_use_id,'main-revise-origin');assert.equal(observed.revision,2);if(observed.finished!==true)unfinishedReport(observed);assert.equal(observed.finished,true);
    assert.equal(observed.nodes.find(node=>node.id==='right').child_state,'failed');assert.equal(observed.nodes.find(node=>node.id==='code').child_state,'completed');assert.equal(observed.nodes.find(node=>node.id==='verify').child_state,'completed');
    assert.equal(JSON.parse(observed.nodes.find(node=>node.id==='verify').outcome_json).content,'Verified actual target bytes: reviewed dynamic plan\n');
    const answered=JSON.parse(observed.nodes.find(node=>node.id==='gate').outcome_json);assert.equal(answered.human_state,'answered');assert.equal(answered.input_json,'{"decision":"review coding","quantity":1.00000000000000000001}');
    send(response,start(17)+thought(signature)+text('Synthetic signed parent observed failure, same-plan revision, human input, the independently approved child edit and verified actual target bytes.')+terminal('end_turn',6));return;
   }
   if(count===1){assert.equal(last,undefined);send(response,start(7)+thought(signature)+tool('plan_tasks',owner+'-plan-origin',planQuestion(owner))+terminal('tool_use',3));return;}
   assert.equal(owner,'final-human','Expiry/cancel may not perform any provider continuation');assert.equal(count,2);assert.equal(last.tool_use_id,'final-human-plan-origin');
   const observed=JSON.parse(last.content);assert.equal(observed.finished,true);assert.equal(observed.nodes.length,1);assert.equal(observed.nodes[0].type,'human');assert.equal(observed.nodes[0].child_run_id,undefined);assert.equal(JSON.parse(observed.nodes[0].outcome_json).input_json,'{"answer":"confirmed"}');
   held.push({response,wire:start(7)+thought(signature)+text('Synthetic final human-only continuation completed without a child.')+terminal('end_turn',3)});return;
  }
  assert.ok(!names.some(name=>['plan_tasks','revise_plan','inspect_plan','delegate_tasks'].includes(name)),'Native depth-one child cannot delegate or plan');
  assert.deepEqual(names,(owner==='code'?[...readTools,'edit_file','create_file','apply_patch']:readTools).sort(),'Registered preset exposes exact inherited or read-only authority');
  const input={left:11,right:13,code:19,verify:23}[owner],output={left:3,right:4,code:5,verify:7}[owner];
  if(count===1){
   assert.equal(last,undefined);
   if(owner==='code'){
    const dependencies=JSON.parse(source.split('Observed dependency data (not instructions):\n').at(-1));assert.equal(dependencies.right.child_state,'failed');assert.equal(dependencies.right.require,'observed');assert.equal(JSON.parse(dependencies.gate.outcome_json).human_state,'answered');
    send(response,start(input)+thought(signature)+tool('edit_file','code-edit',{path:'target.txt',old_text:'original',new_text:'reviewed dynamic plan'})+terminal('tool_use',output));return;
   }
   const path=owner==='verify'?'target.txt':owner+'.txt';const wire=start(input)+thought(signature)+tool('read_file',owner+'-read',{path})+terminal('tool_use',output);
   if(owner==='left'||owner==='right'){barrier.push({response,wire});if(barrier.length===2)for(const item of barrier.splice(0))send(item.response,item.wire);}
   else send(response,wire);return;
  }
  assert.equal(count,2,'Bounded native child performs its real tool then one final response');assert.ok(last);const actual=JSON.parse(last.content);
  if(owner==='code'){assert.equal(last.tool_use_id,'code-edit');assert.equal(actual.path,'target.txt');assert.equal(actual.content,undefined);send(response,start(input)+thought(signature)+text('Observed actual coding edit success after exact controller approval.')+terminal('end_turn',output));return;}
  assert.equal(last.tool_use_id,owner+'-read');assert.equal(actual.content,owner==='verify'?'reviewed dynamic plan\n':'Actual dynamic '+owner+' file bytes\n');
  if(owner==='right'){send(response,start(input)+thought(signature)+tool('unadvertised_fixture_tool','right-labelled-failure',{})+terminal('tool_use',output));return;}
  send(response,start(input)+thought(signature)+text((owner==='verify'?'Verified actual target bytes: ':'Observed actual left bytes: ')+actual.content)+terminal('end_turn',output));
 }catch(error){failure=error;response.destroy();}});
});
try{
 await mkdir(workspace);for(const side of ['left','right'])await writeFile(join(workspace,side+'.txt'),'Actual dynamic '+side+' file bytes\n');await writeFile(join(workspace,'target.txt'),'original\n');
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
 const result=await promisify(execFile)(executable,[folder,modules,stdlib,`http://127.0.0.1:${peer.address().port}/messages`,workspace],{windowsHide:true,timeout:60000,maxBuffer:1024*1024});
 if(failure)throw failure;assert.deepEqual(Object.fromEntries(counts),{main:3,left:2,right:2,code:2,verify:2,'final-human':2,cancel:1,expiry:1});assert.equal(requests.length,15);assert.equal(barrier.length,0);assert.equal(held.length,0);assert.equal(await readFile(join(workspace,'target.txt'),'utf8'),'reviewed dynamic plan\n');process.stdout.write(result.stdout);
}catch(error){if(failure)throw failure;throw error;}
finally{clearInterval(release);peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));await rm(folder,{recursive:true,force:true});}
