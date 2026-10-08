'use strict';
// Host-controller fixtures only; actual native API/persistence has independent
// contracts. These tests never install credentials into the product preview.
const test=require('node:test'),assert=require('node:assert/strict');
const {ProviderProfileController}=require('../client');
const registry=()=>({revision:4,active:'openai',profiles:[{id:'openai',route_id:'openai.responses',provider:'openai',model:'fixture-openai',revision:4}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'openai.chat',provider:'openai',wire:'chat-completions',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]});
test('profile setup discovers privately, rejects forged models and enrolls only on footer selection',async()=>{
 const posted=[],calls=[],state=registry(),key='synthetic-profile-controller-key';
 const controller=new ProviderProfileController({providerProfiles:async()=>state,discoverProfileModels:async(...args)=>{calls.push(['discover',...args]);return {models:[{id:'fixture-claude'}]};},saveProviderProfile:async(...args)=>{calls.push(['save',...args]);return {...state,revision:5,active:args[0],profiles:[...state.profiles,{id:args[0],route_id:args[1],provider:'anthropic',model:args[2],revision:1}]};}},value=>posted.push(value));
 try{await controller.discover(key,'','anthropic.messages');assert.equal(calls.length,1);assert.ok(!JSON.stringify(posted).includes(key));await assert.rejects(controller.save('forged-model'));assert.equal(calls.length,1);await controller.save('fixture-claude');assert.equal(calls[1][2],'anthropic.messages');assert.equal(calls[1][4],key);assert.equal(calls[1][5],4);assert.equal(calls[1][6],true);assert.equal(controller.draft.key,undefined);assert.equal(controller.state.profiles[0].model,'fixture-openai');assert.equal(posted.findLast(value=>value.type==='provider-wire').wire,'anthropic-messages');}finally{controller.dispose();}
});
test('saved-key discovery stays on its owned route while native save can rebind within the family',async()=>{
 const calls=[],state=registry(),controller=new ProviderProfileController({providerProfiles:async()=>state,discoverProfileModels:async(...args)=>{calls.push(args);return {models:[{id:'fixture-chat'}]};},saveProviderProfile:async(...args)=>{calls.push(args);return {...state,revision:5,profiles:[{...state.profiles[0],route_id:args[1],model:args[2],revision:5}]};}},()=>{});
 try{await controller.discover(undefined,'openai','openai.chat');assert.deepEqual(calls[0],['openai','openai.responses',undefined,4]);await controller.save('fixture-chat');assert.equal(calls[1][1],'openai.chat');assert.equal(calls[1][3],undefined);await assert.rejects(controller.discover(undefined,'openai','anthropic.messages'));assert.equal(calls.length,2);}finally{controller.dispose();}
});
test('disposing or changing backend cannot publish late discovery or send a pending key in enrollment',async()=>{
 let release;const pending=new Promise(resolve=>release=resolve),posted=[];let writes=0,current=true;
 const controller=new ProviderProfileController({providerProfiles:async()=>registry(),discoverProfileModels:()=>pending,saveProviderProfile:async()=>{writes++;}},value=>posted.push(value),()=>current);
 const discovery=controller.discover('synthetic-pending-key','','anthropic.messages');await new Promise(resolve=>setImmediate(resolve));const count=posted.length;current=false;release({models:[{id:'fixture-claude'}]});await discovery;assert.equal(posted.length,count);assert.equal(controller.draft,undefined);assert.equal(writes,0);controller.dispose();
});
