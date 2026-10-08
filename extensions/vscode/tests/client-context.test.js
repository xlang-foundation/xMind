'use strict';
// Synthetic public records prove thin adapter guards, never native compaction.
const test=require('node:test'),assert=require('node:assert/strict');
const {BackendClient,ContextViewController,validateContextObservation,validateGraphContext}=require('../client');
const token='synthetic-context-adapter-token-'.padEnd(64,'x');
const record=()=>({session_id:'session',model_id:'model:variant/v1',enabled:true,automatic:false,head_revision:4,source_watermark:9,manual:null,checkpoint:{id:'checkpoint',provider_elapsed_ms:12,preparation_elapsed_ms:14,usage:{input_tokens:41,output_tokens:9}}});
test('context routes preserve actual supplied metrics, model/query and idempotent request/profile preconditions',async()=>{
 const calls=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{calls.push({url,options});return {ok:true,json:async()=>url.includes('/graph-runs/')?{id:'root',session_id:'session',state:'paused',parent_id:'',node_id:'',graph_root:true}:url.includes('/requests/')||options.method==='POST'?{id:'request',state:'pending'}:record()};});
 const actual=await client.context('session','model:variant/v1');await client.contextRequest('session','request','model:variant/v1');
 await client.compactContext('session','request',4,'model:variant/v1',{provider_profile_id:'saved',expected_provider_revision:7});await client.resumeGraph('root',8);
 assert.equal(actual.checkpoint.usage.total_tokens,undefined);assert.ok(calls[0].url.endsWith('/context?model_id=model%3Avariant%2Fv1'));
 assert.deepEqual(JSON.parse(calls[2].options.body),{id:'request',expected_head_revision:4,model_id:'model:variant/v1',provider_profile_id:'saved',expected_provider_revision:7});assert.deepEqual(JSON.parse(calls[3].options.body),{expected_checkpoint_revision:8});
 assert.ok(calls.every(c=>c.options.redirect==='error'&&c.options.headers.Authorization==='Bearer '+token));assert.ok(!JSON.stringify(actual).includes(token));
 const before=calls.length;for(const action of [()=>client.context('../session','model'),()=>client.compactContext('session','request',-1),()=>client.resumeGraph('root',0),()=>client.context('session','bad model')])await assert.rejects(action);assert.equal(calls.length,before);
});
test('public context metadata rejects foreign identities and private/provider-window fields',()=>{
 for(const change of [v=>v.session_id='foreign',v=>v.model_id='foreign',v=>v.checkpoint.encrypted_content='private',v=>v.checkpoint.usage.opaque='private',v=>v.authority_identity='private',v=>v.manual={id:'request',state:'started'},v=>v.head_revision=1.5]){const value=record();change(value);assert.throws(()=>validateContextObservation(value,'session','model:variant/v1'));}
 assert.equal(validateGraphContext({enabled:true,resumable:true,remaining_active_ms:300},{state:'paused'}).resumable,true);
 for(const value of [{enabled:false,resumable:true,remaining_active_ms:300},{enabled:true,resumable:true,remaining_active_ms:null},{enabled:true,resumable:false,remaining_active_ms:300,backend_identity:'private'}])assert.throws(()=>validateGraphContext(value,{state:'paused'}));
 assert.throws(()=>validateGraphContext({enabled:true,resumable:true,remaining_active_ms:300},{state:'running'}));
});
test('shared context controller rechecks the displayed head and makes one genuine mutation with fresh profile binding',async()=>{
 const posted=[],calls=[];let actual=record(),state='pending';const client={context:async()=>actual,contextRequest:async()=>({id:'request',state}),compactContext:async(...args)=>{calls.push(args);state='completed';actual={...actual,head_revision:5};return {id:'request',state:'pending'};}};
 const pin={session:'session',model:'model:variant/v1',generation:1,enabled:true},view=new ContextViewController(client,value=>posted.push(value),()=>pin,async()=>({provider_profile_id:'saved',expected_provider_revision:8}),()=> 'request');
 try{await view.read();await view.compact({session:pin.session,model:pin.model,expected_head_revision:4});assert.deepEqual(calls,[['session','request',4,'model:variant/v1',{provider_profile_id:'saved',expected_provider_revision:8}]]);assert.equal(view.record.head_revision,5);assert.equal(view.record.manual.state,'completed');assert.equal(view.record.checkpoint.usage.total_tokens,undefined);assert.ok(posted.every(message=>!JSON.stringify(message).includes('provider_profile_id')));}finally{view.dispose();}
});
test('stale head, disabled capability, outstanding actual request and detached generation never mutate context',async()=>{
 const pin={session:'session',model:'model:variant/v1',generation:1,enabled:true};let actual=record(),calls=0,pause;
 const client={context:async()=>pause?pause:actual,compactContext:async()=>{calls++;throw new Error('Unexpected mutation');}};
 const view=new ContextViewController(client,()=>{},()=>pin,async()=>undefined,()=> 'request');
 try{await view.read();actual={...actual,head_revision:5};await assert.rejects(view.compact({...pin,expected_head_revision:4}));assert.equal(calls,0);
  actual={...record(),manual:{id:'another',state:'claimed'}};await view.read();await assert.rejects(view.compact({...pin,expected_head_revision:4}));assert.equal(calls,0);
  actual=record();await view.read();let resolve;pause=new Promise(yes=>resolve=yes);const pending=view.compact({...pin,expected_head_revision:4});pin.generation++;view.invalidate();resolve(record());await pending;assert.equal(calls,0);pin.enabled=false;await assert.rejects(view.compact({...pin,expected_head_revision:4}));
 }finally{view.dispose();}
});
test('context observation conflict clears actions and mutation failure never retries automatically',async()=>{
 const pin={session:'session',model:'model:variant/v1',generation:1,enabled:true},posted=[];let failRead=false,calls=0;
 const client={context:async()=>{if(failRead)throw Object.assign(new Error('Changed'),{status:409});return record();},compactContext:async()=>{calls++;throw new Error('Acknowledgement unavailable');}};
 const view=new ContextViewController(client,value=>posted.push(value),()=>pin,async()=>undefined,()=> 'request');
 try{await view.read();await assert.rejects(view.compact({...pin,expected_head_revision:4}));assert.equal(calls,1);failRead=true;await assert.rejects(view.read());assert.equal(view.record,undefined);assert.equal(posted.at(-1).type,'context-clear');assert.equal(calls,1);}finally{view.dispose();}
});
test('older overlapping same-generation context success/error cannot replace or clear a newer observation',async()=>{
 const pin={session:'session',model:'model:variant/v1',generation:1,enabled:true};
 for(const olderError of [false,true]){
  const posted=[];let calls=0,resolve,reject;const delayed=new Promise((yes,no)=>{resolve=yes;reject=no;});
  const client={context:async()=>++calls===1?delayed:{...record(),head_revision:5}};
  const view=new ContextViewController(client,value=>posted.push(value),()=>pin);
  try{const older=view.read();await view.read();assert.equal(view.record.head_revision,5);
   if(olderError)reject(Object.assign(new Error('Old observation conflicted'),{status:409}));else resolve(record());await older;
   assert.equal(view.record.head_revision,5);assert.deepEqual(posted.map(message=>message.type),['context']);assert.equal(calls,2);
  }finally{view.dispose();}
 }
});
test('terminal tracked request cannot hide a different actual newly admitted manual request',async()=>{
 const pin={session:'session',model:'model:variant/v1',generation:1,enabled:true};let acknowledged=false,afterAckReads=0,calls=0;
 const client={context:async()=>acknowledged&&++afterAckReads>=2?{...record(),head_revision:5,manual:{id:'new-request',state:'claimed'}}:record(),
  contextRequest:async()=>({id:'old-request',state:'completed'}),compactContext:async()=>{calls++;acknowledged=true;return {id:'old-request',state:'pending'};}};
 const view=new ContextViewController(client,()=>{},()=>pin,async()=>undefined,()=> 'old-request');
 try{await view.read();await view.compact({...pin,expected_head_revision:4});assert.deepEqual(view.record.manual,{id:'new-request',state:'claimed'});assert.equal(view.record.head_revision,5);await assert.rejects(view.compact({...pin,expected_head_revision:5}));assert.equal(calls,1);}finally{view.dispose();}
});
