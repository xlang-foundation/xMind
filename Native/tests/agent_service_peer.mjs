// Synthetic streaming peer; verifies native worker lifecycle, not live inference.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,rm} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [executable,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
const folder=await mkdtemp(join(tmpdir(),'xmind-service-'));
let failure,requests=0,profiledRequests=0;
const peer=createServer((request,response)=>{
  let source='';request.on('data',data=>{source+=data;});request.on('end',()=>{
    try {
      ++requests;const body=JSON.parse(source);
      assert.equal(body.model,'synthetic-service-protocol');assert.equal(body.stream,true);
      assert.equal(body.messages.at(-1).content,'hold-stream');
      if(body.messages.some(message=>message.role==='system'&&message.content.includes('service-profile-instructions'))){++profiledRequests;assert.ok(body.messages.some(message=>message.role==='system'&&message.content.includes('Selected agent instructions (trusted backend snapshot):')),'Selected profile guidance must enter the actual provider request as trusted system instructions');}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.write('data: {"choices":[{"index":0,"delta":{"content":"Synthetic pending inference stream"}}]}\n\n');
    } catch(error) {failure=error;response.destroy();}
  });
});
try {
  await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  const result=await execute(executable,[join(folder,'state.sqlite'),modules,stdlib,`http://127.0.0.1:${peer.address().port}/chat`],{windowsHide:true,timeout:15000});
  if(failure) throw failure;assert.equal(requests,6,'Two concurrent held streams and four actual notifier dispatch/cancel cycles');assert.equal(profiledRequests,1,'The selected reusable agent must affect exactly its own provider request');process.stdout.write(result.stdout);
} finally {peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));await rm(folder,{recursive:true,force:true});}
