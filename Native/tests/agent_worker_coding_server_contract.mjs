// Actual process-code server, authenticated client and one approved native edit.
import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,stat} from 'node:fs/promises';
import {join} from 'node:path';import {tmpdir} from 'node:os';import {randomBytes} from 'node:crypto';
import {createServer} from 'node:http';import {setTimeout as delay} from 'node:timers/promises';import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),{BackendClient}=require('../../extensions/vscode/client.js');
const [executable,modules,stdlib]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-coding-server-')),workspace=join(root,'workspace');
const auth=randomBytes(32).toString('hex'),key='synthetic-coding-server-not-live',marker='server-code-'+randomBytes(16).toString('hex')+'\n';
await mkdir(workspace);await writeFile(join(workspace,'input.txt'),marker);await writeFile(join(workspace,'AGENTS.md'),'CODING_SERVER_GUIDANCE\n');
let requests=0,failure,child,stdout='',stderr='';
const provider=createServer((request,response)=>{let input='';request.on('data',b=>input+=b);request.on('end',()=>{try{
 assert.equal(request.headers.authorization,'Bearer '+key);assert(!input.includes(key));const body=JSON.parse(input);assert.equal(body.model,'coding-server-fixture');assert.equal(body.stream,true);assert.match(body.messages[0].content,/CODING_SERVER_GUIDANCE/);assert.ok(++requests<=2);
 assert.deepEqual(body.tools.map(t=>t.function.name).sort(),['delegate_tasks','read_file','list_files','search_files','find_files','glob_files','edit_file','create_file','apply_patch','restore_operation','run_process','wait_process','stop_process'].sort());
 if(requests===2){assert.equal(body.messages.length,6);assert.equal(body.messages[3].tool_call_id,'server-read');assert.equal(JSON.parse(body.messages[3].content).content,marker);assert.equal(body.messages[4].tool_call_id,'server-create');assert.ok(JSON.parse(body.messages[4].content).operation_id);assert.equal(body.messages[5].tool_call_id,'server-process');assert.equal(JSON.parse(body.messages[5].content).exit_code,0);}
 const delta=requests===1?{role:'assistant',tool_calls:[{index:0,id:'server-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'input.txt'})}},{index:1,id:'server-create',type:'function',function:{name:'create_file',arguments:JSON.stringify({path:'created.txt',content:marker})}},{index:2,id:'server-process',type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'xlang3',arguments:['-B','-c',"from pathlib import Path; Path('command.txt').write_bytes(Path('created.txt').read_bytes())"],workdir:'.',timeout_ms:10000})}}]}:{role:'assistant',content:'Server coding complete.'};
 response.writeHead(200,{'content-type':'text/event-stream'});response.end('data: '+JSON.stringify({id:'coding-server-response',model:body.model,choices:[{index:0,delta,finish_reason:requests===1?'tool_calls':'stop'}],usage:{prompt_tokens:7,completion_tokens:3,total_tokens:10}})+'\n\ndata: [DONE]\n\n');
 }catch(error){failure=error;response.writeHead(500);response.end();}});});
await new Promise(resolve=>provider.listen(0,'127.0.0.1',resolve));
const processConfig=join(root,'processes.yaml');await writeFile(processConfig,'profiles:\n  - id: xlang3\n    executable: '+JSON.stringify(join(modules,'..','xlang3.exe'))+'\n    prefix_arguments: []\n    max_timeout_ms: 10000\n');
const env={...process.env,XMIND_AUTH_TOKEN:auth,XMIND_API_KEY:key},base=['serve','--modules',modules,'--stdlib',stdlib,'--port','0'];
try{
 for(const extra of [[],['--workspace',workspace],['--workspace',workspace,'--workspace-edits','unsupported']]){
  const invalid=join(root,'invalid.sqlite');const result=await promisify(execFile)(executable,[...base,'--db',invalid,'--agent-execution','process-code',...extra],{env,windowsHide:true,timeout:10000}).then(()=>null,error=>error);assert(result&&result.code!==0);await assert.rejects(stat(invalid),{code:'ENOENT'});
 }
 child=spawn(executable,[...base,'--db',join(root,'state.sqlite'),'--workspace',workspace,'--workspace-edits','approved','--agent-execution','process-code','--process-config',processConfig,'--model','coding-server-fixture','--model-endpoint',`http://127.0.0.1:${provider.address().port}`,'--model-tools','supported','--model-stream-usage','supported','--workers','1'],{env,windowsHide:true});
 child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);let origin;const start=Date.now();
 while(Date.now()-start<15000&&!origin){assert.equal(child.exitCode,null,stderr);origin=/listening on (http:\/\/127\.0\.0\.1:\d+)/.exec(stdout)?.[1];if(!origin)await delay(20);}assert(origin,stderr);
 const client=new BackendClient(origin,()=>auth,(url,options)=>fetch(url,{...options,signal:AbortSignal.timeout(5000)}));const health=await client.health();assert.equal(health.file_edit_proposals,true);assert.equal(health.agent_delegation,true);
 const session=await client.createSession('Actual coding server entry'),run=await client.run(session.id,'Read the input and create the reviewed result.','coding-server-fixture');let actual,approved=new Set();const deadline=Date.now()+15000;
 while(Date.now()<deadline){actual=await client.status(run.id);if(!['queued','running'].includes(actual.state))break;if(approved.size<2){const pending=await client.sessionPermissions(session.id);if(pending.requests.length){assert.equal(pending.requests.length,1);await client.replySessionPermission(session.id,pending.requests[0].id,'allow');assert(!approved.has(pending.requests[0].id));approved.add(pending.requests[0].id);}}await delay(20);}
 if(failure)throw failure;assert.equal(actual.state,'completed',JSON.stringify(actual));assert.equal(approved.size,2);assert.equal(requests,2);assert.equal(await readFile(join(workspace,'created.txt'),'utf8'),marker);assert.equal(await readFile(join(workspace,'command.txt'),'utf8'),marker);
 const events=await client.events(run.id,0),worker=events.find(e=>e.kind==='worker.lease.admitted');assert(worker);assert.notEqual(worker.data.pid,child.pid);assert.equal(events.filter(e=>e.kind==='worker.file.effect_claimed').length,1);assert.equal(events.filter(e=>e.kind==='conversation.tool_turn').length,1);assert.equal((await client.operations(run.id)).length,2);
 const history=await client.history(session.id);assert.equal(history.length,6);assert.equal(history[2].data.tool_call_id,'server-read');assert.equal(history[3].data.tool_call_id,'server-create');assert.equal(history[4].data.tool_call_id,'server-process');assert.equal(history[5].data.content,'Server coding complete.');
 console.log('Unified process-code server verified actual child AgentRunner, authenticated public approval, one native creation, mixed canonical history and two controlled model windows.');
}finally{
 if(child&&child.exitCode===null){child.kill();await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Owned coding server exit unconfirmed')),5000);child.once('exit',()=>{clearTimeout(timer);resolve();});});}
 provider.closeAllConnections();await new Promise(resolve=>provider.close(resolve));
}
