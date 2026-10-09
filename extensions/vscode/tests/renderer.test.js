'use strict';
// DOM renderer contracts use labeled fixtures. No fixture is inserted into the
// interactive preview, and these tests do not claim live model execution.
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {JSDOM}=require('jsdom');const {html}=require('../webview');
function renderer(){
  const dom=new JSDOM(html('fixture-nonce',{source:'https://fixture',css:'fixture.css',marked:'marked.js',purify:'purify.js',script:'chat.js'}),{runScripts:'outside-only'}),posted=[];
  // JSDOM has no native dialog methods. Model only that browser platform API;
  // the production handlers and messages remain under test.
  dom.window.HTMLDialogElement.prototype.showModal=function(){this.setAttribute('open','');};
  dom.window.HTMLDialogElement.prototype.close=function(){this.removeAttribute('open');this.dispatchEvent(new dom.window.Event('close'));};
  const callbackErrors=[];
  dom.window.addEventListener('error',event=>{callbackErrors.push(event.error?.message??event.message);event.preventDefault();});
  const closeWindow=dom.window.close.bind(dom.window);
  dom.window.close=()=>{closeWindow();assert.deepEqual(callbackErrors,[],'Renderer event callbacks must not fail silently');};
  dom.window.acquireVsCodeApi=()=>({postMessage:message=>posted.push(message)});
  dom.window.TextDecoder=TextDecoder;
  dom.window.TextEncoder=TextEncoder;
  for(const file of ['node_modules/marked/lib/marked.umd.js','node_modules/dompurify/dist/purify.min.js','media/chat.js']) dom.window.eval(fs.readFileSync(path.join(__dirname,'..',file),'utf8'));
  return {dom,posted,send:data=>dom.window.dispatchEvent(new dom.window.MessageEvent('message',{data}))};
}
test('footer file-change mode follows native capability and never promises writes for unknown legacy policy',()=>{
 const r=renderer(),doc=r.dom.window.document;try{const mode=doc.getElementById('file-mode');assert.ok(mode.closest('footer'));assert.equal(mode.hidden,true);r.send({type:'capabilities',execution:true,fileEditProposals:false,models:[]});assert.equal(mode.hidden,false);assert.match(mode.textContent,/Read only: file changes disabled/);r.send({type:'capabilities',execution:true,fileEditProposals:true,models:[]});assert.match(mode.textContent,/File changes require approval/);r.send({type:'capabilities',execution:true,fileEditProposals:'<img onerror=unsafe>',models:[]});assert.equal(mode.hidden,true);assert.equal(mode.textContent,'');assert.equal(mode.querySelector('img'),null);r.send({type:'capabilities',execution:true,fileEditProposals:false,models:[]});r.send({type:'workspace-clear'});assert.equal(mode.hidden,true);assert.equal(mode.textContent,'');assert.deepEqual(JSON.parse(JSON.stringify(r.posted)),[{type:'ready'}]);}finally{r.dom.window.close();}
});

test('skill chooser stays in the sidebar footer, retains focus, and sends revision-bound attachment intent',()=>{
 const r=renderer(),doc=r.dom.window.document;const catalogue={workspace_id:'windows-local-file-v1:fixture',authority_id:'a'.repeat(32),skills:[{id:'manual',name:'<img onerror=unsafe> Guide',path:'.agents/skills/manual.md',model_invocable:false}]};const selection={session_id:'session',workspace_id:catalogue.workspace_id,authority_id:catalogue.authority_id,revision:3,ids:[],manual_ids:[],editable:true};
 try{r.send({type:'skills',catalogue,selection});const pane=doc.getElementById('skills-view'),button=doc.querySelector('#skill-list button');assert.equal(pane.closest('footer')!==null,true);assert.equal(pane.hidden,false);assert.equal(pane.querySelector('img'),null);assert.ok(pane.textContent.includes('User attachment only'));button.focus();r.send({type:'skills',catalogue,selection});assert.equal(doc.activeElement,button);button.click();assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'skills-change',session:'session',revision:3,ids:['manual']});assert.equal(button.disabled,true);
 r.send({type:'skills',catalogue,selection:{...selection,revision:4,ids:['manual'],manual_ids:['manual']}});assert.equal(button.textContent,'Remove');r.send({type:'runs',runs:[],busy:true});assert.equal(button.disabled,true);r.send({type:'runs',runs:[],busy:false});doc.getElementById('skills-clear').click();assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'skills-change',session:'session',revision:4,ids:[]});r.send({type:'skills-error',text:'Revision changed'});assert.equal(button.disabled,true);assert.ok(doc.getElementById('skills-status').textContent.includes('Refresh'));r.send({type:'skills-clear'});assert.equal(pane.hidden,true);assert.equal(doc.getElementById('skill-list').children.length,0);
 }finally{r.dom.window.close();}
});

test('effective backend root uses plain text and folder switch clears the old draft',()=>{
 const r=renderer(),doc=r.dom.window.document;
 try{r.send({type:'workspace',root:'D:\\<img onerror=malicious>\\TestProj',roots:[{},{}],backendChangePending:true});const label=doc.getElementById('workspace-root');assert.equal(label.hidden,false);assert.equal(label.querySelector('img'),null);assert.ok(label.textContent.includes('active root from 2 workspace folders'));assert.ok(label.textContent.includes('Backend changes pending; using saved backend'));r.send({type:'workspace',root:'D:\\TestProj',roots:[{}],backendChangePending:'<img onerror=unsafe>'});assert.ok(!label.textContent.includes('pending'));assert.equal(label.querySelector('img'),null);doc.getElementById('prompt').value='old workspace draft';r.send({type:'workspace-clear'});assert.equal(label.hidden,true);assert.equal(doc.getElementById('prompt').value,'');}finally{r.dom.window.close();}
});

test('footer saved-provider chooser uses public profiles, preserves actual selection until acknowledgement and never opens key settings',()=>{
 const r=renderer(),doc=r.dom.window.document,select=doc.querySelector('footer #footer-provider');
 const profiles=[{id:'openai-saved',provider:'openai',model:'fixture-openai'},{id:'claude-key-only',provider:'anthropic',model:''},{id:'gemini-saved',provider:'gemini',model:'models/fixture'},{id:'deepseek-saved',provider:'deepseek',model:'deepseek-flash'}],routes=[];
 try{assert.equal(select.hidden,true);r.send({type:'provider-profiles',active:'openai-saved',profiles,routes});assert.equal(select.hidden,false);assert.equal(select.value,'openai-saved');assert.deepEqual([...select.options].map(value=>value.value),['',...profiles.map(value=>value.id)]);assert.equal(select.options[2].textContent,'Claude · Choose a model');assert.equal(doc.getElementById('provider-settings').open,false);
  const before=r.posted.length;doc.getElementById('provider-key').value='synthetic-discarded-draft';select.value='claude-key-only';select.dispatchEvent(new r.dom.window.Event('change'));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'select-provider',id:'claude-key-only'});assert.equal(r.posted.length,before+1);assert.equal(doc.getElementById('provider-key').value,'');assert.equal(select.value,'openai-saved','Only native acknowledgement can change the selected provider');assert.equal(doc.getElementById('provider-settings').open,false);
  r.send({type:'provider-profiles',active:'claude-key-only',profiles,routes});r.send({type:'model-list',models:[{id:'fixture-claude'}]});assert.equal(select.value,'claude-key-only');assert.equal(doc.getElementById('provider-key').required,false);assert.equal(doc.getElementById('provider-key-label').textContent,'API key (optional)');assert.match(doc.getElementById('provider-key-help').textContent,/Leave blank to use the saved key/);doc.getElementById('model').value='fixture-claude';doc.getElementById('model').dispatchEvent(new r.dom.window.Event('change'));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'model',id:'fixture-claude'});const count=r.posted.length;
  select.value='';select.dispatchEvent(new r.dom.window.Event('change'));select.append(new r.dom.window.Option('Forged profile','forged'));select.value='forged';select.dispatchEvent(new r.dom.window.Event('change'));assert.equal(r.posted.length,count);assert.equal(select.value,'claude-key-only');assert.ok(!JSON.stringify(r.posted).includes('synthetic-discarded-draft'));
  r.send({type:'provider-profiles',active:'',profiles,routes});assert.equal(select.value,'');assert.equal(select.selectedOptions[0].textContent,'Choose provider');assert.equal(select.disabled,false);assert.equal(doc.getElementById('provider-key').required,true);r.send({type:'provider-profiles',active:'',profiles:[],routes});assert.equal(select.disabled,true);assert.equal(select.options[0].textContent,'Add a provider in Settings');assert.equal(r.posted.length,count,'Metadata rendering cannot mutate a profile');
 }finally{r.dom.window.close();}
});

