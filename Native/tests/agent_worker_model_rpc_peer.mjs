// Independent controlled native provider; one hundred seventy-three intentional requests, no live key.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtempSync,writeFileSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
const peerRoot=mkdtempSync(join(tmpdir(),'xmind-worker-mcp-'));let peerCalls=0,a2aCalls=0,refreshCalls=0,refreshSubscriptions=0,refreshClosed=0,catalogueChanged=false;const refreshListeners=new Set();
const [executable,modules,stdlib]=process.argv.slice(2);
const expected={coding_context_overflow:4,coding_context_overflow_count:5,coding_context_overflow_compact:1,coding_context_overflow_repeat:4,coding_context_overflow_repeat_count:5,coding_context_overflow_repeat_compact:1,worker_context_count_failure:0,worker_context_count_failure_count:1,worker_context_count_cancel:0,worker_context_count_cancel_count:1,worker_context_inference_failure:1,worker_context_inference_failure_count:1,worker_context_inference_cancel:1,worker_context_inference_cancel_count:1,coding_context:2,coding_context_count:2,coding_context_compact:3,coding_context_compact_count:4,coding_context_compact_compact:1,coding_skills:5,coding_skills_denied:3,coding_skills_rollback:2,coding_skills_manual:1,coding_callable:2,coding_callable_array:2,coding_callable_denied:2,coding_callable_policy:2,coding_callable_cancel:1,coding_callable_unrecorded:1,coding_callable_alias:2,coding_callable_worker:2,coding_callable_default:2,coding_language:2,coding_language_denied:2,coding_language_attached:2,coding_remote_factory_refresh:3,coding_remote_refresh:3,coding_a2a_factory:2,coding_remote_factory:2,coding_remote:2,coding_async_unrecorded:2,coding_async:3,coding_service:5,coding_command:2,process_create:1,process_denied:1,process_cancel:1,process_delivery:1,process_unrecorded:1,coding_create:2,coding_denied:2,coding_cancel:1,coding_unrecorded:1,coding_attached:2,file_create:1,file_denied:1,file_cancel:1,file_delivery:1,file_unrecorded:1,product_factory_thread:1,product_factory_profile:4,product_factory_attached:2,product_factory_read:2,product_factory_denied:2,product_factory:1,product_service:1,product_supervised_cancel:1,product_supervised:1,product_no_tools:1,normal:1,cancel:1,crash:1,delivery:1,overflow:1,other_error:1,completion:1,completion_delivery:1,completion_rollback:1,completion_hold:1,runner:1,runner_error:1,runner_cancel:1,read:2,read_delivery:1,read_denied:1,read_cancel:1,read_turn_delivery:1,read_turn_rollback:1,read_context:3,runner_read:3,runner_read_cancel:2,native_runner_read:3,native_runner_read_cancel:2,product_read:3,product_read_cancel:2,product_read_error:1,product_profile_read:3,product_profile_read_cancel:2};
const counts=Object.fromEntries(Object.keys(expected).map(mode=>[mode,0]));
let failure;const aborted=new Set();
function chunk(response,mode,delta,finish=null,usage){response.write('data: '+JSON.stringify({id:`rpc-${mode}`,object:'chat.completion.chunk',model:'worker-rpc-fixture',choices:[{index:0,delta,finish_reason:finish}],...(usage?{usage}:{})})+'\n\n');}
const usage={prompt_tokens:7,completion_tokens:3,total_tokens:10};
function sendContext(response, id, items, selectedModel = "worker-rpc-fixture") {
  response.writeHead(200, {'Content-Type': 'text/event-stream'});
  let sequence = 0;
  const emit = (type, fields) => response.write('event: ' + type + '\ndata: ' + JSON.stringify({type, sequence_number: sequence++, ...fields}) + '\n\n');
  emit('response.created', {response: {id, model: selectedModel, status: 'in_progress'}});
  for (let index = 0; index < items.length; ++index) {
    const item = items[index], base = {output_index: index, item_id: item.id};
    const added = {...item, status: 'in_progress'};
    if (item.type === 'reasoning') delete added.encrypted_content;
    if (item.type === 'function_call') added.arguments = '';
    if (item.type === 'message') added.content = [];
    emit('response.output_item.added', {output_index: index, item: added});
    if (item.type === 'function_call') {
      const split = Math.floor(item.arguments.length / 2);
      for (const delta of [item.arguments.slice(0, split), item.arguments.slice(split)]) emit('response.function_call_arguments.delta', {...base, delta});
      emit('response.function_call_arguments.done', {...base, arguments: item.arguments});
    } else if (item.type === 'message') {
      const part = item.content[0];
      emit('response.content_part.added', {...base, content_index: 0, part: {...part, text: ''}});
      emit('response.output_text.delta', {...base, content_index: 0, delta: part.text});
      emit('response.output_text.done', {...base, content_index: 0, text: part.text});
      emit('response.content_part.done', {...base, content_index: 0, part});
    }
    emit('response.output_item.done', {output_index: index, item});
  }
  emit('response.completed', {response: {id, model: selectedModel, status: 'completed', output: items, usage: {input_tokens: 37, output_tokens: 5, total_tokens: 42, input_tokens_details: {cached_tokens: 0}, output_tokens_details: {reasoning_tokens: 1}}}});
  response.end();
}

