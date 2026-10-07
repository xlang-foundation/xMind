// Independent protocol/process fixture only. No product model or remote effect.
import assert from 'node:assert/strict';
import {createInterface} from 'node:readline';
import {spawn} from 'node:child_process';
const mode=process.argv[2];
if(mode==='exit') process.exit(7);
if(mode==='no-read'){process.stdin.pause();process.stdout.write(JSON.stringify({jsonrpc:'2.0',method:'fixture/ready'})+'\n');setInterval(()=>{},1000);}
else if(mode==='flood'){process.stdout.write(Buffer.alloc(2*1024*1024,120));setInterval(()=>{},1000);}
else {
  const input=createInterface({input:process.stdin});
  if(mode==='silent'){process.stdout.write(JSON.stringify({jsonrpc:'2.0',method:'fixture/ready'})+'\n');setInterval(()=>{},1000);}
  for await(const line of input){
    const request=JSON.parse(line);if(mode==='silent')continue;
    assert.equal(request.jsonrpc,'2.0');assert.equal(request.method,'server/discover');
    assert.equal(request.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    assert.deepEqual(request.params._meta['io.modelcontextprotocol/clientCapabilities'],{});
    assert.equal(process.env.XMIND_AUTH_TOKEN,undefined);assert.equal(process.env.XMIND_API_KEY,undefined);
    assert.equal(process.env.XMIND_PARENT_ONLY,undefined);assert.equal(process.env.XMIND_CHILD_ONLY,'configured-fixture');
    let descendant;
    if(mode==='descendant')descendant=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore',windowsHide:true});
    await new Promise(resolve=>process.stderr.write(Buffer.alloc(512*1024,120),resolve));
    const result={resultType:'complete',supportedVersions:['2026-07-28'],capabilities:{tools:{}},
      fixtureText:'你好 🌍\nfixture',arguments:process.argv.slice(3),childConfigured:true,...(descendant?{descendant:descendant.pid}:{})};
    const frame=Buffer.from(JSON.stringify({jsonrpc:'2.0',id:request.id,result})+'\n');
    // Split actual UTF-8 pipe output independently from the native decoder.
    const split=frame.indexOf(Buffer.from('🌍'))+1;
    process.stdout.write(frame.subarray(0,split));await new Promise(resolve=>setTimeout(resolve,10));process.stdout.write(frame.subarray(split));
  }
}
