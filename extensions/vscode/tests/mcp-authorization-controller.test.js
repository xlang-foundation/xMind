'use strict';
// Explicit synthetic backend DTOs: no real OAuth login is claimed by these tests.
const test=require('node:test'),assert=require('node:assert/strict');
const {BackendClient,McpAuthorizationController}=require('../client');
const binding={id:'login-1',server_id:'tools.peer',config_revision:3,credential_revision:0};
const server={id:'tools.peer',config_revision:3,credential_revision:0,enabled:true,configured:true,state:'needs_login',expires_unix_ms:null};
const attempt=(fields={})=>({id:binding.id,server_id:binding.server_id,config_revision:3,credential_revision:0,state:'discovering',authorization_url:null,expires_unix_ms:9999999999999,reason:null,cancellation_requested:false,...fields});
function fixture(reply){
  const calls=[],messages=[],saved=[],scheduled=new Set();
  const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-mcp-controller-token'.padEnd(64,'x'),async(url,options)=>{const call={path:url.replace('http://127.0.0.1:8765',''),method:options.method,body:options.body&&JSON.parse(options.body)};calls.push(call);const data=await reply(call);return {ok:true,json:async()=>data};});
  const controller=new McpAuthorizationController(client,message=>messages.push(message),{save:entries=>saved.push(structuredClone(entries)),requestId:()=>binding.id,schedule:fn=>{scheduled.add(fn);return fn;},unschedule:fn=>scheduled.delete(fn)});
  return {controller,calls,messages,saved,scheduled};
}
test('MCP view restores only observation identity; reload never starts or replays login',async()=>{
  const f=fixture(({path})=>path.endsWith('/servers')?{servers:[server]}:attempt({state:'awaiting_callback',authorization_url:'https://issuer.example.test/authorize?state=synthetic'}));
  await f.controller.read([binding]);assert.deepEqual(f.calls.map(c=>c.method),['GET','GET']);assert.equal(f.scheduled.size,1);assert.deepEqual(f.saved.at(-1),[binding]);assert.ok(!JSON.stringify(f.saved).includes('authorization_url'));
  const opened=[];f.controller.open(server.id,url=>opened.push(url));assert.equal(opened.length,1);f.controller.dispose();assert.equal(f.scheduled.size,0);assert.ok(!f.calls.some(c=>c.path.endsWith('/cancel')),'Closing a view must only detach observation');
});
test('MCP view persists identity before admission and observes lost replies without retrying POST',async()=>{
  let posted=false;const f=fixture(({path,method})=>{if(path.endsWith('/servers'))return {servers:[server]};if(method==='POST'){assert.deepEqual(f.saved.at(-1),[binding]);posted=true;throw new Error('Synthetic lost response');}assert.ok(posted);return attempt();});
  await f.controller.read();await f.controller.start(server.id);assert.equal(f.calls.filter(c=>c.method==='POST').length,1);assert.deepEqual(f.saved.at(-1),[binding]);assert.equal(f.scheduled.size,1);
  await f.controller.read();assert.equal(f.calls.filter(c=>c.method==='POST').length,1);assert.equal(f.messages.at(-1).attempts[0].id,binding.id);f.controller.dispose();
});
test('MCP completion and cancellation follow backend outcomes; terminal grants are not cancelled by a view',async()=>{
  let state=attempt();const f=fixture(({path,method})=>path.endsWith('/servers')?{servers:[server]}:path.endsWith('/cancel')?state=attempt({state:'cancelled',reason:'cancelled',cancellation_requested:true}):state);
  await f.controller.read();await f.controller.start(server.id);await f.controller.cancel(server.id);assert.equal(f.messages.at(-1).attempts[0].state,'cancelled');assert.deepEqual(f.saved.at(-1),[]);assert.equal(f.scheduled.size,0);
  state=attempt({state:'connected',credential_revision:1});await f.controller.start(server.id);const before=f.calls.length;await f.controller.cancel(server.id);assert.equal(f.calls.length,before);assert.deepEqual(f.saved.at(-1),[]);f.controller.dispose();
});
test('MCP observation is retired on disposal and cannot open stale or terminal links',async()=>{
  let release;const f=fixture(({path})=>path.endsWith('/servers')?{servers:[server]}:new Promise(resolve=>release=resolve));const reading=f.controller.read([binding]);while(!release)await new Promise(resolve=>setImmediate(resolve));
  const count=f.messages.length;f.controller.dispose();release(attempt({state:'awaiting_callback',authorization_url:'https://issuer.example.test/authorize'}));await reading;assert.equal(f.messages.length,count);assert.equal(f.scheduled.size,0);assert.throws(()=>f.controller.open(server.id,()=>assert.fail('Must not open retired link')));
});
test('MCP missing attempts after backend restart are discarded without re-admission',async()=>{
  const f=fixture(({path})=>{if(path.endsWith('/servers'))return {servers:[server]};throw Object.assign(new Error('Synthetic missing attempt'),{status:404});});await f.controller.read([binding]);assert.deepEqual(f.saved.at(-1),[]);assert.equal(f.scheduled.size,0);assert.match(f.messages.at(-1).error,/Start a new login/);assert.ok(f.calls.every(c=>c.method==='GET'));f.controller.dispose();
});