test('DeepSeek Settings enrollment uses the footer model chooser and saved response metrics',()=>{
  const r=renderer(),doc=r.dom.window.document,key='synthetic-deepseek-renderer-key',model='deepseek-flash';
  try{
    const routes=[{id:'deepseek.chat',provider:'deepseek',wire:'chat-completions',discovery:true}];
    r.send({type:'provider-profiles',active:'',profiles:[],routes});
    doc.getElementById('settings').click();
    assert.equal(doc.getElementById('provider-settings').open,true,'Settings must actually reach the dialog platform API');
    const route=doc.getElementById('provider-name'),input=doc.getElementById('provider-key');
    assert.equal(route.selectedOptions[0].textContent,'DeepSeek · Chat Completions');
    input.value=key;
    doc.getElementById('provider-form').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));
    assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'saveProviderKey',key,profile:'',route:'deepseek.chat'});
    assert.equal(input.value,'');
    r.send({type:'model-list',models:[{id:model},{id:'deepseek-v4-pro'}]});
    assert.deepEqual([...doc.querySelector('footer #model').options].map(option=>option.value),['',model,'deepseek-v4-pro']);
    assert.equal(doc.querySelector('#provider-settings #model'),null);
    const context={profile_id:'synthetic-deepseek-profile',profile_revision:3,route_id:'deepseek.chat',provider:'deepseek',wire:'chat-completions',model_id:model};
    r.send({type:'provider-profiles',active:context.profile_id,profiles:[{id:context.profile_id,provider:'deepseek',model,route_id:context.route_id,revision:3}],routes});
    r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic recorded DeepSeek response',model,provider_context:context,usage:{prompt_tokens:42,completion_tokens:9,total_tokens:51,prompt_cache_hit_tokens:7,prompt_cache_miss_tokens:35,prompt_tokens_details:{cached_tokens:7},completion_tokens_details:{reasoning_tokens:2}}}}]});
    assert.equal(doc.getElementById('provider-profile').selectedOptions[0].textContent,'DeepSeek · '+model);
    const metrics=doc.querySelector('#history .metrics'),badge=metrics.querySelector('.provider-context');
    assert.equal(badge.textContent,'DeepSeek · Chat Completions');
    assert.match(badge.title,/Route: deepseek\.chat\nModel: deepseek-flash/);
    assert.match(metrics.textContent,/Input 42Output 9Total 51Cached 7Reasoning 2/);
    assert.ok(!doc.body.textContent.includes(key));
    doc.getElementById('settings').click();assert.equal(input.value,'');
  }finally{r.dom.window.close();}
});
const {planFixture,inputMessage}=require('./plan-fixture');
test('context control stays beside the bottom composer, shows actual counters and submits only the displayed session/model/head',()=>{
 const r=renderer(),doc=r.dom.window.document,record={session_id:'session',model_id:'synthetic-model',enabled:true,automatic:false,head_revision:4,source_watermark:9,manual:null,checkpoint:{id:'checkpoint',provider_elapsed_ms:12,preparation_elapsed_ms:14,usage:{input_tokens:41,output_tokens:9}}};
 try{r.send({type:'sessions',sessions:[{id:'session',title:'Synthetic conversation'}],selected:'session'});r.send({type:'capabilities',execution:true,models:[{id:'synthetic-model'}],model:'synthetic-model'});r.send({type:'context',record});
  const area=doc.getElementById('context-view');assert.ok(doc.querySelector('footer #context-view'));assert.ok(doc.querySelector('footer #model'));assert.equal(area.hidden,false);assert.match(area.textContent,/revision 4 · automatic off/);assert.match(area.textContent,/Input tokens 41Output tokens 9/);assert.ok(!area.textContent.includes('Total tokens'),'Missing provider totals are not inferred');
  const before=r.posted.length,button=doc.getElementById('context-compact');button.click();button.click();assert.equal(r.posted.length,before+1);assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'context-compact',session:'session',model:'synthetic-model',expected_head_revision:4});assert.equal(button.disabled,true);
  r.send({type:'context',record:{...record,manual:{id:'request',state:'claimed'}}});assert.equal(button.disabled,true);assert.match(area.textContent,/request · claimed/);r.send({type:'context-clear'});button.click();assert.equal(r.posted.length,before+1);assert.equal(area.hidden,true);
 }finally{r.dom.window.close();}
});
test('graph resume is exposed only from backend eligibility and removed selection cannot act on the old closed owner',()=>{
 const r=renderer(),doc=r.dom.window.document,record={run:{id:'root',session_id:'session',state:'paused',graph_root:true},graph_id:'synthetic-graph',graph_revision:1,checkpoint_revision:7,checkpoint:{nodes:[]},spec:{nodes:[]},context:{enabled:true,resumable:false,remaining_active_ms:1200}};
 try{r.send({type:'runs',runs:[record.run],selected:'root',busy:true});r.send({type:'graph',record,children:[],histories:{}});const button=doc.getElementById('graph-resume');assert.equal(button.hidden,true);assert.match(doc.getElementById('graph-clock').textContent,/1.200s/);
  record.context.resumable=true;r.send({type:'graph',record,children:[],histories:{}});assert.equal(button.hidden,false);button.click();assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'graph-resume',root:'root',revision:7});const before=r.posted.length;
  r.send({type:'runs',runs:[{id:'other',state:'completed'}],selected:'other',busy:false});button.click();assert.equal(r.posted.length,before);assert.equal(doc.getElementById('graph-view').hidden,true);
 }finally{r.dom.window.close();}
});
test('Agent plan renders actual dependencies/correlations/held response metrics safely inside the existing sidebar',()=>{
 const r=renderer(),doc=r.dom.window.document,record=planFixture();try{
  r.send({type:'runs',runs:[record.run],selected:record.run.id,busy:true});r.send({type:'plan',record});
  const area=doc.getElementById('plan-view');assert.equal(area.hidden,false);assert.match(area.textContent,/revision 2/);assert.match(area.textContent,/read → gate · success/);assert.match(area.textContent,/call-second/);assert.match(area.textContent,/attempt-second/);assert.match(area.textContent,/held parent continuations 1/);
  assert.equal(area.querySelectorAll('script').length,0);assert.ok(area.textContent.includes('<script>fixtureAttack()</script>'));assert.equal(area.querySelectorAll('.metrics').length,2);assert.match(area.querySelector('.plan-call .metrics').textContent,/Input tokens 7Output tokens 3/);assert.ok(!area.querySelector('.plan-call .metrics').textContent.includes('Total'),'Missing held-response total is not inferred');assert.ok(doc.querySelector('footer #model'));assert.equal(doc.querySelector('#plan-view #model'),null);assert.equal(doc.getElementById('plan-resume').hidden,true);assert.equal(r.posted.length,1,'Plan observation cannot submit actions');
  const child={run:{id:'coding-child',parent_id:record.run.id,state:'completed'},kind:'dynamic_agent',node_label:'code',preset_id:'workspace.coding',preset_revision:1};r.send({type:'owned-children',parent:record.run,children:[child],histories:{'coding-child':[{role:'assistant',data:{content:'Synthetic observed coding response',usage:{prompt_tokens:19,completion_tokens:5}}}]}});
  const owned=doc.getElementById('owned-view');assert.match(owned.textContent,/Coding agent · effects require their own approval/);assert.ok(!owned.textContent.includes('Read-only investigation'));assert.match(owned.querySelector('.metrics').textContent,/Input 19Output 5Total —/);
 }finally{r.dom.window.close();}
});
test('human answer keeps raw numeric lexemes and draft across observations, binds current identities/CAS and rejects detached form sends',()=>{
 const r=renderer(),doc=r.dom.window.document,record=planFixture();try{
  r.send({type:'runs',runs:[record.run],selected:record.run.id,busy:true});r.send({type:'plan',record});const form=doc.querySelector('.plan-input'),input=form.querySelector('textarea'),raw='{"quantity":1.00000000000000000001,"answer":"<img src=x onerror=fixtureAttack()>"}';form.querySelector('select').value='json';input.value=raw;
  record.plan.state_sequence++;r.send({type:'plan',record});assert.equal(doc.querySelector('.plan-input'),form);assert.equal(input.value,raw);
  form.dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),inputMessage(record,raw));assert.equal(form.querySelector('button').disabled,true);
  r.send({type:'error',text:'Strict backend rejected this input'});assert.equal(input.value,raw);input.value='[]';const count=r.posted.length;form.dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.equal(r.posted.length,count);assert.match(form.textContent,/JSON object/);
  r.send({type:'reset-run'});input.value=raw;form.dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.equal(r.posted.length,count);assert.equal(doc.getElementById('plan-view').hidden,true);
 }finally{r.dom.window.close();}
});
test('dynamic human questions use written answers by default and safely encode actual characters without granting operation approval',()=>{
 const r=renderer(),doc=r.dom.window.document,record=planFixture();try{
  r.send({type:'runs',runs:[record.run],selected:record.run.id,busy:true});r.send({type:'plan',record});const form=doc.querySelector('.plan-input'),format=form.querySelector('select'),input=form.querySelector('textarea');assert.equal(format.value,'text');assert.equal(input.placeholder,'Write your answer…');
  const written='Please use "the reviewed change".\n雪 <script>fixtureAttack()</script> \\ keep the exact answer.';input.value=written;form.dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));const sent=JSON.parse(JSON.stringify(r.posted.at(-1)));assert.deepEqual(sent,inputMessage(record,JSON.stringify({answer:written})));assert.equal(JSON.parse(sent.input_json).answer,written);assert.equal(doc.querySelector('#plan-view script'),null);assert.ok(!r.posted.some(message=>message.type==='decide'));assert.equal(input.value,written);
 }finally{r.dom.window.close();}
});
test('human-only completed frontier exposes explicit resume only for an enabled ready paused owner and preserves unanswered controls separately',()=>{
 const r=renderer(),doc=r.dom.window.document,record=planFixture({answer:true});try{
  r.send({type:'runs',runs:[record.run],selected:record.run.id,busy:true});r.send({type:'plan',record});assert.equal(doc.querySelector('.plan-input'),null);const resume=doc.getElementById('plan-resume');assert.equal(resume.hidden,false);assert.equal(resume.disabled,false);resume.click();assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'plan-resume',root:record.run.id,plan_id:record.plan.id,expected_revision:2,expected_state_sequence:18});
  record.enabled=false;r.send({type:'plan',record});assert.equal(resume.hidden,true);const count=r.posted.length;resume.click();assert.equal(r.posted.length,count);assert.match(doc.getElementById('plan-view').textContent,/retained for inspection/);
 }finally{r.dom.window.close();}
});
test('Agent investigations retain independent metrics and terminal replay does not synthesize a graph or another answer',()=>{
 const r=renderer(),doc=r.dom.window.document,parent={id:'parent',state:'completed'},children=['left','right'].map((id,index)=>({run:{id,parent_id:'parent',state:index?'failed':'completed'},kind:'delegated_leaf',batch_id:'batch',task_id:id,preset_id:'workspace.inspect',preset_revision:1}));
 try{r.send({type:'owned-children',parent,children,histories:{left:[{role:'assistant',data:{content:'Synthetic left response',usage:{prompt_tokens:12,completion_tokens:5}}}],right:[{role:'assistant',data:{content:'Synthetic right failure response',usage:{prompt_tokens:4,completion_tokens:2,total_tokens:6}}}]}});
 const area=doc.getElementById('owned-view');assert.equal(area.hidden,false);assert.match(area.textContent,/left · completed/);assert.match(area.textContent,/right · failed/);assert.equal(area.querySelectorAll('.metrics').length,2);assert.match(area.querySelectorAll('.metrics')[0].textContent,/Input 12Output 5Total —/);assert.match(area.querySelectorAll('.metrics')[1].textContent,/Input 4Output 2Total 6/);assert.ok(!area.textContent.includes('Input 16'),'Child usage must not become an invented aggregate');assert.equal(doc.getElementById('graph-view').hidden,true);assert.equal(doc.getElementById('workflow').value,'');
 r.send({type:'owned-event',child_id:'left',event:{seq:1,run_id:'left',kind:'model.text',data:{text:'Synthetic replay text'}}});assert.equal(area.querySelectorAll('.streaming').length,0);assert.ok(!area.textContent.includes('Synthetic replay text'));
 r.send({type:'owned-clear'});assert.equal(area.hidden,true);assert.equal(area.textContent,'');}finally{r.dom.window.close();}
});
test('run inspector uses selected historical provider context independently of the current model',()=>{
  const r=renderer(),context={profile_id:'historical-profile',profile_revision:1,route_id:'openai.responses',provider:'openai',wire:'responses',model_id:'historical-model'};
  r.send({type:'runs',runs:[{id:'failed-run',state:'failed',provider_context:context}],selected:'failed-run',busy:false});const area=r.dom.window.document.getElementById('run-context');
  assert.equal(area.hidden,false);assert.match(area.textContent,/Admitted profile:OpenAI · Responseshistorical-model/);assert.match(area.querySelector('.provider-context').title,/historical-profile\nProfile version: 1/);
  r.send({type:'capabilities',execution:true,models:[{id:'current-model'}],model:'current-model'});assert.ok(!area.textContent.includes('current-model'));assert.equal(area.querySelector('.metrics'),null,'Admission context must not invent response metrics');
  r.send({type:'runs',runs:[{id:'legacy-run',state:'completed'},{id:'failed-run',state:'failed',provider_context:context}],selected:'legacy-run',busy:false});assert.equal(area.hidden,true);assert.equal(area.textContent,'');
  r.send({type:'runs',runs:[{id:'failed-run',state:'failed',provider_context:context}],selected:'failed-run',busy:false});r.send({type:'reset-run'});assert.equal(area.hidden,true);assert.equal(area.textContent,'');r.dom.window.close();
});
test('run inspector suppresses malformed or secret-bearing provider metadata',()=>{
  const r=renderer(),context={profile_id:'fixture',profile_revision:1,route_id:'openai.responses',provider:'openai',wire:'responses',model_id:'fixture-model'};
  for(const invalid of [{...context,api_key:'synthetic-private-key'},{...context,profile_revision:0},{...context,model_id:'<script>fixtureAttack()</script>'}]){
    r.send({type:'runs',runs:[{id:'fixture-run',state:'cancelled',provider_context:invalid}],selected:'fixture-run',busy:false});const area=r.dom.window.document.getElementById('run-context');assert.equal(area.hidden,true);assert.equal(area.textContent,'');
  }r.dom.window.close();
});
test('profile Settings offers saved and new providers and keeps discovered models in the footer',()=>{
 const r=renderer(),doc=r.dom.window.document;try{
  r.send({type:'provider-profiles',active:'saved-openai',profiles:[{id:'saved-openai',provider:'openai',model:'fixture-openai',route_id:'openai.responses'}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]});
  assert.equal(doc.getElementById('profile-controls').hidden,false);assert.equal(doc.getElementById('profile-use').disabled,true);const profiles=doc.getElementById('provider-profile');profiles.value='';profiles.dispatchEvent(new r.dom.window.Event('change'));const route=doc.getElementById('provider-name');route.value='anthropic.messages';route.dispatchEvent(new r.dom.window.Event('change'));doc.getElementById('provider-key').value='synthetic-ui-profile-key';doc.getElementById('provider-form').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));
  assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'saveProviderKey',key:'synthetic-ui-profile-key',profile:'',route:'anthropic.messages'});assert.equal(doc.getElementById('provider-key').value,'');assert.ok(doc.querySelector('footer #model'));assert.equal(doc.querySelector('#provider-settings #model'),null);
 }finally{r.dom.window.close();}
});

