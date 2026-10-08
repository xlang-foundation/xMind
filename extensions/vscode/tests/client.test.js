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
test('agent and graph requests preserve the observed profile binding without silently acquiring a newer revision',async()=>{
 const sent=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push(JSON.parse(options.body));return {ok:true,json:async()=>({})};});
 const binding={provider_profile_id:'openai-account',expected_provider_revision:7};await client.run('session','Task','same-model',binding);await client.graphRun('session','review',2,'Review','same-model',binding);assert.equal(sent[0].provider_profile_id,'openai-account');assert.equal(sent[0].expected_provider_revision,7);assert.equal(sent[1].expected_provider_revision,7);assert.throws(()=>client.run('session','Task','same-model',{provider_profile_id:'openai-account'}));assert.equal(sent.length,2);
 await client.graphRun('session','review',2,'Model-free',undefined,{provider_profile_id:'',expected_provider_revision:0});assert.equal(sent[2].provider_profile_id,'');
});
const profileState=()=>({revision:1,active:'openai',profiles:[{id:'openai',route_id:'openai.responses',provider:'openai',model:'fixture-model',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]});
test('client accepts only an advertised GenerateContent route and carries its native wire through profile admission',async()=>{
 const {ProviderProfileController,providerEnrollmentWire}=require('../client'),state=profileState();state.active='fixture-gemini';state.profiles.push({id:state.active,route_id:'fixture.custom-gemini',provider:'gemini',model:'fixture-gemini-model',revision:1});state.routes.push({id:'fixture.custom-gemini',provider:'gemini',wire:'gemini-generate-content',discovery:true});
 const requests=[],posted=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>state};}),controller=new ProviderProfileController(client,message=>posted.push(message));
 try{await controller.refresh();assert.deepEqual(controller.state,state);assert.equal(controller.wire('fixture-gemini'),'gemini-generate-content');assert.deepEqual(await controller.admission(),{provider_profile_id:'fixture-gemini',expected_provider_revision:1});assert.equal(requests.length,1);assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/provider/profiles');assert.equal(requests[0].options.headers.Authorization,'Bearer '+token);assert.equal(posted[0].routes.length,3);assert.equal(profileState().routes.some(route=>route.wire==='gemini-generate-content'),false,'Client must not add an unavailable default route');
 assert.throws(()=>providerEnrollmentWire({provider:'gemini',wire:'gemini-generate-content',revision:1,endpoint:'https://generativelanguage.googleapis.com/v1beta'}),/policy/,'Legacy OpenAI enrollment must not acquire a new provider policy');
 for(const bad of [value=>{value.routes[2].wire='gemini_generate_content';},value=>{value.routes[2].api_key='synthetic-private-key';},value=>{value.profiles[1].provider='google';}]){const changed=structuredClone(state);bad(changed);const rejected=new BackendClient('http://localhost:8765',()=>token,async()=>({ok:true,json:async()=>changed}));await assert.rejects(rejected.providerProfiles());}
 }finally{controller.dispose();}
});
test('native provider profile client preserves route, key omission, activation and revision at its authenticated origin',async()=>{
 const requests=[];const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options,body:options.body?JSON.parse(options.body):undefined});return {ok:true,json:async()=>url.endsWith('/models')?{models:[{id:'fixture-model'}]}:profileState()};});
 await client.providerProfiles();await client.discoverProfileModels('openai','openai.responses',undefined,1);await client.saveProviderProfile('claude','anthropic.messages','fixture-claude','synthetic-claude-key',1,true);await client.selectProviderProfile('openai',2);
 assert.deepEqual(requests.map(request=>request.url),['/v1/provider/profiles','/v1/provider/profiles/models','/v1/provider/profiles','/v1/provider/profiles/select'].map(path=>'http://127.0.0.1:8765'+path));
 assert.deepEqual(requests[1].body,{id:'openai',route_id:'openai.responses',expected_revision:1});assert.deepEqual(requests[2].body,{id:'claude',route_id:'anthropic.messages',model:'fixture-claude',api_key:'synthetic-claude-key',expected_revision:1,activate:true});assert.deepEqual(requests[3].body,{id:'openai',expected_revision:2});
 assert.ok(requests.every(request=>request.options.headers.Authorization==='Bearer '+token&&request.options.redirect==='error'));assert.ok(!requests.some(request=>request.url.includes('synthetic-claude-key')));
});

