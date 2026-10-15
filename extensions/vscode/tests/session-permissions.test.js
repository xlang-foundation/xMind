'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {BackendClient}=require('../client');

const pending={session_id:'session-1',requests:[{id:'request-1',run_id:'run-1',workspace_id:'workspace-1',tool:'read_file',arguments_json:'{"path":"README.md"}',state:'awaiting_approval',expires_unix_ms:9007199254740991}]};

test('session permission requests are validated and replies remain bound to their session and request',async()=>{
 const calls=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async(url,options)=>{calls.push({url,options});const value=options.method==='POST'?{...pending.requests[0],state:JSON.parse(options.body).decision==='allow'?'ready':'denied'}:pending;return {ok:true,json:async()=>value};});
 assert.deepEqual(await client.sessionPermissions('session-1'),pending);
 assert.equal(calls[0].url,'http://127.0.0.1:8765/v1/sessions/session-1/permissions');assert.equal(calls[0].options.method,'GET');
 const denied=await client.replySessionPermission('session-1','request-1','deny');assert.equal(denied.state,'denied');
 assert.equal(calls[1].url,'http://127.0.0.1:8765/v1/sessions/session-1/permissions/request-1/reply');assert.deepEqual(JSON.parse(calls[1].options.body),{decision:'deny'});
 const allowed=await client.replySessionPermission('session-1','request-1','allow');assert.equal(allowed.state,'ready');assert.deepEqual(JSON.parse(calls[2].options.body),{decision:'allow'});
});

test('session permission replies reject malformed identities, decisions and response ownership',async()=>{
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async()=>({ok:true,json:async()=>({...pending,session_id:'foreign'})}));
 await assert.rejects(client.sessionPermissions('session-1'),/Invalid pending session/);
 await assert.rejects(client.sessionPermissions('../foreign'),/Invalid execution identity/);
 await assert.rejects(client.replySessionPermission('session-1','request-1','always'),/must be allow or deny/);
 const wrong=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async()=>({ok:true,json:async()=>({...pending.requests[0],id:'other',state:'denied'})}));
 await assert.rejects(wrong.replySessionPermission('session-1','request-1','deny'),/did not settle/);
});