const server=createServer((request,response)=>{
 let bytes='';request.on('data',b=>bytes+=b);request.on('end',()=>{try{
  if(request.url.startsWith('/context_failure_status/')){const mode=request.url.split('/').at(-1),key='worker_context_'+mode;const observed=mode.startsWith('count')?counts[key+'_count']>0:counts[key]>0;response.writeHead(200,{'content-type':'application/json'});response.end(JSON.stringify({observed}));return;}
  if(request.url.startsWith('/worker_context_')){
   const route=/^\/(worker_context_(count_failure|count_cancel|inference_failure|inference_cancel))\/responses(?:\/(input_tokens))?$/.exec(request.url);assert.ok(route);const key=route[1],mode=route[2],count=!!route[3],body=JSON.parse(bytes);assert.equal(request.headers.authorization,'Bearer synthetic-model-rpc-not-live');assert.equal(body.model,'worker-rpc-fixture');assert.ok(body.input.some(m=>m.role==='system'&&JSON.stringify(m).includes('WORKER_FAILED_CONTEXT_GUIDANCE')));const counter=count?key+'_count':key;assert.equal(++counts[counter],1);
   if(count&&mode.startsWith('inference')){response.writeHead(200,{'content-type':'application/json'});response.end('{"object":"response.input_tokens","input_tokens":42}');return;}
   assert.equal(count,mode.startsWith('count'));if(mode.endsWith('cancel')){response.on('close',()=>aborted.add(key));return;}
   response.writeHead(500,{'content-type':'application/json'});response.end('{"error":{"code":"synthetic_context_failure","message":"controlled context transport failure"}}');return;
  }
  if(request.url.startsWith('/coding_context')){
   assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer synthetic-model-rpc-not-live');const body=JSON.parse(bytes);assert.equal(body.model,'worker-rpc-fixture');assert.ok(body.input.some(m=>m.role==='system'&&JSON.stringify(m).includes('WORKER_COUNTED_CONTEXT_GUIDANCE')));const match=/^\/(coding_context(?:_compact|_overflow(?:_repeat)?)?)\/responses(?:\/(input_tokens|compact))?$/.exec(request.url);assert.ok(match);const mode=match[1],compact=mode==='coding_context_compact',overflow=mode.startsWith('coding_context_overflow'),rolling=compact||overflow;
   const outputs=body.input.filter(m=>m.type==='function_call_output');for(const result of outputs)assert.equal(JSON.parse(result.output).content,'Actual counted worker bytes\n');
   if(match[2]==='input_tokens'){const key=mode+'_count';assert.ok(++counts[key]<=expected[key]);const value=rolling?10+outputs.length*80+body.input.filter(m=>m.type==='compaction').length*20:42;response.writeHead(200,{'content-type':'application/json'});response.end(JSON.stringify({object:'response.input_tokens',input_tokens:value}));return;}
   if(match[2]==='compact'){assert.equal(rolling,true);assert.equal(++counts[mode+'_compact'],1);assert.ok(outputs.length>=1);const retained=body.input.filter(m=>m.role==='user').map((m,i)=>({...m,id:'user_worker_compact_'+i,status:'completed'}));response.writeHead(200,{'content-type':'application/json'});response.end(JSON.stringify({id:'resp_worker_compaction',object:'response.compaction',created_at:1764967971,output:[...retained,{id:'cmp_worker',type:'compaction',encrypted_content:'synthetic-worker-private-canonical'}],usage:{input_tokens:31,output_tokens:7,total_tokens:38}}));return;}
   const phase=++counts[mode],last=overflow?4:compact?3:2;assert.ok(phase<=last);assert.equal(body.stream,true);assert.equal(body.max_output_tokens,128);assert.deepEqual(body.tools.map(t=>t.name),['read_file']);if(phase>1)assert.ok(outputs.length>=1);if(rolling&&phase===last){assert.ok(body.input.some(m=>m.type==='compaction'&&m.encrypted_content==='synthetic-worker-private-canonical'));assert.ok(outputs.some(m=>m.call_id==='context-worker-read-2'));assert.equal(outputs.some(m=>m.call_id==='context-worker-read-1'),false);}
   if(overflow&&(phase===3||mode.endsWith('_repeat')&&phase===4)){response.writeHead(400,{'content-type':'application/json'});response.end('{"error":{"code":"context_length_exceeded","message":"controlled no-output context rejection"}}');return;}
   const item=phase<last?{id:'fc_worker_context_'+mode+'_'+phase,type:'function_call',status:'completed',call_id:'context-worker-read-'+phase,name:'read_file',arguments:JSON.stringify({path:'actual.txt'})}:{id:'msg_worker_context_'+mode,type:'message',status:'completed',role:'assistant',content:[{type:'output_text',text:'Counted worker context verified.',annotations:[]}]};sendContext(response,'resp_worker_context_'+mode+'_'+phase,[item]);return;
  }
  if(request.url==='/factory_notify'){
   assert.equal(refreshListeners.size,1);assert.equal(catalogueChanged,false);catalogueChanged=true;for(const listener of refreshListeners)listener.send({jsonrpc:'2.0',method:'notifications/tools/list_changed',params:{_meta:listener.meta}});response.writeHead(200);response.end();return;
  }
  if(request.url==='/factory_refresh_mcp'){
   assert.equal(request.method,'POST');const rpc=JSON.parse(bytes);let result;
   if(rpc.method==='server/discover')result={supportedVersions:['2026-07-28'],capabilities:{tools:{listChanged:true}},_meta:{'io.modelcontextprotocol/serverInfo':{name:'worker-refresh-peer',version:'1'}}};
   else if(rpc.method==='tools/list')result={resultType:'complete',tools:[{name:catalogueChanged?'write_refreshed':'write_initial',description:'Write one approved refresh effect',inputSchema:{type:'object',properties:{text:{type:'string'}},required:['text'],additionalProperties:false}}]};
   else if(rpc.method==='subscriptions/listen'){
    assert.deepEqual(rpc.params.notifications,{toolsListChanged:true});++refreshSubscriptions;response.writeHead(200,{'content-type':'text/event-stream','cache-control':'no-cache'});const meta={'io.modelcontextprotocol/subscriptionId':rpc.id},listener={meta,send:value=>response.write('data: '+JSON.stringify(value)+'\r\n\r\n')};refreshListeners.add(listener);response.on('close',()=>{refreshListeners.delete(listener);++refreshClosed;});listener.send({jsonrpc:'2.0',method:'notifications/subscriptions/acknowledged',params:{_meta:meta,notifications:{toolsListChanged:true}}});return;
   }else {assert.equal(rpc.method,'tools/call');assert.equal(catalogueChanged,true);assert.equal(rpc.params.name,'write_refreshed');assert.deepEqual(rpc.params.arguments,{text:'actual production remote effect'});assert.equal(++refreshCalls,1);writeFileSync(join(peerRoot,'refresh-actual.txt'),rpc.params.arguments.text);result={resultType:'complete',isError:false,content:[{type:'text',text:'Refreshed actual HTTP effect'}],_meta:{'io.modelcontextprotocol/effectAcknowledged':true,'io.modelcontextprotocol/effectIndependentlyVerified':false}};}
   response.writeHead(200,{'content-type':'application/json'});response.end(JSON.stringify({jsonrpc:'2.0',id:rpc.id,result}));return;
  }
  if(request.url==='/factory_a2a_card'){
   assert.equal(request.method,'GET');response.writeHead(200,{'content-type':'application/json','A2A-Version':'1.0'});response.end(JSON.stringify({name:'Factory A2A peer',description:'Isolated worker factory peer',version:'1',capabilities:{streaming:false},supportedInterfaces:[{url:'http://127.0.0.1:'+server.address().port+'/factory_a2a',protocolBinding:'JSONRPC',protocolVersion:'1.0'}],skills:[]}));return;
  }
  if(request.url==='/factory_a2a'){
   assert.equal(request.method,'POST');assert.equal(request.headers['a2a-version'],'1.0');const rpc=JSON.parse(bytes);assert.equal(rpc.method,'SendMessage');assert.equal(rpc.params.message.role,'ROLE_USER');assert.deepEqual(rpc.params.message.parts,[{text:'actual production remote effect'}]);assert.equal(++a2aCalls,1);writeFileSync(join(peerRoot,'a2a-actual.txt'),rpc.params.message.parts[0].text);response.writeHead(200,{'content-type':'application/json','A2A-Version':'1.0'});response.end(JSON.stringify({jsonrpc:'2.0',id:rpc.id,result:{task:{id:'factory-task',contextId:'factory-context',status:{state:'TASK_STATE_COMPLETED'}}}}));return;
  }
  if(request.url==='/factory_mcp'){
   assert.equal(request.method,'POST');const rpc=JSON.parse(bytes);const reply=result=>{response.writeHead(200,{'content-type':'application/json'});response.end(JSON.stringify({jsonrpc:'2.0',id:rpc.id,result}));};
   if(rpc.method==='server/discover'){response.writeHead(404);response.end();return;}
   if(rpc.method==='initialize'){reply({protocolVersion:'2025-11-25',capabilities:{tools:{}},serverInfo:{name:'factory-peer',version:'1'}});return;}
   if(rpc.method==='notifications/initialized'){response.writeHead(202);response.end();return;}
   if(rpc.method==='tools/list'){reply({tools:[{name:'write',description:'Write one isolated fixture effect',inputSchema:{type:'object',properties:{text:{type:'string'}},required:['text'],additionalProperties:false}}]});return;}
   assert.equal(rpc.method,'tools/call');assert.equal(rpc.params.name,'write');assert.deepEqual(rpc.params.arguments,{text:'actual production remote effect'});assert.equal(++peerCalls,1);writeFileSync(join(peerRoot,'actual.txt'),rpc.params.arguments.text);reply({content:[{type:'text',text:'Actual factory peer acknowledged'}]});return;
  }
  const mode=request.url.slice(1);assert.ok(mode in counts);assert.ok(++counts[mode]<=expected[mode]);assert.equal(request.headers.authorization,'Bearer synthetic-model-rpc-not-live');assert.ok(!bytes.includes('synthetic-model-rpc-not-live'));
  const body=JSON.parse(bytes);assert.equal(body.model,'worker-rpc-fixture');assert.equal(body.stream,true);assert.equal(body.stream_options.include_usage,true);
  if(mode==='coding_remote_refresh'||mode==='coding_remote_factory_refresh'){
   const phase=counts[mode];assert.equal(body.messages.length,phase===1?2:phase===2?4:6);assert.match(body.messages[0].content,/PRODUCTION_REMOTE_GUIDANCE/);const tools=body.tools.filter(t=>t.function.name.startsWith('mcp_'));assert.equal(tools.length,1);const alias=tools[0].function.name;
   if(phase===2){assert.notEqual(alias,body.messages[2].tool_calls[0].function.name);assert.equal(JSON.parse(body.messages[3].content).error.code,'remote_catalogue_changed');}
   if(phase===3){assert.equal(alias,body.messages[4].tool_calls[0].function.name);assert.equal(JSON.parse(body.messages[5].content).acknowledged_by_peer,true);}
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase<3?{role:'assistant',tool_calls:[{index:0,id:phase===1?'production-remote-call':'refreshed-remote-call',type:'function',function:{name:alias,arguments:JSON.stringify({text:'actual production remote effect'})}}]}:{role:'assistant',content:'Production remote complete.'},phase<3?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='coding_remote'||mode==='coding_remote_factory'||mode==='coding_a2a_factory'){
   const phase=counts[mode];assert.equal(body.messages.length,phase===1?2:4);assert.match(body.messages[0].content,/PRODUCTION_REMOTE_GUIDANCE/);const remoteTools=body.tools.filter(t=>t.function.name.startsWith(mode==='coding_a2a_factory'?'a2a_delegate_':'mcp_'));assert.equal(remoteTools.length,1);const alias=remoteTools[0].function.name;assert.match(alias,mode==='coding_a2a_factory'?/^a2a_delegate_/:/^mcp_/);
   if(phase===2){assert.equal(body.messages[2].tool_calls[0].function.name,alias);assert.equal(body.messages[3].tool_call_id,'production-remote-call');const result=JSON.parse(body.messages[3].content);assert.equal(result.acknowledged_by_peer,true);if(mode==='coding_remote')assert.equal(result.request_id,'rpc-peer-reply');else if(mode==='coding_a2a_factory'){assert.equal(a2aCalls,1);assert.equal(result.task_id,'factory-task');}else assert.equal(peerCalls,1);assert.ok(result.operation_id);}
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase===1?{role:'assistant',tool_calls:[{index:0,id:'production-remote-call',type:'function',function:{name:alias,arguments:JSON.stringify(mode==='coding_a2a_factory'?{task:'actual production remote effect'}:{text:'actual production remote effect'})}}]}:{role:'assistant',content:'Production remote complete.'},phase===1?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('coding_skills')){
   const phase=counts[mode],manual=mode==='coding_skills_manual',denied=mode==='coding_skills_denied';assert.match(body.messages[0].content,/WORKER_SKILL_ROOT_GUIDANCE/);const restored=manual||phase===5;
   if(restored||phase>=3&&!denied)assert.match(body.messages[0].content,/SKILL_BODY_GUIDANCE/);else assert.equal(body.messages[0].content.includes('SKILL_BODY_GUIDANCE'),false);
   if(phase===5){assert.ok(body.messages.some(m=>m.role==='assistant'&&m.content==='Skill result inspected.'));assert.ok(body.messages.some(m=>m.tool_call_id==='skill-companion'&&JSON.parse(m.content).content==='actual skill companion\n'));assert.ok(body.messages.some(m=>m.role==='user'&&m.content.includes('Discover, load and inspect the workspace skill.')));assert.ok(body.messages.at(-1).content.includes('Use the saved skill without loading again.'));}
   if(phase===1)assert.equal(body.messages.some(m=>m.role==='assistant'&&m.content==='Skill result inspected.'),false);
   if(manual)assert.ok(!body.tools||body.tools.length===0);else assert.deepEqual(body.tools.map(t=>t.function.name).sort(),['list_skills','load_skill','read_file','read_repository_instructions'].sort());
   if(phase===2)assert.match(body.messages[0].content,/SKILL_DIRECTORY_GUIDANCE/);if(phase===3){const loaded=body.messages.find(m=>m.tool_call_id==='skill-load');assert.ok(loaded);const result=JSON.parse(loaded.content);if(denied)assert.equal(result.error.code,'agent_permission_denied');else {assert.equal(result.activation,'requested_for_next_model_request');assert.equal(result.effect_permission,false);}}
   if(phase===4){const read=body.messages.find(m=>m.tool_call_id==='skill-companion');assert.ok(read);assert.equal(JSON.parse(read.content).content,'actual skill companion\n');}
   let tools;if(!restored&&phase===1)tools=[['skill-list','list_skills',{}],['skill-guide','read_repository_instructions',{directory:'.agents/skills/fixture'}]];else if(!restored&&phase===2)tools=[['skill-load','load_skill',{id:'fixture'}]];else if(!restored&&phase===3&&!denied)tools=[['skill-companion','read_file',{path:'.agents/skills/fixture/note.txt'}]];
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,tools?{role:'assistant',tool_calls:tools.map(([id,name,args],index)=>({index,id,type:'function',function:{name,arguments:JSON.stringify(args)}}))}:{role:'assistant',content:'Skill result inspected.'},tools?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('coding_callable')){
   const phase=counts[mode],alias=mode==='coding_callable_alias'?'language_server':'trusted_probe';assert.match(body.messages[0].content,/WORKER_CALLABLE_GUIDANCE/);assert.equal(body.messages.length,phase===1?2:4);assert.equal(body.tools.filter(t=>t.function.name===alias).length,1);if(mode!=='coding_callable_default')assert.deepEqual(body.tools.map(t=>t.function.name),[alias]);else assert.ok(body.tools.some(t=>t.function.name==='read_file'));
   if(phase===2){assert.equal(body.messages[3].tool_call_id,'callable-call');const value=JSON.parse(body.messages[3].content);if(mode==='coding_callable_policy')assert.equal(value.error.code,'agent_permission_denied');else if(mode==='coding_callable_denied')assert.equal(value.error.code,'permission_denied');else if(mode==='coding_callable_array'){assert.equal(value[0],'ACTUAL CALLABLE EFFECT');assert.ok(value[1]>0);}else {assert.equal(value.text,'ACTUAL CALLABLE EFFECT');assert.ok(value.pid>0);}}
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase===1?{role:'assistant',tool_calls:[{index:0,id:'callable-call',type:'function',function:{name:alias,arguments:JSON.stringify({text:'actual callable effect'})}}]}:{role:'assistant',content:'Callable result inspected.'},phase===1?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('coding_language')){
   const phase=counts[mode];assert.match(body.messages[0].content,/WORKER_LANGUAGE_GUIDANCE/);assert.equal(body.messages.length,phase===1?2:4);assert.deepEqual(body.tools.map(t=>t.function.name),['language_server']);let args={server_id:'worker-language-peer',method:'diagnostics',path:'check.ts'};if(mode==='coding_language_attached'){args.folder_id=body.messages[0].content.match(/- folder_id=(\S+) name=.* \(attached\)/)?.[1];assert.ok(args.folder_id);}
   if(phase===2){const result=JSON.parse(body.messages[3].content);if(mode==='coding_language_denied')assert.equal(result.error.code,'agent_permission_denied');else {assert.equal(result.result.items.length,1);assert.equal(result.result.items[0].code,2322);}}
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase===1?{role:'assistant',tool_calls:[{index:0,id:'language-query',type:'function',function:{name:'language_server',arguments:JSON.stringify(args)}}]}:{role:'assistant',content:'Language result inspected.'},phase===1?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='coding_async'||mode==='coding_async_unrecorded'){
   const phase=counts[mode];assert.match(body.messages[0].content,/CODING_ASYNC_GUIDANCE/);assert.deepEqual(body.tools.map(t=>t.function.name),['run_process']);
   if(phase===2){assert.ok(body.messages.find(m=>m.tool_call_id==='async-launch'));assert.equal(body.messages.some(m=>m.role==='system'&&m.content.includes('background_processes_joined_before_final_response')),false);}
   if(phase===3){const note=body.messages.at(-1);assert.equal(note.role,'system');const result=JSON.parse(note.content.slice(note.content.indexOf('{')));assert.equal(result.reason,'background_processes_joined_before_final_response');assert.equal(result.jobs.length,1);assert.equal(result.jobs[0].state,'succeeded');assert.equal(result.jobs[0].exit_code,0);assert.equal(result.jobs[0].stdout.data,'async-done\r\n');assert.equal(body.messages.some(m=>m.content==='Early final must be withheld.'),false);}
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase===1?{role:'assistant',tool_calls:[{index:0,id:'async-launch',type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'xlang3',arguments:['-B','-c',"import time; from pathlib import Path; time.sleep(2); Path('finished.txt').write_bytes(b'actual async result'); print('async-done')"],workdir:'.',timeout_ms:10000,background:true})}}]}:{role:'assistant',content:phase===2?'Early final must be withheld.':'Async result verified.'},phase===1?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='coding_service'){
   const phase=counts[mode];assert.match(body.messages[0].content,/CODING_SERVICE_GUIDANCE/);assert.deepEqual(body.tools.map(t=>t.function.name).sort(),['run_process','stop_process','wait_process']);
   let tool;
   if(phase===1){assert.equal(body.messages.length,2);tool=['service-launch','run_process',{profile:'xlang3',arguments:['-B','-c',"import time; from pathlib import Path; Path('ready.txt').write_bytes(b'ready'); time.sleep(25)"],workdir:'.',timeout_ms:30000,background:true,persistent:true}];}
   if(phase===2){const launch=body.messages.find(m=>m.tool_call_id==='service-launch');assert.ok(launch);const result=JSON.parse(launch.content);assert.equal(result.background,true);assert.equal(result.persistent,true);tool=['service-observe','wait_process',{job_id:result.job_id,timeout_ms:1000}];}
   if(phase===3){const observed=body.messages.find(m=>m.tool_call_id==='service-observe');assert.ok(observed);assert.equal(JSON.parse(observed.content).state,'running');}
   if(phase===4){const prompt=body.messages.filter(m=>m.role==='user').at(-1).content;const match=prompt.match(/^Stop the existing service job (\S+)$/);assert.ok(match);tool=['service-stop','stop_process',{job_id:match[1]}];}
   if(phase===5){const stopped=body.messages.find(m=>m.tool_call_id==='service-stop');assert.ok(stopped);assert.equal(JSON.parse(stopped.content).state,'succeeded');}
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,tool?{role:'assistant',tool_calls:[{index:0,id:tool[0],type:'function',function:{name:tool[1],arguments:JSON.stringify(tool[2])}}]}:{role:'assistant',content:phase===3?'Service ready.':'Service stopped.'},tool?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='coding_command'){
   const phase=counts[mode];assert.equal(body.messages.length,phase===1?2:6);assert.match(body.messages[0].content,/CODING_COMMAND_GUIDANCE/);const marker=body.messages[1].content.match(/CODING_COMMAND_\d+$/);assert.ok(marker);assert.deepEqual(body.tools.map(t=>t.function.name).sort(),['read_file','create_file','run_process'].sort());assert.equal(body.max_completion_tokens,64);
   if(phase===2){assert.equal(body.messages[2].tool_calls.length,3);assert.equal(JSON.parse(body.messages[3].content).content,marker[0]+'\n');assert.ok(JSON.parse(body.messages[4].content).operation_id);const result=JSON.parse(body.messages[5].content);assert.equal(body.messages[5].tool_call_id,'command-process');assert.equal(result.exit_code,0);assert.equal(result.stdout.data,'production-command-ok\r\n');}
   const code="import sys; assert sys.implementation.name == 'xlang3'; from pathlib import Path; Path('command.txt').write_bytes(Path('created.txt').read_bytes()); print('production-command-ok')";
   const tools=[['command-read','read_file',{path:'input.txt'}],['command-create','create_file',{path:'created.txt',content:marker[0]+'\n'}],['command-process','run_process',{profile:'xlang3',arguments:['-B','-c',code],workdir:'.',timeout_ms:10000}]];
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase===1?{role:'assistant',tool_calls:tools.map(([id,name,args],index)=>({index,id,type:'function',function:{name,arguments:JSON.stringify(args)}}))}:{role:'assistant',content:'Production command complete.'},phase===1?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('coding_')){
   const phase=counts[mode];assert.equal(body.messages.length,phase===1?2:5);assert.equal(body.messages[0].role,'system');assert.match(body.messages[0].content,/CODING_ROOT_GUIDANCE/);assert.match(body.messages[0].content,/Native coding worker selected instructions/);assert.equal(body.max_completion_tokens,64);
   const marker=body.messages[1].content.match(/CODING_MARKER_\d+$/)?.[0];assert.ok(marker);assert.deepEqual(body.tools.map(t=>t.function.name).sort(),['create_file','read_file']);
   if(phase===2){assert.equal(body.messages[3].tool_call_id,'coding-read');assert.equal(JSON.parse(body.messages[3].content).content,marker+'\n');assert.equal(body.messages[4].tool_call_id,'coding-file');const result=JSON.parse(body.messages[4].content);if(mode==='coding_denied')assert.equal(result.error.code,'agent_permission_denied');else assert.ok(result.operation_id||result.state||result.path);}
   let folder;if(mode==='coding_attached')folder=body.messages[0].content.match(/- folder_id=(\S+) name=.* \(attached\)/)?.[1];if(mode==='coding_attached')assert.ok(folder);
   const read={path:'input.txt',...(folder?{folder_id:folder}:{})},write={path:'created.txt',content:marker+'\n',...(folder?{folder_id:folder}:{})};
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,phase===1?{role:'assistant',tool_calls:[{index:0,id:'coding-read',type:'function',function:{name:'read_file',arguments:JSON.stringify(read)}},{index:1,id:'coding-file',type:'function',function:{name:'create_file',arguments:JSON.stringify(write)}}]}:{role:'assistant',content:'Coding worker complete.'},phase===1?'tool_calls':'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('process_')){
   assert.equal(body.messages.length,1);assert.equal(body.messages[0].role,'user');assert.ok(body.messages[0].content.startsWith('Native child process RPC '+mode+'. '));const marker=body.messages[0].content.match(/PROCESS_RPC_MARKER_\d+$/);assert.ok(marker);
   assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'run_process');
   const code="import sys; assert sys.implementation.name == 'xlang3'; from pathlib import Path; Path('actual.txt').write_bytes("+JSON.stringify(marker[0]+'\n')+".encode()); print('process-rpc-ok')";
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',tool_calls:[{index:0,id:'actual-process',type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'xlang3',arguments:['-B','-c',code],workdir:'.',timeout_ms:10000})}}]},'tool_calls',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('file_')){
   assert.equal(body.messages.length,1);assert.equal(body.messages[0].role,'user');assert.ok(body.messages[0].content.startsWith('Native child file RPC '+mode+'. '));const marker=body.messages[0].content.match(/FILE_RPC_MARKER_\d+$/);assert.ok(marker);
   assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'create_file');assert.deepEqual(body.tools[0].function.parameters.required,['path','content']);
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',tool_calls:[{index:0,id:'actual-file',type:'function',function:{name:'create_file',arguments:JSON.stringify({path:'actual.txt',content:marker[0]+'\n'})}}]},'tool_calls',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode.startsWith('product_factory')){
   const read=mode!=='product_factory'&&mode!=='product_factory_profile'&&mode!=='product_factory_thread',denied=mode==='product_factory_denied',phase=read?counts[mode]:1;assert.equal(body.messages.length,phase===1?2:4);assert.equal(body.messages[0].role,'system');const text=body.messages[0].content;let previous=-1;for(const marker of ['Factory base instructions.','Factory selected instructions.','FACTORY_REPOSITORY_GUIDANCE','Factory backend policy suffix.']){const index=text.indexOf(marker);assert.ok(index>previous);previous=index;}assert.ok(text.endsWith('Factory backend policy suffix.'));assert.equal(body.max_completion_tokens,32);assert.deepEqual(body.messages[1],{role:'user',content:'Native child model RPC '+mode+'.'});
   if(read){assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');}else assert.ok(!('tools' in body)||body.tools.length===0);
   if(phase===2){assert.equal(body.messages[2].role,'assistant');assert.equal(body.messages[2].tool_calls[0].id,'factory-read');assert.equal(body.messages[3].role,'tool');assert.equal(body.messages[3].tool_call_id,'factory-read');const output=JSON.parse(body.messages[3].content);if(denied)assert.equal(output.error.code,'agent_permission_denied');else assert.match(output.content,/^factory-native-read-\d+\n$/);}
   let args={path:'same.txt'};if(mode==='product_factory_attached'){assert.ok(body.tools[0].function.parameters.properties.folder_id);const match=text.match(/- folder_id=(\S+) name=.* \(attached\)/);assert.ok(match);args.folder_id=match[1];}
   if(mode==='product_factory_profile'&&counts[mode]===2){response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:'Factory partial before cancellation.'});response.on('close',()=>aborted.add('product_factory_profile_cancel'));return;}
   response.writeHead(200,{'content-type':'text/event-stream'});if(read&&phase===1)chunk(response,mode,{role:'assistant',tool_calls:[{index:0,id:'factory-read',type:'function',function:{name:'read_file',arguments:JSON.stringify(args)}}]},'tool_calls',usage);else{chunk(response,mode,{role:'assistant',content:'Factory worker complete.'});chunk(response,mode,{},'stop',usage);}response.end('data: [DONE]\n\n');return;
  }
  if(mode==='product_service'){
   assert.equal(body.messages.length,2);assert.equal(body.messages[0].role,'system');for(const marker of ['Service captured base.','Captured service agent.','SERVICE_WORKER_GUIDANCE'])assert.ok(body.messages[0].content.includes(marker));assert.deepEqual(body.messages[1],{role:'user',content:'Native child model RPC product_service.'});assert.ok(!('tools' in body)||body.tools.length===0);
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:'Service worker complete.'});chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='product_supervised'||mode==='product_supervised_cancel'){
   assert.equal(body.messages.length,2);assert.equal(body.messages[0].role,'system');assert.ok(body.messages[0].content.includes('Supervisor captured instructions.'));assert.ok(body.messages[0].content.includes('SUPERVISOR_GUIDANCE'));assert.deepEqual(body.messages[1],{role:'user',content:`Native child model RPC ${mode}.`});assert.ok(!('tools' in body)||body.tools.length===0);
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:'Supervisor complete.'});if(mode==='product_supervised_cancel'){response.on('close',()=>aborted.add(mode));return;}chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='product_no_tools'){
   assert.equal(body.messages.length,2);assert.equal(body.messages[0].role,'system');assert.match(body.messages[0].content,/ROOT_CONTEXT_GUIDANCE_\d+/);assert.ok(body.messages[0].content.includes('Native context owner policy.'));assert.deepEqual(body.messages[1],{role:'user',content:'Native child model RPC product_no_tools.'});assert.ok(!('tools' in body)||body.tools.length===0);assert.ok(!('tool_choice' in body));
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:'No tools offered.'});chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='product_read_error'){assert.equal(body.messages.length,2);assert.match(body.messages[0].content,/ROOT_CONTEXT_GUIDANCE_\d+/);response.writeHead(400,{'content-type':'application/json'});response.end(JSON.stringify({error:{code:'invalid_request_error',message:'private provider failure must not cross product worker reply'}}));return;}
  if(mode==='read_context'||mode==='runner_read'||mode==='runner_read_cancel'||mode==='native_runner_read'||mode==='native_runner_read_cancel'||mode==='product_read'||mode==='product_read_cancel'||mode==='product_profile_read'||mode==='product_profile_read_cancel'){
   const phase=counts[mode];assert.equal(body.messages.length,phase===1?2:phase===2?5:7);assert.equal(body.messages[0].role,'system');assert.match(body.messages[0].content,/ROOT_CONTEXT_GUIDANCE_\d+/);assert.ok(body.messages[0].content.includes('context-check'));assert.deepEqual(body.messages[1],{role:'user',content:`Native child model RPC ${mode}.`});assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');
   if(phase===1)assert.ok(!body.messages[0].content.includes('NESTED_CONTEXT_GUIDANCE_'));
   else{assert.match(body.messages[0].content,/NESTED_CONTEXT_GUIDANCE_\d+/);assert.equal(body.messages[2].role,'assistant');assert.deepEqual(body.messages[2].tool_calls.map(call=>call.id),['context-first','context-second']);for(const [index,id]of [[3,'context-first'],[4,'context-second']]){assert.equal(body.messages[index].role,'tool');assert.equal(body.messages[index].tool_call_id,id);assert.equal(JSON.parse(body.messages[index].content).error.code,'repository_instructions_required');}}
   if(phase===3){assert.equal(body.messages[5].role,'assistant');assert.equal(body.messages[5].tool_calls[0].id,'context-third');assert.equal(body.messages[6].role,'tool');assert.equal(body.messages[6].tool_call_id,'context-third');assert.match(JSON.parse(body.messages[6].content).content,/^native-context-bytes-\d+\n$/);}
   response.writeHead(200,{'content-type':'text/event-stream'});
   if(phase<3){const ids=phase===1?['context-first','context-second']:['context-third'];chunk(response,mode,{role:'assistant',tool_calls:ids.map((id,index)=>({index,id,type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'nested/same.txt'})}}))},'tool_calls',usage);}
   else{chunk(response,mode,{role:'assistant',content:'Context verified.'});chunk(response,mode,{},'stop',usage);}response.end('data: [DONE]\n\n');return;
  }
  if(mode==='read'&&counts[mode]===2){
   assert.equal(body.messages.length,3);assert.deepEqual(body.messages[0],{role:'user',content:'Native child model RPC read.'});
   assert.equal(body.messages[1].role,'assistant');assert.deepEqual(body.messages[1].tool_calls,[{id:'actual-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'same.txt'})}}]);
   assert.equal(body.messages[2].role,'tool');assert.equal(body.messages[2].tool_call_id,'actual-read');assert.match(JSON.parse(body.messages[2].content).content,/^actual-native-read-\d+\n$/);
   assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:'Read turn recorded.'});chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  assert.deepEqual(body.messages,[{role:'user',content:`Native child model RPC ${mode}.`}]);
  if(mode.startsWith('read')){
   assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');response.writeHead(200,{'content-type':'text/event-stream'});
   chunk(response,mode,{role:'assistant',tool_calls:[{index:0,id:'actual-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'same.txt'})}}]},'tool_calls',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='overflow'||mode==='other_error'||mode==='runner_error'){
   response.writeHead(400,{'content-type':'application/json'});response.end(JSON.stringify({error:{code:mode==='overflow'?'context_length_exceeded':'rate_limit_exceeded',type:'invalid_request_error',param:'messages',message:'private-provider-message must not cross the worker boundary'}}));return;
  }
  response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:`Actual native RPC ${mode}.`});
  if(mode==='cancel'||mode==='crash'||mode==='runner_cancel'){response.on('close',()=>aborted.add(mode));return;}
  chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');
 }catch(error){failure=error;response.writeHead(500);response.end();}});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try{
 const result=await promisify(execFile)(executable,[modules,stdlib,`http://127.0.0.1:${server.address().port}`],{env:{...process.env,XMIND_WORKER_LSP_NODE:process.execPath,XMIND_WORKER_LSP_PEER:fileURLToPath(new URL("./language_server_diagnostics_peer.mjs",import.meta.url))},windowsHide:true,timeout:60000,maxBuffer:2*1024*1024}).catch(error=>{if(failure)throw failure;throw error;});
 if(failure)throw failure;assert.deepEqual(counts,expected);assert.ok(aborted.has("worker_context_count_cancel")&&aborted.has("worker_context_inference_cancel"));assert.ok(aborted.has('cancel')&&aborted.has('crash')&&aborted.has('runner_cancel')&&aborted.has('product_supervised_cancel')&&aborted.has('product_factory_profile_cancel'));
 process.stdout.write(result.stdout);process.stdout.write('Independent peer observed exactly one hundred seventy-three requests: two single-rebuild context cases with eight inference attempts, ten counts and two compactions, four failed/cancelled token counts and two failed/cancelled inference requests, five counted context inference windows plus six native token counts and one compaction, eleven actual skill worker windows, sixteen actual callable worker windows, six actual worker language windows, three HTTP subscription refresh windows, three changed-catalogue windows, two configured A2A factory windows, two configured HTTP MCP factory windows and two production remote coding windows, two failed join-receipt windows, three asynchronous join windows, five cross-root production service windows, two unified xmind coding command windows, five actual child process RPC windows, eight production coding-worker windows, five actual child file RPC windows, eleven production factory windows and one explicit thread window, one AgentService-owned supervised window, one production-supervised streaming cancellation window, one production-supervised completion window, one actual product no-tools window, five profiled worker windows, one held completed child, six product entry windows, five native worker assembly windows, two read AgentRunner approval-cancellation windows, one actual AgentRunner cancellation and one provider failure, three actual read AgentRunner windows, three direct context windows, read twice for canonical continuation, every other captured mode once; no changed/rejected/revoked request reached HTTP.\n');
}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}

assert.equal(peerCalls,1);assert.equal(readFileSync(join(peerRoot,'actual.txt'),'utf8'),'actual production remote effect');assert.equal(a2aCalls,1);assert.equal(readFileSync(join(peerRoot,'a2a-actual.txt'),'utf8'),'actual production remote effect');assert.equal(refreshCalls,1);assert.equal(refreshSubscriptions,2);assert.equal(refreshClosed,2);assert.equal(readFileSync(join(peerRoot,'refresh-actual.txt'),'utf8'),'actual production remote effect');rmSync(peerRoot,{recursive:true});