test('closing Settings keeps the saved discovered footer list and sends no model or profile mutation',()=>{
 const r=renderer(),doc=r.dom.window.document,dialog=doc.getElementById('provider-settings');
 try{dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new r.dom.window.Event('close'));};r.send({type:'provider-profiles',active:'saved',profiles:[{id:'saved',provider:'openai',model:'discovered-0',route_id:'openai.responses'}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true}]});r.send({type:'model-list',models:Array.from({length:135},(_,n)=>({id:'discovered-'+n})),model:'discovered-0'});
  doc.getElementById('settings').click();doc.getElementById('provider-key').value='synthetic-discard-only';doc.getElementById('settings-close').click();assert.equal(dialog.open,false);assert.equal(doc.getElementById('provider-key').value,'');assert.equal(doc.querySelector('footer #model').options.length,136);assert.equal(doc.querySelector('footer #model').value,'discovered-0');assert.equal(r.posted.at(-1).type,'discardProviderKey');assert.ok(!r.posted.some(message=>message.type==='model'||message.type==='select-provider'||message.key==='synthetic-discard-only'));
 }finally{r.dom.window.close();}
});
test('GenerateContent labels depend on advertised routes and keep model choice in the footer',()=>{
 const r=renderer(),doc=r.dom.window.document;try{
  r.send({type:'provider-profiles',active:'saved-openai',profiles:[{id:'saved-openai',provider:'openai',model:'fixture-openai',route_id:'openai.responses'}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true}]});assert.equal([...doc.getElementById('provider-name').options].some(option=>option.textContent.includes('GenerateContent')),false);
  r.send({type:'provider-profiles',active:'saved-gemini',profiles:[{id:'saved-gemini',provider:'gemini',model:'fixture-gemini-model',route_id:'fixture.custom-gemini'}],routes:[{id:'fixture.custom-gemini',provider:'gemini',wire:'gemini-generate-content',discovery:true},{id:'fixture.unknown',provider:'gemini',wire:'unrecognized-wire',discovery:true}]});
  assert.equal(doc.getElementById('provider-profile').selectedOptions[0].textContent,'Gemini · fixture-gemini-model');assert.deepEqual([...doc.getElementById('provider-name').options].map(option=>[option.value,option.textContent]),[['fixture.custom-gemini','Gemini · GenerateContent']]);
  r.send({type:'provider-wire',wire:'gemini-generate-content'});assert.equal(doc.getElementById('provider-mode').textContent,'Gemini GenerateContent');assert.equal(doc.getElementById('provider-mode').hidden,false);r.send({type:'model-list',models:[{id:'fixture-discovered-gemini'}],model:'fixture-discovered-gemini'});assert.equal(doc.querySelector('footer #model').value,'fixture-discovered-gemini');assert.equal(doc.querySelector('#provider-settings #model'),null);
  for(const wire of ['gemini_generate_content',['gemini-generate-content'],{wire:'gemini-generate-content'}]){r.send({type:'provider-wire',wire});assert.equal(doc.getElementById('provider-mode').hidden,true);assert.equal(doc.getElementById('provider-mode').textContent,'');}assert.equal(r.posted.length,1,'Metadata must not submit keys, profiles, runs or selections');
 }finally{r.dom.window.close();}
});
test('advertised Gemini Settings clears the draft key and uses the footer for full discovered model resources',()=>{
 const r=renderer(),doc=r.dom.window.document,key='synthetic-gemini-renderer-key',model='models/fixture-gemini',routes=[{id:'openai.chat',provider:'openai',wire:'chat-completions',discovery:true},{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true},{id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',discovery:true}];
 try{
  r.send({type:'provider-profiles',active:'',profiles:[],routes});const dialog=doc.getElementById('provider-settings'),route=doc.getElementById('provider-name'),input=doc.getElementById('provider-key'),footer=doc.querySelector('footer #model');
  // jsdom omits native dialog methods; only open/close are host fixtures here.
  dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new r.dom.window.Event('close'));};doc.getElementById('settings').click();assert.equal(dialog.open,true);assert.equal(input.type,'password');
  assert.deepEqual([...route.options].map(option=>option.value),routes.map(value=>value.id));route.value='gemini.generate-content';route.dispatchEvent(new r.dom.window.Event('change'));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'discardProviderKey'});
  input.value=key;doc.getElementById('provider-form').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'saveProviderKey',key,profile:'',route:'gemini.generate-content'});assert.equal(input.value,'');assert.equal(doc.getElementById('settings-save').disabled,true);assert.ok(!doc.body.textContent.includes(key));
  r.send({type:'model-list',models:[{id:model},{id:'models/fixture-gemini-next'}]});r.send({type:'settings-state',busy:false,complete:true,text:'Choose a model below to save this profile'});assert.equal(dialog.open,false);assert.equal(input.value,'');assert.equal(doc.querySelectorAll('select#model').length,1);assert.equal(dialog.querySelector('#model'),null,'Settings must not create a popup model picker');assert.deepEqual([...footer.options].map(option=>option.value),['',model,'models/fixture-gemini-next']);
  footer.value=model;footer.dispatchEvent(new r.dom.window.Event('change'));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'model',id:model});
  const context={profile_id:'saved-gemini',profile_revision:1,route_id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',model_id:model};r.send({type:'provider-profiles',active:context.profile_id,profiles:[{id:context.profile_id,route_id:context.route_id,provider:'gemini',model,revision:1}],routes});r.send({type:'provider-wire',wire:context.wire});r.send({type:'model-list',models:[{id:model},{id:'models/fixture-gemini-next'}],model});
  assert.equal(footer.value,model);assert.equal(doc.getElementById('provider-mode').textContent,'Gemini GenerateContent');assert.equal(doc.getElementById('provider-profile').selectedOptions[0].textContent,'Gemini · '+model);assert.deepEqual([...route.options].map(option=>option.value),['gemini.generate-content']);
  r.send({type:'history',history:[{role:'assistant',data:{content:'Recorded synthetic Gemini Settings fixture',model,provider_context:context,usage:{promptTokenCount:21,candidatesTokenCount:8,prompt_tokens:21,completion_tokens:8,prompt_tokens_details:{cached_tokens:1},completion_tokens_details:{reasoning_tokens:2}}}}]});const metrics=doc.querySelector('#history .metrics');assert.match(metrics.textContent,/Input 21Output 8Total —Cached 1Reasoning 2/);assert.equal(metrics.querySelector('.provider-context').textContent,'Gemini · GenerateContent');assert.match(metrics.querySelector('.provider-context').title,/Route: gemini\.generate-content\nModel: models\/fixture-gemini/);assert.ok(!metrics.textContent.includes('Total 29'),'Missing totals must not be estimated');assert.ok(!doc.body.textContent.includes(key));
  doc.getElementById('settings').click();assert.equal(input.value,'','Reopening a saved encrypted profile cannot expose its key');input.value='synthetic-abandoned-gemini-key';doc.getElementById('settings-close').click();assert.equal(input.value,'');assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'discardProviderKey'});
 }finally{r.dom.window.close();}
});

