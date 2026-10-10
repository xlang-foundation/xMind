'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const { BackendClient } = require('../client');
const {SkillViewController,validateSessionSkills}=require('../client');
const skillWorkspace='windows-local-file-v1:synthetic-test-workspace',skillAuthority='a'.repeat(32);
const skillState=(ids=[],revision=0,editable=true)=>({session_id:'session',workspace_id:skillWorkspace,authority_id:skillAuthority,revision,ids,manual_ids:ids,editable});
const skillCatalogue={workspace_id:skillWorkspace,authority_id:skillAuthority,skills:[{id:'manual',name:'User guide',path:'.agents/skills/manual.md',model_invocable:false}]};
test('session skill client posts only exact revision, ids and workspace authority',async()=>{
 const requests=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>skillState(['manual'],1)};});
 const result=await client.replaceSessionSkills('session',['manual'],skillState());assert.equal(result.revision,1);assert.deepEqual(JSON.parse(requests[0].options.body),{ids:['manual'],expected_revision:0,expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority});assert.equal(requests[0].options.method,'POST');assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/sessions/session/skills');
 await assert.rejects(client.replaceSessionSkills('session',['manual','manual'],skillState()),/distinct/);await assert.rejects(client.replaceSessionSkills('session',[],skillState(['manual'],1,false)),/idle/);assert.equal(requests.length,1);assert.throws(()=>validateSessionSkills({...skillState(),manual_ids:['foreign']},'session'),/provenance/);
});
test('skill controller publishes acknowledged manual attachment and removal without model prompts',async()=>{
 let state=skillState();const commands=[],posted=[];const client={skills:async()=>skillCatalogue,sessionSkills:async()=>state,replaceSessionSkills:async(session,ids,previous)=>{commands.push({session,ids,previous});state=skillState(ids,previous.revision+1);return state;}};
 const controller=new SkillViewController(client,m=>posted.push(m),()=>({session:'session',generation:1,enabled:true}));await controller.read();await controller.change({session:'session',revision:0,ids:['manual']});await controller.change({session:'session',revision:1,ids:[]});assert.deepEqual(commands.map(c=>c.ids),[['manual'],[]]);assert.equal(posted.at(-1).selection.revision,2);assert.deepEqual(posted.at(-1).selection.ids,[]);assert.ok(posted.every(m=>m.type==='skills'));
 await assert.rejects(controller.change({session:'session',revision:1,ids:['manual']}),/Refresh/);await assert.rejects(controller.change({session:'session',revision:2,ids:['unknown']}),/catalogue/);assert.equal(commands.length,2);
});
test('skill reads discard responses from a previous conversation generation',async()=>{
 let release,scope={session:'session',generation:1,enabled:true};const posted=[];const controller=new SkillViewController({sessionSkills:()=>new Promise(yes=>release=yes),skills:async()=>skillCatalogue},m=>posted.push(m),()=>scope);
 const pending=controller.read();scope={session:'other',generation:2,enabled:true};controller.invalidate();release(skillState());await pending;assert.equal(controller.record,undefined);assert.ok(!posted.some(m=>m.type==='skills'));
});
test('skill acknowledgements cannot publish into a newly selected conversation',async()=>{
 let release,scope={session:'session',generation:1,enabled:true};const posted=[];const controller=new SkillViewController({sessionSkills:async()=>skillState(),skills:async()=>skillCatalogue,replaceSessionSkills:()=>new Promise(yes=>release=yes)},m=>posted.push(m),()=>scope);await controller.read();const pending=controller.change({session:'session',revision:0,ids:['manual']});scope={session:'other',generation:2,enabled:true};controller.invalidate();release(skillState(['manual'],1));await pending;assert.equal(controller.record,undefined);assert.equal(posted.filter(m=>m.type==='skills').length,1);
});
test('malformed catalogue metadata cannot enter a skill chooser, while source conflicts allow only clearing',async()=>{
 const invalid=new SkillViewController({sessionSkills:async()=>skillState(),skills:async()=>({...skillCatalogue,skills:[{...skillCatalogue.skills[0],body:'Untrusted injected guide'}]})},()=>{},()=>({session:'session',generation:1,enabled:true}));await assert.rejects(invalid.read(),/catalogue entry/);assert.equal(invalid.record,undefined);
 const posted=[];let changed=0;const conflict=Object.assign(new Error('Ambiguous current skill sources'),{status:409});const client={sessionSkills:async()=>skillState(['manual'],1),skills:async()=>{throw conflict;},replaceSessionSkills:async()=>{changed++;return skillState([],2);}};const controller=new SkillViewController(client,m=>posted.push(m),()=>({session:'session',generation:1,enabled:true}));await controller.read();assert.equal(posted.at(-1).catalogueError,conflict.message);await assert.rejects(controller.change({session:'session',revision:1,ids:['manual']}),/catalogue/);await controller.change({session:'session',revision:1,ids:[]});assert.equal(changed,1);assert.deepEqual(posted.at(-1).selection.ids,[]);
});
test('provider enrollment accepts only matching approved OpenAI wire and endpoint pairs',()=>{
  const {providerEnrollmentWire}=require('../client'),base={provider:'openai',revision:1,endpoint:'https://api.openai.com/v1/chat/completions'};
  assert.equal(providerEnrollmentWire(base),'chat-completions');assert.equal(providerEnrollmentWire({...base,wire:'responses',endpoint:'https://api.openai.com/v1/responses'}),'responses');
  for(const changed of [{wire:'responses'},{endpoint:'https://api.openai.com/v1/responses'},{wire:'unknown'},{endpoint:'https://unapproved.invalid/v1/responses',wire:'responses'},{revision:-1}])assert.throws(()=>providerEnrollmentWire({...base,...changed}),/policy/);
});
const token = 'native-client-contract-token-32-bytes';
// Synthetic native SSE frames: parser/ownership/cursor behavior only.
// No native runtime, provider or agent execution is supplied by these fixtures.
const streamRoot=(changed={})=>({id:'root',session_id:'session',state:'completed',parent_id:'',node_id:'',graph_root:false,...changed});
const streamEvent=(seq,run_id='root')=>({seq,run_id,kind:'model.text',data:{text:'Synthetic stream 雪'}});
const streamFrame=(type,data,id)=>`${id===undefined?'':'id: '+id+'\n'}event: ${type}\ndata: ${JSON.stringify(data)}\n\n`;
const streamWire=(events=[],{scope='run',after=0,run=streamRoot(),reason='terminal'}={})=>streamFrame('observation',{run,scope,after})+events.map(e=>streamFrame('committed',e,e.seq)).join('')+streamFrame('end',{reason,after:events.at(-1)?.seq??after});
function streamResponse(text,step=7){const bytes=new TextEncoder().encode(text);return new Response(new ReadableStream({start(controller){for(let i=0;i<bytes.length;i+=step)controller.enqueue(bytes.subarray(i,i+step));controller.close();}}),{headers:{'Content-Type':'text/event-stream'}});}
test('committed stream reader handles split UTF-8 and CRLF using one authenticated read-only request',async()=>{
 const observed=[],sent=[],events=[streamEvent(1),streamEvent(2)],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return streamResponse(': keepalive\r\n\r\n'+streamWire(events).replaceAll('\n','\r\n'),1);});
 const result=await client.eventStream('root',{session_id:'session',onEvent:e=>observed.push(e)});assert.deepEqual(observed,events);assert.equal(result.reason,'terminal');assert.equal(result.cursor,2);assert.equal(result.observation.run.id,'root');assert.equal(sent.length,1);assert.equal(sent[0].url,'http://127.0.0.1:8765/v1/runs/root/events/stream?after=0');assert.equal(sent[0].options.method,'GET');assert.equal(sent[0].options.headers.Authorization,'Bearer '+token);assert.equal(sent[0].options.body,undefined);assert.equal(sent[0].options.redirect,'error');
});
test('graph stream validates owned child conversation before delivering its event',async()=>{
 const sent=[],observed=[],events=[streamEvent(1,'child')],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});if(url.endsWith('/children'))return new Response(JSON.stringify([streamRoot({id:'child',parent_id:'root',node_id:'worker'})]),{headers:{'Content-Type':'application/json'}});return streamResponse(streamWire(events,{scope:'graph',run:streamRoot({graph_root:true})}));});
 assert.equal((await client.eventStream('root',{scope:'graph',session_id:'session',onEvent:e=>observed.push(e)})).cursor,1);assert.deepEqual(observed,events);assert.deepEqual(sent.map(s=>new URL(s.url).pathname),['/v1/graph-runs/root/events/stream','/v1/graph-runs/root/children']);assert.ok(sent.every(s=>s.options.method==='GET'&&s.options.body===undefined));
 for(const children of [[],[streamRoot({id:'child',parent_id:'foreign',node_id:'worker'})],[streamRoot({id:'child',parent_id:'root',session_id:'foreign',node_id:'worker'})]]){const delivered=[],invalid=new BackendClient('http://localhost:8765',()=>token,async url=>url.endsWith('/children')?new Response(JSON.stringify(children)):streamResponse(streamWire(events,{scope:'graph',run:streamRoot({graph_root:true})})));await assert.rejects(invalid.eventStream('root',{scope:'graph',session_id:'session',onEvent:e=>delivered.push(e)}),error=>/Unowned|identity/.test(error.message)&&error.eventCursor===0);assert.deepEqual(delivered,[]);}
});
test('stream rejects changed root/session/scope and events without an initial observation',async()=>{
 for(const source of [streamWire([streamEvent(1)],{run:streamRoot({id:'foreign'})}),streamWire([streamEvent(1)],{run:streamRoot({session_id:'foreign'})}),streamWire([streamEvent(1)],{scope:'tree'}),streamFrame('committed',streamEvent(1),1)]){const events=[],client=new BackendClient('http://localhost:8765',()=>token,async()=>streamResponse(source));await assert.rejects(client.eventStream('root',{session_id:'session',onEvent:e=>events.push(e)}),error=>/identity|observation/.test(error.message)&&error.eventCursor===0);assert.deepEqual(events,[]);}
});
test('stream cursor advances only after delivery and rejects duplicate IDs and conflicting end cursors',async()=>{
 const initial=streamFrame('observation',{run:streamRoot(),scope:'run',after:0});
 for(const [source,cursor,count]of [[initial+streamFrame('committed',streamEvent(1),'01'),0,0],[initial+streamFrame('committed',streamEvent(1),2),0,0],[streamWire([streamEvent(1),streamEvent(1)]),1,1],[initial+streamFrame('committed',streamEvent(1),1)+streamFrame('end',{reason:'terminal',after:4}),1,1]]){let delivered=0;const client=new BackendClient('http://localhost:8765',()=>token,async()=>streamResponse(source));await assert.rejects(client.eventStream('root',{session_id:'session',onEvent:()=>delivered++}),error=>/cursor/.test(error.message)&&error.eventCursor===cursor);assert.equal(delivered,count);}
});
test('stream validates admission values and an abort during token lookup cannot send a request',async()=>{
 let fetched=0,tokens=0;const client=new BackendClient('http://localhost:8765',()=>{tokens++;return token;},async()=>{fetched++;throw Error('No request permitted');});
 for(const changed of [{after:-1},{after:1.5},{after:Number.MAX_SAFE_INTEGER+1},{scope:'unknown'},{session_id:'foreign/session'}])await assert.rejects(client.eventStream('root',{session_id:'session',onEvent(){},...changed}));assert.equal(tokens,0);assert.equal(fetched,0);
 let release;const lookup=new Promise(yes=>release=yes),abort=new AbortController(),pendingClient=new BackendClient('http://localhost:8765',()=>lookup,async()=>{fetched++;throw Error('Retired connection must not fetch');}),pending=pendingClient.eventStream('root',{session_id:'session',signal:abort.signal,onEvent(){}});abort.abort();release(token);await assert.rejects(pending,{name:'AbortError'});assert.equal(fetched,0);
});
test('stream detach preserves delivered cursor and a rejected callback never acknowledges its event',async()=>{
 const abort=new AbortController(),sent=[],source=streamWire([streamEvent(1),streamEvent(2)]),client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return streamResponse(source);});let delivered=0;
 await assert.rejects(client.eventStream('root',{session_id:'session',signal:abort.signal,onEvent(){delivered++;abort.abort();}}),error=>error.name==='AbortError'&&error.eventCursor===1);assert.equal(delivered,1);assert.equal(sent.length,1);assert.equal(sent[0].options.method,'GET');
 await assert.rejects(client.eventStream('root',{session_id:'session',onEvent(){throw Error('Consumer rejected delivery');}}),error=>error.message==='Consumer rejected delivery'&&error.eventCursor===0);assert.equal(sent.length,2);
});
test('EOF, malformed JSON and unsupported frames retain the last accepted cursor',async()=>{
 const initial=streamFrame('observation',{run:streamRoot(),scope:'run',after:0}),committed=streamFrame('committed',streamEvent(1),1);
 for(const tail of ['', 'event: committed\ndata: {\n\n','event: unknown\ndata: {}\n\n','event: end\nevent: end\ndata: {}\n\n']){const events=[],client=new BackendClient('http://localhost:8765',()=>token,async()=>streamResponse(initial+committed+tail));await assert.rejects(client.eventStream('root',{session_id:'session',onEvent:e=>events.push(e)}),error=>error.eventCursor===1);assert.equal(events.length,1);}
});
test('resume uses the accepted scoped cursor and HTTP rejection does not trigger a fallback or command',async()=>{
 const sent=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return streamResponse(streamWire([streamEvent(8)],{scope:'tree',after:7,reason:'reconnect'}));});const result=await client.eventStream('root',{session_id:'session',scope:'tree',after:7,onEvent(){}});assert.equal(result.reason,'reconnect');assert.equal(result.cursor,8);assert.equal(sent[0].url,'http://127.0.0.1:8765/v1/runs/root/tree-events/stream?after=7');
 let rejectedCalls=0;const rejected=new BackendClient('http://localhost:8765',()=>token,async()=>{rejectedCalls++;return new Response(JSON.stringify({detail:'Stream busy'}),{status:503,headers:{'Content-Type':'application/json'}});});await assert.rejects(rejected.eventStream('root',{session_id:'session',onEvent(){throw Error('No event permitted');}}),error=>error.status===503);assert.equal(rejectedCalls,1);
});
test('workspace skill inspection uses the native read-only catalogue without inventing activation',async()=>{
 const catalogue={workspace_id:'synthetic-workspace',authority_id:'a'.repeat(32),skills:[{id:'disabled',model_invocable:false}]},sent=[];
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return {ok:true,json:async()=>catalogue};});
 assert.deepEqual(await client.skills(),catalogue);assert.equal(sent.length,1);assert.equal(sent[0].url,'http://127.0.0.1:8765/v1/workspace/skills');assert.equal(sent[0].options.method,'GET');assert.equal(sent[0].options.body,undefined);
});
test('owned-child client keeps parent scope and validates the committed tree cursor',async()=>{
 const sent=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return {ok:true,json:async()=>[]};});
 await client.ownedChildren('parent');await client.ownedChildHistory('parent','opaque/child');await client.treeEvents('parent',12);await client.delegation();
 assert.deepEqual(sent.map(value=>value.url),['/v1/runs/parent/children','/v1/runs/parent/children/opaque%2Fchild/history','/v1/runs/parent/tree-events?after=12','/v1/agent/delegation'].map(path=>'http://127.0.0.1:8765'+path));
 assert.ok(sent.every(value=>value.options.method==='GET'&&value.options.headers.Authorization==='Bearer '+token));for(const cursor of [-1,1.5,NaN,Number.MAX_SAFE_INTEGER+1])assert.throws(()=>client.treeEvents('parent',cursor));assert.equal(sent.length,4);
});
test('owned observation covers concurrent admission and paged events without inventing aggregate usage',async()=>{
 const {observeOwnedRun}=require('../client'),run={id:'parent',session_id:'session',parent_id:'',graph_root:false,state:'completed'},child={run:{id:'leaf',parent_id:'parent',session_id:'session',state:'completed',graph_root:false},kind:'delegated_leaf',batch_id:'batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1},calls=[];
 const history=[{role:'assistant',data:{content:'Synthetic observed leaf fixture',usage:{prompt_tokens:4,completion_tokens:2}}}];
 const client={treeEvents:async(id,after)=>{calls.push(['events',after]);return Array.from({length:after===0?256:2},(_,index)=>({seq:after+index+1,run_id:index%2?'leaf':'parent',kind:'model.usage',data:{prompt_tokens:4}}));},ownedChildren:async()=>{calls.push(['children']);return [child];},ownedChildHistory:async(parent,id)=>{calls.push(['history',parent,id]);return history;},operations:async()=>[]};
 const result=await observeOwnedRun(client,run,'session',0);assert.deepEqual(calls.slice(0,3),[['events',0],['events',256],['children']]);assert.equal(result.cursor,258);assert.equal(result.caughtUp,true);assert.deepEqual(result.histories.leaf,history);assert.equal(result.usage,undefined);assert.equal(history[0].data.usage.total_tokens,undefined);
 const bounded=await observeOwnedRun({...client,treeEvents:async(id,after)=>Array.from({length:256},(_,index)=>({seq:after+index+1,run_id:'parent',kind:'model.text',data:{text:'synthetic'}}))},run,'session',0);assert.equal(bounded.cursor,4096);assert.equal(bounded.caughtUp,false,'A full bounded page cannot be mistaken for a caught-up terminal cursor');
});
test('owned observation rejects foreign children and events before requesting private child history',async()=>{
 const {observeOwnedRun}=require('../client'),run={id:'parent',session_id:'session'},child={run:{id:'leaf',parent_id:'parent',session_id:'session'},kind:'delegated_leaf',batch_id:'batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1};let histories=0;
 const base={treeEvents:async()=>[],ownedChildren:async()=>[child],ownedChildHistory:async()=>{histories++;return [];},operations:async()=>[]};
 await assert.rejects(observeOwnedRun({...base,ownedChildren:async()=>[{...child,run:{...child.run,session_id:'foreign'}}]},run,'session',0),/identity/);
 await assert.rejects(observeOwnedRun({...base,treeEvents:async()=>[{seq:1,run_id:'foreign',kind:'model.text'}]},run,'session',0),/Unowned/);assert.equal(histories,0);
});
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
  await controller.save(model);assert.equal(requests[2].options.method,'GET');assert.equal(requests[2].url,'http://127.0.0.1:8765/v1/provider/profiles');assert.equal(requests[2].body,undefined);const saved=requests[3];assert.deepEqual(saved.body,{id:discovery.body.id,route_id:'gemini.generate-content',model,api_key:key,expected_revision:1,activate:true});assert.equal(controller.draft.key,undefined);assert.equal(controller.draft.expires,Infinity);
  assert.equal(controller.state.profiles[1].model,model);assert.deepEqual(await controller.admission(),{provider_profile_id:discovery.body.id,expected_provider_revision:2});assert.equal(controller.wire(discovery.body.id),'gemini-generate-content');assert.deepEqual(posted.findLast(value=>value.type==='model-list'),{type:'model-list',models:[{id:model},{id:alternate}],model});assert.deepEqual(posted.findLast(value=>value.type==='provider-wire'),{type:'provider-wire',wire:'gemini-generate-content'});
  await controller.save(alternate);assert.equal(requests.length,6);assert.equal(requests[4].options.method,'GET');assert.equal(requests[4].url,'http://127.0.0.1:8765/v1/provider/profiles');assert.equal(requests[4].body,undefined);assert.deepEqual(requests[5].body,{id:discovery.body.id,route_id:'gemini.generate-content',model:alternate,expected_revision:2,activate:true});assert.equal(controller.state.profiles[1].model,alternate);assert.equal(controller.state.profiles[0].model,'fixture-model');
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
