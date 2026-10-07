// Synthetic inference tests the real compiled native model/tool execution.
// Real files, approvals and embedded xlang3 persistence; no live-model claim.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {randomBytes,createHash} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
const [serverExe,cliExe,modules,stdlib]=process.argv.slice(2);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-edit-')),workspace=join(folder,'workspace');
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
let child,port,errors='',peerError,requests=0;
const continuations=new Map();
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});request.on('end',()=>{
    try {
      requests++;const body=JSON.parse(source);
      assert.deepEqual(body.tools.map(tool=>tool.function.name),['read_repository_instructions','read_file','list_files','search_files','edit_file','create_file']);
      const prompt=body.messages.findLast(message=>message.role==='user').content;
      assert.equal(body.model,prompt==='allowed'?'synthetic-edit-alternate':'synthetic-edit-protocol-model');assert.equal(body.stream_options.include_usage,true);
      const tool=body.messages.findLast(message=>message.role==='tool');let delta,finish;
      if(tool) {
        const outcome=JSON.parse(tool.content);continuations.set(prompt,outcome);
        assert.equal(tool.tool_call_id,`fixture-${prompt}`);
        if(prompt==='allowed'||prompt==='create-allowed') assert.ok(outcome.operation_id && outcome.content_sha256);
        else assert.equal(outcome.error.code,prompt==='denied'||prompt==='create-denied'?'permission_denied':'content_conflict');
        delta={content:'Synthetic inference continuation after actual tool outcome'};finish='stop';
      } else {
        const creation=prompt.startsWith('create-');
        delta={tool_calls:[{index:0,id:`fixture-${prompt}`,type:'function',function:{name:creation?'create_file':'edit_file',arguments:JSON.stringify(creation?{path:`${prompt}.txt`,content:`created ${prompt}\n`}:{path:`${prompt}.txt`,old_text:'original',new_text:`changed ${prompt}`})}}]};finish='tool_calls';
      }
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:null}]})}\n\ndata: ${JSON.stringify({choices:[{index:0,delta:{},finish_reason:finish}]})}\n\ndata: ${JSON.stringify({choices:[],usage:{prompt_tokens:12,completion_tokens:6,total_tokens:18}})}\n\ndata: [DONE]\n\n`);
    } catch(error) {peerError=error;response.writeHead(500);response.end('synthetic fixture failed');}
  });
});
async function api(path,body) {
  const response=await fetch(`http://127.0.0.1:${port}${path}`,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(5000)});
  assert.ok(response.ok,await response.clone().text());return response.json();
}
function cli(...args) {const result=spawnSync(cliExe,[String(port),...args],{env,encoding:'utf8',windowsHide:true,timeout:5000});assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
async function until(read,predicate) {const deadline=Date.now()+5000;while(Date.now()<deadline){const value=await read();if(predicate(value)) return value;if(peerError) throw peerError;await delay(10);}throw new Error(`State deadline: ${errors}`);}
async function start() {
  child=spawn(serverExe,['--db',join(folder,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0','--model','synthetic-edit-protocol-model','--models','synthetic-edit-alternate','--model-stream-usage','supported','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,'--model-tools','supported','--workspace',workspace,'--workspace-edits','approved'],{env,windowsHide:true});
  child.stderr.on('data',data=>{errors+=data;});
  port=await new Promise((resolve,reject)=>{let output='';const timer=setTimeout(()=>reject(new Error(`Readiness deadline: ${errors}`)),10000);child.on('error',error=>{clearTimeout(timer);reject(error);});child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Server exited ${code}: ${errors}`));});child.stdout.on('data',data=>{output+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);resolve(Number(match[1]));}});});
}
async function stop() {
  if(child && child.exitCode===null) {const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;}
}
try {
  await mkdir(workspace);await Promise.all(['allowed','denied','stale','cancelled','interrupted'].map(name=>writeFile(join(workspace,`${name}.txt`),'original\n')));
  await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));await start();
  assert.deepEqual(cli('models'),{default_model:'synthetic-edit-protocol-model',models:[{id:'synthetic-edit-protocol-model'},{id:'synthetic-edit-alternate'}]});
  for(const name of ['allowed','denied','stale','cancelled']) {
    await api('/v1/sessions',{id:name,title:'Synthetic edit protocol fixture'});
    if(name==='allowed') {
      const invalid=await fetch(`http://127.0.0.1:${port}/v1/runs`,{method:'POST',headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:JSON.stringify({id:'invalid-model',session_id:name,prompt:name,model_id:'not-configured'}),signal:AbortSignal.timeout(5000)});
      assert.equal(invalid.status,400);assert.deepEqual(cli('history',name),[]);
    }
    const run=name==='allowed'?cli('run',name,name,'synthetic-edit-alternate'):await api('/v1/runs',{id:`run-${name}`,session_id:name,prompt:name});assert.equal(run.state,'queued');
    const [proposal]=await until(()=>api(`/v1/runs/${run.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'));
    assert.equal(JSON.parse(proposal.arguments_json).before_content,'original\n');
    assert.equal(await readFile(join(workspace,`${name}.txt`),'utf8'),'original\n');
    if(name==='stale') await writeFile(join(workspace,'stale.txt'),'outside change\n');
    if(name==='cancelled') cli('cancel',run.id);else cli('decide',proposal.id,name==='denied'?'deny':'allow');
    await until(()=>api(`/v1/runs/${run.id}`),value=>value.state===(name==='cancelled'?'cancelled':'completed'));
    const outcome=await api(`/v1/operations/${proposal.id}`);
    assert.equal(outcome.state,{allowed:'succeeded',denied:'denied',stale:'failed',cancelled:'cancelled'}[name]);
    const inspection=await fetch(`http://127.0.0.1:${port}/v1/operations/${proposal.id}/inspection`,{headers:{Authorization:`Bearer ${token}`},signal:AbortSignal.timeout(5000)});
    assert.equal(inspection.status,409,'Product server exposes inspection but rejects edits that are not uncertain');
    const actual=await readFile(join(workspace,`${name}.txt`),'utf8');assert.equal(actual,name==='allowed'?'changed allowed\n':name==='stale'?'outside change\n':'original\n');
    if(name==='allowed') assert.equal(continuations.get(name).content_sha256,createHash('sha256').update(actual).digest('hex'));
    const history=cli('history',name);assert.equal(history.length,name==='cancelled'?1:4);
    if(name!=='cancelled') assert.equal(history[2].role,'tool');
    if(name!=='cancelled') {
      const response=history[3].data;
      assert.equal(response.model,name==='allowed'?'synthetic-edit-alternate':'synthetic-edit-protocol-model');
      assert.deepEqual(response.usage,{prompt_tokens:12,completion_tokens:6,total_tokens:18});
      assert.ok(Number.isSafeInteger(response.elapsed_ms)&&response.elapsed_ms>=0);
      assert.ok(Number.isSafeInteger(response.first_token_ms)&&response.first_token_ms>=0&&response.first_token_ms<=response.elapsed_ms,'First output must be measured within the actual model request interval');
    }
  }
  for(const name of ['create-allowed','create-denied','create-stale','create-cancelled']){
    await api('/v1/sessions',{id:name,title:'Actual native creation with synthetic inference'});const run=cli('run',name,name);
    const [proposal]=await until(()=>api(`/v1/runs/${run.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'));
    assert.equal(proposal.tool,'create_file');const plan=JSON.parse(proposal.arguments_json);assert.equal(plan.before_exists,false);assert.equal(plan.after_content,`created ${name}\n`);assert.ok(plan.parent_id);
    await assert.rejects(readFile(join(workspace,`${name}.txt`)),{code:'ENOENT'});
    if(name==='create-stale')await writeFile(join(workspace,`${name}.txt`),'outside creation\n');
    if(name==='create-cancelled')cli('cancel',run.id);else cli('decide',proposal.id,name==='create-denied'?'deny':'allow');
    await until(()=>api(`/v1/runs/${run.id}`),value=>value.state===(name==='create-cancelled'?'cancelled':'completed'));
    assert.equal((await api(`/v1/operations/${proposal.id}`)).state,{'create-allowed':'succeeded','create-denied':'denied','create-stale':'failed','create-cancelled':'cancelled'}[name]);
    if(name==='create-allowed'||name==='create-stale'){const actual=await readFile(join(workspace,`${name}.txt`),'utf8');assert.equal(actual,name==='create-allowed'?`created ${name}\n`:'outside creation\n');if(name==='create-allowed')assert.equal(continuations.get(name).content_sha256,createHash('sha256').update(actual).digest('hex'));}
    else await assert.rejects(readFile(join(workspace,`${name}.txt`)),{code:'ENOENT'});
    const history=cli('history',name);assert.equal(history.length,name==='create-cancelled'?1:4);
  }
  await api('/v1/sessions',{id:'create-interrupted',title:'Restart pending creation'});cli('run','create-interrupted','create-interrupted');
  const creationRun=(await api('/v1/sessions/create-interrupted/runs'))[0];
  const [creationInterrupted]=await until(()=>api(`/v1/runs/${creationRun.id}/operations`),items=>items.some(item=>item.state==='awaiting_approval'));
  await api('/v1/sessions',{id:'interrupted',title:'Restart while awaiting approval'});
  await api('/v1/runs',{id:'run-interrupted',session_id:'interrupted',prompt:'interrupted'});
  const [interrupted]=await until(()=>api('/v1/runs/run-interrupted/operations'),items=>items.some(item=>item.state==='awaiting_approval'));
  const savedResponseHistory=cli('history','allowed');
  await stop();await start();
  assert.deepEqual(cli('history','allowed'),savedResponseHistory,'Selected model, provider usage and measured timing metadata must survive restart unchanged');
  assert.equal((await api(`/v1/runs/${creationRun.id}`)).state,'failed');assert.equal((await api(`/v1/operations/${creationInterrupted.id}`)).state,'cancelled');
  await assert.rejects(readFile(join(workspace,'create-interrupted.txt')),{code:'ENOENT'});assert.equal(await readFile(join(workspace,'create-allowed.txt'),'utf8'),'created create-allowed\n');
  assert.equal((await api('/v1/runs/run-interrupted')).state,'failed');
  assert.equal((await api(`/v1/operations/${interrupted.id}`)).state,'cancelled');
  assert.equal(await readFile(join(workspace,'interrupted.txt'),'utf8'),'original\n');
  assert.equal((await api(`/v1/operations/${continuations.get('allowed').operation_id}`)).state,'succeeded');
  assert.equal(await readFile(join(workspace,'allowed.txt'),'utf8'),'changed allowed\n');
  const retired=await fetch(`http://127.0.0.1:${port}/v1/operations/${interrupted.id}/decision`,{method:'POST',headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:JSON.stringify({decision:'allow'}),signal:AbortSignal.timeout(5000)});
  assert.equal(retired.status,409,'Restart must not grant an unused approval to a retired owner');
  assert.equal(requests,16);if(peerError) throw peerError;
  console.log('Compiled native agent edit loop passed: real approved edits, denial, stale content, cancellation, actual tool-result continuation and pending-approval restart recovery. Inference is synthetic.');
} finally {
  await stop();
  peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));
  assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(folder.includes('xmind-agent-edit-'));await rm(folder,{recursive:true,force:true});
}
