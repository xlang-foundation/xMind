// Independent synthetic wire peer. No inference or product model implementation.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const execute=promisify(execFile),routes=[];
let failure;
const server=createServer((request,response)=>{
  routes.push(request.url);let source='';request.on('data',data=>{source+=data;});
  request.on('end',()=>{
    try {
      assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer provider-protocol-test-not-a-real-key');
      const body=JSON.parse(source);
      assert.equal(body.model,'fixture-deployment');assert.equal(body.stream,true);assert.equal(body.n,1);
      assert.deepEqual(body.messages,[{role:'user',content:'Protocol fixture'}]);
      assert.equal(body.tools[0].function.name,'read_file');assert.equal(body.tools[0].function.parameters.required[0],'path');
      assert.equal(body.stream_options.include_usage,true);assert.equal(body.max_completion_tokens,64);
      assert.ok(!source.includes('provider-protocol-test-not-a-real-key'),'Credential must not enter model request body');
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      if(request.url==='/incomplete') {
        response.end('data: '+JSON.stringify({choices:[{index:0,delta:{content:'partial'}}]})+'\n\n');return;
      }
      const name=request.url==='/unknown'?'unoffered_tool':'read_file';
      const chunks=[
        {choices:[{index:0,delta:{tool_calls:[{index:0,id:'call-1',type:'function',function:{name,arguments:'{"path":'}}]}}]},
        {choices:[{index:0,delta:{tool_calls:[{index:0,function:{arguments:'"README"}'}}]},finish_reason:'tool_calls'}]},
        {choices:[],usage:{prompt_tokens:8,completion_tokens:5,total_tokens:13}}
      ];
      for(const chunk of chunks) response.write('data: '+JSON.stringify(chunk)+'\n\n');
      response.end('data: [DONE]\n\n');
    } catch(error) {failure=error;response.writeHead(400);response.end();}
  });
});
try {
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const result=await execute(process.argv[2],[`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:15000});
  if(failure) throw failure;
  assert.deepEqual(routes,['/chat','/unknown','/incomplete']);
  process.stdout.write(result.stdout);
} finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
