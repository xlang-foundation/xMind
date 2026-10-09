// Independent synthetic provider oracle; native runtime/files/SQLite are real.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
const [binary,modules,stdlib]=process.argv.slice(2);assert.ok(binary&&modules&&stdlib);
const root=await mkdtemp(join(tmpdir(),'xmind-native-model-policy-')),workspace=join(root,'workspace');
await mkdir(workspace);await writeFile(join(workspace,'marker.txt'),'model-policy-file-marker\n',{flag:'wx'});
const key='synthetic-native-model-policy-key',rejectedKey='synthetic-rejected-model-policy-key';
const calls=[];let failure,passed=false,nativeFailure,turn=0;
const peer=createServer((req,res)=>{
  const chunks=[];let size=0;req.on('error',()=>{});res.on('error',()=>{});
  req.on('data',chunk=>{size+=chunk.length;if(size>512*1024){req.destroy();return;}chunks.push(chunk);});
  req.on('end',()=>{try{
    const source=Buffer.concat(chunks).toString('utf8');calls.push({method:req.method,path:req.url});
    assert.equal(req.headers.authorization,'Bearer '+key);assert.ok(!source.includes(key)&&!source.includes(rejectedKey));
    if(req.method==='GET'){
      assert.equal(req.url,'/models');assert.equal(source,'');
      res.writeHead(200,{'Content-Type':'application/json'});
      res.end(JSON.stringify({object:'list',data:['gpt-image-1','gpt-6.1-sol','gpt-6-sol','gpt-6-astra','gpt-6.2-future','gpt-4.1','text-embedding-3-small'].map(id=>({object:'model',id}))}));return;
    }
    assert.equal(req.method,'POST');assert.equal(req.url,'/chat');assert.equal(req.headers.accept,'text/event-stream');
    const body=JSON.parse(source);assert.equal(body.model,'gpt-6-sol');assert.equal(body.reasoning_effort,'none');assert.equal(body.stream,true);assert.equal(body.max_completion_tokens,64);assert.deepEqual(body.stream_options,{include_usage:true});
    assert.ok(body.tools.some(tool=>tool.function.name==='read_file'));assert.equal(Object.hasOwn(body,'reasoning'),false);
    const chunk=(delta,finish=null,usage=null)=>'data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}],usage})+'\n\n';
    res.writeHead(200,{'Content-Type':'text/event-stream'});
    let stream;
    if(++turn===1){
      assert.equal(body.messages.length,2);assert.equal(body.messages[1].content,'Read marker.txt using the actual native tool.');assert.ok(!source.includes('model-policy-file-marker'));
      stream=chunk({role:'assistant',tool_calls:[{index:0,id:'policy-read',type:'function',function:{name:'read_file',arguments:'{"path":"marker.txt"}'}}]})+chunk({},'tool_calls',{prompt_tokens:17,completion_tokens:4,total_tokens:21});
    }else{
      assert.equal(turn,2);assert.equal(body.messages.length,4);assert.equal(body.messages[2].tool_calls[0].id,'policy-read');
      assert.equal(body.messages[3].role,'tool');assert.equal(body.messages[3].tool_call_id,'policy-read');assert.ok(body.messages[3].content.includes('model-policy-file-marker'));
      stream=chunk({role:'assistant',content:'Synthetic native model policy read completed.'})+chunk({},'stop',{prompt_tokens:27,completion_tokens:8,total_tokens:35});
    }
    const wire=Buffer.from(stream+'data: [DONE]\n\n');for(let offset=0;offset<wire.length;offset+=7)res.write(wire.subarray(offset,offset+7));res.end();
  }catch(error){failure??=error;if(!res.headersSent)res.writeHead(500);res.end('Synthetic native model policy oracle failed');}});
});
await new Promise(resolveReady=>peer.listen(0,'127.0.0.1',resolveReady));
try{
  const env={};for(const name of ['SystemRoot','WINDIR','PATH','TEMP','TMP','ComSpec','PATHEXT','USERPROFILE','LOCALAPPDATA'])if(process.env[name])env[name]=process.env[name];
  let result;try{result=await promisify(execFile)(binary,[root,modules,stdlib,workspace,`http://127.0.0.1:${peer.address().port}`],{windowsHide:true,timeout:45000,maxBuffer:1024*1024,env});}catch(error){nativeFailure=error;throw error;}
  if(failure)throw failure;
  assert.deepEqual(calls,[{method:'GET',path:'/models'},{method:'GET',path:'/models'},{method:'GET',path:'/models'},{method:'POST',path:'/chat'},{method:'POST',path:'/chat'},{method:'GET',path:'/models'}],'Exact socket oracle forbids rejected-model requests, retries, route changes or restart replay');
  for(const file of await readdir(root))if(/\.sqlite(?:-wal|-shm)?$/.test(file)){
    const bytes=await readFile(join(root,file));for(const secret of [key,rejectedKey])assert.equal(bytes.includes(Buffer.from(secret)),false,'SQLite must retain encrypted credentials only');
  }
  passed=true;process.stdout.write(result.stdout);
}catch(error){
  await writeFile(join(root,'private-peer-error.log'),String(failure?.stack??error.stack).slice(0,65536),{flag:'wx',mode:0o600});
  if(nativeFailure)await writeFile(join(root,'private-native-failure.json'),JSON.stringify({stdout:nativeFailure.stdout??'',stderr:nativeFailure.stderr??'',code:nativeFailure.code??null}),{flag:'wx',mode:0o600});
  process.stderr.write('Native model policy fixture failed; owned diagnostics retained.\n');process.exitCode=1;
}finally{
  peer.closeAllConnections();if(peer.listening)await new Promise(resolveClosed=>peer.close(resolveClosed));
  if(passed){const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('xmind-native-model-policy-'));await rm(target,{recursive:true,force:true});}
}
