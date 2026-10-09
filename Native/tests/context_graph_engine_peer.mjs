// Independent synthetic Responses/count/compact peer. The tested graph owner,
// model transports, native file reads and embedded xlang3 SQLite are real.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
const [binary,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
assert.ok(binary&&modules&&stdlib,'Compiled contract and xlang3 source paths are required');
const fixture=await mkdtemp(join(tmpdir(),'xmind-context-graph-'));
const requests=new Map(),turns=new Map(),canonical=new Map(),first=[],held={cancel:[],recover:[]};
let peerFailure,successful=false,nativeFailure;
const count=(mode,model,kind)=>{const key=`${mode}:${model}:${kind}`;requests.set(key,(requests.get(key)??0)+1);};
const modelSide=model=>{assert.ok(['graph-context-left','graph-context-right'].includes(model));return model.endsWith('left')?'left':'right';};
function eventStream(response,model,turn){
  const side=modelSide(model),identity=`${side}_${turn}`;
  response.writeHead(200,{'Content-Type':'text/event-stream'});let sequence=0;
  const send=(type,fields)=>response.write(`event: ${type}\ndata: ${JSON.stringify({type,sequence_number:sequence++,...fields})}\n\n`);
  const reason={id:`rs_${identity}`,type:'reasoning',summary:[],encrypted_content:`synthetic-done-${identity}`};
  const item=turn<5?{id:`fc_${identity}`,type:'function_call',call_id:`call_${identity}`,name:'read_file',arguments:JSON.stringify({path:`${side}.txt`}),status:'completed'}:
    {id:`msg_${identity}`,type:'message',role:'assistant',status:'completed',phase:null,content:[{type:'output_text',text:`Synthetic ${side} conclusion after actual native reads.`,annotations:[]}]};
  send('response.created',{response:{id:`resp_${identity}`,model,status:'in_progress'}});
  send('response.output_item.added',{output_index:0,item:{id:reason.id,type:'reasoning',summary:[]}});
  send('response.output_item.done',{output_index:0,item:reason});
  if(turn<5){
    send('response.output_item.added',{output_index:1,item:{id:item.id,type:item.type,call_id:item.call_id,name:item.name,arguments:''}});
    send('response.function_call_arguments.delta',{output_index:1,item_id:item.id,delta:item.arguments});
    send('response.function_call_arguments.done',{output_index:1,item_id:item.id,arguments:item.arguments});
  }else{
    send('response.output_item.added',{output_index:1,item:{id:item.id,type:'message',role:'assistant',content:[]}});
    send('response.content_part.added',{output_index:1,item_id:item.id,content_index:0,part:{type:'output_text',text:''}});
    send('response.output_text.delta',{output_index:1,item_id:item.id,content_index:0,delta:item.content[0].text});
    send('response.output_text.done',{output_index:1,item_id:item.id,content_index:0,text:item.content[0].text});
    send('response.content_part.done',{output_index:1,item_id:item.id,content_index:0,part:item.content[0]});
  }
  send('response.output_item.done',{output_index:1,item});
  const input=20+turn+(side==='right'?30:0),output=5;
  send('response.completed',{response:{id:`resp_${identity}`,model,status:'completed',output:[reason,item],usage:{input_tokens:input,output_tokens:output,total_tokens:input+output}}});response.end();
}
function userItems(body){return body.input.filter(item=>item.role==='user');}
function userText(item){return typeof item.content==='string'?item.content:item.content.map(part=>part.text??'').join('');}
function inspect(body,raw,mode,model,kind){
  const side=modelSide(model),other=side==='left'?'right':'left',users=userItems(body);
  assert.equal(users.length,1,'A graph agent receives its own isolated objective only');
  assert.ok(userText(users[0]).includes(`graph-context-${side}`));
  assert.ok(!userText(users[0]).includes(`graph-context-${other}`),'A sibling objective must not enter another child context');
  const trusted=body.input.filter(item=>item.role==='system'||item.role==='developer');assert.ok(trusted.length>=1);
  assert.equal(trusted[0].role,'system');assert.ok(userText(trusted[0]).includes('xMind'));
  if(kind!=='compact')assert.deepEqual(body.tools.map(tool=>tool.name).sort(),['glob_files','list_files','list_skills','load_skill','read_file','read_repository_instructions','search_files']);
  if(canonical.has(model)&&mode==='main'&&body.input.some(item=>item.type==='compaction'))
    assert.ok(raw.includes(canonical.get(model).slice(1,-1)),'Whole canonical output and exact opaque/decimal/escaped metadata must be replayed unpruned');
  for(const item of body.input.filter(item=>item.type==='function_call_output')){
    const output=JSON.parse(item.output);assert.equal(output.content,`Actual context graph ${side}\n`);
    assert.ok(!item.output.includes(`Actual context graph ${other}`));
  }
}
const server=createServer((request,response)=>{
  request.on('error',()=>{});response.on('error',()=>{});const chunks=[];
  request.on('data',chunk=>chunks.push(chunk));request.on('end',async()=>{
    try{
      assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer context-graph-fixture-token-not-a-real-key');
      const match=/^\/(main|cancel|recover)\/responses(\/input_tokens|\/compact)?$/.exec(request.url);assert.ok(match,'Only the bound synthetic route is allowed');
      const kind=match[2]==='/input_tokens'?'count':match[2]==='/compact'?'compact':'inference';
      const raw=Buffer.concat(chunks).toString('utf8'),body=JSON.parse(raw),model=body.model;
      const mode=match[1]==='main'&&userItems(body).some(item=>userText(item).includes('cancel-root'))?'cancel':match[1];
      count(mode,model,kind);inspect(body,raw,mode,model,kind);
      if(kind==='count'){
        assert.equal(request.headers.accept,'application/json');
        const projected=body.input.some(item=>item.type==='compaction'),outputs=body.input.filter(item=>item.type==='function_call_output').length;
        const value=projected?250:outputs>=4?1000:100;
        response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({object:'response.input_tokens',input_tokens:value}));return;
      }
      if(kind==='compact'){
        assert.equal(mode,'main');assert.equal(request.headers.accept,'application/json');assert.deepEqual(Object.keys(body).sort(),['input','model']);
        assert.equal(body.input.filter(item=>item.type==='function_call_output').length,3,'Only complete old groups may become the compacted prefix');
        const users=userItems(body).map((item,index)=>({id:item.id??`user_${model}_${index}`,type:'message',role:'user',status:'completed',content:typeof item.content==='string'?[{type:'input_text',text:item.content}]:item.content}));
        const opaque=String.raw`{"id":"cmp_${model}","type":"compaction","encrypted_content":"synthetic-${model}-\u0061","private_metadata":{"decimal":1.00000000000000000001,"k\u0065y":"retained"}}`;
        const window='['+users.map(item=>JSON.stringify(item)).join(',')+','+opaque+']';assert.ok(!canonical.has(model));canonical.set(model,window);
        response.writeHead(200,{'Content-Type':'application/json'});response.end(`{"id":"compact_${model}","object":"response.compaction","created_at":1764967971,"output":${window},"usage":{"input_tokens":41,"output_tokens":9,"total_tokens":50}}`);return;
      }
      assert.equal(request.headers.accept,'text/event-stream');assert.equal(body.stream,true);assert.equal(body.store,false);assert.deepEqual(body.include,['reasoning.encrypted_content']);assert.equal(body.max_output_tokens,32);
      if(mode!=='main'){
        held[mode].push(response);if(held[mode].length===2)await writeFile(join(fixture,`${mode}-started`),'Two actual native provider requests reached the independent peer.\n',{flag:'wx'});return;
      }
      const turn=(turns.get(model)??0)+1;turns.set(model,turn);assert.ok(turn<=5);
      if(turn===1){first.push({response,model,turn});if(first.length===2)for(const item of first)eventStream(item.response,item.model,item.turn);}
      else eventStream(response,model,turn);
    }catch(error){peerFailure??=error;if(!response.headersSent)response.writeHead(500);response.end('Synthetic graph context peer rejected a contract request');}
  });
});
try{
  await writeFile(join(fixture,'left.txt'),'Actual context graph left\n');await writeFile(join(fixture,'right.txt'),'Actual context graph right\n');
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const base=`http://127.0.0.1:${server.address().port}`;
  for(const mode of ['exercise','crash','reopen']){
    try{const result=await execute(binary,[fixture,modules,stdlib,base,mode],{windowsHide:true,timeout:45000});if(peerFailure)throw peerFailure;process.stdout.write(result.stdout);}
    catch(error){nativeFailure=error;throw error;}
  }
  for(const model of ['graph-context-left','graph-context-right']){
    for(const [kind,total]of [['inference',5],['count',6],['compact',1]])assert.equal(requests.get(`main:${model}:${kind}`),total);
    for(const mode of ['cancel','recover'])for(const [kind,total]of [['inference',1],['count',1]])assert.equal(requests.get(`${mode}:${model}:${kind}`),total);
  }
  assert.equal([...requests.values()].reduce((a,b)=>a+b,0),32,'Exact attempts distinguish inference, maintenance and counting without retries');successful=true;
}catch(error){
  await writeFile(join(fixture,'private-failure.log'),String(peerFailure?.stack??error.stack??'contract failed'),{flag:'wx',mode:0o600});
  if(nativeFailure)await writeFile(join(fixture,'private-native.json'),JSON.stringify({stdout:nativeFailure.stdout??'',stderr:nativeFailure.stderr??'',code:nativeFailure.code??null,signal:nativeFailure.signal??null}),{flag:'wx',mode:0o600});
  console.error('context_graph_engine_peer_failed; disposable diagnostics retained');process.exitCode=1;
}finally{
  server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
  if(successful){assert.equal(dirname(resolve(fixture)),resolve(tmpdir()));assert.ok(basename(fixture).startsWith('xmind-context-graph-'));await rm(fixture,{recursive:true,force:true});}
}
