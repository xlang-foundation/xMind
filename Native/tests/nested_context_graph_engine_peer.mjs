// Independent synthetic provider; native HTTP, model ownership, reads and SQLite are real.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
const [binary,modules,stdlib]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-nested-context-'));
const turns=new Map(),counts=new Map(),first=[],held=[];let failure,success=false;
const userText=body=>body.input.filter(x=>x.role==='user').map(x=>typeof x.content==='string'?x.content:x.content.map(p=>p.text??'').join('')).join('\n');
function reply(response,model,turn){
 const side=model.endsWith('left')?'left':'right',id=side+'_'+turn;
 const item=turn===1?{id:'fc_'+id,type:'function_call',call_id:'call_'+id,name:'read_file',arguments:JSON.stringify({path:side+'.txt'}),status:'completed'}:{id:'msg_'+id,type:'message',role:'assistant',status:'completed',content:[{type:'output_text',text:'Synthetic '+side+' result after actual native read.',annotations:[]}]};
 response.writeHead(200,{'Content-Type':'text/event-stream'});let seq=0;const send=(type,value)=>response.write('event: '+type+'\ndata: '+JSON.stringify({type,sequence_number:seq++,...value})+'\n\n');
 send('response.created',{response:{id:'resp_'+id,model,status:'in_progress'}});
 send('response.output_item.added',{output_index:0,item:turn===1?{id:item.id,type:item.type,call_id:item.call_id,name:item.name,arguments:''}:{id:item.id,type:item.type,role:'assistant',content:[]}});
 if(turn===1){send('response.function_call_arguments.delta',{output_index:0,item_id:item.id,delta:item.arguments});send('response.function_call_arguments.done',{output_index:0,item_id:item.id,arguments:item.arguments});}
 else{send('response.content_part.added',{output_index:0,item_id:item.id,content_index:0,part:{type:'output_text',text:'',annotations:[]}});send('response.output_text.delta',{output_index:0,item_id:item.id,content_index:0,delta:item.content[0].text});send('response.output_text.done',{output_index:0,item_id:item.id,content_index:0,text:item.content[0].text});send('response.content_part.done',{output_index:0,item_id:item.id,content_index:0,part:item.content[0]});}
 send('response.output_item.done',{output_index:0,item});send('response.completed',{response:{id:'resp_'+id,model,status:'completed',output:[item],usage:{input_tokens:20,output_tokens:5,total_tokens:25}}});response.end();
}
const server=createServer((request,response)=>{const chunks=[];request.on('error',()=>{});response.on('error',()=>{});request.on('data',b=>chunks.push(b));request.on('end',async()=>{try{
 assert.equal(request.headers.authorization,'Bearer context-graph-fixture-token-not-a-real-key');assert.equal(request.method,'POST');assert(['/nested/responses','/nested/responses/input_tokens'].includes(request.url));
 const body=JSON.parse(Buffer.concat(chunks)),model=body.model;assert(['graph-context-left','graph-context-right'].includes(model));const side=model.endsWith('left')?'left':'right';const cancel=userText(body).includes('nested-cancel');const kind=request.url.endsWith('input_tokens')?'count':'inference';const key=(cancel?'cancel':'main')+':'+model+':'+kind;counts.set(key,(counts.get(key)??0)+1);
 assert(userText(body).includes('graph-context-'+side));
 for(const output of body.input.filter(x=>x.type==='function_call_output'))assert.equal(JSON.parse(output.output).content,'Actual context graph '+side+'\n');
 if(kind==='count'){response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({object:'response.input_tokens',input_tokens:100}));return;}
 assert.equal(body.stream,true);if(cancel){held.push(response);if(held.length===2)await writeFile(join(root,'nested-cancel-started'),'Two actual nested native model transports entered.\n',{flag:'wx'});return;}
 const turn=(turns.get(model)??0)+1;turns.set(model,turn);assert(turn<=2,'Completed model tasks must never replay');if(turn===1){first.push({response,model,turn});if(first.length===2)first.forEach(v=>reply(v.response,v.model,v.turn));}else reply(response,model,turn);
 }catch(error){failure??=error;if(!response.headersSent)response.writeHead(500);response.end('Synthetic nested context peer rejected request');}});});
try{
 await writeFile(join(root,'left.txt'),'Actual context graph left\n');await writeFile(join(root,'right.txt'),'Actual context graph right\n');await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const result=await promisify(execFile)(binary,[root,modules,stdlib,'http://127.0.0.1:'+server.address().port,'nested'],{windowsHide:true,timeout:45000});if(failure)throw failure;process.stdout.write(result.stdout);
 for(const model of ['graph-context-left','graph-context-right'])for(const kind of ['count','inference']){assert.equal(counts.get('main:'+model+':'+kind),2);assert.equal(counts.get('cancel:'+model+':'+kind),1);}assert.equal([...counts.values()].reduce((a,b)=>a+b,0),12);success=true;
}catch(error){await writeFile(join(root,'private-peer-error.log'),String(failure?.stack??error.stack)+'\n'+String(error.stderr??''));console.error('nested_context_graph_engine_failed; owned diagnostics retained at '+root);process.exitCode=1;}
finally{server.closeAllConnections();await new Promise(r=>server.close(r));if(success){assert.equal(dirname(resolve(root)),resolve(tmpdir()));await rm(root,{recursive:true,force:true});}}