test('advertised Gemini Settings discovery keeps full model resources and publishes a profile only after footer selection',async()=>{
 // Host/API fixtures verify the thin adapter. The native backend owns encrypted
 // credential storage; this fixture does not claim encryption or live inference.
 const {ProviderProfileController}=require('../client'),key='synthetic-gemini-settings-key',model='models/fixture-gemini',alternate='models/fixture-gemini-next',requests=[],posted=[];
 let state=profileState();state.routes.push({id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',discovery:true});
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{
  const body=options.body?JSON.parse(options.body):undefined;requests.push({url,options,body});
  if(url.endsWith('/profiles/models'))return {ok:true,json:async()=>({models:[{id:model},{id:alternate}]})};
  if(body){assert.equal(body.route_id,'gemini.generate-content');assert.ok([model,alternate].includes(body.model));assert.equal(body.expected_revision,state.revision);state={...state,revision:state.revision+1,active:body.id,profiles:[state.profiles[0],{id:body.id,route_id:body.route_id,provider:'gemini',model:body.model,revision:state.revision+1}]};}
  return {ok:true,json:async()=>structuredClone(state)};
 }),controller=new ProviderProfileController(client,value=>posted.push(value));
 try{
  await controller.refresh();await controller.discover(key,'','gemini.generate-content');
  assert.equal(requests.length,2,'Discovery cannot save or activate a draft profile');assert.equal(controller.state.active,'openai');
  const discovery=requests[1];assert.equal(discovery.url,'http://127.0.0.1:8765/v1/provider/profiles/models');assert.match(discovery.body.id,/^gemini-[0-9a-f-]{36}$/);assert.deepEqual(discovery.body,{id:discovery.body.id,route_id:'gemini.generate-content',api_key:key,expected_revision:1});
  assert.deepEqual(posted.findLast(value=>value.type==='model-list').models,[{id:model},{id:alternate}]);assert.equal(controller.draft.key,key);assert.ok(Number.isFinite(controller.draft.expires));assert.ok(!JSON.stringify(posted).includes(key),'The renderer receives model metadata without the private draft key');
  await assert.rejects(controller.save('fixture-gemini'),/returned model/);assert.equal(requests.length,2,'A shortened or invented alias cannot replace the discovered resource');
  await controller.save(model);const saved=requests[2];assert.deepEqual(saved.body,{id:discovery.body.id,route_id:'gemini.generate-content',model,api_key:key,expected_revision:1,activate:true});assert.equal(controller.draft.key,undefined);assert.equal(controller.draft.expires,Infinity);
  assert.equal(controller.state.profiles[1].model,model);assert.deepEqual(await controller.admission(),{provider_profile_id:discovery.body.id,expected_provider_revision:2});assert.equal(controller.wire(discovery.body.id),'gemini-generate-content');assert.deepEqual(posted.findLast(value=>value.type==='model-list'),{type:'model-list',models:[{id:model},{id:alternate}],model});assert.deepEqual(posted.findLast(value=>value.type==='provider-wire'),{type:'provider-wire',wire:'gemini-generate-content'});
  await controller.save(alternate);assert.deepEqual(requests[3].body,{id:discovery.body.id,route_id:'gemini.generate-content',model:alternate,expected_revision:2,activate:true});assert.equal(controller.state.profiles[1].model,alternate);assert.equal(controller.state.profiles[0].model,'fixture-model');
  assert.ok(requests.every(request=>request.options.headers.Authorization==='Bearer '+token&&request.options.redirect==='error'));assert.ok(!requests.some(request=>request.url.includes(key)));assert.ok(!JSON.stringify(posted).includes(key));assert.ok(!JSON.stringify(controller.state).includes(key));
 }finally{controller.dispose();}
});

test('Gemini Settings cannot discover through absent, disabled or foreign advertised routes',async()=>{
 const {ProviderProfileController}=require('../client'),key='synthetic-unavailable-gemini-key';
 for(const variant of ['absent','disabled','foreign']){
  const state=profileState(),requests=[],posted=[];let profile='';
  if(variant!=='absent')state.routes.push({id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',discovery:variant!=='disabled'});
  if(variant==='foreign'){profile='saved-gemini';state.profiles.push({id:profile,route_id:'gemini.generate-content',provider:'gemini',model:'models/fixture-gemini',revision:1});}
  const client=new BackendClient('http://localhost:8765',()=>token,async(url)=>{requests.push(url);return {ok:true,json:async()=>structuredClone(state)};}),controller=new ProviderProfileController(client,value=>posted.push(value));
  try{await assert.rejects(controller.discover(key,profile,variant==='foreign'?'openai.responses':'gemini.generate-content'),/available route/);assert.deepEqual(requests,['http://127.0.0.1:8765/v1/provider/profiles']);assert.equal(controller.draft,undefined);assert.ok(!posted.some(value=>value.type==='model-list'));assert.ok(!JSON.stringify(posted).includes(key));}
  finally{controller.dispose();}
 }
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
