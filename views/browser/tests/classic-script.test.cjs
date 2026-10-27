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
  '/ui/xterm.js':'extensions/vscode/node_modules/@xterm/xterm/lib/xterm.js',
  '/ui/xterm-fit.js':'extensions/vscode/node_modules/@xterm/addon-fit/lib/addon-fit.js',
  '/ui/terminal.js':'views/browser/terminal.js',
  '/ui/browser.js':'views/browser/browser.js',
  '/ui/marked.js':'extensions/vscode/node_modules/marked/lib/marked.umd.js',
  '/ui/purify.js':'extensions/vscode/node_modules/dompurify/dist/purify.min.js',
  '/ui/chat.js':'extensions/vscode/media/chat.js',
  '/ui/layout.js':'views/browser/layout.js'
};

test('browser reload shows saved-settings restoration instead of a false unconfigured state while local reconnect is pending',async()=>{
 const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'}),window=dom.window,document=window.document;let finishSession;
 window.AbortController=AbortController;window.AbortSignal=AbortSignal;window.TextDecoder=TextDecoder;window.TextEncoder=TextEncoder;
 for(const dialog of document.querySelectorAll('dialog')){dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new window.Event('close'));};}
 window.fetch=async input=>{const url=new URL(input,window.location.origin);if(url.pathname==='/ui/session')return await new Promise(resolve=>{finishSession=resolve;});throw Error('Unexpected route during held reconnect: '+url.pathname);};
 async function until(predicate){for(let n=0;n<200&&!predicate();n++)await new Promise(resolve=>setImmediate(resolve));assert.ok(predicate(),'Browser reconnect reached the expected state');}
 try{for(const script of document.querySelectorAll('script')){const route=script.getAttribute('src');new vm.Script(fs.readFileSync(path.join(root,sources[route]),'utf8'),{filename:route}).runInContext(dom.getInternalVMContext());}window.Terminal={Terminal:class{constructor(){this.output='';}loadAddon(){}open(){}onData(){return{dispose(){}};}onResize(){return{dispose(){}};}write(value){this.output+=value;}dispose(){}}};window.FitAddon={FitAddon:class{fit(){}proposeDimensions(){return{rows:24,cols:80};}}};
  await until(()=>typeof finishSession==='function');assert.equal(document.getElementById('status').textContent,'Restoring saved provider settings from the local xMind backend…');assert.doesNotMatch(document.getElementById('status').textContent,/configure a model|add a provider/i);assert.equal(document.getElementById('send').disabled,true);
  finishSession({ok:false,status:401,json:async()=>({})});await until(()=>document.getElementById('connection').open);assert.match(document.getElementById('connection-error').textContent,/did not accept this page/);
 }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});

