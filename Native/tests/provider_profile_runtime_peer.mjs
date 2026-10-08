// Independent synthetic model peer; the native runtime performs all execution,
// persistence and key resolution. This does not establish live account support.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-profile-runtime-'));
let failure;const observed=[];
const event=value=>`event: ${value.type}\ndata: ${JSON.stringify(value)}\n\n`;
const server=createServer((request,response)=>{
  let raw='';request.on('data',data=>{raw+=data;if(raw.length>65536)request.destroy();});
  request.on('end',()=>{try{
    assert.equal(request.method,'POST');assert.ok(!raw.includes('runtime-openai-fixture-key')&&!raw.includes('runtime-claude-fixture-key'));
    const body=JSON.parse(raw);assert.equal(body.stream,true);observed.push({route:request.url,model:body.model});
    if(request.url==='/chat'){
      assert.equal(request.headers.authorization,'Bearer runtime-openai-fixture-key');assert.equal(request.headers['x-api-key'],undefined);
      assert.ok(['fixture-openai','fixture-openai-updated'].includes(body.model));assert.ok(body.messages.some(message=>message.role==='user'));
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      const completion=`data: ${JSON.stringify({choices:[{index:0,delta:{content:'Actual OpenAI fixture reply'},finish_reason:'stop'}],usage:{prompt_tokens:3,completion_tokens:4,total_tokens:7}})}\n\ndata: [DONE]\n\n`;
      // Keep the first real native request in flight for the ownership assertion.
      if(observed.length===1)setTimeout(()=>response.end(completion),100);
      else response.end(completion);
    }else{
      assert.equal(request.url,'/messages');assert.equal(request.headers.authorization,undefined);
      assert.equal(request.headers['x-api-key'],'runtime-claude-fixture-key');assert.equal(request.headers['anthropic-version'],'2023-06-01');
      assert.equal(body.model,'fixture-claude');assert.equal(body.max_tokens,64);assert.ok(body.messages.some(message=>message.role==='user'));
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.write(event({type:'message_start',message:{id:'msg-profile-fixture',type:'message',role:'assistant',model:body.model,content:[],stop_reason:null,stop_sequence:null,usage:{input_tokens:3,output_tokens:0}}}));
      response.write(event({type:'content_block_start',index:0,content_block:{type:'text',text:''}}));
      response.write(event({type:'content_block_delta',index:0,delta:{type:'text_delta',text:'Actual Claude fixture reply'}}));
      response.write(event({type:'content_block_stop',index:0}));
      response.write(event({type:'message_delta',delta:{stop_reason:'end_turn',stop_sequence:null},usage:{output_tokens:4}}));
      response.end(event({type:'message_stop'}));
    }
  }catch(error){failure=error;response.writeHead(500);response.end('Synthetic profile assertion failed');}});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try{
  const [binary,modules,stdlib]=process.argv.slice(2);
  const result=await execute(binary,[root,modules,stdlib,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:35000});
  if(failure)throw failure;
  assert.deepEqual(observed,[{route:'/chat',model:'fixture-openai'},{route:'/messages',model:'fixture-claude'},{route:'/chat',model:'fixture-openai-updated'},{route:'/messages',model:'fixture-claude'}]);
  process.stdout.write(result.stdout);
}finally{await new Promise(resolve=>server.close(resolve));await rm(root,{recursive:true,force:true});}
