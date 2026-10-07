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
let failure,requests=0;
const peer=createServer((request,response)=>{
  let source='';request.on('data',data=>{source+=data;});request.on('end',()=>{
    try {
      ++requests;const body=JSON.parse(source);
      assert.equal(body.model,'synthetic-service-protocol');assert.equal(body.stream,true);
      assert.equal(body.messages.at(-1).content,'hold-stream');
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.write('data: {"choices":[{"index":0,"delta":{"content":"Synthetic pending inference stream"}}]}\n\n');
    } catch(error) {failure=error;response.destroy();}
  });
});
try {
  await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  const result=await execute(executable,[join(folder,'state.sqlite'),modules,stdlib,`http://127.0.0.1:${peer.address().port}/chat`],{windowsHide:true,timeout:15000});
  if(failure) throw failure;assert.equal(requests,2);process.stdout.write(result.stdout);
} finally {peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));await rm(folder,{recursive:true,force:true});}
