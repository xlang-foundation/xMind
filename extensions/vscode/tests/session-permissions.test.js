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
 await client.replySessionPermission('session-1','request-1','allow',true);assert.deepEqual(JSON.parse(calls[3].options.body),{decision:'allow',grant_resource:true});
});

test('session permission replies reject malformed identities, decisions and response ownership',async()=>{
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async()=>({ok:true,json:async()=>({...pending,session_id:'foreign'})}));
 await assert.rejects(client.sessionPermissions('session-1'),/Invalid pending session/);
 await assert.rejects(client.sessionPermissions('../foreign'),/Invalid execution identity/);
 await assert.rejects(client.replySessionPermission('session-1','request-1','always'),/must be allow or deny/);
 const wrong=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async()=>({ok:true,json:async()=>({...pending.requests[0],id:'other',state:'denied'})}));
 await assert.rejects(wrong.replySessionPermission('session-1','request-1','deny'),/did not settle/);
 await assert.rejects(client.replySessionPermission('session-1','request-1','deny',true),/Invalid permission grant choice/);
});

test('saved session permission grants validate scope and use revision-checked revocation',async()=>{
 const calls=[],grant={id:'grant_a',workspace_id:'workspace-1',policy_binding:'policy-1',action:'read_file',resource:'src/main.cpp',created_unix_ms:1234};
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async(url,options)=>{calls.push({url,options});if(options.method==='DELETE')return {ok:true,json:async()=>({session_id:'session-1',revision:5,grants:[]})};return {ok:true,json:async()=>({session_id:'session-1',revision:4,grants:[grant]})};});
 assert.deepEqual(await client.sessionPermissionGrants('session-1'),{session_id:'session-1',revision:4,grants:[grant]});
 const result=await client.revokeSessionPermissionGrant('session-1','grant_a',4);assert.equal(result.revision,5);
 assert.equal(calls[0].options.method,'GET');assert.equal(calls[0].url,'http://127.0.0.1:8765/v1/sessions/session-1/permission-grants');
 assert.equal(calls[1].options.method,'DELETE');assert.deepEqual(JSON.parse(calls[1].options.body),{expected_revision:4});
 await assert.rejects(client.revokeSessionPermissionGrant('session-1','grant_a',-1),/Invalid saved permission revision/);
 const malformed=new BackendClient('http://127.0.0.1:8765',()=> 'session-permission-test-token-0123456789',async()=>({ok:true,json:async()=>({session_id:'session-1',revision:0,grants:[{...grant,workspace_id:''}]})}));
 await assert.rejects(malformed.sessionPermissionGrants('session-1'),/Invalid saved session permission grants/);
});
