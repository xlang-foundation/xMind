'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {BackendClient,validateSessionModel}=require('../client');

const token='session-model-test-token-0123456789abcdef';
const empty={session_id:'session-1',revision:0,editable:true,selected:null};
const selected={session_id:'session-1',revision:1,editable:true,selected:{model_id:'claude-sonnet-4-5',provider_profile_id:'claude-team',provider_route_id:'anthropic.messages'}};

test('session model reads and writes are session-scoped and revisioned',async()=>{
 const requests=[],client=new BackendClient('http://127.0.0.1:8765',()=>token,async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>options.method==='POST'?selected:empty};});
 assert.deepEqual(await client.sessionModel('session-1'),empty);
 assert.deepEqual(await client.replaceSessionModel('session-1','claude-sonnet-4-5',{provider_profile_id:'claude-team',expected_provider_revision:7},empty),selected);
 assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/sessions/session-1/model');
 assert.equal(requests[0].options.method,'GET');
 assert.deepEqual(JSON.parse(requests[1].options.body),{model_id:'claude-sonnet-4-5',provider_profile_id:'claude-team',expected_provider_revision:7,expected_revision:0});
});

test('session model state rejects foreign sessions, secrets and unbound provider routes',()=>{
 assert.throws(()=>validateSessionModel({...empty,session_id:'foreign'},'session-1'),/state/);
 assert.throws(()=>validateSessionModel({...selected,selected:{...selected.selected,model_id:'sk-secret'}},'session-1'),/provider profile identity/);
 assert.throws(()=>validateSessionModel({...selected,selected:{...selected.selected,provider_route_id:'https://outside.invalid'}},'session-1'),/provider route identity/);
});
