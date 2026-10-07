'use strict';
// Deterministic VS Code API/HTTP fixtures test host-controller behavior only.
// This is not an actual IDE rendering test, native engine test or live inference.
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const { BackendClient,backendOrigin,validateToken } = require('../client');

function harness() {
  const token='synthetic-extension-host-access-token';
  const commands=new Map(),secrets=new Map(),requests=[],views=[],intervals=new Map();
  const state=new Map(),errors=[];
  let pendingHistory;
  const transcript=[{seq:1,role:'user',data:{content:'Earlier user prompt'}},{seq:2,role:'assistant',data:{content:'Persisted synthetic response'}}];
  const fetchImpl=async (url,options)=>{
    assert.equal(options.headers.Authorization,`Bearer ${token}`);
    const target=new URL(url);requests.push(target.pathname+target.search);
    let data;
    if(target.pathname==='/v1/health') data={agent_execution:true,status:'ok'};
    else if(target.pathname==='/v1/sessions') data=[{id:'saved',title:'Saved session'}];
    else if(target.pathname==='/v1/sessions/saved/history') data=pendingHistory?await pendingHistory:transcript;
    else if(target.pathname==='/v1/sessions/saved/runs') data=[{id:'finished',state:'completed'}];
    else if(target.pathname==='/v1/runs/finished') data={id:'finished',state:'completed'};
    else if(target.pathname==='/v1/runs/finished/events') data=target.search==='?after=0'?[{seq:1,kind:'run.completed',data:{}}]:[];
    else throw new Error(`Unexpected native route ${target.pathname}`);
    return {ok:true,json:async()=>data};
  };
  class TestClient extends BackendClient {constructor(url,provider) {super(url,provider,fetchImpl);}}
  const vscode={
    workspace:{isTrusted:true,getConfiguration:()=>({get:()=> 'http://localhost:8765'})},
    ViewColumn:{Beside:2},
    commands:{registerCommand:(name,callback)=>{commands.set(name,callback);return {dispose(){}};}},
    window:{showInputBox:async options=>{assert.equal(options.password,true);return token;},showErrorMessage:message=>errors.push(message),
      createWebviewPanel:()=>{
        const view={posted:[],webview:{html:'',postMessage:message=>{view.posted.push(message);return Promise.resolve(true);},
          onDidReceiveMessage:callback=>{view.receive=callback;return {dispose(){}};}},
          onDidDispose:callback=>{view.close=callback;return {dispose(){}};},reveal(){}};
        views.push(view);return view;
      }}
  };
  const context={subscriptions:[],secrets:{get:async key=>secrets.get(key),store:async (key,value)=>{secrets.set(key,value);}},
    workspaceState:{get:key=>state.get(key),update:async (key,value)=>{state.set(key,value);}}};
  state.set('agentflow.session',{url:'http://127.0.0.1:8765',id:'saved'});
  let intervalID=0;
  const sandbox={module:{exports:{}},require:name=>name==='vscode'?vscode:name==='./client'?{BackendClient:TestClient,backendOrigin,validateToken}:require(name),
    setInterval:callback=>{const id=++intervalID;intervals.set(id,callback);return id;},clearInterval:id=>intervals.delete(id)};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../extension.js'),'utf8'),sandbox,{filename:'extension.js'});
  sandbox.module.exports.activate(context);
  return {token,commands,secrets,requests,views,intervals,state,errors,context,
    pauseHistory(promise) {pendingHistory=promise;}};
}
async function until(predicate) {
  const deadline=Date.now()+2000;
  while(Date.now()<deadline) {if(predicate()) return;await new Promise(resolve=>setImmediate(resolve));}
  throw new Error('Extension host fixture timed out');
}

test('native terminal state refreshes transcript and stops polling without absent approval routes',async()=>{
  const h=harness();await h.commands.get('agentflow.open')();
  const view=h.views[0];assert.ok(view);
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