test('full webpage streams a response, persists a queued follow-up and dispatches it after the turn',async()=>{
 const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'}),window=dom.window,document=window.document,requests=[];
 assert.match(document.querySelector('.local-node').textContent,/Local node · This computer/);assert.equal(document.querySelector('.file-tree-heading').textContent,'Project files');assert.equal(document.getElementById('workspace-config-open').getAttribute('aria-label'),'Configure workspace');assert.equal(document.getElementById('workspace-config-title').textContent,'Configure workspace');assert.match(document.getElementById('workspace-config-dialog').textContent,/Switch the workspace used by the file explorer and agent/);assert.equal(document.getElementById('add-workspace').textContent,'New workspace');assert.equal(document.getElementById('add-workspace-folder').textContent,'Add folder');assert.equal(document.getElementById('workspace-add-title').textContent,'Create workspace');assert.match(document.getElementById('workspace-add-dialog').textContent,/primary folder of a new local workspace/);assert.doesNotMatch(document.body.textContent,/Server access token/);
 assert.ok(document.getElementById('workspace-picker'));assert.ok(document.getElementById('add-workspace'));assert.ok(document.getElementById('add-workspace-folder'));assert.equal(document.getElementById('workspace-config-dialog').open,false);assert.equal(document.getElementById('workspace-add-dialog').open,false);
 const run={id:'root',session_id:'session',parent_id:'',node_id:'',graph_root:false,state:'running'};let body,streamSignal,history=[],inboxItems=[],queuedRun;const terminalCalls=[],terminalMetadata={id:'0123456789abcdef0123456789abcdef',session_id:'session',title:'xMind Browser Terminal',rows:24,columns:80,running:true,exit_code:null,output_sequence:0};
 const frame=(type,data,id)=>`${id===undefined?'':'id: '+id+'\n'}event: ${type}\ndata: ${JSON.stringify(data)}\n\n`,send=(type,data,id)=>body.enqueue(new TextEncoder().encode(frame(type,data,id)));
 window.AbortController=AbortController;window.AbortSignal=AbortSignal;window.TextDecoder=TextDecoder;window.TextEncoder=TextEncoder;window.Terminal={Terminal:class{constructor(){this.output='';}loadAddon(){}open(){}onData(){return{dispose(){}};}onResize(){return{dispose(){}};}write(value){this.output+=value;}dispose(){}}};window.FitAddon={FitAddon:class{fit(){}proposeDimensions(){return{rows:24,cols:80};}}};
 for(const dialog of document.querySelectorAll('dialog')){dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new window.Event('close'));};}
 let rejectFirstSignal=true;window.fetch=async(input,options={})=>{if(rejectFirstSignal&&options.signal){rejectFirstSignal=false;throw new TypeError("Failed to execute 'fetch' on 'Window': Failed to read the 'signal' property from 'RequestInit': Failed to convert value to 'AbortSignal'.");}const url=new URL(input,window.location.origin);requests.push(url.pathname+url.search);assert.equal(url.origin,window.location.origin);assert.equal(options.credentials,'same-origin');assert.ok(!options.headers?.Authorization);
  if(url.pathname.endsWith('/events/stream')){assert.equal(options.headers.Accept,'text/event-stream');streamSignal=options.signal;const streamRunId=url.pathname.split('/')[3],observedRun=streamRunId==='queued-run'?queuedRun:run;return new Response(new ReadableStream({start(controller){body=controller;send('observation',{run:observedRun,scope:'run',after:0});options.signal.addEventListener('abort',()=>{try{controller.error(options.signal.reason);}catch{}},{once:true});}}),{headers:{'Content-Type':'text/event-stream'}});}
  let data;if(url.pathname==='/ui/session'||url.pathname==='/ui/session/disconnect')data={};else if(url.pathname==='/ui/workspaces')data={can_add:true,selected_workspace_id:'windows-local-file-v1:synthetic',workspaces:[{workspace_id:'windows-local-file-v1:synthetic',name:'Example',root:'D:\\Projects\\Example'}]};else if(url.pathname==='/v1/health')data={status:'ok',agent_execution:true,active_session_map:true};else if(url.pathname==='/v1/workspace')data={configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:synthetic',authority_id:'b'.repeat(32)};else if(url.pathname==='/v1/models')data={models:[{id:'fixture-model'}],default_model:'fixture-model'};else if(url.pathname==='/v1/graphs')data={graphs:[]};else if(url.pathname==='/v1/sessions')data=[{id:'session',title:'Synthetic streamed conversation'}];else if(url.pathname==='/v1/sessions/active')data={data:{session:{type:'running'}}};else if(url.pathname==='/v1/sessions/session/terminals'&&options.method==='POST'){terminalCalls.push({path:url.pathname,method:options.method,body:JSON.parse(options.body)});data=terminalMetadata;}else if(url.pathname==='/v1/terminals/'+terminalMetadata.id+'/output'){terminalCalls.push({path:url.pathname,method:options.method});data={encoding:'hex',data:'',next_sequence:0,truncated:false,running:true,exit_code:null};}else if(url.pathname==='/v1/terminals/'+terminalMetadata.id+'/close'){terminalCalls.push({path:url.pathname,method:options.method});data={closed:true};}else if(url.pathname==='/v1/sessions/session/permissions')data={session_id:'session',requests:[]};else if(url.pathname==='/v1/sessions/session/inbox'){if(options.method==='POST'){const payload=JSON.parse(options.body),item={id:payload.id||'fixture-inbox',session_id:'session',content:payload.content,state:'queued',run_id:'',created_ms:1};inboxItems=[item];data={item};}else data={items:inboxItems};}else if(url.pathname.startsWith('/v1/sessions/session/inbox/')&&options.method==='DELETE'){inboxItems=[];return {ok:true,status:204,json:async()=>({})};}else if(url.pathname==='/v1/sessions/session/messages')data={messages:history,has_more:false,next_before:null};else if(url.pathname==='/v1/sessions/session/runs')data=queuedRun?[queuedRun,run]:[run];else if(url.pathname==='/v1/runs'&&options.method==='POST'){const payload=JSON.parse(options.body);assert.equal(payload.inbox_id,inboxItems[0]?.id);assert.equal(payload.prompt,'queue this follow-up');queuedRun={id:'queued-run',session_id:'session',parent_id:'',node_id:'',graph_root:false,state:'queued'};inboxItems=inboxItems.map(item=>({...item,state:'claimed',run_id:queuedRun.id}));data=queuedRun;}else if(url.pathname==='/v1/runs/root')data=run;else if(url.pathname==='/v1/runs/queued-run')data=queuedRun;else if(url.pathname.endsWith('/operations')||url.pathname.endsWith('/events'))data=[];else if(url.pathname==='/v1/provider/profiles')return {ok:false,status:404,json:async()=>({detail:'Synthetic absent profile metadata'})};else if(url.pathname==='/v1/provider/configuration')data={provider:'openai',wire:'responses',revision:0,configured:false,model:''};else throw Error('Unexpected webpage route '+url.pathname);return {ok:true,status:200,json:async()=>structuredClone(data)};
 };
 // Streaming paint is scheduled on a timer; microtask/immediate turns alone
 // can all finish before the first 16ms frame becomes eligible.
 async function until(predicate){const deadline=Date.now()+2000;while(!predicate()&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,5));assert.ok(predicate(),'Full webpage reached the expected streamed state: '+JSON.stringify({status:document.getElementById('status').textContent,terminal:document.getElementById('browser-terminal-status').textContent,history:document.getElementById('history').textContent,live:document.getElementById('live').innerHTML,events:document.getElementById('events').textContent,requests}));}
 try{for(const script of document.querySelectorAll('script')){const route=script.getAttribute('src');new vm.Script(fs.readFileSync(path.join(root,sources[route]),'utf8'),{filename:route}).runInContext(dom.getInternalVMContext());}window.Terminal={Terminal:class{constructor(){this.output='';}loadAddon(){}open(){}onData(){return{dispose(){}};}onResize(){return{dispose(){}};}write(value){this.output+=value;}dispose(){}}};window.FitAddon={FitAddon:class{fit(){}proposeDimensions(){return{rows:24,cols:80};}}};
  await until(()=>body&&document.getElementById('workspace-info').title.includes('D:\\Projects\\Example')&&document.getElementById('status').textContent==='running');assert.match(document.getElementById('workspace-info').title,/D:\\Projects\\Example/);assert.equal(document.querySelector('#sessions option[value="session"]')?.textContent,'● Running · Synthetic streamed conversation');const terminalToggle=document.getElementById('workspace-terminal-toggle');assert.equal(terminalToggle.disabled,false);terminalToggle.click();await until(()=>terminalCalls.some(call=>call.method==='POST'&&call.path==='/v1/sessions/session/terminals'));assert.equal(document.getElementById('browser-terminal-panel').hidden,false);assert.equal(document.querySelector('.file-preview').hidden,true);assert.deepEqual(terminalCalls[0].body,{title:'xMind Browser Terminal',rows:24,columns:80});document.getElementById('browser-terminal-close').click();await until(()=>terminalCalls.some(call=>call.path==='/v1/terminals/'+terminalMetadata.id+'/close'));assert.equal(document.getElementById('browser-terminal-panel').hidden,true);assert.equal(document.querySelector('.file-preview').hidden,false);document.getElementById('workspace-config-open').click();assert.equal(document.getElementById('workspace-config-dialog').open,true);assert.equal(document.getElementById('add-workspace').disabled,false);document.getElementById('add-workspace').click();assert.equal(document.getElementById('workspace-add-dialog').open,true);document.getElementById('workspace-add-cancel').click();assert.equal(document.getElementById('workspace-add-dialog').open,false);
  assert.equal(document.getElementById('send').textContent,'Queue ↑');assert.equal(document.getElementById('send').disabled,false);document.getElementById('prompt').value='queue this follow-up';document.getElementById('prompt').dispatchEvent(new window.Event('input'));document.getElementById('send').click();await until(()=>inboxItems.length===1&&document.getElementById('inbox-status').textContent.includes('queue this follow-up'));assert.equal(inboxItems[0].content,'queue this follow-up');assert.match(document.getElementById('status').textContent,/Prompt saved/);const cancelledId=inboxItems[0].id;document.querySelector('#inbox-status .inbox-cancel').click();await until(()=>inboxItems.length===0);assert.ok(requests.includes('/v1/sessions/session/inbox/'+cancelledId));document.getElementById('prompt').value='queue this follow-up';document.getElementById('prompt').dispatchEvent(new window.Event('input'));document.getElementById('send').click();await until(()=>inboxItems.length===1&&document.getElementById('inbox-status').textContent.includes('queue this follow-up'));
  send('committed',{seq:1,run_id:'root',kind:'model.text',data:{text:'Synthetic full-page streamed reply 雪'}},1);send('committed',{seq:2,run_id:'root',kind:'model.usage',data:{prompt_tokens:5,completion_tokens:3}},2);
  await until(()=>document.getElementById('live').textContent.includes('Synthetic full-page streamed reply 雪')&&document.querySelector('#live .metrics')?.textContent.includes('Out 3'));await new Promise(resolve=>setTimeout(resolve,150));const settled=requests.length;await new Promise(resolve=>setTimeout(resolve,550));assert.equal(requests.length,settled,'Idle page must not issue periodic run polling');assert.equal(requests.filter(route=>route.includes('/stream')).length,1);
  run.state='completed';history=[{id:'msg_1',seq:1,role:'assistant',data:{content:'Synthetic full-page streamed reply 雪',usage:{prompt_tokens:5,completion_tokens:3}}}];send('committed',{seq:3,run_id:'root',kind:'conversation.assistant',data:history[0].data},3);send('committed',{seq:4,run_id:'root',kind:'run.completed',data:{}},4);send('end',{reason:'terminal',after:4});const activeStream=body;activeStream.close();
  await until(()=>queuedRun&&document.getElementById('runs').value==='queued-run'&&document.querySelectorAll('#history .message.assistant').length===1);assert.equal(inboxItems[0].state,'claimed');assert.equal(inboxItems[0].run_id,'queued-run');assert.match(document.querySelector('#history .metrics').textContent,/In 5Out 3Tok —/);assert.ok(requests.some(route=>route==='/v1/runs'));assert.ok(!document.getElementById('connection').open);document.getElementById('disconnect-view').click();await until(()=>requests.includes('/ui/session/disconnect'));assert.equal(streamSignal.aborted,true);assert.ok(!requests.some(route=>route.endsWith('/cancel')));
 }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});

