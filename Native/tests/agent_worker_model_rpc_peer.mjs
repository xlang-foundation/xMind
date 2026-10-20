// Independent controlled native provider; twenty-seven intentional requests, no live key.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [executable,modules,stdlib]=process.argv.slice(2);
const expected={normal:1,cancel:1,crash:1,delivery:1,overflow:1,other_error:1,completion:1,completion_delivery:1,completion_rollback:1,runner:1,runner_error:1,runner_cancel:1,read:2,read_delivery:1,read_denied:1,read_cancel:1,read_turn_delivery:1,read_turn_rollback:1,read_context:3,runner_read:3,runner_read_cancel:2};
const counts=Object.fromEntries(Object.keys(expected).map(mode=>[mode,0]));
let failure;const aborted=new Set();
function chunk(response,mode,delta,finish=null,usage){response.write('data: '+JSON.stringify({id:`rpc-${mode}`,object:'chat.completion.chunk',model:'worker-rpc-fixture',choices:[{index:0,delta,finish_reason:finish}],...(usage?{usage}:{})})+'\n\n');}
const usage={prompt_tokens:7,completion_tokens:3,total_tokens:10};
const server=createServer((request,response)=>{
 let bytes='';request.on('data',b=>bytes+=b);request.on('end',()=>{try{
  const mode=request.url.slice(1);assert.ok(mode in counts);assert.ok(++counts[mode]<=expected[mode]);assert.equal(request.headers.authorization,'Bearer synthetic-model-rpc-not-live');assert.ok(!bytes.includes('synthetic-model-rpc-not-live'));
  const body=JSON.parse(bytes);assert.equal(body.model,'worker-rpc-fixture');assert.equal(body.stream,true);assert.equal(body.stream_options.include_usage,true);
  if(mode==='read_context'||mode==='runner_read'||mode==='runner_read_cancel'){
   const phase=counts[mode];assert.equal(body.messages.length,phase===1?2:phase===2?5:7);assert.equal(body.messages[0].role,'system');assert.match(body.messages[0].content,/ROOT_CONTEXT_GUIDANCE_\d+/);assert.ok(body.messages[0].content.includes('context-check'));assert.deepEqual(body.messages[1],{role:'user',content:`Native child model RPC ${mode}.`});assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');
   if(phase===1)assert.ok(!body.messages[0].content.includes('NESTED_CONTEXT_GUIDANCE_'));
   else{assert.match(body.messages[0].content,/NESTED_CONTEXT_GUIDANCE_\d+/);assert.equal(body.messages[2].role,'assistant');assert.deepEqual(body.messages[2].tool_calls.map(call=>call.id),['context-first','context-second']);for(const [index,id]of [[3,'context-first'],[4,'context-second']]){assert.equal(body.messages[index].role,'tool');assert.equal(body.messages[index].tool_call_id,id);assert.equal(JSON.parse(body.messages[index].content).error.code,'repository_instructions_required');}}
   if(phase===3){assert.equal(body.messages[5].role,'assistant');assert.equal(body.messages[5].tool_calls[0].id,'context-third');assert.equal(body.messages[6].role,'tool');assert.equal(body.messages[6].tool_call_id,'context-third');assert.match(JSON.parse(body.messages[6].content).content,/^native-context-bytes-\d+\n$/);}
   response.writeHead(200,{'content-type':'text/event-stream'});
   if(phase<3){const ids=phase===1?['context-first','context-second']:['context-third'];chunk(response,mode,{role:'assistant',tool_calls:ids.map((id,index)=>({index,id,type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'nested/same.txt'})}}))},'tool_calls',usage);}
   else{chunk(response,mode,{role:'assistant',content:'Context verified.'});chunk(response,mode,{},'stop',usage);}response.end('data: [DONE]\n\n');return;
  }
  if(mode==='read'&&counts[mode]===2){
   assert.equal(body.messages.length,3);assert.deepEqual(body.messages[0],{role:'user',content:'Native child model RPC read.'});
   assert.equal(body.messages[1].role,'assistant');assert.deepEqual(body.messages[1].tool_calls,[{id:'actual-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'same.txt'})}}]);
   assert.equal(body.messages[2].role,'tool');assert.equal(body.messages[2].tool_call_id,'actual-read');assert.match(JSON.parse(body.messages[2].content).content,/^actual-native-read-\d+\n$/);
   assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');
   response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:'Read turn recorded.'});chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');return;
  }
  assert.deepEqual(body.messages,[{role:'user',content:`Native child model RPC ${mode}.`}]);
  if(mode.startsWith('read')){
   assert.equal(body.tools.length,1);assert.equal(body.tools[0].function.name,'read_file');response.writeHead(200,{'content-type':'text/event-stream'});
   chunk(response,mode,{role:'assistant',tool_calls:[{index:0,id:'actual-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'same.txt'})}}]},'tool_calls',usage);response.end('data: [DONE]\n\n');return;
  }
  if(mode==='overflow'||mode==='other_error'||mode==='runner_error'){
   response.writeHead(400,{'content-type':'application/json'});response.end(JSON.stringify({error:{code:mode==='overflow'?'context_length_exceeded':'rate_limit_exceeded',type:'invalid_request_error',param:'messages',message:'private-provider-message must not cross the worker boundary'}}));return;
  }
  response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:`Actual native RPC ${mode}.`});
  if(mode==='cancel'||mode==='crash'||mode==='runner_cancel'){response.on('close',()=>aborted.add(mode));return;}
  chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');
 }catch(error){failure=error;response.writeHead(500);response.end();}});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try{
 const result=await promisify(execFile)(executable,[modules,stdlib,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:60000,maxBuffer:2*1024*1024}).catch(error=>{if(failure)throw failure;throw error;});
 if(failure)throw failure;assert.deepEqual(counts,expected);assert.ok(aborted.has('cancel')&&aborted.has('crash')&&aborted.has('runner_cancel'));
 process.stdout.write(result.stdout);process.stdout.write('Independent peer observed exactly twenty-seven requests: two read AgentRunner approval-cancellation windows, one actual AgentRunner cancellation and one provider failure, three actual read AgentRunner windows, three direct context windows, read twice for canonical continuation, every other captured mode once; no changed/rejected/revoked request reached HTTP.\n');
}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
