'use strict';
// Deterministic VS Code API/HTTP fixtures test host-controller behavior only.
// This is not an actual IDE rendering test, native engine test or live inference.
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const { BackendClient,backendOrigin,validateToken } = require('../client');

function harness(options={}) {
  const token='synthetic-extension-host-access-token';
  const commands=new Map(),secrets=new Map(),requests=[],views=[],intervals=new Map();
  const state=new Map(),errors=[],comparisons=[],bootstrapTasks=[],ready=[];let documentProvider;
  let pendingHistory;
  let pendingOperation;
  let operations=options.operations||[];
  let sidebarProvider;
  const decisions=[];
  const transcript=[{seq:1,role:'user',data:{content:'Earlier user prompt'}},{seq:2,role:'assistant',data:{content:'Persisted synthetic response'}}];
  const fetchImpl=async (url,requestOptions)=>{
    assert.equal(requestOptions.headers.Authorization,`Bearer ${token}`);
    const target=new URL(url);requests.push(target.pathname+target.search);
    let data;
    if(target.pathname==='/v1/health') data=options.health||{agent_execution:true,status:'ok'};
    else if(target.pathname==='/v1/models') {
      if(options.legacyCatalogue) return {ok:false,status:404,json:async()=>({detail:'Resource not found'})};
      data=options.catalogue||{default_model:'synthetic-host-default',models:[{id:'synthetic-host-default'},{id:'synthetic-host-alternate'}]};
    }
    else if(target.pathname==='/v1/sessions') data=[{id:'saved',title:'Saved session'}];
    else if(target.pathname==='/v1/sessions/saved/history') data=pendingHistory?await pendingHistory:transcript;
    else if(target.pathname==='/v1/sessions/saved/runs') data=options.runs||[{id:'finished',state:options.running?'running':'completed'}];
    else if(options.runs && /^\/v1\/runs\/[^/]+(?:\/operations|\/events)?$/.test(target.pathname)) {
      const id=target.pathname.split('/')[3],run=options.runs.find(item=>item.id===id);assert.ok(run,'Host must request only selected-session runs');
      data=target.pathname.endsWith('/operations')?(options.operationsByRun?.[id]||[]):target.pathname.endsWith('/events')?(target.search==='?after=0'?[{seq:1,kind:'run.'+run.state,data:{}}]:[]):run;
    }
    else if(target.pathname==='/v1/runs/finished') data={id:'finished',state:options.running?'running':'completed'};
    else if(target.pathname==='/v1/runs/finished/operations') data=operations;
    else if(target.pathname==='/v1/operations/edit') data=pendingOperation?await pendingOperation:operations[0];
    else if(target.pathname==='/v1/operations/edit/inspection') data=await options.inspection;
    else if(target.pathname==='/v1/operations/edit/decision') {
      const body=JSON.parse(requestOptions.body);decisions.push(body);
      operations=[{...operations[0],state:body.decision==='allow'?'ready':'denied',decision_actor:'fixture-controller'}];data=operations[0];
    }
    else if(target.pathname==='/v1/runs/finished/events') data=target.search==='?after=0'?[{seq:1,kind:'run.completed',data:{}}]:[];
    else throw new Error(`Unexpected native route ${target.pathname}`);
    return {ok:true,json:async()=>data};
  };
  class TestClient extends BackendClient {constructor(url,provider) {super(url,provider,fetchImpl);}}
  const vscode={
    ExtensionMode:{Development:2},ConfigurationTarget:{Global:1},
    Uri:{joinPath:(root,...parts)=>[root,...parts].join('/'),parse:value=>({toString:()=>value})},
    workspace:{isTrusted:true,getConfiguration:()=>({get:()=> 'http://localhost:8765',update:async()=>{}}),registerTextDocumentContentProvider:(scheme,provider)=>{assert.equal(scheme,'xmind-review');documentProvider=provider;return {dispose(){}};}},
    ViewColumn:{Beside:2},
    commands:{registerCommand:(name,callback)=>{commands.set(name,callback);return {dispose(){}};},
      executeCommand:async (name,...args)=>{if(name==='vscode.diff'){comparisons.push(args);return;}if(['workbench.view.extension.xmind','workbench.view.explorer'].includes(name))return;assert.equal(name,'xmind.workspace.focus');sidebarProvider.resolveWebviewView(makeView());}},
    window:{showInputBox:async options=>{assert.equal(options.password,true);return token;},showErrorMessage:message=>errors.push(message),
      registerWebviewViewProvider:(id,provider)=>{assert.equal(id,'xmind.workspace');sidebarProvider=provider;return {dispose(){}};}}
  };
  function makeView(){
        const view={posted:[],webview:{html:'',cspSource:'https://fixture-webview',asWebviewUri:uri=>({toString:()=>uri}),postMessage:message=>{view.posted.push(message);return Promise.resolve(true);},
          onDidReceiveMessage:callback=>{view.receive=callback;return {dispose(){}};}},
          onDidDispose:callback=>{view.close=callback;return {dispose(){}};},show(){}};
        views.push(view);return view;
      }
  const context={extensionMode:options.bootstrap?2:1,extensionUri:'https://fixture-extension',subscriptions:[],secrets:{get:async key=>secrets.get(key),store:async (key,value)=>{secrets.set(key,value);}},
    workspaceState:{get:key=>state.get(key),update:async (key,value)=>{state.set(key,value);}}};
  state.set('agentflow.session',{url:'http://127.0.0.1:8765',id:'saved'});
  let intervalID=0;
  const sandbox={module:{exports:{}},URL,require:name=>name==='vscode'?vscode:name==='./client'?{BackendClient:TestClient,backendOrigin,validateToken}:name==='./webview'?require('../webview'):require(name),
    setTimeout:callback=>{bootstrapTasks.push(callback);return 1;},clearTimeout(){},setInterval:callback=>{const id=++intervalID;intervals.set(id,callback);return id;},clearInterval:id=>intervals.delete(id)};
  if(options.bootstrap)sandbox.process={env:{XMIND_UI_BACKEND_ORIGIN:'http://localhost:8765',XMIND_UI_BOOTSTRAP_TOKEN:token,XMIND_UI_READY_FILE:'labeled-fixture-marker'}};
  const originalRequire=sandbox.require;sandbox.require=name=>name==='./edit-review'?require('../edit-review'):name==='node:fs'?{writeFileSync:(_,data)=>ready.push(JSON.parse(data))}:originalRequire(name);
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../extension.js'),'utf8'),sandbox,{filename:'extension.js'});
  const activation=sandbox.module.exports.activate(context);
  return {token,commands,secrets,requests,views,intervals,state,errors,context,decisions,comparisons,activation,bootstrapTasks,ready,bootstrapEnvironment:sandbox.process?.env,reviewText:uri=>documentProvider.provideTextDocumentContent(uri),
    configureBackend(health,catalogue){options.health=health;options.catalogue=catalogue;},
    configureRuns(runs){options.runs=runs;},
    pauseOperation(promise) {pendingOperation=promise;},
    pauseHistory(promise) {pendingHistory=promise;}};
}
async function until(predicate) {
  const deadline=Date.now()+2000;
  while(Date.now()<deadline) {if(predicate()) return;await new Promise(resolve=>setImmediate(resolve));}
  throw new Error('Extension host fixture timed out');
}

