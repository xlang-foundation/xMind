// Synthetic inference peer for protocol verification only. Product binaries
// execute the actual native loop, filesystem operations and xlang3 persistence.
// This test is not evidence of live inference or coding-task completion.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {createRequire} from 'node:module';
const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');

const [serverExe,cliExe,modules,stdlib]=process.argv.slice(2);
assert.ok(serverExe && cliExe && modules && stdlib,'Pass server, CLI and runtime roots');
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-http-'));
const workspace=join(folder,'workspace'),token=randomBytes(32).toString('hex');
const key='synthetic-native-http-protocol-credential';
const baseEnv={...process.env,XMIND_AUTH_TOKEN:token};
delete baseEnv.XMIND_API_KEY;
let child,port,peerError,requests=0;
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});
  request.on('end',()=>{
    try {
      ++requests;
      assert.equal(request.headers.authorization,`Bearer ${key}`);
      assert.ok(!source.includes(key));
      const body=JSON.parse(source);
      assert.equal(body.model,'synthetic-protocol-model');assert.equal(body.stream,true);
      assert.deepEqual(body.tools.map(tool=>tool.function.name),['read_repository_instructions','read_file','list_files','search_files']);
      const prompt=body.messages.findLast(message=>message.role==='user').content;
      if(prompt==='provider-error') {
        response.writeHead(429,{'Content-Type':'application/json'});
        response.end('{"error":"private-provider-error-body"}');return;
      }
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      const send=value=>response.write(`data: ${JSON.stringify(value)}\n\n`);
      if(prompt==='hold-stream') {
        send({choices:[{index:0,delta:{content:'Synthetic pending inference stream'}}]});return;
      }
      const last=body.messages.at(-1);
      if(last.role==='tool') {
        assert.equal(JSON.parse(last.content).content,'Actual workspace content\n');
        send({choices:[{index:0,delta:{content:'Synthetic peer checked the actual workspace read.'},finish_reason:'stop'}]});
      } else {
        send({choices:[{index:0,delta:{tool_calls:[{index:0,id:`http-call-${requests}`,type:'function',function:{name:'read_file',arguments:'{"path":"README.md"}'}}]},finish_reason:'tool_calls'}]});
      }
      response.end('data: [DONE]\n\n');
    } catch(error) {peerError=error;response.destroy();}
  });
});
async function start(withKey=false) {
  child=spawn(serverExe,['--db',join(folder,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,
    '--port','0','--model','synthetic-protocol-model','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,
    '--model-tools','supported','--workspace',workspace,'--credential-id','integration-provider','--workers','1','--queue-limit','1'],
    {env:withKey?{...baseEnv,XMIND_API_KEY:key}:baseEnv,windowsHide:true});
  let stdout='',stderr='';child.stderr.on('data',data=>{stderr+=data;});
  port=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error(`Server readiness timeout: ${stderr}`)),10000);
    child.once('error',error=>{clearTimeout(timer);reject(error);});
    child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Server exited ${code}: ${stderr}`));});
    child.stdout.on('data',data=>{
      stdout+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(stdout);
      if(match) {clearTimeout(timer);resolve(Number(match[1]));}
    });
  });
}
async function stop() {
  if(!child || child.exitCode!==null) return;
  const exiting=new Promise(resolve=>child.once('exit',resolve));
  // Windows kill is abrupt: recovery assertions below test that actual failure.
  child.kill();await exiting;child=null;
}
async function api(path,body) {
  const response=await fetch(`http://127.0.0.1:${port}${path}`,{
    method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},
    body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000),redirect:'error'});
  return {status:response.status,data:await response.json()};
}
function cli(...args) {
  const result=spawnSync(cliExe,[String(port),...args],{env:baseEnv,encoding:'utf8',windowsHide:true,timeout:15000});
  assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);
}
async function until(read,predicate,label) {
  const deadline=Date.now()+10000;
  while(Date.now()<deadline) {
    if(peerError) throw peerError;
    const value=await read();if(predicate(value)) return value;await delay(25);
  }
  throw new Error(`Timed out waiting for ${label}`);
}
const terminal=(id,state)=>until(async()=> (await api(`/v1/runs/${id}`)).data,value=>value.state===state,`${id} ${state}`);
const streaming=id=>until(async()=> (await api(`/v1/runs/${id}/events`)).data,events=>events.some(event=>event.kind==='model.text'),`${id} stream`);
async function session(id) {assert.equal((await api('/v1/sessions',{id,title:id})).status,201);}
async function chat(input,...args){
 const process=spawn(cliExe,[String(port),'chat',...args],{env:baseEnv,windowsHide:true});let output='',errors='';process.stdout.on('data',bytes=>output+=bytes);process.stderr.on('data',bytes=>errors+=bytes);const timer=setTimeout(()=>process.kill(),12000);
 const result=await new Promise((yes,no)=>{process.once('error',no);process.once('close',(code,signal)=>{clearTimeout(timer);yes({code,signal});});process.stdin.end(input);});assert.equal(result.signal,null,'Interactive CLI must finish without being killed');assert.equal(result.code,0,errors);assert.ok(!output.includes(key));return output.trim()?output.trim().split('\n').map(line=>JSON.parse(line)):[];
}
try {
  await mkdir(workspace);await writeFile(join(workspace,'README.md'),'Actual workspace content\n');
  await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  await start(true);
  assert.equal(cli('health').agent_execution,true);
  await session('coding');
  const run=cli('run','coding','Read the actual README.md');
  assert.equal(run.state,'queued');await terminal(run.id,'completed');
  const history=cli('history','coding');
  assert.deepEqual(history.map(message=>message.role),['user','assistant','tool','assistant']);
  assert.equal(JSON.parse(history[2].data.content).content,'Actual workspace content\n');
  const events=cli('events',run.id);
  assert.ok(events.some(event=>event.kind==='tool.completed'));
  assert.ok(events.some(event=>event.kind==='run.completed'));
  const last=events.at(-1).seq;
  assert.deepEqual(cli('events',run.id,String(last)),[]);
  await until(()=>api(`/v1/runs/${run.id}/cancel`,{}),result=>result.status===409,'completed run release');
  assert.equal((await api(`/v1/runs/${run.id}/transition`,{expected:'completed',next:'running'})).status,404);
  const beforeChat=cli('sessions').length;assert.deepEqual(await chat('/exit\n'),[]);assert.equal(cli('sessions').length,beforeChat,'Leaving an empty chat must not create a session');
  const chatRecords=await chat('Read README from the CLI\nRead README again in the same conversation\n/exit\n'),chatSession=chatRecords.find(record=>record.type==='session').session_id;
  assert.equal(chatRecords.filter(record=>record.type==='session').length,1);const chatRuns=chatRecords.filter(record=>record.type==='run').map(record=>record.run);assert.equal(chatRuns.length,2);assert.ok(chatRuns.every(item=>item.session_id===chatSession));assert.equal(chatRecords.filter(record=>record.type==='turn_finished'&&record.exit_status===0).length,2);assert.equal(cli('history',chatSession).length,8);assert.ok(chatRecords.some(record=>record.kind==='tool.completed'));assert.ok(chatRecords.some(record=>record.kind==='run.completed'));
  const resumedChat=await chat('Read README after reconnect\n/exit\n',chatSession);assert.equal(resumedChat.find(record=>record.type==='session').session_id,chatSession);assert.equal(cli('history',chatSession).length,12);assert.equal(cli('runs',chatSession).length,3,'Reconnect must add only the requested new turn, not replay completed work');

  for(const id of ['held','queued','overflow']) await session(id);
  assert.equal((await api('/v1/runs',{id:'held-run',session_id:'held',prompt:'hold-stream'})).status,202);
  await streaming('held-run');
  assert.equal((await api('/v1/runs',{id:'duplicate-session',session_id:'held',prompt:'must not persist'})).status,409);
  assert.equal((await api('/v1/sessions/held/messages',{role:'user',data:{content:'must not interleave'}})).status,409);
  assert.equal(cli('history','held').length,1);
  assert.equal((await api('/v1/runs',{id:'queued-run',session_id:'queued',prompt:'must not execute'})).status,202);
  assert.equal((await api('/v1/runs',{id:'overflow-run',session_id:'overflow',prompt:'must not persist'})).status,503);
  assert.equal((await api('/v1/runs/overflow-run')).status,404);
  assert.deepEqual(cli('history','overflow'),[]);
  cli('cancel','queued-run');await terminal('queued-run','cancelled');
  // Queued cancellation frees bounded admission capacity before the worker exits.
  assert.equal((await api('/v1/runs',{id:'replacement-run',session_id:'overflow',prompt:'Read README again'})).status,202);
  cli('cancel','held-run');await terminal('held-run','cancelled');
  await terminal('replacement-run','completed');

  await session('error');
  assert.equal((await api('/v1/runs',{id:'error-run',session_id:'error',prompt:'provider-error'})).status,202);
  await terminal('error-run','failed');
  const failed=cli('events','error-run');
  assert.ok(failed.some(event=>event.kind==='run.failed' && event.data.status===429));
  assert.ok(!JSON.stringify(failed).includes('private-provider-error-body'));
  assert.equal(cli('history','error').length,1);

  await stop();await start(); // Reuse encrypted stored credential without env key.
  assert.deepEqual(cli('history','coding'),history);
  assert.equal(cli('history',chatSession).length,12,'Interactive CLI history must survive native restart');
  // Use the actual editor host client against the same native server; no IDE
  // rendering or VS Code SecretStorage behavior is claimed by this wire test.
  const editorClient=new BackendClient(`http://127.0.0.1:${port}`,()=>token);
  assert.equal((await editorClient.health()).agent_execution,true);
  const reused=await editorClient.run('coding','Read the persisted workspace again');
  await terminal(reused.id,'completed');
  assert.equal((await editorClient.status(reused.id)).state,'completed');
  assert.equal((await editorClient.history('coding')).length,8);
  await session('crash');
  const crashed=cli('run','crash','hold-stream');await streaming(crashed.id);
  await stop();await start();
  assert.equal(cli('status',crashed.id).state,'failed');
  assert.equal(cli('history','crash').length,1);
  assert.equal(cli('health').status,'ok');
  if(peerError) throw peerError;
  console.log('Native agent HTTP/CLI contract passed: actual read/tool loop, interactive shared-session turns/reconnect/restart without replay, bounded admission, cancellation, encrypted credential reuse, provider failure and crash recovery. Inference peer is synthetic.');
} finally {
  await stop();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));
  await rm(folder,{recursive:true,force:true});
}
