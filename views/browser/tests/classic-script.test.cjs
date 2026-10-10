'use strict';
// Real product scripts/HTML in one classic-script realm. Backend replies and
// retained histories are explicitly synthetic; no native/live state is used.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {JSDOM}=require('../../../extensions/vscode/node_modules/jsdom');
const {browserHtml}=require('../page.cjs');
const root=path.resolve(__dirname,'../../..');
const sources={
  '/ui/patch-review.js':'extensions/vscode/patch-review.js',
  '/ui/client.js':'extensions/vscode/client.js',
  '/ui/browser.js':'views/browser/browser.js',
  '/ui/marked.js':'extensions/vscode/node_modules/marked/lib/marked.umd.js',
  '/ui/purify.js':'extensions/vscode/node_modules/dompurify/dist/purify.min.js',
  '/ui/chat.js':'extensions/vscode/media/chat.js'
};

test('full webpage script order streams through the cookie bridge, renders real fixture metrics and detaches without commands',async()=>{
 const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'}),window=dom.window,document=window.document,requests=[];
 assert.ok(document.getElementById('workspace-picker'));assert.ok(document.getElementById('add-workspace'));assert.equal(document.getElementById('workspace-add-dialog').open,false);
 const run={id:'root',session_id:'session',parent_id:'',node_id:'',graph_root:false,state:'running'};let body,streamSignal,history=[];
 const frame=(type,data,id)=>`${id===undefined?'':'id: '+id+'\n'}event: ${type}\ndata: ${JSON.stringify(data)}\n\n`,send=(type,data,id)=>body.enqueue(new TextEncoder().encode(frame(type,data,id)));
 window.AbortController=AbortController;window.AbortSignal=AbortSignal;window.TextDecoder=TextDecoder;window.TextEncoder=TextEncoder;
 for(const dialog of document.querySelectorAll('dialog')){dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new window.Event('close'));};}
 let rejectFirstSignal=true;window.fetch=async(input,options={})=>{if(rejectFirstSignal&&options.signal){rejectFirstSignal=false;throw new TypeError("Failed to execute 'fetch' on 'Window': Failed to read the 'signal' property from 'RequestInit': Failed to convert value to 'AbortSignal'.");}const url=new URL(input,window.location.origin);requests.push(url.pathname+url.search);assert.equal(url.origin,window.location.origin);assert.equal(options.credentials,'same-origin');assert.ok(!options.headers?.Authorization);
  if(url.pathname.endsWith('/events/stream')){assert.equal(options.headers.Accept,'text/event-stream');streamSignal=options.signal;return new Response(new ReadableStream({start(controller){body=controller;send('observation',{run,scope:'run',after:0});options.signal.addEventListener('abort',()=>{try{controller.error(options.signal.reason);}catch{}},{once:true});}}),{headers:{'Content-Type':'text/event-stream'}});}
  let data;if(url.pathname==='/ui/session'||url.pathname==='/ui/session/disconnect')data={};else if(url.pathname==='/ui/workspaces')data={can_add:true,selected_workspace_id:'windows-local-file-v1:synthetic',workspaces:[{workspace_id:'windows-local-file-v1:synthetic',name:'Example',root:'D:\\Projects\\Example'}]};else if(url.pathname==='/v1/health')data={status:'ok',agent_execution:true};else if(url.pathname==='/v1/workspace')data={configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:synthetic',authority_id:'b'.repeat(32)};else if(url.pathname==='/v1/models')data={models:[{id:'fixture-model'}],default_model:'fixture-model'};else if(url.pathname==='/v1/graphs')data={graphs:[]};else if(url.pathname==='/v1/sessions')data=[{id:'session',title:'Synthetic streamed conversation'}];else if(url.pathname==='/v1/sessions/session/history')data=history;else if(url.pathname==='/v1/sessions/session/runs')data=[run];else if(url.pathname==='/v1/runs/root')data=run;else if(url.pathname.endsWith('/operations')||url.pathname.endsWith('/events'))data=[];else if(url.pathname==='/v1/provider/profiles')return {ok:false,status:404,json:async()=>({detail:'Synthetic absent profile metadata'})};else if(url.pathname==='/v1/provider/configuration')data={provider:'openai',wire:'responses',revision:0,configured:false,model:''};else throw Error('Unexpected webpage fixture route '+url.pathname);return {ok:true,status:200,json:async()=>structuredClone(data)};
 };
 async function until(predicate){for(let n=0;n<200&&!predicate();n++)await new Promise(resolve=>setImmediate(resolve));assert.ok(predicate(),'Full webpage reached the expected streamed state: '+JSON.stringify({status:document.getElementById('status').textContent,history:document.getElementById('history').textContent,live:document.getElementById('live').innerHTML,events:document.getElementById('events').textContent,requests}));}
 try{for(const script of document.querySelectorAll('script')){const route=script.getAttribute('src');new vm.Script(fs.readFileSync(path.join(root,sources[route]),'utf8'),{filename:route}).runInContext(dom.getInternalVMContext());}
  await until(()=>body&&document.getElementById('workspace-info').textContent.startsWith('Connected')&&document.getElementById('status').textContent==='running');assert.match(document.getElementById('workspace-info').textContent,/D:\\Projects\\Example/);assert.equal(document.getElementById('add-workspace').disabled,false);document.getElementById('add-workspace').click();assert.equal(document.getElementById('workspace-add-dialog').open,true);document.getElementById('workspace-add-cancel').click();assert.equal(document.getElementById('workspace-add-dialog').open,false);
  send('committed',{seq:1,run_id:'root',kind:'model.text',data:{text:'Synthetic full-page streamed reply 雪'}},1);send('committed',{seq:2,run_id:'root',kind:'model.usage',data:{prompt_tokens:5,completion_tokens:3}},2);
  await until(()=>document.getElementById('live').textContent.includes('Synthetic full-page streamed reply 雪')&&document.querySelector('#live .metrics')?.textContent.includes('Output 3'));await new Promise(resolve=>setTimeout(resolve,150));const settled=requests.length;await new Promise(resolve=>setTimeout(resolve,550));assert.equal(requests.length,settled,'Idle page must not issue periodic run polling');assert.equal(requests.filter(route=>route.includes('/stream')).length,1);
  run.state='completed';history=[{role:'assistant',data:{content:'Synthetic full-page streamed reply 雪',usage:{prompt_tokens:5,completion_tokens:3}}}];send('committed',{seq:3,run_id:'root',kind:'conversation.assistant',data:history[0].data},3);send('committed',{seq:4,run_id:'root',kind:'run.completed',data:{}},4);send('end',{reason:'terminal',after:4});body.close();
  await until(()=>document.getElementById('status').textContent==='completed'&&document.querySelectorAll('#history .message.assistant').length===1);assert.match(document.querySelector('#history .metrics').textContent,/Input 5Output 3Total —/);assert.ok(!document.getElementById('connection').open);document.getElementById('disconnect-view').click();await until(()=>requests.includes('/ui/session/disconnect'));assert.equal(streamSignal.aborted,true);assert.ok(!requests.some(route=>route.endsWith('/cancel')||route==='/v1/runs'));
 }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});

