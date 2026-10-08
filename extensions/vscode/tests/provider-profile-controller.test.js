'use strict';
// Host-controller fixtures only; actual native API/persistence has independent
// contracts. These tests never install credentials into the product preview.
const test=require('node:test'),assert=require('node:assert/strict');
const {ProviderProfileController}=require('../client');
const registry=()=>({revision:4,active:'openai',profiles:[{id:'openai',route_id:'openai.responses',provider:'openai',model:'fixture-openai',revision:4}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'openai.chat',provider:'openai',wire:'chat-completions',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]});

test('saved key-only provider selection is a native CAS followed by discovery, with model enrollment only on explicit selection',async()=>{
 let state=registry();state={...state,profiles:[...state.profiles,{id:'claude-key-only',route_id:'anthropic.messages',provider:'anthropic',model:'',revision:1}]};const calls=[],posted=[];
 const controller=new ProviderProfileController({providerProfiles:async()=>state,selectProviderProfile:async(...args)=>{calls.push(['select',...args]);state={...state,revision:5,active:args[0]};return state;},discoverProfileModels:async(...args)=>{calls.push(['discover',...args]);return {models:[{id:'fixture-claude'}]};},saveProviderProfile:async(...args)=>{calls.push(['save',...args]);state={...state,revision:6,profiles:state.profiles.map(value=>value.id===args[0]?{...value,model:args[2],revision:2}:value)};return state;}},value=>posted.push(value));
 try{await controller.refresh();await controller.select('claude-key-only');assert.deepEqual(calls,[['select','claude-key-only',4]]);assert.equal(state.profiles[1].model,'');await controller.discover();assert.deepEqual(calls[1],['discover','claude-key-only','anthropic.messages',undefined,5]);assert.equal(calls.length,2);assert.equal(Object.hasOwn(controller.savedCatalogue,'key'),false);controller.invalidate();await controller.save('fixture-claude');assert.deepEqual(calls[2],['save','claude-key-only','anthropic.messages','fixture-claude',undefined,5,true]);assert.equal(state.profiles[0].model,'fixture-openai');assert.equal(posted.findLast(value=>value.type==='provider-wire').wire,'anthropic-messages');}finally{controller.dispose();}
});

test('conflicting provider selection reads fresh metadata once without replaying the native selection',async()=>{
 let state=registry(),calls=0;const posted=[],controller=new ProviderProfileController({providerProfiles:async()=>state,discoverProfileModels:async()=>({models:[{id:'discovered-only'}]}),selectProviderProfile:async()=>{calls++;state={...state,revision:5,active:''};throw Object.assign(new Error('Provider revision conflict'),{status:409});}},value=>posted.push(value));
 try{await controller.discover();await assert.rejects(controller.select('openai'),error=>error.status===409);assert.equal(calls,1);assert.equal(controller.savedCatalogue,undefined);assert.equal(controller.state.revision,5);assert.equal(posted.findLast(value=>value.type==='provider-profiles').active,'');}finally{controller.dispose();}
});

test('saved-credential discovery survives close and conversation discard, with exact native CAS and no retained key',async()=>{
 let state=registry();const posted=[],calls=[],models=Array.from({length:135},(_,n)=>({id:'fixture-model-'+n}));
 const client={baseUrl:'http://127.0.0.1:8765',providerProfiles:async()=>state,discoverProfileModels:async(...args)=>{calls.push(['discover',...args]);return {models};},saveProviderProfile:async(...args)=>{calls.push(['save',...args]);state={...state,revision:state.revision+1,profiles:[{...state.profiles[0],model:args[2],revision:state.profiles[0].revision+1}]};return state;}};
 const controller=new ProviderProfileController(client,value=>posted.push(value));
 try{await controller.discover();assert.equal(controller.savedCatalogue.ids.length,135);assert.equal(Object.hasOwn(controller.savedCatalogue,'key'),false);controller.invalidate();assert.equal(controller.draft,undefined);assert.equal(posted.at(-1).models.length,135);
  await controller.refresh();controller.invalidate();await controller.save('fixture-model-134');assert.deepEqual(calls,[['discover','openai','openai.responses',undefined,4],['save','openai','openai.responses','fixture-model-134',undefined,4,true]]);assert.equal(posted.findLast(value=>value.type==='model-list').models.length,135);
  controller.invalidate();await controller.save('fixture-model-133');assert.equal(calls.length,3);assert.equal(calls[2][5],5);assert.equal(controller.state.revision,6);assert.equal(controller.savedCatalogue.profile_revision,6);assert.equal(Object.hasOwn(controller.savedCatalogue,'key'),false);
 }finally{controller.dispose();}assert.equal(controller.savedCatalogue,undefined);
});