test('recorded GenerateContent response and run context retain their native identity and supplied metrics',()=>{
 const r=renderer(),doc=r.dom.window.document,context={profile_id:'historical-gemini',profile_revision:2,route_id:'fixture.custom-gemini',provider:'gemini',wire:'gemini-generate-content',model_id:'fixture-historical-gemini'};try{
  r.send({type:'provider-wire',wire:'responses'});r.send({type:'capabilities',execution:true,models:[{id:'current-openai'}],model:'current-openai'});r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic recorded Gemini response',provider_context:context,usage:{prompt_tokens:12,completion_tokens:5,prompt_tokens_details:{cached_tokens:2},completion_tokens_details:{reasoning_tokens:3}}}}]});
  const metrics=doc.querySelector('#history .metrics'),badge=metrics.querySelector('.provider-context');assert.equal(badge.textContent,'Gemini · GenerateContent');assert.match(badge.title,/historical-gemini\nProfile version: 2/);assert.match(badge.title,/Route: fixture.custom-gemini/);assert.match(metrics.textContent,/Input 12Output 5Total —Cached 2Reasoning 3/);assert.ok(!metrics.textContent.includes('current-openai'));
  r.send({type:'runs',runs:[{id:'fixture-gemini-run',state:'failed',provider_context:context}],selected:'fixture-gemini-run',busy:false});const run=doc.getElementById('run-context');assert.equal(run.hidden,false);assert.equal(run.querySelector('.provider-context').textContent,'Gemini · GenerateContent');assert.ok(run.textContent.includes('fixture-historical-gemini'));assert.equal(run.querySelector('.metrics'),null);
  for(const invalid of [{...context,api_key:'synthetic-private-key'},{...context,wire:'gemini_generate_content'},{...context,wire:['gemini-generate-content']}]){r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic response',provider_context:invalid}}]});r.send({type:'runs',runs:[{id:'fixture-gemini-run',state:'failed',provider_context:invalid}],selected:'fixture-gemini-run',busy:false});assert.equal(doc.querySelector('#history .provider-context'),null);assert.equal(run.hidden,true);assert.ok(!doc.body.textContent.includes('synthetic-private-key'));}
  r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic custom provider response',provider_context:{...context,provider:'google'}}}]});assert.equal(doc.querySelector('#history .provider-context').textContent,'google · GenerateContent','Arbitrary supplied provider identities must not be renamed into a default family');
 }finally{r.dom.window.close();}
});

const processProposalFixture=()=>({id:'fixture-command',tool:'run_process',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,
  arguments_json:JSON.stringify({profile_id:'fixture-profile',profile_revision:2,executable:'C:/fixture/tool.exe',executable_id:'opaque-fixture-backend-executable-binding',arguments:['space argument','<script>fixtureAttack()</script>','trailing\\'],workdir:'src',directory_id:'fixture-directory',timeout_ms:120000,output_limit:65536}),result_json:'{}'});
test('conversation rename keeps expected title and draft through conflicts, and cancels when selection changes',()=>{
  const r=renderer(),doc=r.dom.window.document,dialog=doc.getElementById('rename-dialog');dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new r.dom.window.Event('close'));};
  r.send({type:'sessions',sessions:[{id:'first',title:'<script>fixture title</script>'},{id:'second',title:'Other'}],selected:'first'});assert.equal(doc.getElementById('rename').hidden,true);
  r.send({type:'capabilities',execution:false,renameSessions:true,models:[]});assert.equal(doc.getElementById('rename').disabled,false);doc.getElementById('prompt').value='Keep composer draft';doc.getElementById('rename').click();assert.equal(dialog.open,true);
  const title=doc.getElementById('conversation-title');assert.equal(title.value,'<script>fixture title</script>');title.value='Renamed fixture';r.send({type:'sessions',sessions:[{id:'first',title:'Changed elsewhere'},{id:'second',title:'Other'}],selected:'first'});
  doc.getElementById('rename-form').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.deepEqual(JSON.parse(JSON.stringify(r.posted.at(-1))),{type:'rename-session',id:'first',title:'Renamed fixture',expected_title:'<script>fixture title</script>'});
  r.send({type:'rename-result',id:'first',success:false,text:'Title changed'});assert.equal(dialog.open,true);assert.equal(title.value,'Renamed fixture');assert.equal(doc.getElementById('rename-save').disabled,false);
  const sessions=doc.getElementById('sessions');sessions.value='second';sessions.dispatchEvent(new r.dom.window.Event('change'));assert.equal(dialog.open,false);assert.equal(title.value,'');r.send({type:'rename-result',id:'first',success:true});assert.equal(doc.getElementById('prompt').value,'Keep composer draft');
  const sent=r.posted.length;doc.getElementById('rename').click();doc.getElementById('rename-cancel').click();assert.equal(r.posted.length,sent);r.send({type:'capabilities',execution:false,models:[]});assert.equal(doc.getElementById('rename').hidden,true);r.dom.window.close();
});
test('footer displays only a recognized backend-reported provider wire',()=>{
  const r=renderer(),label=r.dom.window.document.getElementById('provider-mode');r.send({type:'provider-wire',wire:'responses'});assert.equal(label.textContent,'Responses');assert.equal(label.hidden,false);r.send({type:'provider-wire',wire:'chat-completions'});assert.equal(label.textContent,'Chat Completions');r.send({type:'provider-wire',wire:'<script>fixtureAttack()</script>'});assert.equal(label.hidden,true);assert.equal(label.textContent,'');
});
const processOutcomeFixture=()=>{
  const output='<script>fixtureAttack()</script>\u001b[31m\nfixture stdout';
  return {operation_id:'fixture-command',profile_id:'fixture-profile',pid:123,exit_code:7,termination:'exited',elapsed_ms:1500,
    stdout:{encoding:'utf-8',data:output,byte_count:90,retained_bytes:Buffer.byteLength(output)},
    stderr:{encoding:'hex',data:'ff00fe0a',byte_count:4,retained_bytes:4},truncated:true,process_tree_retired:true,independently_verified:false};
};
const processOutputFixture=(data,offset=0,channel='stdout',id='fixture-stream')=>({type:'event',event:{kind:'process.output',data:{operation_id:id,profile_id:'fixture-profile',channel,encoding:'hex',offset,retained_bytes:data.length/2,data}}});

test('model protocol failure shows only recognized diagnostics and preserves conversation context',()=>{
  const r=renderer(),doc=r.dom.window.document,card=doc.getElementById('run-failure');
  doc.getElementById('prompt').value='Keep unsent request';
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'model_protocol_error',protocol_error_code:'responses_arguments_mismatch',message:'private fixture provider body'}}});
  r.send({type:'transcript',history:[]});assert.equal(card.hidden,false);assert.match(card.textContent,/model response could not be validated/);assert.match(card.textContent,/Review the recorded tool outcomes/);assert.equal(card.querySelector('pre').textContent,'responses_arguments_mismatch');assert.equal(card.querySelector('.metrics'),null);assert.equal(doc.querySelector('#history .assistant'),null);assert.equal(doc.getElementById('prompt').value,'Keep unsent request');assert.ok(!card.textContent.includes('private fixture provider body'));
  for(const protocol_error_code of ['private-fixture-key','<script>fixtureAttack()</script>','responses_arguments_mismatch private-fixture-key',null,{}]){
    r.send({type:'event',event:{kind:'run.failed',data:{reason:'model_protocol_error',protocol_error_code}}});assert.equal(card.querySelector('details'),null);assert.ok(!card.textContent.includes('private-fixture-key'));assert.equal(card.querySelector('script'),null);
  }
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'agent_error',protocol_error_code:'responses_arguments_mismatch'}}});assert.equal(card.querySelector('details'),null);r.send({type:'reset-run'});assert.equal(card.hidden,true);r.dom.window.close();
});
test('provider failures remain visible through transcript refresh without invented replies or metrics',()=>{
  const r=renderer(),doc=r.dom.window.document,failure={type:'event',event:{kind:'run.failed',data:{reason:'provider_http_error',status:400,message:'<script>fixtureAttack()</script> private body'}}};
  r.send(failure);r.send(failure);r.send({type:'transcript',history:[]});
  const card=doc.getElementById('run-failure');assert.equal(card.hidden,false);assert.match(card.textContent,/HTTP 400/);assert.equal(card.querySelectorAll('h4').length,1);
  assert.equal(card.querySelector('.metrics'),null);assert.equal(doc.querySelector('#history .assistant'),null);assert.equal(doc.getElementById('empty').hidden,true);
  assert.ok(!card.textContent.includes('private body'));assert.ok(!card.textContent.includes('API key'));assert.equal(card.querySelector('script'),null);
  r.send({type:'reset-run'});assert.equal(card.hidden,true);r.send(failure);r.send({type:'history',history:[]});assert.equal(card.hidden,true);
  r.send(failure);r.send({type:'user',text:'Fixture next request'});assert.equal(card.hidden,true);
});
test('incompatible provider history and unavailable saved context explain recovery without fabricating a reply',()=>{
  const r=renderer(),doc=r.dom.window.document;
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'incompatible_provider_history',message:'private-fixture-history'}}});
  r.send({type:'transcript',history:[]});
  const card=doc.getElementById('run-failure');assert.equal(card.hidden,false);assert.match(card.textContent,/Start a new conversation/);assert.match(card.textContent,/recorded history is preserved/);
  assert.ok(!card.textContent.includes('private-fixture-history'));assert.equal(card.querySelector('.metrics'),null);assert.equal(doc.querySelector('#history .assistant'),null);
  for(const reason of ['context_unavailable','context_binding_changed']){
    r.send({type:'event',event:{kind:'run.failed',data:{reason,message:'private-fixture-history'}}});r.send({type:'transcript',history:[]});
    assert.match(card.textContent,/saved model context/);assert.match(card.textContent,/Start a new conversation/);assert.match(card.textContent,/recorded history is preserved/);assert.ok(!card.textContent.includes('private-fixture-history'));assert.equal(card.querySelector('.metrics'),null);assert.equal(doc.querySelector('#history .assistant'),null);
  }
  r.send({type:'reset-run'});assert.equal(card.hidden,true);
});
test('unknown failure reasons remain safe and DOM callback exceptions invalidate the fixture',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send({type:'event',event:{kind:'run.failed',data:{reason:'<img onerror=fixtureAttack()>',status:'401'}}});
  const card=doc.getElementById('run-failure');assert.match(card.textContent,/Execution failed/);assert.ok(!card.textContent.includes('401'));assert.equal(card.querySelector('img'),null);
  r.dom.window.close();
  const broken=renderer();broken.dom.window.document.getElementById('provider-settings').showModal=()=>{throw new Error('Synthetic callback failure');};broken.dom.window.document.getElementById('settings').click();
  assert.throws(()=>broken.dom.window.close(),/Renderer event callbacks must not fail silently/,'An actual DOM callback exception must invalidate the fixture instead of becoming a green test');
});
test('recorded provider identifiers render without raw error messages or unknown fields',()=>{
  const r=renderer(),doc=r.dom.window.document;
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'provider_http_error',status:400,provider_error_type:'invalid_request_error',provider_error_code:'unsupported_parameter',provider_error_param:'n',message:'private-fixture-key'}}});
  const card=doc.getElementById('run-failure');assert.match(card.textContent,/Type: invalid_request_error/);assert.match(card.textContent,/Code: unsupported_parameter/);assert.match(card.textContent,/Parameter: n/);assert.ok(!card.textContent.includes('private-fixture-key'));
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'provider_http_error',status:400,provider_error_type:'private-fixture-key',provider_error_code:'<script>fixtureAttack()</script>',provider_error_param:'private-fixture-key'}}});
  assert.equal(card.querySelector('details'),null);assert.ok(!card.textContent.includes('private-fixture-key'));assert.equal(card.querySelector('script'),null);
});

