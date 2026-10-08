// Independent synthetic catalogue + DeepSeek SSE. The native profile runtime,
// authentication, encrypted credential and xlang3 SQLite history are real.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
const [executable,modules,stdlib]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib,'Pass compiled profile fixture and embedded-xlang3 roots');
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-deepseek-profile-')),workspace=join(root,'workspace');
const key='synthetic-deepseek-profile-key-not-live';
const rejectedKey='synthetic-deepseek-rejected-profile-key-not-live';
const reasoning='Synthetic enrolled DeepSeek reasoning, retained exactly.';
const answer='Synthetic DeepSeek enrolled native inference completed.';
const prompt='Inspect the enrolled native DeepSeek profile.';
const usage={prompt_tokens:17,completion_tokens:9,total_tokens:26,prompt_cache_hit_tokens:7,prompt_cache_miss_tokens:10,prompt_tokens_details:{cached_tokens:7},completion_tokens_details:{reasoning_tokens:4}};
const routes=[];let failure,passed=false,nativeFailure;
const peer=createServer((request,response)=>{
  const chunks=[];let size=0;request.on('error',()=>{});response.on('error',()=>{});
  request.on('data',chunk=>{size+=chunk.length;if(size>512*1024){request.destroy();return;}chunks.push(chunk);});
  request.on('end',()=>{try{
    const source=Buffer.concat(chunks).toString('utf8');routes.push({method:request.method,path:request.url});
    assert.equal(request.headers.authorization,'Bearer '+key);assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['anthropic-version'],undefined);assert.equal(request.headers['x-goog-api-key'],undefined);
    assert.ok(!source.includes(key)&&!source.includes(rejectedKey),'Secrets belong only in the configured transport credential');
    if(request.method==='GET'){
      assert.equal(request.url,'/models');assert.equal(source,'');assert.equal(request.headers.accept,'application/json');
      response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({object:'list',data:[{object:'model',id:'deepseek-v4-pro'},{object:'model',id:'deepseek-future-unknown'},{object:'model',id:'deepseek-flash'}]}));return;
    }
    assert.equal(request.method,'POST');assert.equal(request.url,'/chat/completions');assert.equal(request.headers.accept,'text/event-stream');assert.equal(request.headers['content-type'],'application/json');
    const body=JSON.parse(source);assert.equal(body.model,'deepseek-flash');assert.equal(body.stream,true);assert.equal(body.max_tokens,64);assert.deepEqual(body.stream_options,{include_usage:true});
    for(const field of ['n','max_completion_tokens','thinking','reasoning_effort'])assert.equal(Object.hasOwn(body,field),false);
    assert.ok(!source.includes('provider_items')&&!source.includes('provider_context')&&!source.includes('credential_id'));
    assert.equal(body.messages.length,2);assert.equal(body.messages[0].role,'system');assert.ok(body.messages[0].content.startsWith('Synthetic DeepSeek enrollment fixture:'));assert.deepEqual(body.messages[1],{role:'user',content:prompt});
    assert.deepEqual(body.tools.map(tool=>tool.function.name).sort(),['read_repository_instructions','read_file','list_files','search_files','plan_tasks','revise_plan','inspect_plan'].sort(),'The actual enrolled ordinary Agent must retain its backend tool/planning capability');
    response.writeHead(200,{'Content-Type':'text/event-stream'});
    const chunk=(delta,finish=null,metrics=null)=>'data: '+JSON.stringify({id:'synthetic-enrolled-response',object:'chat.completion.chunk',model:'deepseek-flash',choices:[{index:0,delta,finish_reason:finish}],usage:metrics})+'\n\n';
    const wire=Buffer.from(chunk({role:'assistant',reasoning_content:reasoning.slice(0,17)})+chunk({reasoning_content:reasoning.slice(17)})+chunk({content:answer})+chunk({},'stop',usage)+'data: [DONE]\n\n');
    for(let offset=0;offset<wire.length;offset+=13)response.write(wire.subarray(offset,offset+13));response.end();
  }catch(error){failure??=error;if(!response.headersSent)response.writeHead(400);response.end();}});
});
try{
  await mkdir(workspace);await new Promise(resolveReady=>peer.listen(0,'127.0.0.1',resolveReady));
  const env={};for(const name of ['SystemRoot','WINDIR','PATH','TEMP','TMP','ComSpec','PATHEXT','USERPROFILE','LOCALAPPDATA'])if(process.env[name])env[name]=process.env[name];
  let result;try{result=await execute(executable,[root,modules,stdlib,workspace,`http://127.0.0.1:${peer.address().port}`],{windowsHide:true,timeout:45000,maxBuffer:1024*1024,env});}catch(error){nativeFailure=error;throw error;}
  if(failure)throw failure;
  assert.deepEqual(routes,[{method:'GET',path:'/models'},{method:'GET',path:'/models'},{method:'POST',path:'/chat/completions'},{method:'GET',path:'/models'}],'Exact actual socket oracle excludes rejected policy/selection/admission dispatch, retry or reopen replay');
  for(const entry of await readdir(root,{withFileTypes:true}))if(entry.isFile()&&/\.sqlite(?:-wal|-shm)?$/.test(entry.name)){
    const bytes=await readFile(join(root,entry.name));for(const secret of [key,rejectedKey])assert.equal(bytes.includes(Buffer.from(secret)),false,'Actual SQLite bytes must not contain plaintext synthetic provider keys');
  }
  passed=true;process.stdout.write(result.stdout);
}catch(error){
  await writeFile(join(root,'private-peer-error.log'),String(failure?.stack??error.stack??'contract failed').slice(0,65536),{flag:'wx',mode:0o600});
  if(nativeFailure)await writeFile(join(root,'private-native-failure.json'),JSON.stringify({stdout:nativeFailure.stdout??'',stderr:nativeFailure.stderr??'',code:nativeFailure.code??null,signal:nativeFailure.signal??null}),{flag:'wx',mode:0o600});
  process.stderr.write('Native DeepSeek profile fixture failed; owned diagnostics retained.\n');process.exitCode=1;
}finally{
  peer.closeAllConnections();if(peer.listening)await new Promise(resolveClosed=>peer.close(resolveClosed));
  if(passed){const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('xmind-deepseek-profile-'));await rm(target,{recursive:true,force:true});}
}