test('external profile, registry, route or backend identity changes retire saved catalogue without a model write',async()=>{
 for(const change of [s=>({...s,revision:5}),s=>({...s,active:''}),s=>({...s,profiles:[{...s.profiles[0],revision:5}]}),s=>({...s,profiles:[{...s.profiles[0],route_id:'openai.chat'}]}),s=>({...s,routes:s.routes.map(r=>r.id==='openai.responses'?{...r,wire:'chat-completions'}:r)})]){
  let state=registry(),writes=0;const controller=new ProviderProfileController({providerProfiles:async()=>state,discoverProfileModels:async()=>({models:[{id:'discovered-only'}]}),saveProviderProfile:async()=>{writes++;}},()=>{});
  try{await controller.discover();controller.invalidate();state=change(state);await assert.rejects(controller.save('discovered-only'),/settings changed/);assert.equal(writes,0);assert.equal(controller.savedCatalogue,undefined);assert.equal(controller.draft,undefined);}finally{controller.dispose();}
 }
 let writes=0;const first={baseUrl:'http://127.0.0.1:8765',providerProfiles:async()=>registry(),discoverProfileModels:async()=>({models:[{id:'discovered-only'}]}),saveProviderProfile:async()=>{writes++;}},controller=new ProviderProfileController(first,()=>{});
 try{await controller.discover();controller.client={...first};await controller.refresh();assert.equal(controller.savedCatalogue,undefined);assert.equal(await controller.save('discovered-only'),false);assert.equal(writes,0);}finally{controller.dispose();}
});

test('unsaved keys never enter retained catalogue and discard or expiry removes their selection authority',async()=>{
 const posted=[],writes=[],controller=new ProviderProfileController({providerProfiles:async()=>registry(),discoverProfileModels:async()=>({models:[{id:'draft-only'}]}),saveProviderProfile:async(...args)=>writes.push(args)},value=>posted.push(value));
 const originalTimer=global.setTimeout;let expire;global.setTimeout=callback=>{expire=callback;return undefined;};
 try{await controller.discover('synthetic-unsaved-only','','anthropic.messages');assert.equal(controller.savedCatalogue,undefined);const draft=controller.draft;controller.invalidate();assert.equal(draft.key,undefined);assert.equal(await controller.save('draft-only'),false);
  await controller.discover('synthetic-expiring-only','','anthropic.messages');expire();assert.equal(controller.draft,undefined);assert.equal(controller.savedCatalogue,undefined);assert.equal(await controller.save('draft-only'),false);assert.equal(writes.length,0);assert.ok(!JSON.stringify(posted).includes('synthetic-unsaved-only'));assert.ok(!JSON.stringify(posted).includes('synthetic-expiring-only'));
 }finally{global.setTimeout=originalTimer;controller.dispose();}
});