test('production browser footer drives the same saved-provider CAS and explicit model enrollment using public metadata only',async()=>{
 const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'}),window=dom.window,document=window.document,requests=[];
 let state={revision:3,active:'openai',profiles:[{id:'openai',provider:'openai',route_id:'openai.responses',model:'fixture-openai',revision:1},{id:'claude',provider:'anthropic',route_id:'anthropic.messages',model:'',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true},{id:'openrouter.chat',provider:'openrouter',wire:'chat-completions',discovery:true}]},connectionState={revision:1,active_id:'local',profiles:[{id:'local',name:'Local',kind:'local',origin:'',revision:1,status:'connected',command_routing:true,authenticated:true}]};
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
  else if(url.pathname==='/v1/connections'&&options.method!=='POST')data=connectionState;
  else if(url.pathname==='/v1/connections'&&options.method==='POST'){assert.equal(body.expected_revision,connectionState.revision);assert.ok(!Object.hasOwn(body,'access_token'));connectionState={revision:connectionState.revision+1,active_id:'local',profiles:body.profiles.map(profile=>profile.id==='local'?{...connectionState.profiles[0],name:profile.name}:{...profile,revision:connectionState.revision+1,status:'not_connected',command_routing:false,authenticated:false})};data=connectionState;}
  else throw Error('Unexpected synthetic browser profile route');return {ok:true,status:200,json:async()=>structuredClone(data)};
 };
 async function until(predicate){for(let n=0;n<200&&!predicate();n++)await new Promise(resolve=>setImmediate(resolve));assert.ok(predicate(),'Actual classic script view reached the expected public observation');}
 try{for(const script of document.querySelectorAll('script')){const pathName=script.getAttribute('src');new vm.Script(fs.readFileSync(path.join(root,sources[pathName]),'utf8'),{filename:pathName}).runInContext(dom.getInternalVMContext());}
  const footer=document.querySelector('footer #footer-provider'),model=document.querySelector('footer #model');await until(()=>document.getElementById('workspace-info').title.includes('D:\\Projects\\Example')&&footer.value==='openai'&&model.value==='fixture-openai');assert.equal(document.getElementById('provider-settings').open,false);assert.equal(footer.options[2].textContent,'Claude · Choose a model');
  footer.value='claude';footer.dispatchEvent(new window.Event('change'));await until(()=>footer.value==='claude'&&model.options.length===3);assert.equal(model.value,'');assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles/select').length,1);assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles'&&request.method==='POST').length,0);assert.equal(document.getElementById('provider-settings').open,false);
  assert.equal(document.querySelectorAll('#connection-form').length,1);assert.equal(document.querySelectorAll('#runtime-connection-form').length,1);document.getElementById('settings').click();assert.equal(document.getElementById('provider-key').required,false);assert.match(document.getElementById('provider-key-help').textContent,/Leave blank to use the saved key/);const profileChoice=document.getElementById('provider-profile');profileChoice.value='';profileChoice.dispatchEvent(new window.Event('change'));assert.ok([...document.getElementById('provider-name').options].some(option=>option.value==='openrouter.chat'&&option.textContent==='OpenRouter · Chat Completions'),'Web Settings must expose the advertised OpenRouter route for a new profile');await until(()=>document.getElementById('connections-status').textContent.includes('active: Local'));assert.match(document.getElementById('connections-list').textContent,/This xMind runtime/);document.getElementById('connection-name').value='Engineering';document.getElementById('connection-origin').value='https://nexus.example.com';document.getElementById('connection-add').click();await until(()=>connectionState.profiles.length===2&&document.getElementById('connections-status').textContent.includes('Local stays active'));assert.equal(connectionState.profiles[1].authenticated,false);assert.match(document.getElementById('connections-list').textContent,/Remote execution remains unavailable/);window.confirm=()=>true;Array.from(document.querySelectorAll('#connections-list button')).find(button=>button.textContent==='Remove connection').click();await until(()=>connectionState.profiles.length===1&&document.getElementById('connections-status').textContent==='Connection removed. Local stays active.');assert.doesNotMatch(document.getElementById('connections-list').textContent,/nexus\.example/);document.getElementById('settings-close').click();await until(()=>model.options.length===3);model.value='fixture-claude';model.dispatchEvent(new window.Event('change'));await until(()=>requests.some(request=>request.path==='/v1/provider/profiles'&&request.method==='POST')&&model.value==='fixture-claude');assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles/select').length,1);assert.equal(requests.filter(request=>request.path==='/v1/provider/profiles'&&request.method==='POST').length,1);assert.ok(!requests.some(request=>request.body&&Object.hasOwn(request.body,'api_key')));assert.equal(state.profiles[0].model,'fixture-openai');
  document.getElementById('disconnect-view').click();await until(()=>footer.hidden);assert.equal(footer.disabled,true);assert.equal(model.disabled,true);assert.ok(!requests.some(request=>request.path==='/v1/runs'));
 }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});
