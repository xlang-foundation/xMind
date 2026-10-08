// Provider protocol replies/signatures/credential are synthetic. The compiled
// native parent/leaf engines, filesystem effects, permissions and xlang3 DB are real.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,basename,resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {existsSync} from 'node:fs';

const [executable,modules,stdlib]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib,'Expected actual native contract and xlang3 source/module roots');
const root=await mkdtemp(join(tmpdir(),'xmind-native-delegation-')),workspace=join(root,'workspace');
const key='synthetic-native-delegation-key-not-live';
const files={left:'Actual left delegation file: "quoted" 雪\n',right:'Actual right delegation file bytes\n'};
const findings={left:'Synthetic left finding after the actual left file read.',right:'Synthetic right finding after the actual right file read.',followup:'Synthetic follow-up finding after the actual right file read.'};
for(const side of ['a','b'])for(const file of ['left','right'])findings[`parallel-${side}-${file}`]=`Synthetic parallel-${side}-${file} finding after the actual ${file} file read.`;
const tasks=(labels)=>({tasks:labels.map(label=>({id:label,objective:`fixture-leaf-${label}: Read ${label.endsWith('left')?'left':'right'}.txt and report the observed finding.`,preset:'workspace.inspect'}))});
const rawTasks=JSON.stringify(tasks(['left','right']));
const event=value=>'event: '+value.type+'\ndata: '+JSON.stringify(value)+'\n\n';
function start(input){return event({type:'message_start',message:{id:'synthetic-delegation-message',type:'message',role:'assistant',model:'fixture-delegation',content:[],stop_reason:null,stop_sequence:null,usage:{input_tokens:input,output_tokens:0}}});}
function thinking(signature,index=0){return event({type:'content_block_start',index,content_block:{type:'thinking',thinking:'',signature:''}})+event({type:'content_block_delta',index,delta:{type:'thinking_delta',thinking:'Synthetic private thought for '+signature}})+event({type:'content_block_delta',index,delta:{type:'signature_delta',signature}})+event({type:'content_block_stop',index});}
function text(value,index=0){return event({type:'content_block_start',index,content_block:{type:'text',text:''}})+event({type:'content_block_delta',index,delta:{type:'text_delta',text:value}})+event({type:'content_block_stop',index});}
function tool(name,id,argumentsText,index=1){return event({type:'content_block_start',index,content_block:{type:'tool_use',id,name,input:{}}})+event({type:'content_block_delta',index,delta:{type:'input_json_delta',partial_json:argumentsText.slice(0,9)}})+event({type:'content_block_delta',index,delta:{type:'input_json_delta',partial_json:argumentsText.slice(9)}})+event({type:'content_block_stop',index});}
function terminal(reason,output){return event({type:'message_delta',delta:{stop_reason:reason,stop_sequence:null},usage:{output_tokens:output}})+event({type:'message_stop'});}
function send(response,wire,end=true){response.writeHead(200,{'Content-Type':'text/event-stream'});const bytes=Buffer.from(wire);for(let offset=0;offset<bytes.length;offset+=11)response.write(bytes.subarray(offset,offset+11));if(end)response.end();}
const publicText=messages=>messages.flatMap(message=>message.content||[]).filter(item=>item.type==='text').map(item=>item.text).join('\n');
const results=messages=>messages.flatMap(message=>message.content||[]).filter(item=>item.type==='tool_result');
let failure,child;const requests=[],counts=new Map(),held=[],pendingSuccess=[],pendingFaults=[],pendingParallel=[];
const releaseFaults=setInterval(()=>{
 for(let index=pendingFaults.length-1;index>=0;--index){const item=pendingFaults[index];if(existsSync(join(workspace,'release-'+item.route.slice(1)))){pendingFaults.splice(index,1);send(item.response,item.wire);}}
},10);
const routeCount=(route,owner)=>{const id=route+':'+owner;const count=(counts.get(id)||0)+1;counts.set(id,count);return count;};
const server=createServer((request,response)=>{
 const chunks=[];let bytes=0;request.on('data',chunk=>{bytes+=chunk.length;if(bytes>1024*1024){request.destroy();return;}chunks.push(chunk);});
 request.on('end',()=>{try{
  assert.equal(request.method,'POST');assert.equal(request.headers['x-api-key'],key);assert.equal(request.headers.authorization,undefined);
  const source=Buffer.concat(chunks).toString('utf8');assert.ok(!source.includes(key),'Owned provider key cannot enter model context');
  const body=JSON.parse(source);assert.equal(body.model,'fixture-delegation');assert.equal(body.stream,true);assert.ok(body.max_tokens>0);
  const names=body.tools.map(item=>item.name),parent=names.includes('delegate_tasks'),user=publicText(body.messages),route=request.url;
  const parentSide=parent&&route==='/parallel'?(user.includes('fixture-parent-parallel-a')?'a':'b'):undefined;
  const label=parent?(parentSide?'parent-'+parentSide:'parent'):(['parallel-a-left','parallel-a-right','parallel-b-left','parallel-b-right','left','right','followup','forbidden','nested','oversize'].find(label=>user.includes('fixture-leaf-'+label))||'unknown');
  const count=routeCount(route,label);requests.push({route,owner:label,count});
  if(parent){
   assert.ok(names.includes('edit_file')&&names.includes('create_file'),'Delegation must retain the configured parent coding authority');
   assert.ok(!source.includes('opaque-leaf-'),'Child signed receipts cannot enter another execution parent history');
   if(route==='/parallel'){
    if(count===1){send(response,start(5)+thinking('opaque-parent-parallel-'+parentSide)+tool('delegate_tasks','delegate-parallel-'+parentSide,JSON.stringify(tasks([`parallel-${parentSide}-left`,`parallel-${parentSide}-right`])))+terminal('tool_use',2));return;}
    assert.equal(count,2);const actual=results(body.messages).at(-1);assert.ok(actual.content.includes(findings[`parallel-${parentSide}-left`])&&actual.content.includes(findings[`parallel-${parentSide}-right`]));
    send(response,start(9)+text('Synthetic occupied parent '+parentSide+' joined its two actual leaf investigations.')+terminal('end_turn',2));return;
   }
   if(route==='/success'){
    if(count===1){assert.equal(results(body.messages).length,0);send(response,start(11)+thinking('opaque-parent-delegate')+tool('delegate_tasks','delegate-success',rawTasks)+terminal('tool_use',3));return;}
    const actual=results(body.messages).at(-1);assert.ok(actual);
    if(count===2){
     assert.equal(actual.tool_use_id,'delegate-success');assert.ok(actual.content.includes(findings.left)&&actual.content.includes(findings.right),'Actual child outcomes must reach the parent continuation');
     assert.ok(source.includes('opaque-parent-delegate'),'Original signed parent tool receipt must replay unchanged');
     send(response,start(19)+thinking('opaque-parent-edit')+tool('edit_file','edit-after-delegation',JSON.stringify({path:'target.txt',old_text:'original',new_text:'reviewed native delegation'}))+terminal('tool_use',5));return;
    }
    assert.equal(count,3);assert.equal(actual.tool_use_id,'edit-after-delegation');const effect=JSON.parse(actual.content);
    assert.equal(effect.path,'target.txt');assert.equal(effect.content_sha256,createHash('sha256').update('reviewed native delegation\n').digest('hex'));
    send(response,start(23)+thinking('opaque-parent-final')+text('Synthetic parent verified actual joined investigations and the approved file edit.',1)+terminal('end_turn',7));return;
   }
   if(route==='/followup'){
    if(count===1){send(response,start(5)+thinking('opaque-parent-failure-batch')+tool('delegate_tasks','delegate-failed',JSON.stringify(tasks(['forbidden'])))+terminal('tool_use',2));return;}
    const actual=results(body.messages).at(-1);
    if(count===2){assert.ok(/failed|error/i.test(actual.content),'Actual rejected leaf outcome must be returned as a failure');send(response,start(7)+thinking('opaque-parent-followup-batch')+tool('delegate_tasks','delegate-followup',JSON.stringify(tasks(['followup'])))+terminal('tool_use',2));return;}
    assert.equal(count,3);assert.ok(actual.content.includes(findings.followup));send(response,start(9)+text('Synthetic parent used a new batch after the observed leaf failure.')+terminal('end_turn',3));return;
   }
   if(route==='/nested'){
    if(count===1){send(response,start(5)+thinking('opaque-parent-nested')+tool('delegate_tasks','delegate-nested',JSON.stringify(tasks(['nested'])))+terminal('tool_use',2));return;}
    assert.equal(count,2);assert.ok(/failed|error/i.test(results(body.messages).at(-1).content));send(response,start(7)+text('Synthetic parent observed rejected recursive leaf delegation.')+terminal('end_turn',2));return;
   }
   if(route==='/budget'){
    if(count===1){send(response,start(5)+thinking('opaque-parent-budget')+tool('delegate_tasks','delegate-budget',rawTasks)+terminal('tool_use',2));return;}
    assert.equal(count,2);assert.ok(/execution_budget_exhausted/.test(results(body.messages).at(-1).content),'Actual failed leaves must preserve the held parent continuation');
    send(response,start(7)+text('Synthetic parent used its preserved final shared model-call allowance.')+terminal('end_turn',2));return;
   }
   if(route==='/cancel'||route==='/admission-fault'||route==='/conversation-fault'||route==='/settlement-fault'){
    assert.equal(count,1,'Cancelled/faulted parent cannot perform an unrecorded continuation');
    const chosen=route==='/settlement-fault'?['left']:['left','right'];
    send(response,start(5)+thinking('opaque-parent-'+route.slice(1))+tool('delegate_tasks','delegate-'+route.slice(1),JSON.stringify(tasks(chosen)))+terminal('tool_use',2));return;
   }
   if(route==='/oversize'){
    if(count===1){send(response,start(5)+thinking('opaque-parent-oversize')+tool('delegate_tasks','delegate-oversize',JSON.stringify(tasks(['oversize'])))+terminal('tool_use',2));return;}
    assert.equal(count,2);const actual=results(body.messages).at(-1);assert.ok(/limit|exceed|oversiz/i.test(actual.content));assert.ok(actual.content.length<=65536);assert.ok(!actual.content.includes('"'.repeat(100)),'Bounded failure cannot silently pass or copy the oversized child answer');
    send(response,start(8)+text('Synthetic parent observed the explicit child envelope limit.')+terminal('end_turn',2));return;
   }
   throw new Error('Unexpected fixture parent provider route');
  }
  assert.deepEqual([...names].sort(),['list_files','read_file','read_repository_instructions','search_files']);
  assert.notEqual(label,'unknown','Accepted native leaf objective must identify its own fixture task');
  if(label==='left')assert.ok(!user.includes('fixture-leaf-right'));if(label==='right')assert.ok(!user.includes('fixture-leaf-left'));
  assert.ok(!source.includes('opaque-parent-'),'Parent signed continuation must not be copied into leaf prompt/history');
  if(route==='/cancel'){
   assert.equal(count,1);response.once('close',()=>held.push(label));
   send(response,start(4)+event({type:'content_block_start',index:0,content_block:{type:'text',text:''}})+event({type:'content_block_delta',index:0,delta:{type:'text_delta',text:'Synthetic held leaf fragment '+label}}),false);return;
  }
  if(label==='forbidden'){
   assert.equal(count,1);send(response,start(3)+thinking('opaque-leaf-forbidden')+tool('edit_file','forbidden-leaf-edit',JSON.stringify({path:'target.txt',old_text:'original',new_text:'unauthorized leaf mutation'}))+terminal('tool_use',1));return;
  }
  if(label==='nested'){
   assert.equal(count,1);send(response,start(3)+thinking('opaque-leaf-nested')+tool('delegate_tasks','unoffered-recursive-delegation',JSON.stringify(tasks(['left'])))+terminal('tool_use',1));return;
  }
  const left=label.endsWith('left'),path=left?'left.txt':'right.txt',rawArguments='{"pa\\u0074h":'+JSON.stringify(path)+'}';
  if(count===1){
   const wire=start(label==='left'?13:17)+thinking('opaque-leaf-'+label)+tool('read_file','read-'+label,rawArguments)+terminal('tool_use',2);
   if(route==='/success'){pendingSuccess.push({response,wire});if(pendingSuccess.length===2)for(const item of pendingSuccess)send(item.response,item.wire);}
   else if(route==='/parallel'){pendingParallel.push({response,wire});if(pendingParallel.length===4)for(const item of pendingParallel)send(item.response,item.wire);}
   else if(route==='/conversation-fault'||route==='/settlement-fault')pendingFaults.push({route,response,wire});
   else send(response,wire);return;
  }
  assert.equal(count,2);const actual=results(body.messages).at(-1);assert.equal(actual.tool_use_id,'read-'+label);
  assert.deepEqual(JSON.parse(actual.content),{path,content:files[left?'left':'right']},'Only actual native workspace file bytes satisfy leaf continuation');
  assert.ok(source.includes(rawArguments),'Raw escaped leaf call arguments must replay within its own signed turn');
  assert.ok(source.includes('opaque-leaf-'+label));
  const answer=label==='oversize'?'"'.repeat(40000):findings[label];
  send(response,start(label==='left'?29:31)+thinking('opaque-leaf-final-'+label)+text(answer,1)+terminal('end_turn',label==='left'?7:9));
 }catch(error){failure=error;response.destroy();}});
});
try{
 await mkdir(workspace);await writeFile(join(workspace,'left.txt'),files.left);await writeFile(join(workspace,'right.txt'),files.right);await writeFile(join(workspace,'target.txt'),'original\n');
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const running=promisify(execFile)(executable,[join(root,'state.sqlite'),modules,stdlib,workspace,'http://127.0.0.1:'+server.address().port],{windowsHide:true,timeout:100000,maxBuffer:4*1024*1024});child=running.child;
 let result;try{result=await running;}catch(error){if(failure)throw failure;throw error;}if(failure)throw failure;
 assert.equal(counts.get('/success:parent'),3);assert.equal(counts.get('/success:left'),2);assert.equal(counts.get('/success:right'),2);assert.equal(pendingSuccess.length,2,'Both actual leaf workers must reach the provider before either first response is released');
 assert.equal(counts.get('/followup:parent'),3);assert.equal(counts.get('/followup:forbidden'),1);assert.equal(counts.get('/followup:followup'),2);
 assert.equal(counts.get('/nested:parent'),2);assert.equal(counts.get('/nested:nested'),1);assert.equal(requests.filter(request=>request.route==='/nested').length,3,'Rejected recursive delegation must create no grandchild provider request');
 assert.equal(counts.get('/budget:parent'),2);assert.equal(requests.filter(request=>request.route==='/budget'&&request.owner!=='parent').length,1,'Only one actual leaf transport may consume the last unheld allowance');
 assert.equal(counts.get('/parallel:parent-a'),2);assert.equal(counts.get('/parallel:parent-b'),2);assert.equal(pendingParallel.length,4,'Both occupied actual parent workers must obtain separate global leaf workers without deadlock');
 for(const side of ['a','b'])for(const file of ['left','right'])assert.equal(counts.get(`/parallel:parallel-${side}-${file}`),2);
 assert.deepEqual([...held].sort(),['left','right'],'Root cancellation must close both held native leaf transport sockets');
 assert.equal(counts.get('/admission-fault:parent'),1);assert.equal(requests.filter(request=>request.route==='/admission-fault').length,1,'Atomic admission rollback cannot launch leaves');
 assert.equal(counts.get('/conversation-fault:parent'),1);assert.equal(counts.get('/settlement-fault:parent'),1);assert.equal(counts.get('/oversize:parent'),2);
 assert.equal(await readFile(join(workspace,'left.txt'),'utf8'),files.left);assert.equal(await readFile(join(workspace,'right.txt'),'utf8'),files.right);assert.equal(await readFile(join(workspace,'target.txt'),'utf8'),'reviewed native delegation\n');
 for(const entry of await readdir(root,{withFileTypes:true}))if(entry.isFile()&&/\.sqlite(?:-wal|-shm)?$/.test(entry.name))assert.equal((await readFile(join(root,entry.name))).includes(Buffer.from(key)),false,'Closed actual native SQLite files cannot contain the plaintext fixture provider key');
 assert.ok(!result.stdout.includes(key)&&!result.stderr.includes(key));process.stdout.write(result.stdout);
}finally{
 clearInterval(releaseFaults);
 server.closeAllConnections();if(server.listening)await new Promise(resolve=>server.close(resolve));
 const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('xmind-native-delegation-'));await rm(target,{recursive:true,force:true});
}
