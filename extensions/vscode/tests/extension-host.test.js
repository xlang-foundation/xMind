'use strict';
// Deterministic VS Code API/HTTP fixtures test host-controller behavior only.
// This is not an actual IDE rendering test, native engine test or live inference.
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const { BackendClient,backendOrigin,validateToken,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanObservation,validatePlanInputText,ContextViewController,validateGraphContext } = require('../client');
const {planFixture,inputMessage}=require('./plan-fixture');

function harness(options={}) {
  const token='synthetic-extension-host-access-token';
  const commands=new Map(),secrets=new Map(),requests=[],views=[],intervals=new Map();
  const state=new Map(),errors=[],comparisons=[],bootstrapTasks=[],ready=[];let documentProvider;
  let pendingHistory;
  let pendingOperation;
  let operations=options.operations||[];
  let sidebarProvider,folderListener;const backendOwners=[];
  const renameRequests=[],decisions=[],providerRequests=[],discoveryRequests=[],inputPrompts=[],pickers=[],graphRequests=[],humanInputs=[],planInputs=[],planResumes=[],contextRequests=[],graphResumes=[];
  const transcript=[{seq:1,role:'user',data:{content:'Earlier user prompt'}},{seq:2,role:'assistant',data:{content:'Persisted synthetic response'}}];
  const fetchImpl=async (url,requestOptions)=>{
    assert.equal(requestOptions.headers.Authorization,`Bearer ${token}`);
    const target=new URL(url);requests.push(target.pathname+target.search);
    let data;
    if(target.pathname.endsWith('/context/compact')){const body=JSON.parse(requestOptions.body);contextRequests.push(body);options.contextRecord={...options.contextRecord,head_revision:options.contextRecord.head_revision+1};data={id:body.id,state:'pending'};}
    else if(target.pathname.includes('/context/requests/'))data={id:target.pathname.split('/').at(-1),state:'completed'};
    else if(target.pathname.endsWith('/context'))data=await (options.contextRead?options.contextRead():options.contextRecord);
    else if(target.pathname.endsWith('/resume')&&target.pathname.startsWith('/v1/graph-runs/')){graphResumes.push(JSON.parse(requestOptions.body));data=options.graphRoot.run;}
    else if(target.pathname==='/v1/agent/planning')data={enabled:true,tools:['inspect_plan','plan_tasks','revise_plan']};
    else if(options.plan&&target.pathname===`/v1/runs/${options.plan.run.id}/plan`)data=options.planRead?await options.planRead():options.plan;
    else if(options.plan&&target.pathname===`/v1/runs/${options.plan.run.id}/plan/human/${options.plan.questions[0].id}`){planInputs.push(JSON.parse(requestOptions.body));data={...options.plan.run,state:'running'};}
    else if(options.plan&&target.pathname===`/v1/runs/${options.plan.run.id}/plan/resume`){planResumes.push(JSON.parse(requestOptions.body));data={...options.plan.run,state:'running'};}
    else if(target.pathname==='/v1/provider/profiles')return options.profileRegistry?{ok:true,json:async()=>requestOptions.method==='POST'?options.profileSave(JSON.parse(requestOptions.body)):options.profileRegistry()}:{ok:false,status:404,json:async()=>({detail:'Legacy backend has no profile API'})};
    if(data!==undefined)return {ok:true,json:async()=>data};
    if(target.pathname==='/v1/provider/profiles/models')return {ok:true,json:async()=>({models:options.profileDiscovery?await options.profileDiscovery(JSON.parse(requestOptions.body)):options.profileModels||options.catalogue.models})};
    if(target.pathname==='/v1/provider/profiles/select')return {ok:true,json:async()=>options.profileSelect(JSON.parse(requestOptions.body))};
    if(target.pathname==='/v1/runs'&&options.onAdmission)return options.onAdmission(JSON.parse(requestOptions.body));
    if(target.pathname==='/v1/health') data=options.health||{agent_execution:true,status:'ok'};
    else if(target.pathname==='/v1/graphs')data={graphs:options.graphs||[]};
    else if(target.pathname==='/v1/graph-runs'){
      graphRequests.push(JSON.parse(requestOptions.body));data=options.graphRoot.run;
    }
    else if(target.pathname.startsWith('/v1/graph-runs/')){
      const root=options.graphRoot;assert.ok(root,'Graph fixture must be explicit');assert.equal(target.pathname.split('/')[3],root.run.id);
      if(target.pathname.endsWith('/events'))data=target.search==='?after=0'?(options.graphEvents||[]):[];
      else if(target.pathname.endsWith('/history'))data=options.childHistory||[];
      else if(target.pathname.endsWith('/children'))data=options.graphChildren||[];
      else if(target.pathname.includes('/human/')){humanInputs.push(JSON.parse(requestOptions.body));data=options.graphInputResult||root;}
      else data=root;
    }
    else if(target.pathname==='/v1/models') {
      if(options.legacyCatalogue) return {ok:false,status:404,json:async()=>({detail:'Resource not found'})};
      data=options.catalogue||{default_model:'synthetic-host-default',models:[{id:'synthetic-host-default'},{id:'synthetic-host-alternate'}]};
    }
    else if(target.pathname==='/v1/provider/models'){
      discoveryRequests.push(JSON.parse(requestOptions.body));
      if(options.discoveryError || (options.rejectSavedKey && !discoveryRequests.at(-1).api_key))return {ok:false,status:502,json:async()=>({detail:'Model discovery provider returned HTTP 401'})};
      data=await (options.discoveredModels||{models:[{id:'fixture-other'},{id:'fixture-model'}]});
    }
    else if(target.pathname==='/v1/provider/configuration'){
      if(requestOptions.method==='POST'){
        const value=JSON.parse(requestOptions.body);providerRequests.push(value);options.health={agent_execution:true,status:'ok'};options.catalogue={default_model:value.model,models:[{id:value.model}]};data={...options.providerSetup,revision:options.providerSetup.revision+1,configured:true,model:value.model};
      }else data=options.providerSetup;
    }
    else if(target.pathname==='/v1/sessions') data=[{id:'saved',title:options.savedTitle||'Saved session'}];
    else if(target.pathname==='/v1/sessions/saved/title'){const value=JSON.parse(requestOptions.body);renameRequests.push(value);if(options.renameConflict)return {ok:false,status:409,json:async()=>({detail:'Title changed'})};options.savedTitle=value.title;data={id:'saved',title:value.title};}
    else if(target.pathname==='/v1/sessions/saved/history') data=pendingHistory?await pendingHistory:transcript;
    else if(target.pathname==='/v1/sessions/saved/runs') data=options.runs||[{id:'finished',state:options.running?'running':'completed'}];
    else if(options.ownedChildren&&target.pathname.endsWith('/children'))data=options.ownedChildren;
    else if(options.ownedChildren&&target.pathname.includes('/children/')&&target.pathname.endsWith('/history')){const child=options.ownedChildren.find(value=>value.run.id===target.pathname.split('/')[5]);assert.ok(child);assert.equal(target.pathname.split('/')[3],child.run.parent_id);data=options.ownedHistories?.[child.run.id]||[];}
    else if(options.ownedChildren&&target.pathname.endsWith('/tree-events'))data=target.search==='?after=0'?(options.treeEvents||[]):[];
    else if(options.ownedChildren?.some(child=>target.pathname==='/v1/runs/'+child.run.id+'/operations'))data=[];
    else if(options.graphChildren?.some(child=>target.pathname==='/v1/runs/'+child.id+'/operations'))data=operations;
    else if(options.runs && /^\/v1\/runs\/[^/]+(?:\/operations|\/events)?$/.test(target.pathname)) {
      const id=target.pathname.split('/')[3],run=options.runs.find(item=>item.id===id);assert.ok(run,'Host must request only selected-session runs');
      data=target.pathname.endsWith('/operations')?(options.operationsByRun?.[id]||[]):target.pathname.endsWith('/events')?(target.search==='?after=0'?[{seq:1,kind:'run.'+run.state,data:{}}]:[]):run;
    }
    else if(target.pathname==='/v1/runs/finished') data={id:'finished',state:options.running?'running':'completed'};
    else if(options.graphChildren?.some(child=>target.pathname==='/v1/runs/'+child.id+'/operations'))data=operations;
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
    workspace:{isTrusted:true,workspaceFolders:[{name:'TestProj',uri:{fsPath:'D:\\TestProj',toString:()=> 'file:///D:/TestProj'}}],onDidChangeWorkspaceFolders:callback=>{folderListener=callback;return {dispose(){}};},getConfiguration:()=>({get:()=> 'http://localhost:8765',update:async()=>{}}),registerTextDocumentContentProvider:(scheme,provider)=>{assert.equal(scheme,'xmind-review');documentProvider=provider;return {dispose(){}};}},
    ViewColumn:{Beside:2},
    commands:{registerCommand:(name,callback)=>{commands.set(name,callback);return {dispose(){}};},
      executeCommand:async (name,...args)=>{if(name==='vscode.diff'){comparisons.push(args);return;}if(['workbench.view.extension.xmind','workbench.view.explorer'].includes(name))return;assert.equal(name,'xmind.workspace.focus');sidebarProvider.resolveWebviewView(makeView());}},
    window:{showQuickPick:async(items,settings)=>{pickers.push({items,settings});return items.find(item=>item.label===options.modelInput);},showInputBox:async prompt=>{inputPrompts.push(prompt);if(prompt.title==='xMind: OpenAI API key'){assert.equal(prompt.password,true);return options.keyInput;}assert.equal(prompt.password,true);return token;},showErrorMessage:message=>{errors.push(message);return options.errorChoice;},
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
  // Native ownership is a separate mock boundary here. Its actual host lifecycle
  // and authenticated HTTP admission races are exercised in workspace-backend.
  class TestWorkspaceBackend{
    constructor(){this.epoch=0;this.active=undefined;}
    invalidate(){this.epoch++;this.active=undefined;}
    dispose(){this.invalidate();}
    async selectedFolder(){return {};}
    async connect(){const root=vscode.workspace.workspaceFolders[0].uri.fsPath;let owner=backendOwners.find(value=>value.metadata.root===root);if(!owner){owner={origin:'http://127.0.0.1:'+(8765+backendOwners.length),metadata:{configured:true,root,workspace_id:'windows-local-file-v1:1:'+backendOwners.length,authority_id:'b'.repeat(32)}};backendOwners.push(owner);await context.secrets.store('xmind.auth:'+owner.origin,token);}this.active={...owner,roots:vscode.workspace.workspaceFolders.map(value=>({fsPath:value.uri.fsPath})),epoch:this.epoch};return this.active;}
    async attach(){return this.connect();}
    async prepare(){if(!this.active)throw new Error('Workspace disconnected');return {origin:this.active.origin,epoch:this.epoch,fields:{expected_workspace_id:this.active.metadata.workspace_id,expected_workspace_authority_id:this.active.metadata.authority_id}};}
    assert(ticket){if(!this.active||ticket.epoch!==this.epoch||ticket.origin!==this.active.origin)throw new Error('Workspace changed');}
  }
  let intervalID=0;
  const sandbox={module:{exports:{}},URL,require:name=>name==='vscode'?vscode:name==='./client'?{BackendClient:TestClient,backendOrigin,validateToken,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanObservation,validatePlanInputText,ContextViewController,validateGraphContext}:name==='./webview'?require('../webview'):require(name),
    setTimeout:callback=>{bootstrapTasks.push(callback);return 1;},clearTimeout(){},setInterval:callback=>{const id=++intervalID;intervals.set(id,callback);return id;},clearInterval:id=>intervals.delete(id)};
  if(options.bootstrap)sandbox.process={env:{XMIND_UI_BACKEND_ORIGIN:'http://localhost:8765',XMIND_UI_BOOTSTRAP_TOKEN:token,XMIND_UI_READY_FILE:'labeled-fixture-marker'}};
  const originalRequire=sandbox.require;sandbox.require=name=>name==='./workspace-backend'?{WorkspaceBackend:TestWorkspaceBackend,machineSetting:(_,key)=>key==='backendUrl'?'http://localhost:8765':undefined}:name==='./native-runtime'?{resolveNativeRuntime:async()=>{throw new Error('No actual runtime in mocked host');}}:name==='./browser-view'?require('../browser-view'):name==='./edit-review'?require('../edit-review'):name==='node:fs'?{writeFileSync:(_,data)=>ready.push(JSON.parse(data))}:originalRequire(name);
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../extension.js'),'utf8'),sandbox,{filename:'extension.js'});
  const activation=sandbox.module.exports.activate(context);
  return {token,commands,secrets,requests,views,intervals,state,errors,context,renameRequests,decisions,comparisons,activation,bootstrapTasks,ready,providerRequests,discoveryRequests,inputPrompts,pickers,graphRequests,humanInputs,planInputs,planResumes,contextRequests,graphResumes,bootstrapEnvironment:sandbox.process?.env,reviewText:uri=>documentProvider.provideTextDocumentContent(uri),
    backendOwners,changeWorkspace(fsPath){vscode.workspace.workspaceFolders=[{name:'Changed',uri:{fsPath,toString:()=> 'file:///'+fsPath}}];folderListener();},
    configureBackend(health,catalogue){options.health=health;options.catalogue=catalogue;},
    configureRuns(runs){options.runs=runs;},
    pauseOperation(promise) {pendingOperation=promise;},
    pauseHistory(promise) {pendingHistory=promise;}};
}

test('VS Code conversation selection recovers discovery when native provider publication outlives its retired acknowledgement',async()=>{
 let registry={revision:4,active:'first',profiles:[{id:'first',route_id:'openai.responses',provider:'openai',model:'',revision:1},{id:'next',route_id:'anthropic.messages',provider:'anthropic',model:'next-current',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]};
 let release;const acknowledgement=new Promise(resolve=>release=resolve),selections=[],writes=[],admissions=[];
 const options={health:{agent_execution:false,provider_profile_admission:true},catalogue:{models:[],default_model:''},profileModels:[{id:'first-current'},{id:'first-alternate'}],profileRegistry:()=>registry,profileSelect:body=>{selections.push(body);registry={...registry,revision:5,active:body.id};options.health={agent_execution:true,provider_profile_admission:true};options.catalogue={models:[{id:'next-current'}],default_model:'next-current'};options.profileModels=[{id:'next-current'},{id:'next-alternate'}];return acknowledgement;},profileSave:body=>{writes.push(body);throw new Error('No model enrollment expected');},onAdmission:body=>{admissions.push(body);return {ok:true,json:async()=>({id:'finished',state:'completed'})};}};
 const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];
 try{view.receive({type:'ready'});await until(()=>view.posted.findLast(m=>m.type==='model-list')?.models.length===2);view.receive({type:'select-provider',id:'next'});await until(()=>selections.length===1);view.receive({type:'select',id:'saved'});release(registry);await until(()=>view.posted.findLast(m=>m.type==='status')?.text==='completed');await new Promise(resolve=>setImmediate(resolve));
  assert.deepEqual(selections,[{id:'next',expected_revision:4}]);assert.deepEqual(writes,[]);assert.deepEqual(view.posted.findLast(m=>m.type==='model-list').models,[{id:'next-current'},{id:'next-alternate'}]);assert.equal(view.posted.findLast(m=>m.type==='provider-profiles').active,'next');assert.equal(h.requests.filter(p=>p==='/v1/provider/profiles/models').length,2);assert.ok(!h.requests.includes('/v1/runs'));assert.ok(view.posted.some(m=>m.type==='history'));
  view.receive({type:'send',prompt:'Synthetic explicit submission after observed provider recovery'});await until(()=>admissions.length===1);assert.equal(admissions[0].provider_profile_id,'next');assert.equal(admissions[0].expected_provider_revision,5);assert.equal(admissions[0].model_id,'next-current');assert.deepEqual(writes,[]);
 }finally{view.close();}
});

test('VS Code recovery discards a late catalogue after another conversation intent and reads the current profile without writes',async()=>{
 const profiles=['first','next','last'].map(id=>({id,provider:'openai',route_id:'openai.responses',model:id+'-current',revision:1}));let registry={revision:4,active:'first',profiles,routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true}]};
 let release;const pending=new Promise(resolve=>release=resolve),discoveries=[],writes=[];
 const options={health:{agent_execution:true},runs:[],catalogue:{models:[{id:'first-current'}],default_model:'first-current'},profileRegistry:()=>registry,profileDiscovery:body=>{discoveries.push(body);return body.id==='next'?pending:[{id:body.id+'-current'},{id:body.id+'-alternate'}];},profileSave:body=>{writes.push(body);throw new Error('No enrollment expected');}};
 const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];
 try{view.receive({type:'ready'});await until(()=>view.posted.findLast(m=>m.type==='model-list')?.models.length===2);registry={...registry,revision:5,active:'next'};options.catalogue={models:[{id:'next-current'}],default_model:'next-current'};view.receive({type:'select',id:'saved'});await until(()=>discoveries.some(x=>x.id==='next'));
  registry={...registry,revision:6,active:'last'};options.catalogue={models:[{id:'last-current'}],default_model:'last-current'};view.receive({type:'select',id:'saved'});release([{id:'retired-next-only'}]);await until(()=>view.posted.findLast(m=>m.type==='model-list')?.models.some(x=>x.id==='last-alternate'));
  assert.deepEqual(discoveries.map(x=>({id:x.id,revision:x.expected_revision,hasKey:Object.hasOwn(x,'api_key')})),[{id:'first',revision:4,hasKey:false},{id:'next',revision:5,hasKey:false},{id:'last',revision:6,hasKey:false}]);assert.deepEqual(writes,[]);assert.ok(!view.posted.some(m=>m.type==='model-list'&&m.models.some(x=>x.id==='retired-next-only')));assert.equal(view.posted.findLast(m=>m.type==='provider-profiles').active,'last');assert.ok(!h.requests.includes('/v1/runs'));
 }finally{view.close();}
});

