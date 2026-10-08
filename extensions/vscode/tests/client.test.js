'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const { BackendClient } = require('../client');
test('provider enrollment accepts only matching approved OpenAI wire and endpoint pairs',()=>{
  const {providerEnrollmentWire}=require('../client'),base={provider:'openai',revision:1,endpoint:'https://api.openai.com/v1/chat/completions'};
  assert.equal(providerEnrollmentWire(base),'chat-completions');assert.equal(providerEnrollmentWire({...base,wire:'responses',endpoint:'https://api.openai.com/v1/responses'}),'responses');
  for(const changed of [{wire:'responses'},{endpoint:'https://api.openai.com/v1/responses'},{wire:'unknown'},{endpoint:'https://unapproved.invalid/v1/responses',wire:'responses'},{revision:-1}])assert.throws(()=>providerEnrollmentWire({...base,...changed}),/policy/);
});
const token = 'native-client-contract-token-32-bytes';
const profileState=()=>({revision:1,active:'openai',profiles:[{id:'openai',route_id:'openai.responses',provider:'openai',model:'fixture-model',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]});
test('native provider profile client preserves route, key omission, activation and revision at its authenticated origin',async()=>{
 const requests=[];const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options,body:options.body?JSON.parse(options.body):undefined});return {ok:true,json:async()=>url.endsWith('/models')?{models:[{id:'fixture-model'}]}:profileState()};});
 await client.providerProfiles();await client.discoverProfileModels('openai','openai.responses',undefined,1);await client.saveProviderProfile('claude','anthropic.messages','fixture-claude','synthetic-claude-key',1,true);await client.selectProviderProfile('openai',2);
 assert.deepEqual(requests.map(request=>request.url),['/v1/provider/profiles','/v1/provider/profiles/models','/v1/provider/profiles','/v1/provider/profiles/select'].map(path=>'http://127.0.0.1:8765'+path));
 assert.deepEqual(requests[1].body,{id:'openai',route_id:'openai.responses',expected_revision:1});assert.deepEqual(requests[2].body,{id:'claude',route_id:'anthropic.messages',model:'fixture-claude',api_key:'synthetic-claude-key',expected_revision:1,activate:true});assert.deepEqual(requests[3].body,{id:'openai',expected_revision:2});
 assert.ok(requests.every(request=>request.options.headers.Authorization==='Bearer '+token&&request.options.redirect==='error'));assert.ok(!requests.some(request=>request.url.includes('synthetic-claude-key')));
});
test('profile metadata rejects secret references, duplicate identities and inconsistent backend route families',async()=>{
 const variants=[state=>{state.credential_id='private';},state=>{state.profiles[0].api_key='private';},state=>{state.profiles.push({...state.profiles[0]});},state=>{state.routes[0].provider='anthropic';},state=>{state.active='unknown';},state=>{state.revision=0;},state=>{state.routes[0].wire='unrecognized';},state=>{state.profiles[0].model='sk-private';},state=>{state.profiles[0].revision=2;}];
 for(const modify of variants){const state=profileState();modify(state);const client=new BackendClient('http://localhost:8765',()=>token,async()=>({ok:true,json:async()=>state}));await assert.rejects(client.providerProfiles());}
 const state=profileState();state.profiles[0].model='';const client=new BackendClient('http://localhost:8765',()=>token,async()=>({ok:true,json:async()=>state}));assert.equal((await client.providerProfiles()).profiles[0].model,'','Trusted key-only migration metadata must remain repairable');
});
test('invalid profile mutations never leave the client and discovery rejects key reflection',async()=>{
 let sent=0;const client=new BackendClient('http://localhost:8765',()=>token,async()=>{sent++;return {ok:true,json:async()=>({models:[{id:'prefix-fixture-private-key'}]})};});
 await assert.rejects(client.selectProviderProfile('../profile?',1));await assert.rejects(client.selectProviderProfile('openai',Number.MAX_SAFE_INTEGER+1));await assert.rejects(client.saveProviderProfile('openai','openai.responses','fixture-model','key with space',0));await assert.rejects(client.saveProviderProfile('openai','openai.responses','fixture-private-key','fixture-private-key',0));assert.equal(sent,0);
 await assert.rejects(client.discoverProfileModels('openai','openai.responses','fixture-private-key',0));assert.equal(sent,1);
});
test('graph access adapter preserves backend identity, revision and raw human JSON',async()=>{
 const requests=[];const client=new BackendClient('http://127.0.0.1:8765',()=>token,async(url,options)=>{requests.push({url,body:options.body?JSON.parse(options.body):undefined});return {ok:true,json:async()=>({})};});
 await client.graphRun('session','workflow',3,'Task');await client.graphInput('root','answer.step','{"answer":1,"answer":2}',7);await client.graphChildHistory('root','child/opaque');
 assert.deepEqual(requests[0].body,{session_id:'session',graph_id:'workflow',graph_revision:3,prompt:'Task'});assert.deepEqual(requests[1].body,{input_json:'{"answer":1,"answer":2}',expected_checkpoint_revision:7});assert.ok(requests[2].url.endsWith('/v1/graph-runs/root/children/child%2Fopaque/history'));
});

test('session discovery, URL encoding, error handling and origin boundaries', async () => {
  const server = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json');
    if (req.headers.authorization !== `Bearer ${token}`) { res.statusCode = 401; res.end('{"detail":"Authentication required"}'); return; }
    if (req.url === '/v1/sessions') res.end(JSON.stringify([{ id: 'session-1', title: '<script>untrusted</script>' }]));
    else if (req.url === '/v1/runs/run%2F1/events?after=12') res.end(JSON.stringify([{ seq: 13, kind: 'run.completed' }]));
    else { res.statusCode = 409; res.end(JSON.stringify({ detail: 'Session already active' })); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const client = new BackendClient(`http://127.0.0.1:${server.address().port}`, () => token);
    assert.equal((await client.sessions())[0].title, '<script>untrusted</script>');
    assert.equal((await client.events('run/1', 12))[0].seq, 13);
    await assert.rejects(client.cancel('active'), /Session already active/);
    assert.throws(() => new BackendClient('https://remote.example'), /loopback/);
    assert.throws(() => new BackendClient('http://user:password@localhost'), /loopback/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('origin-scoped authentication and native request bodies', async () => {
  let current = token;
  const requests = [];
  const client = new BackendClient('http://localhost:8765', async () => current, async (url,options) => {
    requests.push({url,options});return {ok:true,json:async()=>({state:'queued'})};
  });
  await client.run('session','Real prompt');
  assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/runs');
  assert.equal(requests[0].options.headers.Authorization,`Bearer ${token}`);
  assert.deepEqual(JSON.parse(requests[0].options.body),{session_id:'session',prompt:'Real prompt'});
  current = 'rotated-native-client-token-32-bytes';
  await client.cancel('run');
  assert.equal(requests[1].options.headers.Authorization,`Bearer ${current}`);
  assert.equal(requests[1].options.body,'{}');
  assert.equal(requests[1].options.redirect,'error');
  current = 'invalid';
  await assert.rejects(client.sessions(),/authentication token/);
  assert.equal(requests.length,2,'Invalid token must not leave the extension host');
  for (const url of ['http://localhost/path','http://localhost?query=1','http://localhost/#fragment','http://[::1]:8765']) {
    assert.throws(()=>new BackendClient(url,()=>token),/origin/);
  }
});