test('durable command output renders independent channels without executing markup or controls',()=>{
  const r=renderer(),doc=r.dom.window.document,text='<script>fixtureAttack()</script>\u001b[31m\nfixture output',initialMessages=r.posted.length;
  r.send(processOutputFixture(Buffer.from(text).toString('hex')));r.send(processOutputFixture('ff00fe',0,'stderr'));
  const card=doc.querySelector('.process-stream');assert.ok(card.textContent.includes('Captured command output · fixture-profile'));
  assert.ok(card.textContent.includes('<script>fixtureAttack()</script>\\u001b[31m'));assert.ok(card.textContent.includes('ff00fe'));assert.equal(card.querySelector('script'),null);assert.equal(r.dom.window.fixtureAttack,undefined);assert.equal(r.posted.length,initialMessages);
  assert.ok(!card.textContent.includes('Exited'));assert.ok(!card.textContent.includes('PID '));
});
test('output replay preserves fragmented Unicode and ignores only exact duplicate bytes',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send(processOutputFixture('f09f'));assert.equal(doc.querySelector('.process-stream pre').textContent,'f09f');
  r.send(processOutputFixture('f09f'));r.send(processOutputFixture('8c8d',2));assert.equal(doc.querySelector('.process-stream pre').textContent,'🌍');
  assert.ok(doc.querySelector('.process-stream summary').textContent.includes('4 retained bytes'));
  r.send(processOutputFixture('4142',0));assert.ok(doc.querySelector('.process-stream').textContent.includes('Output sequence incomplete'));assert.equal(doc.querySelector('.process-stream pre').textContent,'🌍');
});
test('gaps, malformed bytes and capture overflow never invent output',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send(processOutputFixture('not hex'));assert.equal(doc.querySelector('.process-stream'),null);
  r.send(processOutputFixture('41',5));assert.equal(doc.querySelector('.process-stream pre').textContent,'');assert.ok(doc.querySelector('.process-stream').textContent.includes('Output sequence incomplete'));
  r.send({type:'reset-run'});for(let i=0;i<16;i++)r.send(processOutputFixture('41'.repeat(4096),i*4096));
  assert.equal(doc.querySelector('.process-stream pre').textContent.length,65536);r.send(processOutputFixture('42',0,'stderr'));assert.ok(doc.querySelector('.process-stream').textContent.includes('Output sequence incomplete'));assert.equal(doc.querySelectorAll('.process-stream pre')[1].textContent,'');
});
test('recorded output survives transcript refresh and clears on run or conversation change',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send(processOutputFixture('4142'));r.send({type:'transcript',preserveLive:true,history:[]});assert.equal(doc.querySelector('.process-stream pre').textContent,'AB');assert.equal(doc.getElementById('empty').hidden,true);
  r.send({type:'reset-run'});assert.equal(doc.querySelector('.process-stream'),null);r.send(processOutputFixture('43'));r.send({type:'history',history:[]});assert.equal(doc.querySelector('.process-stream'),null);assert.equal(doc.getElementById('empty').hidden,false);
  r.send(processOutputFixture('44'));r.send({type:'user',text:'New fixture request'});assert.equal(doc.querySelector('.process-stream'),null);
});
test('the sidebar bounds command stream cards and leaves excess output in recorded activity',()=>{
  const r=renderer(),doc=r.dom.window.document;for(let i=0;i<65;i++)r.send(processOutputFixture('41',0,'stdout','fixture-stream-'+i));assert.equal(doc.querySelectorAll('.process-stream').length,64);assert.ok(doc.getElementById('process-stream-limit').textContent.includes('recorded command results'));
  r.send({type:'reset-run'});assert.equal(doc.getElementById('process-stream-limit'),null);
});

test('command approval reviews literal arguments before sending only a decision ID',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture();r.send({type:'operations',operations:[operation]});
  const section=doc.querySelector('#operations .operation'),payload=JSON.parse(operation.arguments_json);
  assert.equal(section.querySelector('.process-argv').textContent,JSON.stringify([payload.executable,...payload.arguments],null,2));
  assert.match(section.textContent,/Profile fixture-profile · revision 2/);assert.match(section.textContent,/Directory: src/);assert.match(section.textContent,/Timeout 120s/);assert.match(section.textContent,/65,536 bytes/);
  assert.equal(section.querySelectorAll('script').length,0);
  assert.ok(section.textContent.includes('Executable binding'));assert.ok(section.textContent.includes('opaque-fixture-backend-executable-binding'));
  const allow=[...section.querySelectorAll('button')].find(button=>button.textContent==='Allow command');allow.click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'decide',id:operation.id,decision:'allow'}));assert.ok([...section.querySelectorAll('button')].every(button=>button.disabled));r.dom.window.close();
});
test('approval displays bound guidance hashes and rejects malformed source metadata',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture(),plan=JSON.parse(operation.arguments_json);
  const source={path:'src/<script>fixtureAttack()</script>\u001bAGENTS.md',workspace_id:'fixture-root',file_id:'fixture-file',content_sha256:'a'.repeat(64),byte_count:17};
  plan.repository_guidance={version:1,directory:'src',sources:[source]};operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});
  const detail=doc.querySelector('.guidance-binding');assert.ok(detail);assert.ok(detail.textContent.includes(source.content_sha256));assert.ok(detail.textContent.includes('17 bytes'));assert.ok(detail.textContent.includes('\\u001b'));assert.equal(detail.querySelector('script'),null);assert.equal(r.dom.window.fixtureAttack,undefined);
  let allow=[...doc.querySelectorAll('#operations button')].find(x=>x.textContent==='Allow command');assert.equal(allow.disabled,false);assert.ok(detail.textContent.includes('a new approval'));
  for(const bad of [{...source,content_sha256:'not-a-hash'},{...source,byte_count:16385},{...source,file_id:''},null]){plan.repository_guidance.sources=[bad];operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});allow=[...doc.querySelectorAll('#operations button')].find(x=>x.textContent==='Allow command');assert.equal(allow.disabled,true);assert.ok(doc.querySelector('#operations').textContent.includes('binding is malformed'));}
  plan.repository_guidance.sources=[];operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});assert.ok(doc.querySelector('.guidance-binding').textContent.includes('No AGENTS.md sources'));allow=[...doc.querySelectorAll('#operations button')].find(x=>x.textContent==='Allow command');assert.equal(allow.disabled,false);r.dom.window.close();
});

test('file, command and MCP approvals render active skill bindings and fail closed for malformed snapshots',()=>{
  const r=renderer(),doc=r.dom.window.document;
  const skill={id:'Review <script>fixtureAttack()</script>\u001b',path:'.agents/skills/review/SKILL.md',workspace_id:'fixture-root',file_id:'fixture-skill-file',content_sha256:'b'.repeat(64),byte_count:37};
  for(const tool of ['replace_file','create_file','run_process','mcp_tool']){
    const operation=tool==='run_process'?processProposalFixture():{id:'fixture-'+tool,tool,state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,result_json:'{}'};
    const plan=tool==='run_process'?JSON.parse(operation.arguments_json):tool==='mcp_tool'?{server_config_id:'fixture-server',peer_tool:'fixture.write',config_revision:2,arguments_json:'{"body":"fixture"}'}:{path:'file.cpp',before_exists:tool!=='create_file',before_content:tool==='create_file'?'':'old',after_content:'new'};
    const field=tool==='mcp_tool'?'instructions':'repository_guidance',label=tool==='run_process'?'Allow command':tool==='replace_file'?'Allow edit':tool==='create_file'?'Allow creation':'Allow tool';
    const guidance={version:1,directory:'.',sources:[],skills:[skill]};plan[field]=guidance;operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});
    const detail=doc.querySelector('.guidance-binding');assert.ok(detail);assert.match(detail.textContent,/Repository and skill guidance/);assert.ok(detail.textContent.includes(skill.content_sha256));assert.ok(detail.textContent.includes('37 bytes'));assert.ok(detail.textContent.includes('\\u001b'));assert.equal(detail.querySelector('script'),null);assert.equal(r.dom.window.fixtureAttack,undefined);assert.equal([...doc.querySelectorAll('#operations button')].find(button=>button.textContent===label).disabled,false);
    for(const bad of [null,{...skill,id:''},{...skill,id:'x'.repeat(257)},{...skill,workspace_id:'other-root'},{...skill,content_sha256:'invalid'},{...skill,byte_count:16385}]){
      guidance.skills=[bad];operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});const buttons=[...doc.querySelectorAll('#operations button')];assert.equal(buttons.find(button=>button.textContent===label).disabled,true);assert.equal(buttons.find(button=>button.textContent==='Deny').disabled,false);assert.ok(doc.querySelector('#operations').textContent.includes('binding is malformed'));
    }
    for(const skills of [{},Array(9).fill(skill)]){guidance.skills=skills;operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});assert.equal([...doc.querySelectorAll('#operations button')].find(button=>button.textContent===label).disabled,true);}
    guidance.skills=[skill];operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});[...doc.querySelectorAll('#operations button')].find(button=>button.textContent===label).click();assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'decide',id:operation.id,decision:'allow'}));
    r.send({type:'operations',operations:[{...operation,state:'uncertain'}]});assert.ok(![...doc.querySelectorAll('#operations button')].some(button=>button.textContent===label||button.textContent==='Deny'));assert.ok(doc.querySelector('.guidance-binding').textContent.includes(skill.content_sha256));
  }
  r.dom.window.close();
});

test('approval polling preserves inspected nodes, focus and pending decisions while changed bindings replace the card',()=>{
 const r=renderer(),doc=r.dom.window.document;
 try{
  const source={id:'fixture-guide',path:'.agents/skills/fixture/SKILL.md',workspace_id:'fixture-root',file_id:'fixture-file',content_sha256:'a'.repeat(64),byte_count:17},guidance={version:1,directory:'.',sources:[],skills:[source]};
  for(const tool of ['create_file','replace_file','run_process','mcp_tool']){
   const item=tool==='run_process'?processProposalFixture():{id:'stable-'+tool,tool,state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,result_json:'{}'};
   const plan=tool==='run_process'?JSON.parse(item.arguments_json):tool==='mcp_tool'?{server_config_id:'fixture-peer',peer_tool:'fixture.write',config_revision:1,arguments_json:'{}'}:{path:'fixture.txt',before_content:'',after_content:'fixture'};
   plan[tool==='mcp_tool'?'instructions':'repository_guidance']=guidance;item.arguments_json=JSON.stringify(plan);
   r.send({type:'operations',operations:[item]});const card=doc.querySelector('#operations .operation'),detail=card.querySelector('.guidance-binding'),allow=[...card.querySelectorAll('button')].find(button=>button.textContent.startsWith('Allow'));
   detail.open=true;allow.focus();const observer=new r.dom.window.MutationObserver(()=>{});observer.observe(doc.getElementById('operations'),{childList:true,subtree:true});
   for(let i=0;i<5;i++)r.send({type:'operations',operations:[JSON.parse(JSON.stringify(item))]});
   assert.equal(observer.takeRecords().length,0,'Unchanged polls must not detach approval controls');assert.equal(doc.querySelector('#operations .operation'),card);assert.equal(detail.open,true);assert.equal(doc.activeElement,allow);
   const sibling={id:'stable-neighbor',tool:'create_file',workspace_id:'fixture-root',state:'cancelled',expires_unix_ms:item.expires_unix_ms,arguments_json:'{"path":"neighbor.txt","after_content":"fixture"}',result_json:'{}'};
   r.send({type:'operations',operations:[item,sibling]});r.send({type:'operations',operations:[item,{...sibling,state:'failed'}]});assert.equal(doc.querySelector('#operations .operation'),card);assert.equal(detail.open,true);assert.equal(doc.activeElement,allow);assert.equal(card.querySelectorAll('.guidance-binding').length,1);
   allow.click();const posted=r.posted.length;assert.equal(allow.disabled,true);r.send({type:'operations',operations:[item,sibling]});assert.equal(allow.disabled,true,'Polling must not reenable an in-flight decision');allow.click();assert.equal(r.posted.length,posted);assert.equal(r.posted.at(-1).id,item.id);
   const field=tool==='mcp_tool'?'instructions':'repository_guidance',changed={...plan,[field]:{...guidance,skills:[{...source,content_sha256:'b'.repeat(64)}]}};r.send({type:'operations',operations:[{...item,arguments_json:JSON.stringify(changed)},sibling]});const replacement=doc.querySelector('#operations .operation');assert.notEqual(replacement,card);assert.equal(card.isConnected,false);assert.ok(replacement.textContent.includes('b'.repeat(64)));
   const now=r.dom.window.Date.now;r.dom.window.Date.now=()=>item.expires_unix_ms+1;try{r.send({type:'operations',operations:[{...item,arguments_json:JSON.stringify(changed)},sibling]});assert.equal(doc.querySelector('#operations .operation'),replacement);assert.ok([...replacement.querySelectorAll('button')].every(button=>button.disabled));}finally{r.dom.window.Date.now=now;}
   observer.disconnect();r.send({type:'operations',operations:[]});assert.equal(doc.querySelectorAll('#operations .operation').length,0);
  }
  const uncertain={id:'stable-inspection',tool:'replace_file',state:'uncertain',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:'{"path":"fixture.txt","before_content":"old","after_content":"new"}',result_json:'{}'};
  r.send({type:'operations',operations:[uncertain]});r.send({type:'edit-inspection',id:uncertain.id,inspection:{match:'different',observed_unix_ms:1,same_file:true,observed:{path:'fixture.txt',size:7,content_sha256:'c'.repeat(64)}}});const inspection=doc.querySelector('.edit-inspection');r.send({type:'operations',operations:[uncertain]});assert.equal(doc.querySelector('.edit-inspection'),inspection);assert.match(inspection.textContent,/Differs from the recorded states/);
 }finally{r.dom.window.close();}
});