test('production classic scripts share a realm without collisions and restore owned history through the browser bridge',async()=>{
  const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'});
  const window=dom.window,document=window.document,requests=[];
  const parent={id:'fixture-parent',session_id:'fixture-session',parent_id:'',graph_root:false,state:'completed'};
  const child={run:{id:'fixture-leaf',session_id:parent.session_id,parent_id:parent.id,graph_root:false,state:'completed'},kind:'delegated_leaf',batch_id:'fixture-batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1};
  const parentHistory=[{id:'msg_1',seq:1,role:'assistant',data:{content:'Synthetic retained parent answer',model:'fixture-model',usage:{prompt_tokens:13,completion_tokens:5}}}];
  const childHistory=[{role:'assistant',data:{content:'Synthetic retained child finding',model:'fixture-model',usage:{prompt_tokens:7,completion_tokens:3}}}];
  const routes={
    '/ui/session':{},
    '/ui/workspaces':{can_add:true,selected_workspace_id:'windows-local-file-v1:synthetic',workspaces:[{workspace_id:'windows-local-file-v1:synthetic',name:'Example',root:'D:\\Projects\\Example'}]},
    '/v1/health':{status:'ok',agent_execution:true,owned_child_observation:true},
    '/v1/workspace':{configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:synthetic',authority_id:'b'.repeat(32)},
    '/v1/models':{models:[{id:'fixture-model'}],default_model:'fixture-model'},
    '/v1/graphs':{graphs:[]},
    '/v1/sessions':[{id:parent.session_id,title:'Synthetic retained conversation'}],
    ['/v1/sessions/'+parent.session_id+'/permissions']:{session_id:parent.session_id,requests:[]},
    ['/v1/sessions/'+parent.session_id+'/messages?limit=40']:{messages:parentHistory,has_more:false,next_before:null},
    ['/v1/sessions/'+parent.session_id+'/inbox']:{items:[]},
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
    if(route==='/v1/workspace/files?path=.')return {ok:false,status:401,json:async()=>({detail:'Authentication required'})};
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
    for(let attempts=0;attempts<100&&!document.getElementById('workspace-info').title.includes('D:\\Projects\\Example');++attempts)await new Promise(resolve=>setImmediate(resolve));
    assert.match(document.getElementById('workspace-info').title,/D:\\Projects\\Example/);
    for(let attempts=0;attempts<100&&!document.getElementById('workspace-files').textContent.includes('needs an update');++attempts)await new Promise(resolve=>setImmediate(resolve));assert.match(document.getElementById('workspace-files').textContent,/This xMind runtime needs an update to browse workspace files/);
    assert.equal(document.getElementById('connection').open,false);
    assert.equal(document.getElementById('sessions').value,parent.session_id);
    assert.equal(document.querySelector('footer #model').value,'fixture-model');
    assert.match(document.getElementById('history').textContent,/Synthetic retained parent answer/);
    assert.match(document.querySelector('#history .metrics').textContent,/In 13Out 5Tok —/);
    assert.match(document.getElementById('owned-view').textContent,/inspect · completed.*Synthetic retained child finding/s);
    assert.match(document.querySelector('#owned-view .metrics').textContent,/In 7Out 3Tok —/);
    assert.equal(requests.filter(request=>request.route==='/ui/session').length,1);
    assert.ok(requests.every(request=>request.credentials==='same-origin'&&!request.headers?.Authorization),'Restoration uses the browser session, with no key or master token fixture');
    assert.equal(requests.filter(request=>request.method==='POST'&&request.route!=='/ui/session').length,0,'Loading a retained page cannot submit a run, effect or profile change');
  }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});
