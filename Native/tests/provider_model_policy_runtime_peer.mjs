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
const calls=[];let failure,passed=false,nativeFailure,turn=0,xaiTurn=0;
function matchingPaths(value,needle,path='$',out=[]){if(typeof value==='string'){if(value.includes(needle))out.push(path);}else if(Array.isArray(value))value.forEach((item,index)=>matchingPaths(item,needle,`${path}[${index}]`,out));else if(value&&typeof value==='object')for(const [key,item] of Object.entries(value))matchingPaths(item,needle,`${path}.${key}`,out);return out;}
const peer=createServer((req,res)=>{
  const chunks=[];let size=0;req.on('error',()=>{});res.on('error',()=>{});
  req.on('data',chunk=>{size+=chunk.length;if(size>512*1024){req.destroy();return;}chunks.push(chunk);});
  req.on('end',()=>{try{
    const source=Buffer.concat(chunks).toString('utf8');calls.push({method:req.method,path:req.url});
    assert.equal(req.headers.authorization,'Bearer '+key);assert.ok(!source.includes(key)&&!source.includes(rejectedKey));
    if(req.method==='GET'){
      assert.equal(source,'');
      res.writeHead(200,{'Content-Type':'application/json'});
      if(req.url==='/models')res.end(JSON.stringify({object:'list',data:['gpt-image-1','gpt-6.1-sol','gpt-6-sol','gpt-6-astra','gpt-6.2-future','gpt-4.1','text-embedding-3-small'].map(id=>({object:'model',id}))}));
      else {assert.equal(req.url,'/xai/models');res.end(JSON.stringify({object:'list',data:['grok-4.7','grok-4.6','grok-future','grok-imagine-image-2.0'].map(id=>({object:'model',id}))}));}return;
    }
    if(req.url==='/xai/responses'){
      const body=JSON.parse(source);assert.equal(req.method,'POST');assert.equal(req.headers.accept,'text/event-stream');assert.equal(body.model,'grok-4.7');assert.equal(body.stream,true);assert.equal(body.store,false);assert.equal(body.max_output_tokens,64);assert.ok(body.tools.some(tool=>tool.type==='function'&&tool.name==='read_file'));
      const seq={value:0},send=(type,fields)=>res.write(`event: ${type}\ndata: ${JSON.stringify({type,sequence_number:seq.value++,...fields})}\n\n`),id=`xai_resp_${++xaiTurn}`;
      res.writeHead(200,{'Content-Type':'text/event-stream'});send('response.created',{response:{id,model:body.model,status:'in_progress'}});
      if(xaiTurn===1){assert.ok(!source.includes('model-policy-file-marker'),`Workspace marker appeared before the native read at request paths ${JSON.stringify(matchingPaths(body,'model-policy-file-marker'))}`);assert.equal(body.input.findLast(item=>item.role==='user').content[0].text,'Read marker.txt using Grok through the native Responses route.');const item={id:'xai_fc_1',type:'function_call',call_id:'xai_read_1',name:'read_file',arguments:'{"path":"marker.txt"}',status:'completed'};send('response.output_item.added',{output_index:0,item:{...item,status:'in_progress',arguments:''}});send('response.function_call_arguments.delta',{output_index:0,item_id:item.id,delta:item.arguments});send('response.function_call_arguments.done',{output_index:0,item_id:item.id,arguments:item.arguments});send('response.output_item.done',{output_index:0,item});send('response.completed',{response:{id,model:body.model,status:'completed',output:[item],usage:{input_tokens:31,output_tokens:5,total_tokens:36}}});res.end();return;}
      assert.equal(xaiTurn,2);const output=body.input.findLast(item=>item.type==='function_call_output');assert.equal(output.call_id,'xai_read_1');assert.ok(output.output.includes('model-policy-file-marker'));
      const item={id:'xai_msg_2',type:'message',status:'completed',role:'assistant',content:[{type:'output_text',text:'Synthetic xAI native model policy read completed.',annotations:[]}]};send('response.output_item.added',{output_index:0,item:{id:item.id,type:'message',status:'in_progress',role:'assistant',content:[]}});send('response.content_part.added',{output_index:0,item_id:item.id,content_index:0,part:{type:'output_text',text:''}});send('response.output_text.delta',{output_index:0,item_id:item.id,content_index:0,delta:item.content[0].text});send('response.output_text.done',{output_index:0,item_id:item.id,content_index:0,text:item.content[0].text});send('response.content_part.done',{output_index:0,item_id:item.id,content_index:0,part:item.content[0]});send('response.output_item.done',{output_index:0,item});send('response.completed',{response:{id,model:body.model,status:'completed',output:[item],usage:{input_tokens:42,output_tokens:9,total_tokens:51}}});res.end();return;
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
  const expectedCalls=[{method:'GET',path:'/models'},{method:'GET',path:'/models'},{method:'GET',path:'/xai/models'},{method:'GET',path:'/models'},{method:'POST',path:'/chat'},{method:'POST',path:'/chat'},{method:'POST',path:'/xai/responses'},{method:'POST',path:'/xai/responses'},{method:'GET',path:'/models'},{method:'GET',path:'/xai/models'}];
  assert.equal(JSON.stringify(calls),JSON.stringify(expectedCalls),`Exact socket oracle forbids rejected-model requests, retries, route changes or restart replay; actual calls ${JSON.stringify(calls)}`);
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
