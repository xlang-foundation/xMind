// Independent bounded synthetic Claude Messages socket peer. The native child
// owns actual AgentRunner lifecycle, filesystem tools and encrypted xlang3 SQL.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,basename,resolve} from 'node:path';
const [executable,modules,stdlib]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib,'Pass actual native agent fixture and embedded-xlang3 import roots');
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-anthropic-agent-')),workspace=join(root,'workspace');
const key='synthetic-anthropic-agent-key-not-live',firstFile='Actual Claude native file bytes: "quoted" and Unicode 雪\n',secondFile='Second actual Claude native file bytes\n';
const finalAnswer='Synthetic Claude peer checked both real native reads 🌍.',reopenedAnswer='Synthetic Claude peer accepted the SQLite-restored conversation.',recoveredAnswer='Synthetic Claude peer accepted recovery after actual cancellation.';
let child,failure,heldClosed=false,mainRequests=0,cancelRequests=0,continuationPrefix;const routes=[];
const event=value=>'event: '+value.type+'\ndata: '+JSON.stringify(value)+'\n\n';
const start=usage=>({type:'message_start',message:{id:'synthetic-claude-message',type:'message',role:'assistant',model:'fixture-claude',content:[],stop_reason:null,stop_sequence:null,usage}});
function tool(index,id,path,name='read_file',malformed=false){
 const argumentsText=malformed?'{"path":"README.md","path":"second.txt"}':JSON.stringify({path});
 return event({type:'content_block_start',index,content_block:{type:'tool_use',id,name,input:{}}})+
  event({type:'content_block_delta',index,delta:{type:'input_json_delta',partial_json:argumentsText.slice(0,7)}})+
  event({type:'content_block_delta',index,delta:{type:'input_json_delta',partial_json:argumentsText.slice(7)}})+
  event({type:'content_block_stop',index});
}
function text(content){
 return event({type:'content_block_start',index:0,content_block:{type:'text',text:''}})+
  event({type:'content_block_delta',index:0,delta:{type:'text_delta',text:content}})+event({type:'content_block_stop',index:0});
}
function terminal(reason,output){
 return event({type:'message_delta',delta:{stop_reason:reason,stop_sequence:null},usage:{output_tokens:output}})+event({type:'message_stop'});
}
function write(response,wire,end=true){
 if(!response.headersSent)response.writeHead(200,{'Content-Type':'text/event-stream'});
 const bytes=Buffer.from(wire);for(let offset=0;offset<bytes.length;offset+=7)response.write(bytes.subarray(offset,offset+7));
 if(end)response.end();
}
function assertResults(messages,left='toolu-native-left',right='toolu-native-right'){
 assert.deepEqual(messages[0],{role:'user',content:[{type:'text',text:'Read the actual Claude fixture files'}]});
 assert.deepEqual(messages[1],{role:'assistant',content:[{type:'tool_use',id:left,name:'read_file',input:{path:'README.md'}},{type:'tool_use',id:right,name:'read_file',input:{path:'second.txt'}}]});
 assert.equal(messages[2].role,'user');assert.equal(messages[2].content.length,2);
 for(const [index,id,path,content] of [[0,left,'README.md',firstFile],[1,right,'second.txt',secondFile]]){
  const result=messages[2].content[index];assert.equal(result.type,'tool_result');assert.equal(result.tool_use_id,id);assert.equal(typeof result.content,'string');
  assert.deepEqual(JSON.parse(result.content),{path,content},'Only actual native filesystem bytes may satisfy the provider continuation');
 }
}
const peer=createServer((request,response)=>{
 const chunks=[];let bytes=0;request.on('data',chunk=>{bytes+=chunk.length;if(bytes>512*1024){request.destroy();return;}chunks.push(chunk);});
 request.on('end',()=>{try{
  routes.push(request.url);assert.equal(request.method,'POST');assert.equal(request.headers['x-api-key'],key);assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-goog-api-key'],undefined);assert.equal(request.headers['anthropic-version'],'2023-06-01');assert.equal(request.headers.accept,'text/event-stream');assert.equal(request.headers['content-type'],'application/json');
  const source=Buffer.concat(chunks).toString('utf8');assert.equal(source.includes(key),false);assert.equal(source.includes('provider_context'),false);assert.equal(source.includes('provider_items'),false);assert.equal(source.includes('credential_id'),false);
  const body=JSON.parse(source);assert.deepEqual(Object.keys(body).sort(),['max_tokens','messages','model','stream','system','tools']);assert.equal(body.model,'fixture-claude');assert.equal(body.stream,true);assert.equal(body.max_tokens,64);
  assert.equal(body.system.length,1);assert.equal(body.system[0].type,'text');assert.ok(body.system[0].text.startsWith('Synthetic Claude native agent contract:'));
  assert.deepEqual(body.tools.map(item=>item.name),['read_repository_instructions','read_file','list_files','search_files']);
  if(request.url==='/main'){
   ++mainRequests;
   if(mainRequests===1){
    assert.deepEqual(body.messages,[{role:'user',content:[{type:'text',text:'Read the actual Claude fixture files'}]}]);
    write(response,event(start({input_tokens:11,output_tokens:0,cache_creation_input_tokens:0,cache_read_input_tokens:7}))+tool(0,'toolu-native-left','README.md')+tool(1,'toolu-native-right','second.txt')+terminal('tool_use',4));return;
   }
   assertResults(body.messages);
   if(mainRequests===2){
    assert.equal(body.messages.length,3);continuationPrefix=JSON.stringify(body.messages);
    write(response,event(start({input_tokens:19,output_tokens:0,cache_creation_input_tokens:5,cache_read_input_tokens:0}))+text(finalAnswer)+terminal('end_turn',6));return;
   }
   assert.equal(mainRequests,3);assert.equal(body.messages.length,5);assert.equal(JSON.stringify(body.messages.slice(0,3)),continuationPrefix,'Actual SQLite reload must preserve exact provider call/result continuation prefix');
   assert.deepEqual(body.messages[3],{role:'assistant',content:[{type:'text',text:finalAnswer}]});assert.deepEqual(body.messages[4],{role:'user',content:[{type:'text',text:'Continue Claude after SQLite reopen'}]});
   write(response,event(start({input_tokens:0,output_tokens:0}))+text(reopenedAnswer)+terminal('end_turn',0));return;
  }
  if(request.url==='/cancel'){
   ++cancelRequests;
   if(cancelRequests===1){
    assert.deepEqual(body.messages,[{role:'user',content:[{type:'text',text:'Hold actual Claude stream'}]}]);response.once('close',()=>{heldClosed=true;});
    write(response,event(start({input_tokens:0,output_tokens:0}))+event({type:'content_block_start',index:0,content_block:{type:'text',text:''}})+event({type:'content_block_delta',index:0,delta:{type:'text_delta',text:'Synthetic held Claude fragment'}}),false);
    child.stdin.write('fixture-held\n');return;
   }
   assert.equal(cancelRequests,2);assert.equal(source.includes('Synthetic held Claude fragment'),false,'Cancelled partial output cannot become a persisted assistant replay');
   assert.deepEqual(body.messages,[{role:'user',content:[{type:'text',text:'Hold actual Claude stream'},{type:'text',text:'Recover Claude after cancellation'}]}]);
   write(response,event(start({input_tokens:3,output_tokens:0}))+text(recoveredAnswer)+terminal('end_turn',2));return;
  }
  if(request.url==='/rollback'){
   assert.deepEqual(body.messages,[{role:'user',content:[{type:'text',text:'Rollback actual Claude tool history'}]}]);
   write(response,event(start({input_tokens:5,output_tokens:0}))+tool(0,'toolu-rollback-left','README.md')+tool(1,'toolu-rollback-right','second.txt')+terminal('tool_use',2));return;
  }
  const route=request.url.slice(1);assert.ok(['malformed','unknown','incomplete','late-error','truncated'].includes(route));assert.deepEqual(body.messages,[{role:'user',content:[{type:'text',text:'Reject Claude '+route}]}]);
  let wire=event(start({input_tokens:1,output_tokens:0}));
  wire+=tool(0,'toolu-rejected-'+route,'README.md',route==='unknown'?'unoffered_function':'read_file',route==='malformed');
  if(route==='incomplete'){write(response,wire);return;}
  wire+=terminal(route==='truncated'?'max_tokens':'tool_use',1);
  if(route==='late-error')wire+=event({type:'error',error:{type:'overloaded_error',message:'private-provider-error-body '+key}});
  write(response,wire);
 }catch(error){failure=error;response.destroy();}});
});
try{
 await mkdir(workspace);await writeFile(join(workspace,'README.md'),firstFile);await writeFile(join(workspace,'second.txt'),secondFile);await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
 const running=execute(executable,[join(root,'state.sqlite'),modules,stdlib,workspace,'http://127.0.0.1:'+peer.address().port],{windowsHide:true,timeout:40000,maxBuffer:2*1024*1024});child=running.child;
 let result;try{result=await running;}catch(error){if(failure)throw failure;throw error;}if(failure)throw failure;
 assert.equal(mainRequests,3);assert.equal(cancelRequests,2);assert.equal(heldClosed,true,'Actual cancellation must close the held native transport socket');
 assert.deepEqual(routes,['/main','/main','/main','/malformed','/unknown','/incomplete','/late-error','/truncated','/cancel','/cancel','/rollback'],'Rejected/cancelled/rolled-back native runs must not automatically retry providers or tools');
 assert.equal(await readFile(join(workspace,'README.md'),'utf8'),firstFile);assert.equal(await readFile(join(workspace,'second.txt'),'utf8'),secondFile);
 for(const entry of await readdir(root,{withFileTypes:true}))if(entry.isFile()&&/\.sqlite(?:-wal|-shm)?$/.test(entry.name))assert.equal((await readFile(join(root,entry.name))).includes(Buffer.from(key)),false,'Actual closed native credential SQLite files must not contain plaintext provider key');
 assert.equal(result.stdout.includes(key)||result.stderr.includes(key),false);process.stdout.write(result.stdout);
}finally{
 peer.closeAllConnections();if(peer.listening)await new Promise(resolve=>peer.close(resolve));
 const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('xmind-anthropic-agent-'));await rm(target,{recursive:true,force:true});
}
