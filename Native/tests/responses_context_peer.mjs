// Independent synthetic OpenAI-shaped HTTP peer. Native sockets/codecs are
// real; opaque state, provider replies and token counts are deliberately synthetic.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const execute=promisify(execFile);
const [executable]=process.argv.slice(2);
assert.ok(executable,'Native context contract executable is required');
const canonical=String.raw`[
 {"id":"msg_original","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Synthetic original task. 🌍"}]},
 {"id":"msg_continue","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Continue the synthetic task."}]},
 {"id":"cmp_retained","type":"compaction","encrypted_content":"synthetic-opaque-\u0061","retained_metadata":{"decimal":1.00000000000000000001,"i\u0064":"escaped","large":18446744073709551617}}
]`;
const recanonical=String.raw`[
 {"id":"msg_original","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Synthetic original task. 🌍"}]},
 {"id":"msg_continue","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Continue the synthetic task."}]},
 {"id":"msg_followup","type":"message","role":"user","status":"completed","content":[{"type":"input_text","text":"Synthetic follow-up after compact."}]},
 {"id":"cmp_recompacted","type":"compaction","encrypted_content":"synthetic-next-opaque-\u0061","retained_metadata":{"decimal":1.00000000000000000001}}
]`;
const compact='{"id":"resp_synthetic_context","object":"response.compaction","created_at":1764967971,"output":'+canonical+',"usage":{"input_tokens":19,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":2},"output_tokens":7,"output_tokens_details":{"reasoning_tokens":3},"total_tokens":26}}';
const recompacted=compact.replace(canonical,recanonical);
const requests=new Map();let redirected=0,peerFailure,ordinaryInput;
const expectedTools=[{type:'function',name:'read_file',description:'Synthetic read definition.',parameters:{type:'object',properties:{path:{type:'string'}},required:['path'],additionalProperties:false},strict:false}];
function observeInput(body,raw,projected){
  assert.equal(body.model,'synthetic-responses-context-model');
  if(projected){
    assert.ok(raw.includes(canonical.slice(1,-1)),'Canonical output must be replayed raw, without opaque/escaped-key/number normalization or pruning');
    assert.deepEqual(body.input.filter(item=>item.type==='message'&&!item.id).map(item=>({role:item.role,text:item.content[0].text})),[
      {role:'system',text:'Synthetic repository instructions.'},{role:'developer',text:'Synthetic backend instructions.'},{role:'user',text:'Synthetic follow-up after compact.'}
    ]);
    assert.deepEqual(body.input.filter(item=>item.id).map(item=>item.id),['msg_original','msg_continue','cmp_retained']);
    assert.equal(body.input.find(item=>item.id==='cmp_retained').encrypted_content,'synthetic-opaque-a');
  }else{
    assert.deepEqual(body.input.map(item=>item.type),['message','message','function_call','function_call_output','message','message','message']);
    assert.equal(body.input[0].role,'system');assert.equal(body.input[0].content[0].text,'Synthetic repository instructions.');
    assert.equal(body.input[2].call_id,'call_original');assert.equal(body.input[2].arguments,String.raw`{"pa\u0074h":"README.md","decimal":1.00000000000000000001}`);
    assert.equal(body.input[3].call_id,'call_original');assert.equal(body.input[3].output,'Synthetic original file result.');
    const expected=['Synthetic legacy plain answer with quote " and slash \\.',String.raw`{"source":"graph_join","outputs":{"read":{"content":"Synthetic graph data\n"}}}`];
    for(const [offset,text]of expected.entries())assert.deepEqual(body.input[4+offset],{type:'message',role:'assistant',content:[{type:'output_text',text}]},'Plain historical text must use output_text without invented IDs or provider items');
    for(const index of [0,1,6])assert.equal(body.input[index].content[0].type,'input_text','Trusted/user content remains input_text');
    ordinaryInput??=structuredClone(body.input);assert.deepEqual(body.input,ordinaryInput,'Inference/count/compact share the exact same original conversation payload');
  }
}
function stream(response,inputTokens=27,answer='Synthetic compact continuation.'){
  response.writeHead(200,{'Content-Type':'text/event-stream'});let sequence=0;
  const send=(type,values)=>response.write('event: '+type+'\ndata: '+JSON.stringify({type,sequence_number:sequence++,...values})+'\n\n');
  const reasoning={id:'rs_context',type:'reasoning',summary:[],encrypted_content:'synthetic-context-done-opaque'};
  const message={id:'msg_context',type:'message',role:'assistant',status:'completed',phase:'final_answer',content:[{type:'output_text',text:answer,annotations:[]}]};
  send('response.created',{response:{id:'resp_context_followup',model:'synthetic-responses-context-model',status:'in_progress'}});
  send('response.output_item.added',{output_index:0,item:{id:reasoning.id,type:'reasoning',summary:[]}});
  send('response.output_item.done',{output_index:0,item:reasoning});
  send('response.output_item.added',{output_index:1,item:{id:message.id,type:'message',role:'assistant',content:[]}});
  send('response.content_part.added',{output_index:1,item_id:message.id,content_index:0,part:{type:'output_text',text:''}});
  send('response.output_text.delta',{output_index:1,item_id:message.id,content_index:0,delta:message.content[0].text});
  send('response.output_text.done',{output_index:1,item_id:message.id,content_index:0,text:message.content[0].text});
  send('response.content_part.done',{output_index:1,item_id:message.id,content_index:0,part:message.content[0]});
  send('response.output_item.done',{output_index:1,item:message});
  send('response.completed',{response:{id:'resp_context_followup',model:'synthetic-responses-context-model',status:'completed',output:[reasoning,message],usage:{input_tokens:inputTokens,output_tokens:5,total_tokens:inputTokens+5}}});response.end();
}
const server=createServer((request,response)=>{
  request.on('error',()=>{});response.on('error',()=>{});
  try{
    if(request.url==='/redirect-target'){redirected++;response.writeHead(500);response.end();return;}
    requests.set(request.url,(requests.get(request.url)??0)+1);
    assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer context-fixture-token-not-a-real-key');
    assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['x-goog-api-key'],undefined);assert.equal(request.headers['anthropic-version'],undefined);assert.equal(request.headers['content-type'],'application/json');
    const chunks=[];request.on('data',value=>chunks.push(value));request.on('end',()=>{
      try{
        const raw=Buffer.concat(chunks).toString('utf8'),body=JSON.parse(raw),projected=request.url.startsWith('/continuation/')||request.url.startsWith('/after-failure/');observeInput(body,raw,projected);
        const counting=request.url.endsWith('/input_tokens'),streaming=request.url==='/continuation/responses'||request.url==='/initial/responses';
        if(counting){assert.deepEqual(Object.keys(body).sort(),['input','model','reasoning','tools']);assert.deepEqual(body.tools,expectedTools);assert.deepEqual(body.reasoning,{effort:'medium'});assert.equal(request.headers.accept,'application/json');}
        else if(streaming){assert.equal(body.stream,true);assert.equal(body.store,false);assert.deepEqual(body.include,['reasoning.encrypted_content']);assert.equal(body.max_output_tokens,128);assert.deepEqual(body.tools,expectedTools);assert.equal(request.headers.accept,'text/event-stream');stream(response,projected?27:43,projected?'Synthetic compact continuation.':'Synthetic plain-history continuation.');return;}
        else {assert.ok(request.url.endsWith('/compact'));assert.deepEqual(Object.keys(body).sort(),['input','model']);assert.equal(request.headers.accept,'application/json');}
        if(request.url.startsWith('/redirect/')){response.writeHead(302,{Location:`http://127.0.0.1:${server.address().port}/redirect-target`});response.end();return;}
        if(request.url.startsWith('/limit/')){response.writeHead(400,{'Content-Type':'application/json'});response.end('{"error":{"type":"invalid_request_error","code":"context_length_exceeded","param":"input","message":"private-context-fixture"}}');return;}
        if(request.url.startsWith('/cancel/')||request.url.startsWith('/timeout/'))return;
        response.writeHead(200,{'Content-Type':'application/json; charset=utf-8'});
        if(request.url.startsWith('/unsupported/')){response.end('{"id":"resp_unsupported","object":"response.compaction","created_at":1,"output":[{"id":"cmp_future","type":"future_opaque_item","encrypted_content":"private-synthetic-future"}]}');return;}
        if(request.url.startsWith('/duplicate/')){response.end('{"id":"resp_duplicate","object":"response.compaction","created_at":1,"output":[{"id":"cmp","type":"compaction","encrypted_content":"synthetic","encrypted_\\u0063ontent":"other"}]}');return;}
        if(request.url.startsWith('/user-drift/')){response.end(compact.replace('Synthetic original task. 🌍','Invented synthetic user authority.'));return;}
        if(request.url.startsWith('/invalid-count/')){response.end('{"object":"response.input_tokens","input_tokens":1.5}');return;}
        if(counting){response.end('{"object":"response.input_tokens","input_tokens":'+(projected?27:43)+'}');return;}
        const bytes=Buffer.from(projected?recompacted:compact),split=bytes.indexOf(Buffer.from('🌍'))+2;response.write(bytes.subarray(0,split));setTimeout(()=>response.end(bytes.subarray(split)),5);
      }catch(error){peerFailure??=error;response.writeHead(500);response.end();}
    });
  }catch(error){peerFailure??=error;response.writeHead(500);response.end();}
});
try{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const result=await execute(executable,[`http://127.0.0.1:${server.address().port}`],{timeout:20000,windowsHide:true});
  if(peerFailure)throw peerFailure;
  assert.equal(redirected,0,'Native context adapters must not follow redirects or forward credentials');
  assert.deepEqual(Object.fromEntries(requests),{
    '/initial/responses/input_tokens':1,'/initial/responses':1,'/initial/responses/compact':1,
    '/continuation/responses/input_tokens':1,'/continuation/responses':1,'/continuation/responses/compact':1,
    '/unsupported/responses/compact':1,'/invalid-count/responses/input_tokens':1,'/duplicate/responses/compact':1,'/user-drift/responses/compact':1,
    '/redirect/responses/compact':1,'/limit/responses/compact':1,'/cancel/responses/compact':1,
    '/timeout/responses/input_tokens':1,'/after-failure/responses/input_tokens':1
  },'Exact independent native HTTP attempts prove no retries and no dispatch of malformed routes/pre-cancelled work');
  process.stdout.write(result.stdout);
}catch(error){if(peerFailure)throw peerFailure;throw error;}
finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