test('native terminal state refreshes transcript and stops polling with durable operation inspection',async()=>{
  const h=harness();await h.commands.get('agentflow.open')();
  const view=h.views[0];assert.ok(view);
  const inlineScript=/<script nonce="[^"]+">([\s\S]*?)<\/script>/.exec(view.webview.html);
  assert.ok(inlineScript,'Webview must include its nonce-authorized script');new vm.Script(inlineScript[1]);
  assert.equal(h.secrets.get('xmind.auth:http://127.0.0.1:8765'),h.token);
  assert.ok(!view.webview.html.includes(h.token));
  view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  assert.ok(view.posted.some(message=>message.type==='status' && message.text==='completed'));
  assert.equal(h.intervals.size,0,'Terminal state must stop observation');
  assert.ok(!h.requests.some(route=>route.includes('approvals')));
  assert.ok(!JSON.stringify(view.posted).includes(h.token));
  assert.ok(!JSON.stringify([...h.state.values()]).includes(h.token));
  assert.equal(h.errors.length,0);view.close();
});

const pendingEdit={id:'edit',run_id:'finished',workspace_id:'verified-fixture-root',tool:'replace_file',state:'awaiting_approval',expires_unix_ms:Date.now()+600000,decision_actor:'',result_json:'{}',arguments_json:JSON.stringify({before_content:'actual before fixture',after_content:'<script>untrusted file text</script>',before_sha256:'fixture-hash',file_id:'fixture-file'})};
const uncertainEdit={...pendingEdit,state:'uncertain'};
const inspectionFixture={operation:uncertainEdit,observed:{path:'file.cpp',workspace_id:uncertainEdit.workspace_id,file_id:'fixture-file',content_sha256:'a'.repeat(64),size:12},match:'after',same_file:true,observed_unix_ms:Date.now(),quarantine_released:false};
test('older run inspection remains accessible and selected run persists across view reopening',async()=>{
  const older={...uncertainEdit,run_id:'older'};
  const h=harness({runs:[{id:'older',state:'failed'},{id:'latest',state:'completed'}],operationsByRun:{older:[older]},inspection:{...inspectionFixture,operation:older}});
  await h.commands.get('agentflow.open')();let view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  assert.equal(view.posted.filter(message=>message.type==='runs').at(-1).selected,'latest');
  view.receive({type:'select-run',id:'older'});
  await until(()=>view.posted.some(message=>message.type==='operations' && message.operations[0]?.run_id==='older'));
  view.receive({type:'inspect-edit',id:'edit'});await until(()=>view.posted.some(message=>message.type==='edit-inspection'));
  assert.equal(h.state.get('xmind.observedRun').id,'older');view.close();
  await h.commands.get('agentflow.open')();view=h.views[1];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  assert.equal(view.posted.filter(message=>message.type==='runs').at(-1).selected,'older');
  assert.ok(!h.requests.includes('/v1/runs'),'Inspecting history must not submit a run');assert.equal(h.decisions.length,0);view.close();
});
test('run selection rejects IDs outside the selected conversation before fetching their details',async()=>{
  const h=harness({runs:[{id:'latest',state:'completed'}]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  view.receive({type:'select-run',id:'another-session-run'});await until(()=>view.posted.some(message=>message.type==='error'));
  assert.ok(!h.requests.some(route=>route.includes('another-session-run')));assert.equal(h.state.get('xmind.observedRun').id,'latest');view.close();
});
test('older terminal run keeps observing an active conversation and rejects another submission',async()=>{
  const h=harness({runs:[{id:'older',state:'failed'},{id:'latest',state:'running'}]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='status' && message.text==='running'));
  view.receive({type:'select-run',id:'older'});await until(()=>view.posted.some(message=>message.type==='status' && message.text==='failed'));
  assert.equal(h.intervals.size,1);assert.equal(view.posted.filter(message=>message.type==='runs').at(-1).busy,true);
  view.receive({type:'send',prompt:'Must not submit from historical selection'});await until(()=>view.posted.some(message=>message.type==='error'));
  assert.ok(!h.requests.includes('/v1/runs'));
  h.configureRuns([{id:'older',state:'failed'},{id:'latest',state:'completed'}]);await [...h.intervals.values()][0]();
  assert.equal(h.intervals.size,0);assert.equal(view.posted.filter(message=>message.type==='runs').at(-1).busy,false);view.close();
});
test('uncertain edit inspection uses the host-only read endpoint without deciding or accepting webview paths',async()=>{
  const h=harness({health:{agent_execution:false},operations:[uncertainEdit],inspection:inspectionFixture});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  view.receive({type:'inspect-edit',id:'edit',path:'forged-path',match:'before',quarantine_released:true});
  await until(()=>view.posted.some(message=>message.type==='edit-inspection'));
  assert.deepEqual(view.posted.find(message=>message.type==='edit-inspection').inspection,inspectionFixture);
  assert.ok(h.requests.includes('/v1/operations/edit/inspection'));assert.equal(h.decisions.length,0);
  assert.ok(!h.requests.some(route=>route.includes('forged-path')));
  view.receive({type:'inspect-edit',id:'unknown'});await until(()=>view.posted.some(message=>message.type==='error'));
  assert.equal(h.requests.filter(route=>route.endsWith('/inspection')).length,1);view.close();
});
test('inspection rejects a different backend operation and discards observations after selection changes',async()=>{
  const h=harness({operations:[uncertainEdit],inspection:{...inspectionFixture,operation:{...uncertainEdit,arguments_json:'{}'}}});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  view.receive({type:'inspect-edit',id:'edit'});await until(()=>view.posted.some(message=>message.type==='error'));
  assert.ok(!view.posted.some(message=>message.type==='edit-inspection'));view.close();
  let resolveInspection;const deferred=new Promise(resolve=>{resolveInspection=resolve;});
  const late=harness({operations:[uncertainEdit],inspection:deferred});await late.commands.get('agentflow.open')();const lateView=late.views[0];lateView.receive({type:'ready'});
  await until(()=>lateView.posted.some(message=>message.type==='transcript'));
  lateView.receive({type:'inspect-edit',id:'edit'});await until(()=>late.requests.includes('/v1/operations/edit/inspection'));
  lateView.receive({type:'select-run',id:'finished'});resolveInspection(inspectionFixture);
  await until(()=>late.requests.filter(route=>route==='/v1/sessions/saved/history').length>=3);
  assert.ok(!lateView.posted.some(message=>message.type==='edit-inspection'));assert.equal(late.decisions.length,0);lateView.close();
});
test('only a reviewed pending operation can be decided and payload stays backend-owned',async()=>{
  const h=harness({running:true,operations:[pendingEdit]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='operations' && message.operations.length));
  const rendered=view.posted.find(message=>message.type==='operations' && message.operations.length);
  assert.equal(rendered.operations[0].arguments_json,pendingEdit.arguments_json,'Host must preserve exact recorded review bytes');
  view.receive({type:'decide',id:'unreviewed',decision:'allow'});
  await until(()=>view.posted.some(message=>message.type==='error'));
  assert.equal(h.decisions.length,0,'Unreviewed operation must never reach backend decision API');
  view.receive({type:'decide',id:'edit',decision:'allow',actor:'spoof',arguments_json:'changed'});
  await until(()=>h.decisions.length===1);
  assert.deepEqual(h.decisions[0],{decision:'allow'},'Webview cannot supply actor or replacement arguments');
  await until(()=>view.posted.some(message=>message.type==='operations' && message.operations[0]?.state==='ready'));
  view.receive({type:'decide',id:'edit',decision:'allow'});
  await until(()=>view.posted.filter(message=>message.type==='error').length===2);
  assert.equal(h.decisions.length,1,'Granted operation must not be decided twice');view.close();
});
test('closing the view during proposal revalidation cannot send an approval',async()=>{
  const h=harness({running:true,operations:[pendingEdit]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='operations' && message.operations.length));
  let resolveOperation;h.pauseOperation(new Promise(resolve=>{resolveOperation=resolve;}));
  view.receive({type:'decide',id:'edit',decision:'allow'});
  await until(()=>h.requests.includes('/v1/operations/edit'));view.close();resolveOperation(pendingEdit);
  await h.commands.get('agentflow.open')();assert.equal(h.decisions.length,0,'Disposed view must not issue a grant after late response');h.views[1].close();
});
test('changed proposal bytes invalidate the displayed review',async()=>{
  const h=harness({running:true,operations:[pendingEdit]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='operations' && message.operations.length));
  h.pauseOperation(Promise.resolve({...pendingEdit,arguments_json:'{"different":"payload"}'}));
  view.receive({type:'decide',id:'edit',decision:'allow'});
  await until(()=>view.posted.some(message=>message.type==='error'));
  assert.equal(h.decisions.length,0,'Changed reviewed bytes must never be approved');view.close();
});
test('session selection intent invalidates an in-flight approval before queued selection runs',async()=>{
  const h=harness({running:true,operations:[pendingEdit]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='operations' && message.operations.length));
  let resolveOperation;h.pauseOperation(new Promise(resolve=>{resolveOperation=resolve;}));
  view.receive({type:'decide',id:'edit',decision:'allow'});
  await until(()=>h.requests.includes('/v1/operations/edit'));
  view.receive({type:'select',id:'saved'});resolveOperation(pendingEdit);
  await until(()=>h.requests.filter(route=>route==='/v1/sessions/saved/history').length===2);
  assert.equal(h.decisions.length,0,'Selection intent must prevent a grant while the selection handler is still queued');view.close();
});

test('closing a panel during history loading cannot start a detached poller',async()=>{
  const h=harness();let resolveHistory;
  h.pauseHistory(new Promise(resolve=>{resolveHistory=resolve;}));
  await h.commands.get('agentflow.open')();const view=h.views[0];
  view.receive({type:'ready'});
  await until(()=>h.requests.includes('/v1/sessions/saved/history'));
  view.close();resolveHistory([]);
  await h.commands.get('agentflow.open')();
  assert.equal(h.views.length,2);assert.equal(h.intervals.size,0);
  assert.ok(!h.requests.includes('/v1/sessions/saved/runs'),'Disposed selection must not continue fetching runs');
  assert.equal(h.views[1].posted.length,0,'Old panel response must not reach reopened view');
  h.views[1].close();
});
test('older native servers without a catalogue keep default execution without invented model IDs',async()=>{
  const h=harness({legacyCatalogue:true});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='capabilities'));
  const capability=view.posted.find(message=>message.type==='capabilities');assert.equal(capability.execution,true);assert.equal(capability.model,undefined);assert.equal(capability.models.length,0);view.close();
});