test('older metadata reads cannot erase the newer saved-profile observation or republish its catalogue',async()=>{
 let state=registry(),resolve,hold=false;const controller=new ProviderProfileController({providerProfiles:()=>hold?new Promise(yes=>resolve=yes):Promise.resolve(state),discoverProfileModels:async()=>({models:[{id:'discovered-only'}]})},()=>{});
 try{await controller.discover();hold=true;const old=controller.refresh();hold=false;state={...state,revision:5};await controller.refresh();resolve(registry());await old;assert.equal(controller.state.revision,5);assert.equal(controller.savedCatalogue,undefined);}finally{controller.dispose();}
});
test('admission reconciliation refreshes changed metadata and discards pending keys without replay',async()=>{
 let state=registry(),reads=0;const posted=[],controller=new ProviderProfileController({providerProfiles:async()=>{reads++;return state;}},value=>posted.push(value));
 try{await controller.refresh();const binding=await controller.admission();controller.draft={key:'synthetic-pending-key'};state={...state,revision:5};
 assert.equal(await controller.reconcileAdmission(binding,{status:409}),true);assert.equal(controller.draft,undefined);assert.deepEqual(await controller.admission(),{provider_profile_id:'openai',expected_provider_revision:5});assert.equal(reads,2);assert.ok(!JSON.stringify(posted).includes('synthetic-pending-key'));
 assert.equal(await controller.reconcileAdmission(await controller.admission(),{status:409}),false);assert.equal(await controller.reconcileAdmission(binding,{status:503}),false);assert.equal(reads,3);}finally{controller.dispose();}
});
test('retired admission reconciliation cannot publish a late metadata response',async()=>{
 let release;const pending=new Promise(resolve=>release=resolve),posted=[],controller=new ProviderProfileController({providerProfiles:()=>pending},value=>posted.push(value));
 const request=controller.reconcileAdmission({provider_profile_id:'openai',expected_provider_revision:4},{status:409});controller.dispose();release({...registry(),revision:5});assert.equal(await request,false);assert.deepEqual(posted,[]);
});
test('admission uses the profile snapshot already shown in this view and does not refresh past an external change',async()=>{
 let reads=0;const controller=new ProviderProfileController({providerProfiles:async()=>{reads++;return registry();}},()=>{});
 try{await controller.refresh();const binding=await controller.admission();assert.deepEqual(binding,{provider_profile_id:'openai',expected_provider_revision:4});assert.equal(reads,1);controller.invalidate();assert.deepEqual(await controller.admission(),binding);assert.equal(reads,1);}finally{controller.dispose();}
});
test('profile setup discovers privately, rejects forged models and enrolls only on footer selection',async()=>{
 const posted=[],calls=[],state=registry(),key='synthetic-profile-controller-key';
 const controller=new ProviderProfileController({providerProfiles:async()=>state,discoverProfileModels:async(...args)=>{calls.push(['discover',...args]);return {models:[{id:'fixture-claude'}]};},saveProviderProfile:async(...args)=>{calls.push(['save',...args]);return {...state,revision:5,active:args[0],profiles:[...state.profiles,{id:args[0],route_id:args[1],provider:'anthropic',model:args[2],revision:1}]};}},value=>posted.push(value));
 try{await controller.discover(key,'','anthropic.messages');assert.equal(calls.length,1);assert.ok(!JSON.stringify(posted).includes(key));await assert.rejects(controller.save('forged-model'));assert.equal(calls.length,1);await controller.save('fixture-claude');assert.equal(calls[1][2],'anthropic.messages');assert.equal(calls[1][4],key);assert.equal(calls[1][5],4);assert.equal(calls[1][6],true);assert.equal(controller.draft.key,undefined);assert.equal(controller.state.profiles[0].model,'fixture-openai');assert.equal(posted.findLast(value=>value.type==='provider-wire').wire,'anthropic-messages');}finally{controller.dispose();}
});
test('saved-key discovery stays on its owned route while native save can rebind within the family',async()=>{
 const calls=[],state=registry(),controller=new ProviderProfileController({providerProfiles:async()=>state,discoverProfileModels:async(...args)=>{calls.push(args);return {models:[{id:'fixture-chat'}]};},saveProviderProfile:async(...args)=>{calls.push(args);return {...state,revision:5,profiles:[{...state.profiles[0],route_id:args[1],model:args[2],revision:5}]};}},()=>{});
 try{await controller.discover(undefined,'openai','openai.chat');assert.deepEqual(calls[0],['openai','openai.responses',undefined,4]);await controller.save('fixture-chat');assert.equal(calls[1][1],'openai.chat');assert.equal(calls[1][3],undefined);assert.equal(controller.savedCatalogue,undefined);assert.equal(controller.draft,undefined);await assert.rejects(controller.discover(undefined,'openai','anthropic.messages'));assert.equal(calls.length,2);}finally{controller.dispose();}
});
test('disposing or changing backend cannot publish late discovery or send a pending key in enrollment',async()=>{
 let release;const pending=new Promise(resolve=>release=resolve),posted=[];let writes=0,current=true;
 const controller=new ProviderProfileController({providerProfiles:async()=>registry(),discoverProfileModels:()=>pending,saveProviderProfile:async()=>{writes++;}},value=>posted.push(value),()=>current);
 const discovery=controller.discover('synthetic-pending-key','','anthropic.messages');await new Promise(resolve=>setImmediate(resolve));const count=posted.length;current=false;release({models:[{id:'fixture-claude'}]});await discovery;assert.equal(posted.length,count);assert.equal(controller.draft,undefined);assert.equal(writes,0);controller.dispose();
});