test('malformed or expired command proposals cannot be allowed',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture();
  r.send({type:'operations',operations:[{...operation,arguments_json:'{"profile_id":"fixture-profile"}'}]});
  let buttons=[...doc.querySelectorAll('#operations button')];assert.ok(buttons.find(button=>button.textContent==='Allow command').disabled);assert.ok(!buttons.find(button=>button.textContent==='Deny').disabled);
  assert.match(doc.querySelector('#operations').textContent,/details are unavailable/);
  for(const binding of [undefined,'',null,'contains\0NUL','x'.repeat(513)]){
    const plan=JSON.parse(operation.arguments_json);plan.executable_id=binding;r.send({type:'operations',operations:[{...operation,arguments_json:JSON.stringify(plan)}]});
    buttons=[...doc.querySelectorAll('#operations button')];assert.ok(buttons.find(button=>button.textContent==='Allow command').disabled);assert.ok(!buttons.find(button=>button.textContent==='Deny').disabled);
  }
  r.send({type:'operations',operations:[{...operation,expires_unix_ms:Date.now()-1000}]});buttons=[...doc.querySelectorAll('#operations button')];assert.ok(buttons.every(button=>button.disabled));r.dom.window.close();
});

test('command results preserve separate escaped output, hex bytes and actual exit metrics',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture(),result=processOutcomeFixture();
  r.send({type:'operations',operations:[{...operation,state:'succeeded',result_json:JSON.stringify(result)}]});const view=doc.querySelector('.process-result');
  assert.match(view.textContent,/Exited · exit 7/);assert.match(view.textContent,/PID 123/);assert.match(view.textContent,/1.50s/);
  assert.equal(view.querySelector('.process-stdout').textContent,'<script>fixtureAttack()</script>\\u001b[31m\nfixture stdout');assert.equal(view.querySelector('.process-stderr').textContent,'ff00fe0a');
  assert.match(view.textContent,/stderr · 4 bytes · 4 retained · hex/);assert.match(view.textContent,/capture limit/);assert.match(view.textContent,/not been independently verified/);
  assert.equal(view.querySelectorAll('script').length,0);assert.equal(doc.querySelectorAll('#operations button').length,0);
  r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(result),tool_call_id:'fixture-call'}}]});
  assert.ok(doc.querySelector('#history .process-result'));assert.match(doc.querySelector('#history summary').textContent,/Command result/);r.dom.window.close();
});

test('uncertain commands remain blocked and malformed outcomes retain raw evidence',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture(),result={...processOutcomeFixture(),termination:'cancelled',exit_code:1};
  r.send({type:'operations',operations:[{...operation,state:'uncertain',result_json:JSON.stringify(result)}]});
  assert.match(doc.querySelector('#operations').textContent,/Cancelled · exit 1/);assert.match(doc.querySelector('#operations').textContent,/through this profile are blocked/);assert.match(doc.querySelector('#operations').textContent,/will not retry/);assert.equal(doc.querySelectorAll('#operations button').length,0);
  const malformed={...result,stderr:{encoding:'hex',data:'not valid hex'}};r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(malformed)}}]});
  assert.equal(doc.querySelectorAll('#history .process-result').length,0);assert.equal(doc.querySelector('#history pre').textContent,JSON.stringify(malformed));r.dom.window.close();
});

test('command output metrics require exact encoded retained bytes and safe drained counts',()=>{
  const r=renderer(),doc=r.dom.window.document,result=processOutcomeFixture();result.stdout={encoding:'utf-8',data:'🌍\0',byte_count:5,retained_bytes:5};
  r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(result)}}]});assert.equal(doc.querySelector('.process-stdout').textContent,'🌍\\u0000');assert.ok(doc.querySelector('.process-result').textContent.includes('5 bytes · 5 retained'));
  for(const patch of [{retained_bytes:3},{byte_count:4},{byte_count:-1},{byte_count:undefined},{retained_bytes:undefined},{byte_count:Number.MAX_SAFE_INTEGER+1}]){
    const malformed={...result,stdout:{...result.stdout,...patch}};r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(malformed)}}]});
    assert.equal(doc.querySelector('.process-result'),null);assert.equal(doc.querySelector('#history pre').textContent,JSON.stringify(malformed));
  }
  const malformed={...result,stderr:{...result.stderr,retained_bytes:3}};r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(malformed)}}]});assert.equal(doc.querySelector('.process-result'),null);assert.equal(doc.querySelector('#history pre').textContent,JSON.stringify(malformed));r.dom.window.close();
});
test('new-file review distinguishes absence, previews exact content and never retries uncertainty',()=>{
  const r=renderer(),doc=r.dom.window.document;const operation={id:'fixture-create',tool:'create_file',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({path:'new.cpp',parent_id:'fixture-parent',before_exists:false,before_content:'',after_content:'actual proposed source\n'}),result_json:'{}'};
  r.send({type:'operations',operations:[operation]});assert.equal(doc.querySelector('#operations .after').textContent,'actual proposed source\n');assert.match(doc.querySelector('#operations').textContent,/New file/);
  const controls=[...doc.querySelectorAll('#operations button')];controls.find(button=>button.textContent==='Compare changes').click();assert.equal(r.posted.at(-1).type,'review');assert.equal(r.posted.at(-1).id,operation.id);
  controls.find(button=>button.textContent==='Allow creation').click();assert.equal(r.posted.at(-1).type,'decide');assert.equal(r.posted.at(-1).decision,'allow');
  r.send({type:'operations',operations:[{...operation,state:'uncertain'}]});assert.equal(doc.querySelectorAll('#operations button').length,0);assert.match(doc.querySelector('#operations').textContent,/Creation is uncertain/);r.dom.window.close();
});