test('VS Code retains saved-profile discovery after Settings close and conversation selection, and rejects another-view stale catalogue',async()=>{
 let registry={revision:4,active:'first',profiles:[{id:'first',route_id:'openai.responses',provider:'openai',model:'fixture-current',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true}]};const writes=[];
 const options={health:{agent_execution:true},catalogue:{models:[{id:'fixture-current'}],default_model:'fixture-current'},profileModels:Array.from({length:135},(_,n)=>({id:'discovered-'+n})),profileRegistry:()=>registry,profileSave:body=>{writes.push(body);registry={...registry,revision:registry.revision+1,profiles:[{...registry.profiles[0],revision:registry.profiles[0].revision+1,model:body.model}]};options.catalogue={models:[{id:body.model}],default_model:body.model};return registry;}};
 const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];
 try{view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='model-list'&&m.models.length===135));
  const metadataReads=h.requests.filter(path=>path==='/v1/provider/profiles').length;view.receive({type:'discardProviderKey'});await until(()=>h.requests.filter(path=>path==='/v1/provider/profiles').length>metadataReads);await new Promise(resolve=>setImmediate(resolve));assert.equal(view.posted.findLast(m=>m.type==='model-list').models.length,135);assert.equal(writes.length,0);
  view.receive({type:'select',id:'saved'});await until(()=>view.posted.findLast(m=>m.type==='status')?.text==='completed');await new Promise(resolve=>setImmediate(resolve));view.receive({type:'model',id:'discovered-134'});await until(()=>writes.length===1);assert.equal(writes[0].model,'discovered-134');assert.equal(writes[0].expected_revision,4);assert.equal(Object.hasOwn(writes[0],'api_key'),false);await until(()=>view.posted.findLast(m=>m.type==='model-list')?.model==='discovered-134');assert.equal(view.posted.findLast(m=>m.type==='model-list').models.length,135);
  registry={...registry,revision:6,profiles:[{...registry.profiles[0],revision:3,model:'externally-selected'}]};options.catalogue={models:[{id:'externally-selected'}],default_model:'externally-selected'};view.receive({type:'model',id:'discovered-133'});await until(()=>view.posted.some(m=>m.type==='error'&&/settings changed/.test(m.text)));assert.equal(writes.length,1);assert.equal(view.posted.findLast(m=>m.type==='capabilities').model,'externally-selected');assert.ok(!h.requests.includes('/v1/runs'));
 }finally{view.close();}
});
test('opened folder changes rebind the existing sidebar to its own backend and clear the old draft/observations without cancelling execution',async()=>{
 const h=harness({running:true});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
 try{
  await until(()=>view.posted.some(message=>message.type==='workspace'&&message.root==='D:\\TestProj'));const priorReceiver=view.receive;
  h.changeWorkspace('D:\\OtherProject');await until(()=>h.backendOwners.length===2&&view.receive!==priorReceiver);view.receive({type:'ready'});
  await until(()=>view.posted.some(message=>message.type==='workspace'&&message.root==='D:\\OtherProject'));
  assert.equal(h.backendOwners[0].metadata.root,'D:\\TestProj');assert.equal(h.backendOwners[1].origin,'http://127.0.0.1:8766');
  assert.ok(view.posted.some(message=>message.type==='workspace-clear'));assert.ok(!h.requests.some(value=>value.endsWith('/cancel')));
  assert.equal(h.inputPrompts.length,0,'Single-folder managed startup never asks for a server token');
  assert.equal(h.views.length,1,'The right sidebar stays the same view');assert.ok(!JSON.stringify(view.posted).includes(h.token));
 }finally{view.close();}
});
test('an accepted old-folder run survives a delayed reply without becoming the new folder observation',async()=>{
 let finish;const admission=new Promise(resolve=>{finish=resolve;});const submissions=[];
 const h=harness({onAdmission:body=>{submissions.push(body);return admission;}});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
 try{await until(()=>view.posted.some(message=>message.type==='status'&&message.text==='completed'));view.receive({type:'send',prompt:'Old folder task'});await until(()=>submissions.length===1);const priorReceiver=view.receive;
  h.changeWorkspace('D:\\OtherProject');finish({ok:true,json:async()=>({id:'accepted-old-folder',session_id:'saved',state:'running'})});
  await until(()=>h.backendOwners.length===2&&view.receive!==priorReceiver);view.receive({type:'ready'});await until(()=>view.posted.some(message=>message.type==='workspace'&&message.root==='D:\\OtherProject'));
  assert.equal(submissions.length,1);assert.equal(submissions[0].expected_workspace_id,'windows-local-file-v1:1:0');assert.ok(!h.requests.some(route=>route.includes('accepted-old-folder')));assert.ok(!h.requests.some(route=>route.endsWith('/cancel')));
 }finally{view.close();}
});
async function until(predicate) {
  const deadline=Date.now()+2000;
  while(Date.now()<deadline) {if(predicate()) return;await new Promise(resolve=>setImmediate(resolve));}
  throw new Error('Extension host fixture timed out');
}

