// Synthetic inference protocol only. The tested native runner executes actual
// workspace reads. This fixture is not live-model validation or a product path.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [executable,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-')),workspace=join(folder,'workspace');
let failure,requests=0;
const server=createServer((request,response)=>{
  let source='';request.on('data',data=>{source+=data;});request.on('end',()=>{
    try {
      requests++;assert.equal(request.headers.authorization,'Bearer engine-protocol-test-not-a-real-key');
      assert.ok(!source.includes('engine-protocol-test-not-a-real-key'));
      const body=JSON.parse(source);assert.equal(body.model,'fixture-deployment');assert.equal(body.stream,true);
      assert.deepEqual(body.tools.map(item=>item.function.name),['read_repository_instructions','read_file','list_files','search_files']);
      if(request.url==='/error') {response.writeHead(429,{'Content-Type':'application/json'});response.end('{"error":"do-not-log-provider-body"}');return;}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      if(request.url==='/claim'||request.url==='/delay') {response.write('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic pending stream'}}]})+'\n\n');return;}
      const last=body.messages.at(-1);
      if(last.role==='tool') {
        const result=JSON.parse(last.content);
        if(request.url==='/denied') assert.equal(result.error.code,'access_denied');
        else assert.equal(result.content,'Actual file content written by fixture\n');
        response.write('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic inference peer accepted the actual tool result.'},finish_reason:'stop'}]})+'\n\n');
      } else {
        const path=request.url==='/denied'?'../outside.txt':'README.md';
        response.write('data: '+JSON.stringify({choices:[{index:0,delta:{tool_calls:[{index:0,id:`call-${requests}`,type:'function',function:{name:'read_file',arguments:JSON.stringify({path})}}]},finish_reason:'tool_calls'}]})+'\n\n');
      }
      response.write('data: '+JSON.stringify({choices:[],usage:{prompt_tokens:8,completion_tokens:5,total_tokens:13}})+'\n\n');response.end('data: [DONE]\n\n');
    } catch(error) {failure=error;response.writeHead(400);response.end();}
  });
});
try {
  await mkdir(workspace);await writeFile(join(workspace,'README.md'),'Actual file content written by fixture\n');
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const result=await execute(executable,[join(folder,'state.sqlite'),modules,stdlib,workspace,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:30000});
  if(failure) throw failure;assert.equal(requests,9);process.stdout.write(result.stdout);
} finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));await rm(folder,{recursive:true,force:true});}