test('production browser footer drives the same saved-provider CAS and explicit model enrollment using public metadata only',async()=>{
 const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'}),window=dom.window,document=window.document,requests=[];
 let state={revision:3,active:'openai',profiles:[{id:'openai',provider:'openai',route_id:'openai.responses',model:'fixture-openai',revision:1},{id:'claude',provider:'anthropic',route_id:'anthropic.messages',model:'',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]};
 window.AbortController=AbortController;window.AbortSignal=AbortSignal;window.TextDecoder=TextDecoder;window.TextEncoder=TextEncoder;
 for(const dialog of document.querySelectorAll('dialog')){dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new window.Event('close'));};}
 window.fetch=async(input,options={})=>{
  const url=new URL(input,window.location.origin);assert.equal(url.origin,window.location.origin);const body=options.body?JSON.parse(options.body):undefined;requests.push({path:url.pathname,method:options.method||'GET',body});assert.equal(options.credentials,'same-origin');assert.ok(!options.headers?.Authorization);
  let data;const active=state.profiles.find(profile=>profile.id===state.active);
  if(url.pathname==='/ui/session'||url.pathname==='/ui/session/disconnect')data={};
  else if(url.pathname==='/ui/workspaces')data={can_add:true,selected_workspace_id:'windows-local-file-v1:synthetic',workspaces:[{workspace_id:'windows-local-file-v1:synthetic',name:'Example',root:'D:\\Projects\\Example'}]};
  else if(url.pathname==='/v1/health')data={status:'ok',agent_execution:!!active.model};
  else if(url.pathname==='/v1/workspace')data={configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:synthetic',authority_id:'b'.repeat(32)};
  else if(url.pathname==='/v1/models')data={models:[{id:active.model}],default_model:active.model};
  else if(url.pathname==='/v1/graphs')data={graphs:[]};else if(url.pathname==='/v1/sessions')data=[];
  else if(url.pathname==='/v1/provider/profiles'&&options.method!=='POST')data=state;
  else if(url.pathname==='/v1/provider/profiles/select'){assert.deepEqual(body,{id:'claude',expected_revision:3});state={...state,active:'claude',revision:4};data=state;}
  else if(url.pathname==='/v1/provider/profiles/models'){assert.deepEqual(body,{id:state.active,route_id:active.route_id,expected_revision:state.revision});data={models:state.active==='openai'?[{id:'fixture-openai'}]:[{id:'fixture-claude'},{id:'fixture-claude-next'}]};}
  else if(url.pathname==='/v1/provider/profiles'&&options.method==='POST'){assert.deepEqual(body,{id:'claude',route_id:'anthropic.messages',model:'fixture-claude',expected_revision:4,activate:true});state={...state,revision:5,profiles:state.profiles.map(profile=>profile.id==='claude'?{...profile,model:body.model,revision:2}:profile)};data=state;}
  else throw Error('Unexpected synthetic browser profile route');return {ok:true,status:200,json:async()=>structuredClone(data)};
 };
 async function until(predicate){for(let n=0;n<200&&!predicate();n++)await new Promise(resolve=>setImmediate(resolve));assert.ok(predicate(),'Actual classic script view reached the expected public observation');}
 try{for(const script of document.querySelectorAll('script')){const pathName=script.getAttribute('src');new vm.Script(fs.readFileSync(path.join(root,sources[pathName]),'utf8'),{filename:pathName}).runInContext(dom.getInternalVMContext());}
  const footer=document.querySelector('footer #footer-provider'),model=document.querySelector('footer #model');await until(()=>document.getElementById('workspace-info').textContent.startsWith('Connected')&&footer.value==='openai'&&model.value==='fixture-openai');assert.equal(document.getElementById('provider-settings').open,false);assert.equal(footer.options[2].textContent,'Claude · Choose a model');
  footer.value='claude';footer.dispatchEvent(new window.Event('change'));await until(()=>footer.value==='claude'&&model.options.length===3);assert.equal(model.value,'');assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles/select').length,1);assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles'&&request.method==='POST').length,0);assert.equal(document.getElementById('provider-settings').open,false);
  document.getElementById('settings').click();assert.equal(document.getElementById('provider-key').required,false);assert.match(document.getElementById('provider-key-help').textContent,/Leave blank to use the saved key/);document.getElementById('settings-close').click();await until(()=>model.options.length===3);model.value='fixture-claude';model.dispatchEvent(new window.Event('change'));await until(()=>requests.some(request=>request.path==='/v1/provider/profiles'&&request.method==='POST')&&model.value==='fixture-claude');assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles/select').length,1);assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles'&&request.method==='POST').length,1);assert.ok(!requests.some(request=>request.body&&Object.hasOwn(request.body,'api_key')));assert.equal(state.profiles[0].model,'fixture-openai');
  document.getElementById('disconnect-view').click();await until(()=>footer.hidden);assert.equal(footer.disabled,true);assert.equal(model.disabled,true);assert.ok(!requests.some(request=>request.path==='/v1/runs'));
 }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});
