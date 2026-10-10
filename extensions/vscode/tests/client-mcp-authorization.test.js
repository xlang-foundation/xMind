'use strict';
// Synthetic DTO/transport fixtures verify thin-client boundaries only.
const test=require('node:test'),assert=require('node:assert/strict');
const {BackendClient,validateMcpAuthorizationServers,validateMcpAuthorizationAttempt}=require('../client');
const token='synthetic-mcp-authorization-owner'.padEnd(64,'x');
const server=()=>({id:'tools.peer',config_revision:3,credential_revision:0,enabled:true,configured:true,state:'needs_login',expires_unix_ms:null});
const attempt=()=>({id:'login-1',server_id:'tools.peer',state:'discovering',config_revision:3,credential_revision:0,authorization_url:null,expires_unix_ms:9999999999999,reason:null,cancellation_requested:false});
test('MCP renewal uses exact existing-grant revision and rejects absent grants before dispatch',async()=>{
 const calls=[],client=new BackendClient('http://127.0.0.1:8765',()=>token,async(url,options)=>{calls.push({url,options});return {ok:true,json:async()=>({...attempt(),credential_revision:7,state:'failed',reason:'refresh_uncertain'})};});
 await assert.rejects(client.renewMcpAuthorization('tools.peer',3,0,'login-1'));assert.equal(calls.length,0);
 const result=await client.renewMcpAuthorization('tools.peer',3,7,'login-1');assert.equal(result.reason,'refresh_uncertain');assert.equal(calls[0].url,client.baseUrl+'/v1/mcp/authorization/renewals');assert.deepEqual(JSON.parse(calls[0].options.body),{server_id:'tools.peer',expected_config_revision:3,expected_credential_revision:7,request_id:'login-1'});
});
test('MCP setup client sends exact authenticated commands and binds observations to the request owner',async()=>{
  const seen=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{seen.push({url,options});return {ok:true,json:async()=>url.endsWith('/servers')?{servers:[server()]}:attempt()};});
  await client.mcpAuthorizationServers();await client.startMcpAuthorization('tools.peer',3,0,'login-1');await client.mcpAuthorization('login-1',{server_id:'tools.peer',config_revision:3,credential_revision:0});await client.cancelMcpAuthorization('login-1',{server_id:'tools.peer',config_revision:3,credential_revision:0});
  assert.deepEqual(seen.map(({url,options})=>[url.replace(client.baseUrl,''),options.method]),[['/v1/mcp/authorization/servers','GET'],['/v1/mcp/authorization/attempts','POST'],['/v1/mcp/authorization/attempts/login-1','GET'],['/v1/mcp/authorization/attempts/login-1/cancel','POST']]);
  assert.deepEqual(JSON.parse(seen[1].options.body),{server_id:'tools.peer',expected_config_revision:3,expected_credential_revision:0,request_id:'login-1'});assert.deepEqual(JSON.parse(seen[3].options.body),{});
  assert.ok(seen.every(({options})=>options.headers.Authorization==='Bearer '+token&&options.redirect==='error'));
});
test('MCP invalid destinations, identities and revisions never dispatch',async()=>{
  let calls=0;const client=new BackendClient('http://127.0.0.1:8765',()=>token,async()=>{calls++;throw new Error('unexpected dispatch');});
  for(const args of [['tools/peer',3,0,'login-1'],['tools.peer',0,0,'login-1'],['tools.peer',3,-1,'login-1'],['tools.peer',3,Number.MAX_SAFE_INTEGER+1,'login-1'],['tools.peer',3,0,'../login-1'],['tools.peer',3,0,'x'.repeat(129)]])await assert.rejects(client.startMcpAuthorization(...args));
  await assert.rejects(client.mcpAuthorization('foreign/id'));await assert.rejects(client.cancelMcpAuthorization('login?key'));assert.equal(calls,0);
});
test('MCP public metadata rejects private fields, duplicate servers and unsupported states',()=>{
  assert.equal(validateMcpAuthorizationServers({servers:[server()]}).servers[0].state,'needs_login');
  for(const mutate of [v=>v.servers[0].access_token='private',v=>v.servers.push(server()),v=>v.servers[0].credential_revision=-1,v=>v.servers[0].state='ready',v=>v.servers[0].enabled=1,v=>v.servers[0].expires_unix_ms=1.2]){const value={servers:[server()]};mutate(value);assert.throws(()=>validateMcpAuthorizationServers(value));}
  assert.throws(()=>validateMcpAuthorizationAttempt({...attempt(),refresh_token:'private'}));
});
test('MCP authorization links are HTTPS and confined to the actual callback-wait phase',()=>{
  const value={...attempt(),state:'awaiting_callback',authorization_url:'https://issuer.example.test/authorize?state=synthetic&code_challenge=synthetic'};assert.equal(validateMcpAuthorizationAttempt(value).state,'awaiting_callback');
  for(const url of ['http://issuer.example.test/authorize','javascript:alert(1)','https://name:secret@issuer.example.test/authorize','https://issuer.example.test/authorize#fragment','https://issuer.example.test/authorize\n','https://issuer.example.test/authorize x'])assert.throws(()=>validateMcpAuthorizationAttempt({...value,authorization_url:url}));
  assert.throws(()=>validateMcpAuthorizationAttempt({...value,authorization_url:null}));assert.throws(()=>validateMcpAuthorizationAttempt({...value,state:'connected',credential_revision:1}));
  assert.throws(()=>validateMcpAuthorizationAttempt({...attempt(),state:'failed',reason:'raw provider secret'}));
  assert.equal(validateMcpAuthorizationAttempt({...attempt(),state:'denied',reason:'access_denied'}).reason,'access_denied');
});
test('MCP responses cannot switch server, attempt or configuration, or invent a successful credential revision',async()=>{
  const binding={id:'login-1',server_id:'tools.peer',config_revision:3,credential_revision:0};
  for(const fields of [{id:'foreign'},{server_id:'foreign'},{config_revision:4},{credential_revision:1},{state:'connected',credential_revision:0}])assert.throws(()=>validateMcpAuthorizationAttempt({...attempt(),...fields},binding));
  assert.equal(validateMcpAuthorizationAttempt({...attempt(),state:'connected',credential_revision:1},binding).credential_revision,1);
  const client=new BackendClient('http://127.0.0.1:8765',()=>token,async()=>({ok:true,json:async()=>({...attempt(),id:'foreign'})}));await assert.rejects(client.startMcpAuthorization('tools.peer',3,0,'login-1'),/ownership/);
});