test('external MCP approval names the server and tool and uncertainty never offers replay',()=>{
  const r=renderer(),doc=r.dom.window.document;
  const operation={id:'fixture-mcp',tool:'mcp_tool',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({server_config_id:'<fixture-server>',peer_tool:'fixture.write',config_revision:2,arguments_json:'{"body":"fixture"}'}),result_json:'{}'};
  r.send({type:'operations',operations:[operation]});
  assert.match(doc.querySelector('#operations').textContent,/External tool: <fixture-server> \/ fixture.write/);
  assert.equal(doc.querySelectorAll('#operations fixture-server').length,0);
  const allow=[...doc.querySelectorAll('#operations button')].find(button=>button.textContent==='Allow tool');assert.ok(allow);allow.click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'decide',id:operation.id,decision:'allow'}));
  r.send({type:'operations',operations:[{...operation,state:'uncertain'}]});
  assert.equal(doc.querySelectorAll('#operations button').length,0);assert.match(doc.querySelector('#operations').textContent,/on this configured server remain blocked/);
  r.send({type:'operations',operations:[{...operation,state:'succeeded'}]});assert.match(doc.querySelector('#operations').textContent,/not independently verified/);r.dom.window.close();
});
test('run detail selector preserves conversation history and blocks submission while another run is active',()=>{
  const r=renderer(),doc=r.dom.window.document;
  r.send({type:'capabilities',execution:true,models:[{id:'fixture-model'}],model:'fixture-model'});
  r.send({type:'history',history:[{role:'assistant',data:{content:'Existing fixture history'}}]});
  r.send({type:'runs',runs:[{id:'older',state:'failed'},{id:'latest',state:'running'}],selected:'older',busy:true});
  r.send({type:'status',text:'failed'});
  r.send({type:'capabilities',execution:true,models:[{id:'fixture-model'}],model:'fixture-model'});
  assert.equal(doc.querySelector('#run-picker').hidden,false);assert.equal(doc.querySelector('#runs').value,'older');assert.equal(doc.querySelector('#send').disabled,true);
  doc.querySelector('#prompt').value='Must not submit while another run is active';doc.querySelector('#prompt').dispatchEvent(new r.dom.window.KeyboardEvent('keydown',{key:'Enter'}));assert.ok(!r.posted.some(message=>message.type==='send'));
  doc.querySelector('#runs').value='latest';doc.querySelector('#runs').dispatchEvent(new r.dom.window.Event('change'));
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'select-run',id:'latest'}));
  r.send({type:'event',event:{kind:'model.text',data:{text:'Selected fixture stream'}}});r.send({type:'reset-run'});
  assert.equal(doc.querySelector('#live').childElementCount,0);assert.equal(doc.querySelector('#history .message-body').textContent.trim(),'Existing fixture history');
  r.send({type:'runs',runs:[{id:'older',state:'failed'},{id:'latest',state:'completed'}],selected:'older',busy:false});assert.equal(doc.querySelector('#send').disabled,false);
  r.dom.window.close();
});
test('uncertain edit inspection renders escaped observations and never offers an effect approval',()=>{
  const r=renderer();const operation={id:'fixture-uncertain',tool:'replace_file',state:'uncertain',workspace_id:'fixture-root',expires_unix_ms:Date.now(),arguments_json:'{}',result_json:'{}'};
  r.send({type:'operations',operations:[operation]});
  const doc=r.dom.window.document,buttons=[...doc.querySelectorAll('#operations button')];assert.equal(buttons.length,1);assert.equal(buttons[0].textContent,'Inspect actual file');buttons[0].click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'inspect-edit',id:operation.id}));
  const inspection={match:'after',same_file:true,observed_unix_ms:Date.now(),observed:{path:'<script>fixtureAttack()</script>',size:3,content_sha256:'b'.repeat(64)}};
  r.send({type:'edit-inspection',id:operation.id,inspection});
  assert.match(doc.querySelector('.edit-inspection').textContent,/Matches recorded after state/);
  assert.match(doc.querySelector('.edit-inspection').textContent,/outcome remains uncertain/);
  assert.match(doc.querySelector('.edit-inspection pre').textContent,/<script>fixtureAttack/);assert.equal(doc.querySelectorAll('#operations script').length,0);
  r.send({type:'operations',operations:[]});r.send({type:'edit-inspection',id:operation.id,inspection});assert.equal(doc.querySelectorAll('.edit-inspection').length,0);r.dom.window.close();
});
test('durable response profile badges use recorded identities and reject malformed metadata',()=>{
  const r=renderer(),context={profile_id:'saved-profile',profile_revision:2,route_id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',model_id:'fixture-claude'};
  const show=value=>r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic renderer response',provider_context:value}}]});show(context);
  const doc=r.dom.window.document;assert.equal(doc.querySelector('.provider-context').textContent,'Claude · Messages');assert.match(doc.querySelector('.provider-context').title,/saved-profile\nProfile version: 2/);assert.match(doc.querySelector('.metrics').textContent,/Input —Output —Total —/);
  for(const bad of [{...context,api_key:'synthetic-private-key'},{...context,profile_revision:0},{...context,profile_id:'<img onerror=fixtureAttack()>'},{...context,wire:'unknown'}]){show(bad);assert.equal(doc.querySelector('.provider-context'),null);assert.ok(!doc.body.textContent.includes('synthetic-private-key'));}
  show(undefined);assert.equal(doc.querySelector('.provider-context'),null,'Old history cannot invent a current profile');r.dom.window.close();
});
test('history renders Markdown/code, copies plain code and shows exact provider usage',()=>{
  const r=renderer();r.send({type:'history',history:[{role:'assistant',data:{content:'## Fixture heading\n\n**Fixture bold**\n\n```js\nx < y\n```',model:'synthetic-renderer-fixture',usage:{prompt_tokens:12,completion_tokens:6,total_tokens:18},elapsed_ms:1500}}]});
  const doc=r.dom.window.document;assert.equal(doc.querySelector('.markdown h2').textContent,'Fixture heading');assert.equal(doc.querySelector('.markdown strong').textContent,'Fixture bold');
  assert.match(doc.querySelector('.metrics').textContent,/Input 12Output 6Total 18/);assert.match(doc.querySelector('.metrics').textContent,/1.50s/);
  doc.querySelector('.copy-code').click();assert.equal(r.posted.at(-1).type,'copy');assert.equal(r.posted.at(-1).text,'x < y\n');r.dom.window.close();
});
test('unknown historical usage stays unavailable and malicious Markdown is sanitized',()=>{
  const r=renderer();r.send({type:'history',history:[{role:'assistant',data:{content:'<script>fixtureAttack()</script><img src="https://fixture.invalid" onerror="fixtureAttack()"><iframe src="https://fixture.invalid"></iframe>[fixture](javascript:fixtureAttack())'}}]});
  const card=r.dom.window.document.querySelector('.message');assert.equal(card.querySelectorAll('script,iframe,[onerror]').length,0);assert.equal(card.querySelectorAll('a[href^="javascript:"]').length,0);
  assert.equal(card.querySelector('.metrics').textContent,'Input —Output —Total —');r.dom.window.close();
});
test('editor context appends to an existing question as plain draft text without submitting',()=>{
 const r=renderer(),doc=r.dom.window.document;try{r.send({type:'workspace',root:'D:\\Repo',roots:[]});doc.getElementById('prompt').value='Explain this code';r.send({type:'append-context',root:'D:\\Repo',text:'Synthetic editor context <script>fixtureAttack()</script>'});assert.equal(doc.getElementById('prompt').value,'Explain this code\n\nSynthetic editor context <script>fixtureAttack()</script>');assert.equal(doc.querySelectorAll('#prompt script').length,0);assert.equal(doc.activeElement,doc.getElementById('prompt'));assert.ok(!r.posted.some(m=>m.type==='send'));}finally{r.dom.window.close();}
});
test('foreign workspace, cleared root, invalid and oversized editor context cannot replace the draft',()=>{
 const r=renderer(),doc=r.dom.window.document;try{r.send({type:'workspace',root:'D:\\Repo',roots:[]});doc.getElementById('prompt').value='Preserve my draft';for(const message of [{root:'D:\\Elsewhere',text:'Foreign'},{root:'D:\\Repo',text:42},{root:'D:\\Repo',text:'x'.repeat(49153)}]){r.send({type:'append-context',...message});assert.equal(doc.getElementById('prompt').value,'Preserve my draft');}r.send({type:'workspace-clear'});r.send({type:'append-context',root:'D:\\Repo',text:'Late'});assert.equal(doc.getElementById('prompt').value,'');r.send({type:'workspace',root:'D:\\Repo',roots:[]});const large='x'.repeat(262140);doc.getElementById('prompt').value=large;r.send({type:'append-context',root:'D:\\Repo',text:'More context'});assert.equal(doc.getElementById('prompt').value,large);assert.match(doc.getElementById('status').textContent,/draft is too large/);}finally{r.dom.window.close();}
});
test('file creation approval discloses parent directory effects as escaped paths',()=>{
 const r=renderer(),doc=r.dom.window.document;
 r.send({type:'operations',operations:[{id:'nested-create',tool:'create_file',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({path:'src/deep/file.ts',before_content:'',after_content:'new file',create_directories:['src','src/<script>fixtureAttack()</script>']}),result_json:'{}'}]});
 assert.match(doc.getElementById('operations').textContent,/also creates these missing parent folders/);assert.deepEqual([...doc.querySelectorAll('#operations li')].map(e=>e.textContent),['src','src/<script>fixtureAttack()</script>']);assert.equal(doc.querySelectorAll('#operations script').length,0);assert.ok([...doc.querySelectorAll('#operations button')].some(e=>e.textContent==='Allow creation'));r.dom.window.close();
});
test('provider failure codes render actionable messages and exact failed-response usage',()=>{
 const r=renderer(),doc=r.dom.window.document;
 const messages={responses_server_error:/provider reported a server error/,responses_output_token_limit:/reached its output token limit/,responses_content_filter:/content filter/,responses_rate_limit:/reported a rate limit/};
 for(const [code,expected] of Object.entries(messages)){
  r.send({type:'reset-run'});r.send({type:'event',event:{kind:'model.usage',data:{prompt_tokens:17,completion_tokens:8,total_tokens:25}}});r.send({type:'event',event:{kind:'run.failed',data:{reason:'model_protocol_error',protocol_error_code:code,private:'DO_NOT_ECHO'}}});
  assert.match(doc.getElementById('run-failure').textContent,expected);assert.ok(doc.getElementById('run-failure').textContent.includes(code));assert.match(doc.querySelector('#live .metrics').textContent,/Input 17Output 8Total 25/);assert.ok(!doc.getElementById('run-failure').textContent.includes('DO_NOT_ECHO'));
 }
 r.dom.window.close();
});
test('live token badges use model usage events and reset when the persisted transcript arrives',()=>{
  const r=renderer();r.send({type:'event',event:{kind:'model.text',data:{text:'Synthetic renderer fixture'}}});r.send({type:'event',event:{kind:'model.usage',data:{prompt_tokens:8,completion_tokens:0,total_tokens:8}}});
  assert.equal(r.dom.window.document.querySelector('#live .metrics').textContent,'Input 8Output 0Total 8');
  r.send({type:'transcript',history:[{role:'assistant',data:{content:'Synthetic renderer fixture',usage:{prompt_tokens:8,completion_tokens:0,total_tokens:8}}}]});
  assert.equal(r.dom.window.document.querySelector('#live').childElementCount,0);assert.equal(r.dom.window.document.querySelectorAll('#history .message').length,1);r.dom.window.close();
});
test('tool-turn checkpoint starts a new live response and history refresh preserves its received prefix',()=>{
  const r=renderer();r.send({type:'event',event:{kind:'model.text',data:{text:'First fixture response'}}});r.send({type:'event',event:{kind:'conversation.tool_turn',data:{}}});
  r.send({type:'event',event:{kind:'model.text',data:{text:'Second fixture prefix'}}});r.send({type:'transcript',preserveLive:true,history:[{role:'assistant',data:{content:'First fixture response'}}]});
  assert.equal(r.dom.window.document.querySelector('#live .message-body').textContent.trim(),'Second fixture prefix');assert.equal(r.dom.window.document.querySelectorAll('#history .message').length,1);r.dom.window.close();
});
test('bottom model selector exposes only backend-advertised IDs and sends the selected ID',()=>{
  const r=renderer();r.send({type:'capabilities',execution:true,model:'synthetic-catalogue-a',models:[{id:'synthetic-catalogue-a'},{id:'synthetic-catalogue-b'}]});
  const selector=r.dom.window.document.querySelector('footer #model');assert.ok(selector);assert.equal(selector.disabled,false);assert.deepEqual([...selector.options].map(option=>option.value),['','synthetic-catalogue-a','synthetic-catalogue-b']);
  selector.value='synthetic-catalogue-b';selector.dispatchEvent(new r.dom.window.Event('change'));assert.equal(r.posted.at(-1).type,'model');assert.equal(r.posted.at(-1).id,'synthetic-catalogue-b');r.dom.window.close();
});
test('provider cache/reasoning counts and backend first-token time render without derived estimates',()=>{
  const r=renderer();r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic metrics fixture',usage:{prompt_tokens:12,completion_tokens:6,total_tokens:18,prompt_tokens_details:{cached_tokens:4},completion_tokens_details:{reasoning_tokens:2}},first_token_ms:200,elapsed_ms:1250}}]});
  const text=r.dom.window.document.querySelector('.metrics').textContent;assert.match(text,/Cached 4/);assert.match(text,/Reasoning 2/);assert.match(text,/First token 0.20s/);assert.match(text,/1.25s/);r.dom.window.close();
});
test('Claude usage events and persisted metadata keep uncached input, cache writes and reads distinct',()=>{
  const r=renderer(),doc=r.dom.window.document,usage={input_tokens:2,output_tokens:5,cache_creation_input_tokens:13,cache_read_input_tokens:21,prompt_tokens:2,completion_tokens:5,input_tokens_scope:'uncached',prompt_tokens_details:{cached_tokens:21}};
  try{
    r.send({type:'event',event:{kind:'model.text',data:{text:'Synthetic Claude metrics fixture'}}});r.send({type:'event',event:{kind:'model.usage',data:usage}});
    const live=doc.querySelector('#live .metrics');assert.deepEqual([...live.children].map(el=>el.textContent),['Input (uncached) 2','Output 5','Total —','Cache write 13','Cached 21']);assert.match(live.title,/excludes cache writes and reads/);
    const context={profile_id:'fixture-claude',profile_revision:2,route_id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',model_id:'fixture-recorded-claude'};
    r.send({type:'transcript',history:[{role:'assistant',data:{content:'Synthetic Claude metrics fixture',usage,model:context.model_id,provider_context:context,first_token_ms:125,elapsed_ms:1100}}]});
    assert.equal(doc.getElementById('live').childElementCount,0);const recorded=doc.querySelector('#history .metrics');assert.deepEqual([...recorded.children].map(el=>el.textContent),['Input (uncached) 2','Output 5','Total —','Cache write 13','Cached 21','fixture-recorded-claude','Claude · Messages','First token 0.13s','1.10s']);assert.ok(!recorded.textContent.includes('Total 41'),'Cache accounting must not invent a total');assert.match(recorded.querySelector('.provider-context').title,/fixture-claude\nProfile version: 2/);
  }finally{r.dom.window.close();}
});
test('zero Claude counters remain visible while absent input and output stay unavailable',()=>{
  const r=renderer(),doc=r.dom.window.document;try{
    r.send({type:'event',event:{kind:'model.usage',data:{prompt_tokens:0,completion_tokens:0,input_tokens_scope:'uncached',cache_creation_input_tokens:0,prompt_tokens_details:{cached_tokens:0}}}});
    assert.deepEqual([...doc.querySelector('#live .metrics').children].map(el=>el.textContent),['Input (uncached) 0','Output 0','Total —','Cache write 0','Cached 0']);
    r.send({type:'transcript',history:[{role:'assistant',data:{content:'Synthetic absent input/output fixture',usage:{cache_creation_input_tokens:0,cache_read_input_tokens:0,prompt_tokens_details:{cached_tokens:0}}}}]});
    assert.deepEqual([...doc.querySelector('#history .metrics').children].map(el=>el.textContent),['Input —','Output —','Total —','Cache write 0','Cached 0']);
    r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic raw-only fixture',usage:{input_tokens:9,output_tokens:4,cache_read_input_tokens:8,input_tokens_scope:'uncached'}}}]});
    assert.deepEqual([...doc.querySelector('#history .metrics').children].map(el=>el.textContent),['Input (uncached) —','Output —','Total —'],'The view consumes normalized counters without computing or translating missing ones');
  }finally{r.dom.window.close();}
});
test('cache-write badges require supplied safe nonnegative integer counts',()=>{
  const r=renderer(),doc=r.dom.window.document;try{
    for(const cache_creation_input_tokens of [undefined,null,-1,0.5,'12',true,NaN,Infinity,Number.MAX_SAFE_INTEGER+1,{},[]]){
      const usage={prompt_tokens:3,completion_tokens:1,input_tokens_scope:'uncached',cache_creation_input_tokens,prompt_tokens_details:{cached_tokens:0}};
      r.send({type:'event',event:{kind:'model.usage',data:usage}});assert.deepEqual([...doc.querySelector('#live .metrics').children].map(el=>el.textContent),['Input (uncached) 3','Output 1','Total —','Cached 0']);
      r.send({type:'transcript',history:[{role:'assistant',data:{content:'Synthetic invalid cache-write fixture',usage}}]});assert.deepEqual([...doc.querySelector('#history .metrics').children].map(el=>el.textContent),['Input (uncached) 3','Output 1','Total —','Cached 0']);
    }
  }finally{r.dom.window.close();}
});
test('common provider metrics keep their original label unless native usage marks uncached input',()=>{
  const r=renderer(),doc=r.dom.window.document;try{
    for(const input_tokens_scope of [undefined,'all','UNCACHED',['uncached'],{scope:'uncached'}]){
      r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic common metrics fixture',usage:{prompt_tokens:12,completion_tokens:6,total_tokens:31,input_tokens_scope,prompt_tokens_details:{cached_tokens:0},completion_tokens_details:{reasoning_tokens:0}},first_token_ms:200,elapsed_ms:1250}}]});
      assert.deepEqual([...doc.querySelector('#history .metrics').children].map(el=>el.textContent),['Input 12','Output 6','Total 31','Cached 0','Reasoning 0','First token 0.20s','1.25s']);
    }
  }finally{r.dom.window.close();}
});

test('pending file comparison sends only an operation ID and remains separate from approval',()=>{
  const r=renderer();r.send({type:'operations',operations:[{id:'fixture-edit',tool:'replace_file',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({path:'file.cpp',before_content:'old',after_content:'new'})}]});
  const buttons=[...r.dom.window.document.querySelectorAll('#operations button')];
  const compare=buttons.find(button=>button.textContent==='Compare changes');assert.ok(compare);compare.click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'review',id:'fixture-edit'}));
  assert.ok(!buttons.find(button=>button.textContent==='Allow edit').disabled);r.dom.window.close();
});

