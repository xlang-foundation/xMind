// Independent controlled native provider; seventeen intentional requests, no live key.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [executable,modules,stdlib]=process.argv.slice(2);
const expected={normal:1,cancel:1,crash:1,delivery:1,overflow:1,other_error:1,completion:1,completion_delivery:1,completion_rollback:1,runner:1,read:2,read_delivery:1,read_denied:1,read_cancel:1,read_turn_delivery:1,read_turn_rollback:1};
const counts=Object.fromEntries(Object.keys(expected).map(mode=>[mode,0]));
let failure;const aborted=new Set();
function chunk(response,mode,delta,finish=null,usage){response.write('data: '+JSON.stringify({id:`rpc-${mode}`,object:'chat.completion.chunk',model:'worker-rpc-fixture',choices:[{index:0,delta,finish_reason:finish}],...(usage?{usage}:{})})+'\n\n');}
const usage={prompt_tokens:7,completion_tokens:3,total_tokens:10};
const server=createServer((request,response)=>{
 let bytes='';request.on('data',b=>bytes+=b);request.on('end',()=>{try{
  const mode=request.url.slice(1);assert.ok(mode in counts);assert.ok(++counts[mode]<=expected[mode]);assert.equal(request.headers.authorization,'Bearer synthetic-model-rpc-not-live');assert.ok(!bytes.includes('synthetic-model-rpc-not-live'));
  const body=JSON.parse(bytes);assert.equal(body.model,'worker-rpc-fixture');assert.equal(body.stream,true);assert.equal(body.stream_options.include_usage,true);
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
  if(mode==='overflow'||mode==='other_error'){
   response.writeHead(400,{'content-type':'application/json'});response.end(JSON.stringify({error:{code:mode==='overflow'?'context_length_exceeded':'rate_limit_exceeded',type:'invalid_request_error',param:'messages',message:'private-provider-message must not cross the worker boundary'}}));return;
  }
  response.writeHead(200,{'content-type':'text/event-stream'});chunk(response,mode,{role:'assistant',content:`Actual native RPC ${mode}.`});
  if(mode==='cancel'||mode==='crash'){response.on('close',()=>aborted.add(mode));return;}
  chunk(response,mode,{},'stop',usage);response.end('data: [DONE]\n\n');
 }catch(error){failure=error;response.writeHead(500);response.end();}});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try{
 const result=await promisify(execFile)(executable,[modules,stdlib,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:60000,maxBuffer:2*1024*1024});
 if(failure)throw failure;assert.deepEqual(counts,expected);assert.ok(aborted.has('cancel')&&aborted.has('crash'));
 process.stdout.write(result.stdout);process.stdout.write('Independent peer observed exactly seventeen requests: read twice for canonical continuation, every other captured mode once; no rejected/revoked request reached HTTP.\n');
}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
