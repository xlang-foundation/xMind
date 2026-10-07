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
  const state=new Map(),errors=[];
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
    if(target.pathname==='/v1/health') data={agent_execution:true,status:'ok'};
    else if(target.pathname==='/v1/models') {
      if(options.legacyCatalogue) return {ok:false,status:404,json:async()=>({detail:'Resource not found'})};
      data={default_model:'synthetic-host-default',models:[{id:'synthetic-host-default'},{id:'synthetic-host-alternate'}]};
    }
    else if(target.pathname==='/v1/sessions') data=[{id:'saved',title:'Saved session'}];
    else if(target.pathname==='/v1/sessions/saved/history') data=pendingHistory?await pendingHistory:transcript;
    else if(target.pathname==='/v1/sessions/saved/runs') data=[{id:'finished',state:'completed'}];
    else if(target.pathname==='/v1/runs/finished') data={id:'finished',state:options.running?'running':'completed'};
    else if(target.pathname==='/v1/runs/finished/operations') data=operations;
    else if(target.pathname==='/v1/operations/edit') data=pendingOperation?await pendingOperation:operations[0];
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
    Uri:{joinPath:(root,...parts)=>[root,...parts].join('/')},
    workspace:{isTrusted:true,getConfiguration:()=>({get:()=> 'http://localhost:8765'})},
    ViewColumn:{Beside:2},
    commands:{registerCommand:(name,callback)=>{commands.set(name,callback);return {dispose(){}};},
      executeCommand:async name=>{if(name==='workbench.view.extension.xmind')return;assert.equal(name,'xmind.workspace.focus');sidebarProvider.resolveWebviewView(makeView());}},
    window:{showInputBox:async options=>{assert.equal(options.password,true);return token;},showErrorMessage:message=>errors.push(message),
      registerWebviewViewProvider:(id,provider)=>{assert.equal(id,'xmind.workspace');sidebarProvider=provider;return {dispose(){}};}}
  };
  function makeView(){
        const view={posted:[],webview:{html:'',cspSource:'https://fixture-webview',asWebviewUri:uri=>({toString:()=>uri}),postMessage:message=>{view.posted.push(message);return Promise.resolve(true);},
          onDidReceiveMessage:callback=>{view.receive=callback;return {dispose(){}};}},
          onDidDispose:callback=>{view.close=callback;return {dispose(){}};},show(){}};
        views.push(view);return view;
      }
  const context={extensionUri:'https://fixture-extension',subscriptions:[],secrets:{get:async key=>secrets.get(key),store:async (key,value)=>{secrets.set(key,value);}},
    workspaceState:{get:key=>state.get(key),update:async (key,value)=>{state.set(key,value);}}};
  state.set('agentflow.session',{url:'http://127.0.0.1:8765',id:'saved'});
  let intervalID=0;
  const sandbox={module:{exports:{}},URL,require:name=>name==='vscode'?vscode:name==='./client'?{BackendClient:TestClient,backendOrigin,validateToken}:name==='./webview'?require('../webview'):require(name),
    setInterval:callback=>{const id=++intervalID;intervals.set(id,callback);return id;},clearInterval:id=>intervals.delete(id)};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../extension.js'),'utf8'),sandbox,{filename:'extension.js'});
  sandbox.module.exports.activate(context);
  return {token,commands,secrets,requests,views,intervals,state,errors,context,decisions,
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