test('production classic scripts share a realm without collisions and restore owned history through the browser bridge',async()=>{
  const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'});
  const window=dom.window,document=window.document,requests=[];
  const parent={id:'fixture-parent',session_id:'fixture-session',parent_id:'',graph_root:false,state:'completed'};
  const child={run:{id:'fixture-leaf',session_id:parent.session_id,parent_id:parent.id,graph_root:false,state:'completed'},kind:'delegated_leaf',batch_id:'fixture-batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1};
  const parentHistory=[{role:'assistant',data:{content:'Synthetic retained parent answer',model:'fixture-model',usage:{prompt_tokens:13,completion_tokens:5}}}];
  const childHistory=[{role:'assistant',data:{content:'Synthetic retained child finding',model:'fixture-model',usage:{prompt_tokens:7,completion_tokens:3}}}];
  const routes={
    '/ui/session':{},
    '/ui/workspaces':{can_add:true,selected_workspace_id:'windows-local-file-v1:synthetic',workspaces:[{workspace_id:'windows-local-file-v1:synthetic',name:'Example',root:'D:\\Projects\\Example'}]},
    '/v1/health':{status:'ok',agent_execution:true,owned_child_observation:true},
    '/v1/workspace':{configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:synthetic',authority_id:'b'.repeat(32)},
    '/v1/models':{models:[{id:'fixture-model'}],default_model:'fixture-model'},
    '/v1/graphs':{graphs:[]},
    '/v1/sessions':[{id:parent.session_id,title:'Synthetic retained conversation'}],
    ['/v1/sessions/'+parent.session_id+'/history']:parentHistory,
    ['/v1/sessions/'+parent.session_id+'/runs']:[parent],
    ['/v1/runs/'+parent.id]:parent,
    ['/v1/runs/'+parent.id+'/children']:[child],
    ['/v1/runs/'+parent.id+'/children/'+child.run.id+'/history']:childHistory,
    ['/v1/runs/'+parent.id+'/operations']:[],
    ['/v1/runs/'+child.run.id+'/operations']:[],
    '/v1/provider/configuration':{provider:'openai',wire:'responses',revision:0,configured:false,model:''}
  };
  window.AbortController=AbortController;window.AbortSignal=AbortSignal;
  window.TextDecoder=TextDecoder;window.TextEncoder=TextEncoder;
  for(const dialog of document.querySelectorAll('dialog')){
    dialog.showModal=()=>{dialog.open=true;};
    dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new window.Event('close'));};
  }
  window.sessionStorage.setItem('xmind.view.selection',JSON.stringify({session:parent.session_id,run:parent.id,model:'fixture-model'}));
  window.fetch=async(input,options={})=>{
    const url=new URL(input,window.location.origin);assert.equal(url.origin,window.location.origin,'Fixture rejects any provider/native network destination');
    const route=url.pathname+url.search;requests.push({route,method:options.method||'GET',credentials:options.credentials,headers:options.headers});
    if(url.pathname.endsWith('/tree-events')){assert.equal(url.searchParams.get('after'),'0');return {ok:true,status:200,json:async()=>[]};}
    if(route==='/v1/provider/profiles')return {ok:false,status:404,json:async()=>({detail:'Synthetic older profile metadata unavailable'})};
    assert.ok(Object.hasOwn(routes,route),'Unexpected classic-script fixture route: '+route);
    return {ok:true,status:200,json:async()=>structuredClone(routes[route])};
  };
  try{
    const scripts=[...document.querySelectorAll('script')];
    assert.deepEqual(scripts.map(script=>script.getAttribute('src')),Object.keys(sources),'Execute the production emitted script order, not separate CommonJS modules');
    for(const script of scripts){
      assert.notEqual(script.type,'module','The product page uses classic script globals');
      const route=script.getAttribute('src');
      // vm.Script persists global lexical declarations, unlike isolated strict
      // eval/CommonJS loading which missed the original duplicate declaration.
      new vm.Script(fs.readFileSync(path.join(root,sources[route]),'utf8'),{filename:route}).runInContext(dom.getInternalVMContext());
      if(route==='/ui/browser.js'){
        assert.equal(typeof window.xMindView?.postMessage,'function','Install the real browser bridge before the renderer');
        assert.equal(typeof window.acquireVsCodeApi,'undefined','The webpage has no VS Code host fallback');
      }
    }
    for(let attempts=0;attempts<100&&!document.getElementById('workspace-info').textContent.startsWith('Connected');++attempts)await new Promise(resolve=>setImmediate(resolve));
    assert.match(document.getElementById('workspace-info').textContent,/Connected to the native xMind runtime/);assert.match(document.getElementById('workspace-info').textContent,/D:\\Projects\\Example/);
    assert.equal(document.getElementById('connection').open,false);
    assert.equal(document.getElementById('sessions').value,parent.session_id);
    assert.equal(document.querySelector('footer #model').value,'fixture-model');
    assert.match(document.getElementById('history').textContent,/Synthetic retained parent answer/);
    assert.match(document.querySelector('#history .metrics').textContent,/Input 13Output 5Total —/);
    assert.match(document.getElementById('owned-view').textContent,/inspect · completed.*Synthetic retained child finding/s);
    assert.match(document.querySelector('#owned-view .metrics').textContent,/Input 7Output 3Total —/);
    assert.equal(requests.filter(request=>request.route==='/ui/session').length,1);
    assert.ok(requests.every(request=>request.credentials==='same-origin'&&!request.headers?.Authorization),'Restoration uses the browser session, with no key or master token fixture');
    assert.equal(requests.filter(request=>request.method==='POST'&&request.route!=='/ui/session').length,0,'Loading a retained page cannot submit a run, effect or profile change');
  }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});