test('edit comparison opens exact revalidated backend snapshots without granting or writing',async()=>{
  const proposal={...pendingEdit,arguments_json:JSON.stringify({path:'src/example.cpp',before_content:'before\n',after_content:'after\n'})};
  const h=harness({running:true,operations:[proposal]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='operations'&&message.operations.length));
  view.receive({type:'review',id:'edit',before_content:'forged webview text'});
  await until(()=>h.comparisons.length===1);
  const [before,after,title]=h.comparisons[0];assert.equal(h.reviewText(before),'before\n');assert.equal(h.reviewText(after),'after\n');
  assert.match(before.toString(),/^xmind-review:/);assert.equal(title,'xMind proposed edit: src/example.cpp');assert.equal(h.decisions.length,0);
  h.pauseOperation(Promise.resolve({...proposal,arguments_json:'{"changed":"proposal"}'}));view.receive({type:'review',id:'edit'});
  await until(()=>view.posted.some(message=>message.type==='error'));assert.equal(h.comparisons.length,1);view.close();
});
test('creation comparison opens absent-to-proposed snapshots without creating a file or granting',async()=>{
  const proposal={...pendingEdit,tool:'create_file',arguments_json:JSON.stringify({path:'new.cpp',before_exists:false,before_content:'',after_content:'proposed new source\n'})};
  const h=harness({running:true,operations:[proposal]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(message=>message.type==='operations'&&message.operations.length));
  view.receive({type:'review',id:'edit'});await until(()=>h.comparisons.length===1);const [before,after,title]=h.comparisons[0];assert.equal(h.reviewText(before),'');assert.equal(h.reviewText(after),'proposed new source\n');assert.equal(title,'xMind proposed new file: new.cpp');assert.equal(h.decisions.length,0);view.close();
});

test('refresh reloads backend execution capabilities and history without submitting a run',async()=>{
  const h=harness({health:{agent_execution:false}});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript')&&h.intervals.size===0);
  const histories=h.requests.filter(route=>route==='/v1/sessions/saved/history').length;
  h.configureBackend({agent_execution:true},{default_model:'configured-after-connect',models:[{id:'configured-after-connect'}]});
  view.receive({type:'refresh'});
  await until(()=>h.requests.filter(route=>route==='/v1/sessions/saved/history').length>histories);
  const latest=view.posted.filter(message=>message.type==='capabilities').at(-1);
  assert.equal(latest.execution,true);assert.equal(latest.model,'configured-after-connect');
  assert.equal(h.requests.filter(route=>route==='/v1/health').length,2);
  assert.ok(!h.requests.includes('/v1/runs'),'Reconnect must never resubmit accepted work');view.close();
});

test('selected configured model survives view reopening and retired models fall back on refresh',async()=>{
  const h=harness();await h.commands.get('agentflow.open')();let view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  view.receive({type:'model',id:'synthetic-host-alternate'});
  await until(()=>h.state.get('xmind.model')?.id==='synthetic-host-alternate');view.close();
  await h.commands.get('agentflow.open')();view=h.views[1];view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='transcript'));
  assert.equal(view.posted.find(message=>message.type==='capabilities').model,'synthetic-host-alternate');
  h.configureBackend({agent_execution:true},{default_model:'new-configured-model',models:[{id:'new-configured-model'}]});view.receive({type:'refresh'});
  await until(()=>view.posted.some(message=>message.type==='capabilities'&&message.model==='new-configured-model'));
  assert.equal(view.posted.filter(message=>message.type==='capabilities').at(-1).models.length,1);view.close();
});

test('normal development bootstrap completes activation before resolving the sidebar and clears private environment',async()=>{
  const h=harness({bootstrap:true});await h.activation;
  assert.ok(h.commands.has('agentflow.open'));assert.equal(h.views.length,0);assert.equal(h.bootstrapTasks.length,1);
  assert.equal(h.secrets.get('xmind.auth:http://127.0.0.1:8765'),h.token);
  assert.equal(Object.keys(h.bootstrapEnvironment).length,0,'Private bootstrap variables must be cleared before view opening');
  h.bootstrapTasks[0]();await until(()=>h.ready.length===1);
  assert.equal(h.ready[0].location,'secondarySidebar');assert.equal(h.ready[0].origin,'http://127.0.0.1:8765');
  assert.ok(!JSON.stringify(h.ready).includes(h.token));assert.ok(!h.views[0].webview.html.includes(h.token));h.views[0].close();
});
