// Synthetic loopback domain replies exercise the actual native CLI adapter.
// No provider inference, compaction, SQLite or installed editor is claimed.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [binary]=process.argv.slice(2);assert.ok(binary,'Pass the compiled native CLI');
const execute=promisify(execFile),token='synthetic-context-cli-auth-'.padEnd(64,'x'),requests=[];
let enabled=true;
const server=createServer(async(request,response)=>{
  const chunks=[];for await(const chunk of request)chunks.push(chunk);
  const body=chunks.length?JSON.parse(Buffer.concat(chunks).toString('utf8')):undefined;
  const url=new URL(request.url,'http://127.0.0.1');requests.push({method:request.method,url,body});
  assert.equal(request.headers.authorization,'Bearer '+token);
  let result;
  if(url.pathname==='/v1/health')result={context_controls:enabled,provider_profile_admission:true};
  else if(url.pathname==='/v1/provider/profiles')result={revision:9,active:'saved',profiles:[{id:'saved'}]};
  else if(url.pathname==='/v1/sessions/session/context')result={session_id:'session',model_id:'model:variant/v1',enabled:true,automatic:true,head_revision:4,source_watermark:7,manual:null,checkpoint:{id:'actual-checkpoint',provider_elapsed_ms:12,preparation_elapsed_ms:14,usage:{input_tokens:40,output_tokens:8}}};
  else if(url.pathname==='/v1/sessions/session/context/requests/request')result={id:'request',state:'completed'};
  else if(url.pathname==='/v1/sessions/session/context/compact'){
    assert.equal(request.method,'POST');assert.deepEqual(body,{expected_head_revision:4,id:'request',model_id:'model:variant/v1',provider_profile_id:'saved',expected_provider_revision:9});result={id:'request',state:'pending'};
  }else if(url.pathname==='/v1/graph-runs/root/resume'){
    assert.equal(request.method,'POST');assert.deepEqual(body,{expected_checkpoint_revision:7});result={id:'root',session_id:'session',state:'paused',parent_id:'',node_id:'',graph_root:true};
  }else {response.writeHead(404);response.end('{}');return;}
  response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify(result));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const port=String(server.address().port);
const run=args=>execute(binary,[port,...args],{windowsHide:true,timeout:5000,env:{...process.env,XMIND_AUTH_TOKEN:token}});
try{
  const observed=JSON.parse((await run(['context','session','model:variant/v1'])).stdout);assert.equal(observed.checkpoint.usage.total_tokens,undefined,'Missing metrics stay missing');
  assert.deepEqual(JSON.parse((await run(['context-request','session','request','model:variant/v1'])).stdout),{id:'request',state:'completed'});
  assert.deepEqual(JSON.parse((await run(['compact-context','session','4','request','model:variant/v1'])).stdout),{id:'request',state:'pending'});
  assert.equal(JSON.parse((await run(['resume-graph','root','7'])).stdout).state,'paused','Queued wake acknowledgement is not completed execution');
  assert.deepEqual(requests.map(value=>[value.method,value.url.pathname]),[['GET','/v1/sessions/session/context'],['GET','/v1/sessions/session/context/requests/request'],['GET','/v1/health'],['GET','/v1/provider/profiles'],['POST','/v1/sessions/session/context/compact'],['POST','/v1/graph-runs/root/resume']]);
  assert.ok(requests.slice(0,2).every(value=>value.url.searchParams.get('model_id')==='model:variant/v1'));
  const before=requests.length;for(const args of [['compact-context','session','-1','request'],['resume-graph','root','0'],['context','session','invalid model'],['compact-context','session','4','../request']])await assert.rejects(run(args));assert.equal(requests.length,before,'Invalid local identities/preconditions dispatch nothing');
  enabled=false;await assert.rejects(run(['compact-context','session','4','request','model:variant/v1']));assert.equal(requests.length,before+1);assert.equal(requests.at(-1).url.pathname,'/v1/health','Unavailable capability cannot dispatch a compaction');
  console.log('Native context/resume CLI adapter contract passed; synthetic loopback records only, no actual compaction.');
}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