test('VS Code footer provider command selects a saved key-only profile via native CAS, then discovers without a key and waits for explicit model choice',async()=>{
 let registry={revision:4,active:'first',profiles:[{id:'first',route_id:'openai.responses',provider:'openai',model:'fixture-current',revision:1},{id:'claude-key-only',route_id:'anthropic.messages',provider:'anthropic',model:'',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]};const selections=[],writes=[];
 const options={health:{agent_execution:true},catalogue:{models:[{id:'fixture-current'}],default_model:'fixture-current'},profileModels:[{id:'fixture-claude'},{id:'fixture-claude-next'}],profileRegistry:()=>registry,profileSelect:body=>{selections.push(body);assert.deepEqual(body,{id:'claude-key-only',expected_revision:4});registry={...registry,revision:5,active:body.id};options.health={agent_execution:false};options.catalogue={models:[],default_model:''};return registry;},profileSave:body=>{writes.push(body);registry={...registry,revision:6,profiles:registry.profiles.map(profile=>profile.id===body.id?{...profile,revision:2,model:body.model}:profile)};options.health={agent_execution:true};options.catalogue={models:[{id:body.model}],default_model:body.model};return registry;}};
 const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];try{view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='settings-state'&&m.complete));assert.match(view.webview.html,/footer-provider/);view.receive({type:'select-provider',id:'claude-key-only'});await until(()=>view.posted.findLast(m=>m.type==='provider-profiles')?.active==='claude-key-only'&&view.posted.findLast(m=>m.type==='model-list')?.model==='');assert.equal(selections.length,1);assert.equal(writes.length,0);assert.ok(view.posted.some(m=>m.type==='provider-wire'&&m.wire==='anthropic-messages'));assert.equal(h.inputPrompts.filter(value=>value.title==='xMind: OpenAI API key').length,0);assert.equal(h.pickers.length,0);
  view.receive({type:'model',id:'fixture-claude'});await until(()=>writes.length===1);assert.deepEqual(writes[0],{id:'claude-key-only',route_id:'anthropic.messages',model:'fixture-claude',expected_revision:5,activate:true});assert.equal(registry.profiles[0].model,'fixture-current');await until(()=>view.posted.findLast(m=>m.type==='model-list')?.model==='fixture-claude');assert.equal(view.posted.findLast(m=>m.type==='model-list').models.length,2);assert.ok(!h.requests.includes('/v1/runs'));
 }finally{view.close();}
});
test('VS Code context control uses actual observed head and keeps the request acknowledgement separate from completed execution',async()=>{
 const options={health:{agent_execution:true,context_controls:true},contextRecord:{session_id:'saved',model_id:'synthetic-host-default',enabled:true,automatic:true,head_revision:4,source_watermark:7,manual:null,checkpoint:{id:'checkpoint',provider_elapsed_ms:12,preparation_elapsed_ms:14,usage:{input_tokens:41,output_tokens:9}}}},h=harness(options);
 await h.commands.get('agentflow.open')();const view=h.views[0];try{view.receive({type:'ready'});await until(()=>view.posted.some(message=>message.type==='context'));
  view.receive({type:'context-compact',session:'saved',model:'synthetic-host-default',expected_head_revision:4});await until(()=>h.contextRequests.length===1&&view.posted.some(message=>message.type==='context'&&message.record.manual?.state==='completed'));
  assert.equal(h.contextRequests[0].expected_head_revision,4);assert.equal(h.contextRequests[0].model_id,'synthetic-host-default');assert.ok(/^[0-9a-f]{32}$/.test(h.contextRequests[0].id));assert.ok(!Object.hasOwn(h.contextRequests[0],'actor'));assert.ok(!JSON.stringify(view.posted).includes(h.token));assert.equal(view.posted.findLast(message=>message.type==='context').record.checkpoint.usage.total_tokens,undefined);
  view.receive({type:'context-compact',session:'foreign',model:'synthetic-host-default',expected_head_revision:5});await until(()=>view.posted.some(message=>message.type==='error'&&message.text.includes('Refresh')));assert.equal(h.contextRequests.length,1);
 }finally{view.close();}
});
test('VS Code graph Resume is bound to selected root and freshly observed checkpoint/eligibility',async()=>{
 const run={id:'root',session_id:'saved',state:'paused',parent_id:'',node_id:'',graph_root:true},options={health:{agent_execution:true},runs:[run],graphRoot:{run,graph_id:'graph',graph_revision:1,checkpoint_revision:7,spec:{nodes:[{id:'gate',type:'human',prompt:'Synthetic answered question'}]},checkpoint:{nodes:[{id:'gate',state:'completed',output:{answer:'done'}}]},context:{enabled:true,resumable:true,remaining_active_ms:1000}}},h=harness(options);
 await h.commands.get('agentflow.open')();const view=h.views[0];try{view.receive({type:'ready'});await until(()=>view.posted.some(message=>message.type==='graph'));
  view.receive({type:'graph-resume',root:'root',revision:7});await until(()=>h.graphResumes.length===1);assert.deepEqual(h.graphResumes,[{expected_checkpoint_revision:7}]);
  options.graphRoot={...options.graphRoot,checkpoint_revision:8};view.receive({type:'graph-resume',root:'root',revision:7});await until(()=>view.posted.some(message=>message.type==='error'&&message.text.includes('changed')));assert.equal(h.graphResumes.length,1);
 }finally{view.close();}
});
test('VS Code Agent observes scoped leaf histories and tree events without inventing a graph or resubmitting work',async()=>{
 const parent={id:'finished',session_id:'saved',state:'completed',parent_id:'',graph_root:false},leaf={run:{id:'leaf',session_id:'saved',parent_id:'finished',state:'completed',graph_root:false},kind:'delegated_leaf',batch_id:'batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1};
 const h=harness({health:{agent_execution:true,owned_child_observation:true},runs:[parent],ownedChildren:[leaf],ownedHistories:{leaf:[{seq:3,role:'assistant',data:{content:'Synthetic leaf response',usage:{prompt_tokens:4,completion_tokens:2}}}]},treeEvents:[{seq:1,run_id:'leaf',kind:'conversation.assistant',data:{}},{seq:2,run_id:'finished',kind:'run.completed',data:{}}]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
 await until(()=>view.posted.some(message=>message.type==='transcript')&&h.intervals.size===0);const observation=view.posted.findLast(message=>message.type==='owned-children');assert.equal(observation.parent.id,'finished');assert.equal(observation.children[0].run.id,'leaf');assert.equal(observation.histories.leaf[0].data.usage.prompt_tokens,4);assert.ok(view.posted.some(message=>message.type==='owned-event'&&message.child_id==='leaf'));assert.ok(!view.posted.some(message=>message.type==='graph'));assert.ok(!h.requests.includes('/v1/runs'));assert.ok(h.requests.includes('/v1/runs/finished/children/leaf/history'));assert.equal(h.errors.length,0);view.close();
});
test('native admission profile conflict updates the sidebar account/model without task replay',async()=>{
  let registry={revision:4,active:'first',profiles:[{id:'first',route_id:'openai.responses',provider:'openai',model:'fixture-first',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true}]};const bodies=[];
  const options={health:{agent_execution:true,provider_profile_admission:true},catalogue:{default_model:'fixture-first',models:[{id:'fixture-first'}]},profileRegistry:()=>registry,onAdmission:body=>{
    bodies.push(body);registry={...registry,revision:5,active:'second',profiles:[...registry.profiles,{id:'second',route_id:'openai.responses',provider:'openai',model:'fixture-second',revision:1}]};options.catalogue={default_model:'fixture-second',models:[{id:'fixture-second'}]};
    return {ok:false,status:409,json:async()=>({detail:'Provider profile changed before run admission'})};
  }};
  const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
  await until(()=>view.posted.some(value=>value.type==='provider-profiles'&&value.active==='first'));await until(()=>view.posted.some(value=>value.type==='transcript'));
  view.receive({type:'send',prompt:'Retained host fixture task'});await until(()=>view.posted.some(value=>value.type==='error'&&value.text.includes('draft has been kept')));
  assert.equal(bodies.length,1);assert.equal(bodies[0].provider_profile_id,'first');assert.equal(bodies[0].expected_provider_revision,4);assert.ok(view.posted.some(value=>value.type==='provider-profiles'&&value.active==='second'));assert.ok(view.posted.some(value=>value.type==='capabilities'&&value.model==='fixture-second'));assert.ok(!view.posted.some(value=>value.type==='user'));view.close();
});

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
const providerFixture={revision:0,provider:'openai',model:'',endpoint:'https://api.openai.com/v1/chat/completions',configured:false};
test('saved Responses enrollment restores discovery and reports the native wire',async()=>{
  const h=harness({providerSetup:{...providerFixture,revision:1,configured:true,model:'fixture-other',wire:'responses',endpoint:'https://api.openai.com/v1/responses'}});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='model-list'));assert.ok(view.posted.some(m=>m.type==='provider-wire'&&m.wire==='responses'));view.receive({type:'model',id:'fixture-model'});await until(()=>h.providerRequests.length===1);assert.ok(!Object.hasOwn(h.providerRequests[0],'api_key'));await until(()=>view.posted.some(m=>m.type==='capabilities'&&m.model==='fixture-model'));view.close();
});
async function setupView(options={}){
  const h=harness({health:{agent_execution:false,status:'ok'},providerSetup:providerFixture,...options});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='settings-state'&&!m.busy));return {h,view};
}
test('extension answers only a displayed and freshly owned question with exact raw input/CAS, without changing approval authority',async()=>{
 const record=planFixture({root:'finished',session:'saved'}),options={health:{agent_execution:false,agent_planning:true,owned_child_observation:true},plan:record,runs:[record.run],ownedChildren:[]};const {h,view}=await setupView(options);
 try{await until(()=>view.posted.some(message=>message.type==='plan'));const raw='{"quantity":1.00000000000000000001,"answer":true}';view.receive(inputMessage(record,raw));await until(()=>h.planInputs.length===1);assert.deepEqual(h.planInputs,[{input_json:raw,expected_revision:2,expected_state_sequence:17}]);assert.equal(h.decisions.length,0);assert.equal(h.graphRequests.length,0);assert.ok(!JSON.stringify(view.posted).includes(h.token));}finally{view.close();}
});
test('extension rejects forged question fields, duplicate input keys and stale fresh-head observations before POST',async()=>{
 const record=planFixture({root:'finished',session:'saved'});let stale=false;const options={health:{agent_execution:false,agent_planning:true,owned_child_observation:true},plan:record,planRead:async()=>stale?{...record,plan:{...record.plan,state_sequence:18}}:record,runs:[record.run],ownedChildren:[]};const {h,view}=await setupView(options);
 try{await until(()=>view.posted.some(message=>message.type==='plan'));view.receive({...inputMessage(record),backend_node_id:'foreign-node'});await until(()=>view.posted.some(message=>message.type==='error'));assert.equal(h.planInputs.length,0);
  const errors=view.posted.filter(message=>message.type==='error').length;view.receive(inputMessage(record,'{"x":1,"\\u0078":2}'));await until(()=>view.posted.filter(message=>message.type==='error').length>errors);assert.equal(h.planInputs.length,0);
  stale=true;const before=view.posted.filter(message=>message.type==='error').length;view.receive(inputMessage(record));await until(()=>view.posted.filter(message=>message.type==='error').length>before);assert.equal(h.planInputs.length,0);assert.ok(view.posted.some(message=>message.type==='plan'&&message.record.plan.state_sequence===18));
 }finally{view.close();}
});
test('extension final-human-only ready paused owner exposes authenticated explicit resume with the observed preconditions',async()=>{
 const record=planFixture({root:'finished',session:'saved',answer:true}),{h,view}=await setupView({health:{agent_execution:false,agent_planning:true,owned_child_observation:true},plan:record,runs:[record.run],ownedChildren:[]});
 try{await until(()=>view.posted.some(message=>message.type==='plan'));view.receive({type:'plan-resume',root:record.run.id,plan_id:record.plan.id,expected_revision:2,expected_state_sequence:18});await until(()=>h.planResumes.length===1);assert.deepEqual(h.planResumes,[{expected_revision:2,expected_state_sequence:18}]);assert.equal(h.planInputs.length,0);assert.equal(h.decisions.length,0);}finally{view.close();}
});
test('extension close during fresh plan revalidation cannot post an answer or publish its obsolete observation',async()=>{
 const record=planFixture({root:'finished',session:'saved'});let hold=false,release;const options={health:{agent_execution:false,agent_planning:true,owned_child_observation:true},plan:record,planRead:async()=>hold?new Promise(yes=>release=yes):record,runs:[record.run],ownedChildren:[]};const {h,view}=await setupView(options);
 await until(()=>view.posted.some(message=>message.type==='plan'));hold=true;view.receive(inputMessage(record));await until(()=>release);const count=view.posted.length;view.close();release(record);await new Promise(resolve=>setImmediate(resolve));await new Promise(resolve=>setImmediate(resolve));assert.equal(h.planInputs.length,0);assert.equal(view.posted.length,count);assert.equal(h.intervals.size,0);
});
test('extension plan-read conflict clears the old action projection and never retries its input mutation',async()=>{
 const record=planFixture({root:'finished',session:'saved'});let conflict=false;const options={health:{agent_execution:false,agent_planning:true,owned_child_observation:true},plan:record,planRead:async()=>{if(conflict)throw Object.assign(new Error('Actual synthetic observation conflict'),{status:409});return record;},runs:[record.run],ownedChildren:[]};const {h,view}=await setupView(options);
 try{await until(()=>view.posted.some(message=>message.type==='plan'));const before=view.posted.filter(message=>message.type==='plan-clear').length;conflict=true;view.receive(inputMessage(record));await until(()=>view.posted.some(message=>message.type==='error'&&message.text.includes('observation conflict')));assert.equal(h.planInputs.length,0);assert.ok(view.posted.filter(message=>message.type==='plan-clear').length>before);assert.ok(!view.posted.some(message=>message.type==='event'&&message.event.kind==='run.failed'));}finally{view.close();}
});
test('settings key fetches a sidebar list; only a separate model selection saves encrypted backend settings',async()=>{
  const {h,view}=await setupView();view.receive({type:'saveProviderKey',key:'synthetic-private-provider-key',endpoint:'https://outside.invalid'});await until(()=>view.posted.some(m=>m.type==='model-list'));
  assert.equal(h.providerRequests.length,0);assert.equal(h.pickers.length,0);assert.ok(!h.inputPrompts.some(p=>p.title==='xMind: OpenAI API key'));
  assert.deepEqual(Array.from(view.posted.find(m=>m.type==='model-list').models,m=>m.id),['fixture-model','fixture-other']);
  view.receive({type:'model',id:'fixture-model'});await until(()=>h.providerRequests.length===1);await until(()=>view.posted.some(m=>m.type==='capabilities'&&m.execution));
  assert.deepEqual(h.providerRequests,[{model:'fixture-model',api_key:'synthetic-private-provider-key',expected_revision:0}]);
  assert.ok(!JSON.stringify(view.posted).includes('synthetic-private-provider-key'));assert.ok(!JSON.stringify([...h.state.values(),...h.secrets.values()]).includes('synthetic-private-provider-key'));assert.ok(!h.requests.includes('/v1/runs'));view.close();
});
test('sidebar selection rejects an ID outside the actual discovery response',async()=>{
  const {h,view}=await setupView();view.receive({type:'saveProviderKey',key:'synthetic-private-provider-key'});await until(()=>view.posted.some(m=>m.type==='model-list'));view.receive({type:'model',id:'forged-model'});await until(()=>view.posted.some(m=>m.type==='error'));assert.equal(h.providerRequests.length,0);assert.match(view.posted.find(m=>m.type==='error').text,/returned by OpenAI/);view.close();
});
test('sidebar rename binds selected session and original title, refreshes metadata and reports conflicts without inference',async()=>{
  for(const conflict of [false,true]){
    const {h,view}=await setupView({health:{agent_execution:false,status:'ok',session_rename:true},renameConflict:conflict});
    view.receive({type:'rename-session',id:'foreign',title:'Forged',expected_title:'Old'});await until(()=>view.posted.some(message=>message.type==='rename-result'));assert.equal(h.renameRequests.length,0);
    const historyReads=h.requests.filter(route=>route==='/v1/sessions/saved/history').length;
    view.receive({type:'rename-session',id:'saved',title:'Renamed fixture',expected_title:'Saved session'});await until(()=>h.renameRequests.length===1&&view.posted.some(message=>message.type==='rename-result'&&message.id==='saved'));
    assert.deepEqual(h.renameRequests,[{title:'Renamed fixture',expected_title:'Saved session'}]);assert.equal(view.posted.findLast(message=>message.type==='rename-result').success,!conflict);
    assert.equal(h.requests.filter(route=>route==='/v1/sessions/saved/history').length,historyReads);assert.ok(!h.requests.includes('/v1/runs'));view.close();
  }
});
test('invalid keys and unsupported provider destinations never transmit credentials',async()=>{
  for(const options of [{key:'bad key'},{key:'synthetic-key',providerSetup:{...providerFixture,endpoint:'https://outside.invalid'}}]){
    const {h,view}=await setupView(options);view.receive({type:'saveProviderKey',key:options.key});await until(()=>view.posted.some(m=>m.type==='error'));assert.equal(h.discoveryRequests.length,0);assert.equal(h.providerRequests.length,0);view.close();
  }
});
test('discovery errors and malformed or empty catalogs never save provider settings',async()=>{
  for(const options of [{discoveryError:true},{discoveredModels:{models:[]}},{discoveredModels:{models:[{id:'bad\nmodel'}]}}]){
    const {h,view}=await setupView(options);view.receive({type:'saveProviderKey',key:'synthetic-key'});await until(()=>view.posted.some(m=>m.type==='error'));assert.equal(h.providerRequests.length,0);assert.ok(!view.posted.some(m=>m.type==='model-list'));view.close();
  }
});
test('closing during discovery prevents publishing a list or saving a credential',async()=>{
  let supply;const discoveredModels=new Promise(resolve=>supply=resolve);const {h,view}=await setupView({discoveredModels});view.receive({type:'saveProviderKey',key:'synthetic-key'});await until(()=>h.discoveryRequests.length===1);view.close();supply({models:[{id:'fixture-model'}]});await new Promise(resolve=>setImmediate(resolve));assert.equal(h.providerRequests.length,0);assert.ok(!view.posted.some(m=>m.type==='model-list'));assert.equal(h.pickers.length,0);
});
test('saved key populates the sidebar automatically and changes models without a key popup',async()=>{
  const h=harness({providerSetup:{...providerFixture,revision:1,configured:true,model:'fixture-other'}});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='model-list'));view.receive({type:'model',id:'fixture-model'});await until(()=>h.providerRequests.length===1);
  assert.deepEqual(h.discoveryRequests,[{expected_revision:1}]);assert.deepEqual(h.providerRequests,[{model:'fixture-model',expected_revision:1}]);assert.equal(h.pickers.length,0);assert.ok(!h.inputPrompts.some(p=>p.title==='xMind: OpenAI API key'));view.close();
});
test('rejected saved key shows settings guidance and replacement uses the same sidebar selection',async()=>{
  const h=harness({providerSetup:{...providerFixture,revision:1,configured:true,model:'fixture-other'},rejectSavedKey:true});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='settings-state'&&m.text.includes('OpenAI rejected')));view.receive({type:'saveProviderKey',key:'synthetic-replacement-key'});await until(()=>view.posted.some(m=>m.type==='model-list'));view.receive({type:'model',id:'fixture-model'});await until(()=>h.providerRequests.length===1);
  assert.deepEqual(h.discoveryRequests,[{expected_revision:1},{api_key:'synthetic-replacement-key',expected_revision:1}]);assert.deepEqual(h.providerRequests,[{model:'fixture-model',api_key:'synthetic-replacement-key',expected_revision:1}]);assert.ok(!JSON.stringify(view.posted).includes('synthetic-replacement-key'));assert.equal(h.pickers.length,0);view.close();
});
const uncertainEdit={...pendingEdit,state:'uncertain'};
function graphFixture(){const run={id:'graph-root',session_id:'saved',state:'paused',graph_root:true};return {runs:[run],graphRoot:{run,graph_id:'fixture.flow',graph_revision:2,checkpoint_revision:4,spec:{nodes:[{id:'worker',type:'agent'},{id:'answer',type:'human',prompt:'Supply actual data'}]},checkpoint:{nodes:[{id:'worker',state:'completed'},{id:'answer',state:'waiting_human'}]}},graphs:[{id:'fixture.flow',revision:2,node_count:2,executable:true}],graphChildren:[{id:'child-a',session_id:'saved',parent_id:'graph-root',node_id:'worker',state:'completed',graph_root:false}],childHistory:[{seq:1,role:'assistant',data:{content:'Synthetic child response',usage:{prompt_tokens:12,completion_tokens:7,total_tokens:19}}}],graphEvents:[{seq:1,run_id:'child-a',kind:'conversation.assistant',data:{content:'Synthetic child response'}}]};}
test('graph observation keeps child transcript and usage out of the root stream',async()=>{
 const h=harness(graphFixture());await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graph'));const snapshot=view.posted.find(m=>m.type==='graph');assert.equal(snapshot.histories['child-a'][0].data.usage.prompt_tokens,12);assert.equal(snapshot.record.checkpoint_revision,4);assert.ok(view.posted.some(m=>m.type==='graph-event'&&m.node_id==='worker'));assert.ok(!view.posted.some(m=>m.type==='event'&&m.event.run_id==='child-a'));view.close();
});
test('human input binds observed root, waiting node and exact checkpoint; raw JSON reaches the backend',async()=>{
 const h=harness(graphFixture());await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graph'));
 for(const value of [{root:'foreign-root',node:'answer',revision:4},{root:'graph-root',node:'worker',revision:4},{root:'graph-root',node:'answer',revision:3}]){const count=view.posted.filter(m=>m.type==='error').length;view.receive({type:'graph-input',...value,input_json:'{}'});await until(()=>view.posted.filter(m=>m.type==='error').length>count);assert.equal(h.humanInputs.length,0);}
 view.receive({type:'graph-input',root:'graph-root',node:'answer',revision:4,input_json:'{"answer":1,"answer":2}'});await until(()=>h.humanInputs.length===1);assert.deepEqual(h.humanInputs,[{input_json:'{"answer":1,"answer":2}',expected_checkpoint_revision:4}],'Adapter must not erase duplicate keys before native validation');view.close();
});
test('selection intent invalidates queued human input before another conversation is displayed',async()=>{
 const h=harness(graphFixture());await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graph'));view.receive({type:'graph-input',root:'graph-root',node:'answer',revision:4,input_json:'{}'});view.receive({type:'refresh'});await until(()=>view.posted.some(m=>m.type==='error'));assert.equal(h.humanInputs.length,0);view.close();
});
test('graph child approval retains exact current operation review and uses the shared decision endpoint',async()=>{
 const options=graphFixture();options.graphRoot.run.state='running';options.graphChildren[0].state='running';options.graphRoot.checkpoint.nodes[0].state='running';options.operations=[{...pendingEdit,run_id:'child-a',arguments_json:JSON.stringify({...JSON.parse(pendingEdit.arguments_json),path:'file.cpp'})}];const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graph'));
 view.receive({type:'review',id:'edit'});await until(()=>h.comparisons.length===1);assert.equal(h.decisions.length,0);view.receive({type:'decide',id:'edit',decision:'allow'});await until(()=>h.decisions.length===1);assert.deepEqual(h.decisions,[{decision:'allow'}]);view.close();
});
test('graph catalog allows model-free tool graphs and rejects arbitrary view plans or graph IDs',async()=>{
 const options=graphFixture();options.runs=[];options.health={agent_execution:false,status:'ok'};const h=harness(options);await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graphs'));
 view.receive({type:'graph-select',id:'forged'});await until(()=>view.posted.some(m=>m.type==='error'));assert.equal(h.graphRequests.length,0);view.receive({type:'graph-select',id:'fixture.flow'});view.receive({type:'send',prompt:'Actual requested task',spec:{nodes:[]},graph_revision:99});await until(()=>h.graphRequests.length===1);assert.deepEqual(h.graphRequests,[{session_id:'saved',graph_id:'fixture.flow',graph_revision:2,prompt:'Actual requested task',expected_workspace_id:'windows-local-file-v1:1:0',expected_workspace_authority_id:'b'.repeat(32)}]);view.close();
});
test('workflow choice persists for the same backend and restores only an advertised executable graph',async()=>{
 const options=graphFixture();options.runs=[];const h=harness(options);await h.commands.get('agentflow.open')();let view=h.views[0];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graphs'));view.receive({type:'graph-select',id:'fixture.flow'});await until(()=>h.state.get('xmind.workflow')?.id==='fixture.flow');assert.equal(h.state.get('xmind.workflow').url,'http://127.0.0.1:8765');view.close();await h.commands.get('agentflow.open')();view=h.views[1];view.receive({type:'ready'});await until(()=>view.posted.some(m=>m.type==='graphs'));assert.equal(view.posted.find(m=>m.type==='graphs').selected,'fixture.flow');view.close();
});
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
  const process={...pendingEdit,tool:'run_process',arguments_json:JSON.stringify({profile_id:'fixture-profile',profile_revision:1,executable:'C:/fixture/xlang3.exe',executable_id:'fixture-reviewed-binding',arguments:['fixture-script.py'],workdir:'.',directory_id:'fixture-directory',timeout_ms:120000,output_limit:65536})};
  const changedBinding=JSON.stringify({...JSON.parse(process.arguments_json),executable_id:'fixture-changed-binding'});
  for(const [proposal,replacement] of [[pendingEdit,'{"different":"payload"}'],[process,changedBinding]]){
    const h=harness({running:true,operations:[proposal]});await h.commands.get('agentflow.open')();const view=h.views[0];view.receive({type:'ready'});
    await until(()=>view.posted.some(message=>message.type==='operations' && message.operations.length));
    h.pauseOperation(Promise.resolve({...proposal,arguments_json:replacement}));
    view.receive({type:'decide',id:'edit',decision:'allow'});
    await until(()=>view.posted.some(message=>message.type==='error'));
    assert.equal(h.decisions.length,0,'Changed file proposal or executable binding must never receive a late approval');view.close();
  }
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
