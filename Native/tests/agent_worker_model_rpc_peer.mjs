// Independent controlled native provider; ninety intentional requests, no live key.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [executable,modules,stdlib]=process.argv.slice(2);
const expected={coding_async_unrecorded:2,coding_async:3,coding_service:5,coding_command:2,process_create:1,process_denied:1,process_cancel:1,process_delivery:1,process_unrecorded:1,coding_create:2,coding_denied:2,coding_cancel:1,coding_unrecorded:1,coding_attached:2,file_create:1,file_denied:1,file_cancel:1,file_delivery:1,file_unrecorded:1,product_factory_thread:1,product_factory_profile:4,product_factory_attached:2,product_factory_read:2,product_factory_denied:2,product_factory:1,product_service:1,product_supervised_cancel:1,product_supervised:1,product_no_tools:1,normal:1,cancel:1,crash:1,delivery:1,overflow:1,other_error:1,completion:1,completion_delivery:1,completion_rollback:1,completion_hold:1,runner:1,runner_error:1,runner_cancel:1,read:2,read_delivery:1,read_denied:1,read_cancel:1,read_turn_delivery:1,read_turn_rollback:1,read_context:3,runner_read:3,runner_read_cancel:2,native_runner_read:3,native_runner_read_cancel:2,product_read:3,product_read_cancel:2,product_read_error:1,product_profile_read:3,product_profile_read_cancel:2};
const counts=Object.fromEntries(Object.keys(expected).map(mode=>[mode,0]));
let failure;const aborted=new Set();
function chunk(response,mode,delta,finish=null,usage){response.write('data: '+JSON.stringify({id:`rpc-${mode}`,object:'chat.completion.chunk',model:'worker-rpc-fixture',choices:[{index:0,delta,finish_reason:finish}],...(usage?{usage}:{})})+'\n\n');}
const usage={prompt_tokens:7,completion_tokens:3,total_tokens:10};
const server=createServer((request,response)=>{
 let bytes='';request.on('data',b=>bytes+=b);request.on('end',()=>{try{
  const mode=request.url.slice(1);assert.ok(mode in counts);assert.ok(++counts[mode]<=expected[mode]);assert.equal(request.headers.authorization,'Bearer synthetic-model-rpc-not-live');assert.ok(!bytes.includes('synthetic-model-rpc-not-live'));
  const body=JSON.parse(bytes);assert.equal(body.model,'worker-rpc-fixture');assert.equal(body.stream,true);assert.equal(body.stream_options.include_usage,true);
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
 const result=await promisify(execFile)(executable,[modules,stdlib,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:60000,maxBuffer:2*1024*1024}).catch(error=>{if(failure)throw failure;throw error;});
 if(failure)throw failure;assert.deepEqual(counts,expected);assert.ok(aborted.has('cancel')&&aborted.has('crash')&&aborted.has('runner_cancel')&&aborted.has('product_supervised_cancel')&&aborted.has('product_factory_profile_cancel'));
 process.stdout.write(result.stdout);process.stdout.write('Independent peer observed exactly ninety requests: two failed join-receipt windows, three asynchronous join windows, five cross-root production service windows, two unified xmind coding command windows, five actual child process RPC windows, eight production coding-worker windows, five actual child file RPC windows, eleven production factory windows and one explicit thread window, one AgentService-owned supervised window, one production-supervised streaming cancellation window, one production-supervised completion window, one actual product no-tools window, five profiled worker windows, one held completed child, six product entry windows, five native worker assembly windows, two read AgentRunner approval-cancellation windows, one actual AgentRunner cancellation and one provider failure, three actual read AgentRunner windows, three direct context windows, read twice for canonical continuation, every other captured mode once; no changed/rejected/revoked request reached HTTP.\n');
}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