test('top-right settings dialog clears key input and leaves model selection in the footer',()=>{
  const r=renderer(),doc=r.dom.window.document,dialog=doc.getElementById('provider-settings');
  // jsdom has no dialog implementation; emulate only these native DOM methods.
  dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new r.dom.window.Event('close'));};
  assert.ok(doc.querySelector('header #settings'));assert.equal(doc.querySelector('footer #configureModel'),null);
  doc.getElementById('settings').click();assert.equal(dialog.open,true);assert.equal(doc.getElementById('provider-key').type,'password');doc.getElementById('provider-key').value='synthetic-ui-key';
  doc.getElementById('provider-form').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.equal(r.posted.at(-1).type,'saveProviderKey');assert.equal(r.posted.at(-1).key,'synthetic-ui-key');assert.equal(doc.getElementById('provider-key').value,'');
  r.send({type:'model-list',models:[{id:'fixture-returned-model'}]});r.send({type:'settings-state',complete:true,busy:false,text:'Models fetched'});assert.equal(dialog.open,false);assert.equal(doc.getElementById('model').disabled,false);assert.equal(doc.getElementById('send').disabled,true);assert.equal(r.posted.filter(m=>m.type==='model').length,0);
  const model=doc.getElementById('model');model.value='fixture-returned-model';model.dispatchEvent(new r.dom.window.Event('change'));assert.equal(r.posted.at(-1).id,'fixture-returned-model');r.dom.window.close();
});
function graphRenderFixture(){return {type:'graph',record:{run:{id:'fixture-root',state:'paused'},graph_id:'fixture.flow',graph_revision:2,checkpoint_revision:4,spec:{nodes:[{id:'answer',type:'human',prompt:'Choose input <script>fixtureAttack()</script>'},{id:'worker',type:'agent'}]},checkpoint:{nodes:[{id:'answer',state:'waiting_human'},{id:'worker',state:'completed'}]}},children:[{id:'child-worker',node_id:'worker',state:'completed'}],histories:{'child-worker':[{role:'assistant',data:{content:'**Synthetic child response**',model:'fixture-model',usage:{prompt_tokens:12,completion_tokens:7,total_tokens:19},elapsed_ms:1400}}]}};}
test('graph human form preserves a draft across checkpoints and sends raw input with the displayed revision',()=>{
 const r=renderer(),doc=r.dom.window.document,fixture=graphRenderFixture();r.send(fixture);const input=doc.querySelector('.graph-input textarea');input.value='{"path":"left.txt","path":"right.txt"}';fixture.record.checkpoint_revision=5;r.send(fixture);assert.equal(doc.querySelector('.graph-input textarea'),input);assert.equal(input.value,'{"path":"left.txt","path":"right.txt"}');
 doc.querySelector('.graph-input').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.equal(r.posted.at(-1).type,'graph-input');assert.equal(r.posted.at(-1).root,'fixture-root');assert.equal(r.posted.at(-1).node,'answer');assert.equal(r.posted.at(-1).revision,5);assert.equal(r.posted.at(-1).input_json,input.value);assert.equal(doc.querySelector('#graph-view script'),null);
 fixture.record.run.state='completed';fixture.record.checkpoint.nodes[0]={id:'answer',state:'completed',output:{accepted:true}};r.send(fixture);assert.equal(doc.querySelector('.graph-input'),null);r.send({type:'graph-clear'});assert.equal(doc.getElementById('graph-view').hidden,true);r.dom.window.close();
});
test('graph child responses retain actual supplied per-response metrics independently of the root transcript',()=>{
 const r=renderer(),doc=r.dom.window.document;r.send(graphRenderFixture());const worker=[...doc.querySelectorAll('.graph-node')].find(row=>row.querySelector('h4').textContent.startsWith('worker ·')),child=worker.querySelector('.graph-responses');assert.ok(child.textContent.includes('Synthetic child response'));assert.ok(child.textContent.includes('Input 12'));assert.ok(child.textContent.includes('Output 7'));assert.equal(doc.getElementById('history').children.length,0);assert.equal(doc.getElementById('live').children.length,0);
 r.send({type:'graph-event',node_id:'worker',event:{run_id:'child-worker',kind:'model.text',data:{text:'Synthetic next turn'}}});r.send({type:'graph-event',node_id:'worker',event:{run_id:'child-worker',kind:'model.usage',data:{prompt_tokens:9,completion_tokens:3,total_tokens:12}}});assert.ok(worker.querySelector('.graph-live').textContent.includes('Input 9'));assert.equal(doc.getElementById('live').children.length,0);r.dom.window.close();
});
test('footer workflow chooser enables a real advertised tool graph on a model-free backend',()=>{
 const r=renderer(),doc=r.dom.window.document;r.send({type:'capabilities',execution:false,models:[]});r.send({type:'graphs',graphs:[{id:'fixture.read',revision:1,node_count:1,executable:true},{id:'fixture.unavailable',revision:1,node_count:1,executable:false}],selected:'fixture.read'});assert.equal(doc.querySelector('footer #workflow').value,'fixture.read');assert.equal(doc.getElementById('send').disabled,false);assert.equal(doc.querySelector('#workflow option[value="fixture.unavailable"]').disabled,true);doc.getElementById('prompt').value='Actual task intent';doc.getElementById('send').click();assert.equal(r.posted.at(-1).type,'send');assert.equal(r.posted.at(-1).prompt,'Actual task intent');r.dom.window.close();
});
test('graph join summary keeps full observed outputs expandable without fabricated aggregate metrics',()=>{
 const r=renderer(),doc=r.dom.window.document;r.send({type:'transcript',history:[{role:'assistant',data:{source:'graph_join',graph_id:'fixture.read',content:'Raw joined JSON text',nodes:[{id:'read',state:'completed',output:{content:'Observed fixture bytes'}}]}}]});const card=doc.querySelector('#history .message');assert.ok(card.textContent.includes('Completed graph fixture.read.'));assert.equal(card.querySelector('.metrics'),null);assert.equal(card.querySelector('details').open,false);assert.ok(card.querySelector('pre').textContent.includes('Observed fixture bytes'));assert.equal(card.querySelector('.markdown'),null);r.dom.window.close();
});
