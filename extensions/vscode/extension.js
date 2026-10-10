'use strict';
const vscode = require('vscode');
const crypto = require('node:crypto');
const { BackendClient, backendOrigin, validateToken, providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanObservation,validatePlanInputText,ContextViewController,validateGraphContext,SkillViewController,EventStreamSubscription } = require('./client');
const { html } = require('./webview');
const { editReview } = require('./edit-review');
const {patchReview}=require('./patch-review');
const { browserViewLauncher } = require('./browser-view');
const { WorkspaceBackend,machineSetting } = require('./workspace-backend');
const { resolveNativeRuntime } = require('./native-runtime');
const { captureEditorSelection } = require('./editor-selection');

async function activate(context) {
  const browserViews=browserViewLauncher(vscode,context);
  const workspaceBackend=new WorkspaceBackend(vscode,context,resolveNativeRuntime);
  // Interactive preview uses a normal development host. VS Code test hosts
  // deliberately use in-memory storage and cannot verify reconnect persistence.
  let previewReady,previewOrigin;
  if(typeof process!=='undefined' && context.extensionMode===vscode.ExtensionMode?.Development && process.env.XMIND_UI_BACKEND_ORIGIN && process.env.XMIND_UI_READY_FILE) {
    previewOrigin=backendOrigin(process.env.XMIND_UI_BACKEND_ORIGIN);previewReady=process.env.XMIND_UI_READY_FILE;
    const token=process.env.XMIND_UI_BOOTSTRAP_TOKEN;
    delete process.env.XMIND_UI_BOOTSTRAP_TOKEN;delete process.env.XMIND_UI_BACKEND_ORIGIN;delete process.env.XMIND_UI_READY_FILE;
    if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before connecting to xMind Server.');
    await vscode.workspace.getConfiguration('agentflow').update('backendUrl',previewOrigin,vscode.ConfigurationTarget.Global);
    if(token)await context.secrets.store(`xmind.auth:${previewOrigin}`,validateToken(token));
  }
  const showEditReview=editReview(vscode,context);
  let panel;
  let sidebarView;
  let resolveSidebar;
  let client;
  let health;
  let sessionId;
  let runId;
  let sessionRuns=[];
  let cursor = 0;
  let eventSubscription,snapshotTimer,snapshotPromise;
  let pendingTreeEvents=false;
  let generation = 0;
  let opening;
  let receiveSubscription,disposeSubscription;
  let contextReadyView,pendingEditorContexts=[];
  let messages = Promise.resolve();
  let reviewed = new Map();
  let modelCatalogue = {models:[],default_model:''};
  let selectedModel;
  let providerSelection;
  let profileController;
  let graphCatalogue=[],selectedGraph,graphSnapshot;
  let graphChildren=new Map(),childHistory=new Map();
  let ownedObservation=false;
  let planningObservation=false,planSnapshot,pendingPlanRead=false,planReadConflicts=0;
  let contextObservation=false,contextController;
  let skillController;
  const stateKey = 'agentflow.session';
  const modelStateKey = 'xmind.model';
  const runStateKey = 'xmind.observedRun';
  const graphStateKey = 'xmind.workflow';
  const connectionState=()=>({url:client.baseUrl,...(workspaceBackend.stateFields?.()||{})});
  const savedConnection=record=>{const scope=workspaceBackend.stateFields?.();return scope?record?.workspace_id===scope.workspace_id&&record?.profile_directory===scope.profile_directory:record?.url===client.baseUrl;};

  const post = message => panel?.webview.postMessage(message.type==='capabilities'?{...message,fileEditProposals:client?health?.file_edit_proposals:undefined}:message);
  async function readSkills(){
    if(!panel||!client)return;const target=client,version=generation;
    if(health?.skill_controls!==true){if(skillController)skillController.invalidate();return;}
    if(!skillController||skillController.client!==target)skillController=new SkillViewController(target,post,()=>({session:sessionId,generation,enabled:!!panel&&client===target&&health?.skill_controls===true}));
    try{await skillController.read();}catch(error){if(client===target&&version===generation)post({type:'skills-error',text:error.message});}
  }
  const stop = () => { clearTimeout(snapshotTimer);snapshotTimer=undefined;eventSubscription?.stop();generation++;contextController?.invalidate();skillController?.invalidate();profileController?.invalidate(); };
  const streamCurrent=pin=>!!panel&&panel===pin.view&&generation===pin.generation&&client===pin.client&&sessionId===pin.session&&configuredOrigin()===pin.origin;
  function watch(){
    const active=value=>['queued','running','paused'].includes(value.state);
    const run=sessionRuns.find(value=>value.id===runId&&active(value))||sessionRuns.find(active)||((pendingTreeEvents||pendingPlanRead)&&sessionRuns.find(value=>value.id===runId));
    if(!panel||!client||!sessionId||!run){eventSubscription?.stop();return;}
    eventSubscription??=new EventStreamSubscription({current:streamCurrent,onObservation:async(value,pin)=>{if(streamCurrent(pin))await poll();},onEvent:async(event,pin)=>{
      if(!streamCurrent(pin))return;
      if(pin.root===runId&&event.seq>cursor){
        if(event.run_id!==runId&&!graphChildren.has(event.run_id))await poll();
        if(!streamCurrent(pin))return;
        if(event.seq>cursor){const child=graphChildren.get(event.run_id);if(event.run_id!==runId&&!child)throw new Error('Refresh the owned child before displaying its event');const accepted=await post({type:event.run_id===runId?'event':pin.scope==='graph'?'graph-event':'owned-event',event,node_id:child?.node_id,child_id:event.run_id});if(!streamCurrent(pin))return;if(accepted===false)throw new Error('Sidebar did not accept the committed event');cursor=event.seq;}
      }
      if(!snapshotTimer)snapshotTimer=setTimeout(()=>{snapshotTimer=undefined;if(streamCurrent(pin))poll().then(()=>{if(streamCurrent(pin))watch();});},100);
    },onEnd:async(result,pin)=>{if(!streamCurrent(pin))return;await poll();if(streamCurrent(pin))watch();},onError:(error,pin)=>{if(streamCurrent(pin))post({type:'error',text:error.message});}});
    eventSubscription.watch({client,root:run.id,session:sessionId,scope:run.graph_root?'graph':ownedObservation?'tree':'run',generation,view:panel,origin:client.baseUrl,after:run.id===runId?cursor:0});
  }
  async function readContext(){
    if(!panel||!client)return;
    if(!contextObservation){if(contextController?.record)contextController.invalidate();return;}
    const target=client;
    contextController??=new ContextViewController(target,post,()=>({session:sessionId,model:selectedModel,generation,enabled:!!panel&&client===target&&configuredOrigin()===target.baseUrl&&contextObservation&&!sessionRuns.find(run=>run.id===runId)?.graph_root}),async()=>{
      if(!contextProfileAdmission)return;return profileController.admission();
    },()=>crypto.randomBytes(16).toString('hex'));return contextController.read();
  }
  let contextProfileAdmission=false;
  const busySession=()=>sessionRuns.some(run=>['queued','running','paused'].includes(run.state));
  const observedOperation=id=>id===runId || graphChildren.has(id);
  const clearGraph=()=>{pendingTreeEvents=false;planReadConflicts=0;pendingPlanRead=false;planSnapshot=undefined;post({type:'plan-clear'});graphSnapshot=undefined;graphChildren.clear();childHistory.clear();post({type:'graph-clear'});post({type:'owned-clear'});};
  const presentRuns=()=>post({type:'runs',runs:sessionRuns,selected:runId,busy:busySession()});
  context.subscriptions.push(vscode.window.registerWebviewViewProvider('xmind.workspace', {
    resolveWebviewView(view) {
      sidebarView = view;
      if (resolveSidebar) { const resolve = resolveSidebar; resolveSidebar = undefined; resolve(view); }
      else open().catch(error => vscode.window.showErrorMessage(error.message));
    }
  }, { webviewOptions: { retainContextWhenHidden: true } }));
  async function acquireSidebar() {
    if (sidebarView) return sidebarView;
    const available = new Promise(resolve => { resolveSidebar = resolve; });
    await vscode.commands.executeCommand('workbench.view.extension.xmind');
    await vscode.commands.executeCommand('xmind.workspace.focus');
    return available;
  }

  async function refreshGraphs(){
    const target=client,version=generation;let catalogue;
    try{catalogue=await target.graphs();}catch(error){if(error.status!==404)throw error;catalogue={graphs:[]};}
    if(client!==target || version!==generation || !panel)return;
    if(!Array.isArray(catalogue.graphs) || catalogue.graphs.length>256 || catalogue.graphs.some(g=>!g || typeof g.id!=='string' || !/^[A-Za-z0-9_.-]{1,64}$/.test(g.id) || !Number.isSafeInteger(g.revision) || g.revision<1 || typeof g.executable!=='boolean'))throw new Error('Invalid backend graph catalogue.');
    graphCatalogue=catalogue.graphs;if(!graphCatalogue.some(g=>g.id===selectedGraph&&g.executable))selectedGraph=undefined;
    post({type:'graphs',graphs:graphCatalogue,selected:selectedGraph});
  }
  async function pollGraph(id,version){
    const events=await client.graphEvents(id,cursor);
    const root=await client.graph(id),children=await client.graphChildren(id);
    if(version!==generation || !panel)return;
    if(root.run.id!==id || root.run.session_id!==sessionId || !root.run.graph_root || !Array.isArray(children) || children.some(c=>c.parent_id!==id || c.session_id!==sessionId || typeof c.node_id!=='string'))throw new Error('Graph observation identity changed.');
    const owned=new Map(children.map(c=>[c.id,c]));
    for(const event of events)if(event.run_id!==id && !owned.has(event.run_id))throw new Error('Event does not belong to the observed graph.');
    const definitions=new Map(root.spec.nodes.map(n=>[n.id,n]));
    for(const child of children){
      if(definitions.get(child.node_id)?.type!=='agent')continue;
      const history=await client.graphChildHistory(id,child.id);if(version!==generation || !panel)return;childHistory.set(child.id,{state:child.state,history});
    }
    const operations=[];for(const child of children){operations.push(...await client.operations(child.id));if(version!==generation || !panel)return;}
    graphSnapshot=root;graphChildren=owned;reviewed=new Map(operations.map(o=>[o.id,o]));
    post({type:'graph',record:root,children,histories:Object.fromEntries([...childHistory].filter(([child])=>owned.has(child)).map(([child,value])=>[child,value.history]))});
    for(const event of events){if(event.seq<=cursor)continue;post({type:event.run_id===id?'event':'graph-event',event,node_id:owned.get(event.run_id)?.node_id});cursor=event.seq;}
    post({type:'operations',operations:operations.map(operation=>({...operation,node_id:owned.get(operation.run_id)?.node_id}))});post({type:'status',text:root.run.state});
    sessionRuns=await client.runs(sessionId);if(version!==generation || !panel)return;presentRuns();
    if(['completed','failed','cancelled'].includes(root.run.state)){
      const finalEvents=await client.graphEvents(id,cursor);if(version!==generation || !panel)return;
      for(const event of finalEvents){if(event.seq<=cursor)continue;if(event.run_id!==id&&!owned.has(event.run_id))throw new Error('Final event does not belong to the observed graph.');post({type:event.run_id===id?'event':'graph-event',event,node_id:owned.get(event.run_id)?.node_id});cursor=event.seq;}
      const history=await client.history(sessionId);if(version!==generation || !panel)return;post({type:'transcript',history});
    }
  }
  async function poll() {
    const version=generation;
    while(snapshotPromise){await snapshotPromise;if(version!==generation||!panel)return;}
    if(!runId||version!==generation||!panel)return;
    const pending=pollSnapshot();snapshotPromise=pending;
    try{await pending;}finally{if(snapshotPromise===pending)snapshotPromise=undefined;}
  }
  async function pollSnapshot() {
    const id = runId;
    const version = generation;
    try {
      if(sessionRuns.find(run=>run.id===id)?.graph_root){await pollGraph(id,version);return;}
      if(ownedObservation){await pollOwned(id,version);return;}
      const events = await client.events(id, cursor);
      if (version !== generation) return;
      for (const event of events) {
        if(event.seq<=cursor)continue;
        post({ type: 'event', event });
        cursor = event.seq;
      }
      {
        const history = await client.history(sessionId);
        if (version !== generation) return;
        post({ type:'transcript',history,preserveLive:true });
      }
      const run = await client.status(id);
      if (version !== generation) return;
      const runs=await client.runs(sessionId);
      if(version!==generation) return;
      sessionRuns=runs;presentRuns();
      post({ type: 'status', text: run.state });
      const operations = await client.operations(id);
      if (version !== generation) return;
      reviewed = new Map(operations.map(operation => [operation.id, operation]));
      post({ type: 'operations', operations });
      if (['completed', 'cancelled', 'failed'].includes(run.state)) {
        // A terminal transition can occur between the event query and status query.
        const finalEvents = await client.events(id, cursor);
        if (version !== generation) return;
        for (const event of finalEvents) { if(event.seq<=cursor)continue;post({ type: 'event', event }); cursor = event.seq; }
        const history = await client.history(sessionId);
        if (version !== generation) return;
        post({ type: 'transcript', history });

      }
    } catch (error) { if (version === generation) post({ type: 'error', text: error.message }); }
    finally { if(version===generation&&panel){if(!busySession())await readSkills();await readContext().catch(error=>{if(version===generation)post({type:'error',text:error.message});});watch();} }
  }

  async function pollOwned(id,version){
    let run=await client.status(id);if(version!==generation||!panel)return;
    const validRoot=value=>{if(value.id!==id||value.session_id!==sessionId||value.parent_id||value.graph_root)throw new Error('Owned run observation identity changed');};validRoot(run);
    const publish=snapshot=>{
      graphChildren=new Map(snapshot.children.map(child=>[child.run.id,child.run]));
      reviewed=new Map(snapshot.operations.map(operation=>[operation.id,operation]));
      post({type:'owned-children',parent:run,children:snapshot.children,histories:snapshot.histories});
      for(const event of snapshot.events){if(event.seq<=cursor)continue;post({type:event.run_id===id?'event':'owned-event',event,child_id:event.run_id});cursor=event.seq;}
      post({type:'operations',operations:snapshot.operations.map(operation=>({...operation,node_id:graphChildren.get(operation.run_id)?.node_id}))});
    };
    let snapshot=await observeOwnedRun(client,run,sessionId,cursor);if(version!==generation||!panel)return;publish(snapshot);
    run=await client.status(id);if(version!==generation||!panel)return;
    validRoot(run);
    if(['completed','failed','cancelled'].includes(run.state)){snapshot=await observeOwnedRun(client,run,sessionId,cursor);if(version!==generation||!panel)return;publish(snapshot);}
    await readPlan(id,version);if(version!==generation||!panel)return;
    const history=await client.history(sessionId),runs=await client.runs(sessionId);if(version!==generation||!panel)return;
    sessionRuns=runs;presentRuns();post({type:'transcript',history,preserveLive:['queued','running','paused'].includes(run.state)});post({type:'status',text:run.state});
    pendingTreeEvents=!snapshot.caughtUp;
  }

  async function readPlan(root,version){
    if(!planningObservation)return;const target=client;let record;try{record=validatePlanObservation(await target.plan(root),root);}catch(error){
      if(error.status!==409)throw error;if(version!==generation||!panel||target!==client||runId!==root||configuredOrigin()!==target.baseUrl)return;
      planSnapshot=undefined;post({type:'plan-clear'});pendingPlanRead=++planReadConflicts<3;if(!pendingPlanRead)post({type:'error',text:'Plan is changing. Refresh its observation before answering or resuming.'});return;
    }
    if(version!==generation||!panel||target!==client||runId!==root||configuredOrigin()!==target.baseUrl)return;
    if(record.run.session_id!==sessionId)throw new Error('Plan conversation identity changed');
    planReadConflicts=0;pendingPlanRead=false;planSnapshot=JSON.parse(JSON.stringify(record));post({type:'plan',record:planSnapshot});return planSnapshot;
  }
  function readyPlan(record){return record?.enabled&&record.run.state==='paused'&&record.plan&&!record.plan.halted&&record.plan.claimed.length===0&&!record.questions.some(question=>question.state==='waiting')&&(record.plan.ready.length>0||record.plan.report_ready)&&record.calls.some(call=>call.pending);}
  async function controlPlan(message){
    const version=generation,target=client,view=panel,root=runId,session=sessionId,observed=planSnapshot;
    const current=()=>panel===view&&version===generation&&client===target&&runId===root&&sessionId===session&&configuredOrigin()===target.baseUrl;
    if(!vscode.workspace.isTrusted||!observed?.plan||!observed.enabled||message.root!==root||observed.run.id!==root||observed.run.session_id!==session||message.plan_id!==observed.plan.id||message.expected_revision!==observed.plan.revision||message.expected_state_sequence!==observed.plan.state_sequence)throw new Error('Refresh the selected plan before providing input or resuming.');
    const answer=message.type==='plan-input';let question;
    if(answer){question=observed.questions.find(value=>value.id===message.request);if(!['paused','running'].includes(observed.run.state)||!question||question.state!=='waiting'||question.backend_node_id!==message.backend_node_id||question.definition_revision!==message.definition_revision||question.published_event_seq!==message.published_event_seq||Date.now()>=question.expires_unix_ms)throw new Error('Refresh the current owned human question.');validatePlanInputText(message.input_json);}
    else if(!readyPlan(observed))throw new Error('The selected owner has no ready paused plan.');
    let fresh;try{fresh=validatePlanObservation(await target.plan(root),root);}catch(error){if(error.status===409&&current()){planSnapshot=undefined;post({type:'plan-clear'});}throw error;}if(!current())return;
    if(fresh.run.session_id!==session)throw new Error('Plan conversation identity changed');planSnapshot=JSON.parse(JSON.stringify(fresh));post({type:'plan',record:planSnapshot});
    if(!fresh.enabled||fresh.plan?.id!==observed.plan.id||fresh.plan.revision!==message.expected_revision||fresh.plan.state_sequence!==message.expected_state_sequence)throw new Error('Plan changed. Review the refreshed plan before submitting again.');
    if(answer){const actual=fresh.questions.find(value=>value.id===question.id);for(const field of ['id','plan_id','root_run_id','backend_node_id','definition_revision','published_event_seq','question','state','expires_unix_ms'])if(!actual||actual[field]!==question[field])throw new Error('Human question changed. Review the refreshed plan.');if(!['paused','running'].includes(fresh.run.state)||Date.now()>=actual.expires_unix_ms)throw new Error('Human question is no longer awaiting input.');}
    else if(!readyPlan(fresh))throw new Error('The selected owner is no longer ready to resume.');
    const result=answer?await target.planInput(root,question.id,message.input_json,message.expected_revision,message.expected_state_sequence):await target.resumePlan(root,message.expected_revision,message.expected_state_sequence);
    if(!current())return;if(result.id!==root||result.session_id!==session||result.parent_id||result.graph_root)throw new Error('Plan controller result identity changed');await poll();
    watch();
  }

  async function selectSession(id) {
    stop();
    const version = generation;
    clearGraph();
    sessionId = id;
    runId = undefined;
    sessionRuns=[];presentRuns();
    reviewed.clear();
    post({ type: 'operations', operations: [] });
    cursor = 0;
    await context.workspaceState.update(stateKey, { ...connectionState(), id });
    if (version !== generation || !panel) return;
    const history = await client.history(id);
    if (version !== generation || !panel) return;
    post({ type: 'history', history });
    const runs = await client.runs(id);
    if (version !== generation || !panel) return;
    const previousProfile=profileController?.state;await profileController?.refresh();
    if(version!==generation||!panel)return;
    if(previousProfile&&(previousProfile.revision!==profileController.state?.revision||previousProfile.active!==profileController.state?.active)){
      const current=await capabilities();if(version!==generation||!panel)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:selectedModel});
    }
    const retainedCatalogue=profileController?.models(selectedModel);
    sessionRuns=runs;
    presentRuns();
    // Native may have published a provider after its acknowledgement was
    // retired by a conversation switch. Re-discover that observed saved
    // profile without replaying selection or enrolling a model.
    if(previousProfile&&!retainedCatalogue&&profileController?.state?.active){try{await configureModel();}catch{}}
    if(version!==generation||!panel)return;
    const savedRun=context.workspaceState.get(runStateKey);
    const selected=savedConnection(savedRun) && savedRun.session_id===id?sessionRuns.find(run=>run.id===savedRun.id):undefined;
    const latest = selected||runs.at(-1);
    if (latest) {
      runId = latest.id;presentRuns();
      await context.workspaceState.update(runStateKey,{...connectionState(),session_id:id,id:runId});
      if(version!==generation || !panel) return;
      // History already includes completed messages; replay events in a separate log.
      watch();
      await poll();
    } else {presentRuns();post({ type: 'status', text: 'Ready' });}
    await readSkills();
  }

  async function selectRun(id) {
    if(!sessionId) throw new Error('Select a conversation before choosing a run.');
    const version=generation;
    const runs=await client.runs(sessionId);
    if(version!==generation || !panel) return;
    if(!runs.some(run=>run.id===id)) throw new Error('Run does not belong to the selected conversation.');
    clearGraph();sessionRuns=runs;runId=id;cursor=0;reviewed.clear();presentRuns();
    post({type:'operations',operations:[]});post({type:'reset-run'});
    await context.workspaceState.update(runStateKey,{...connectionState(),session_id:sessionId,id});
    if(version!==generation || !panel) return;
    await poll();watch();
  }

  async function refresh() {
    const version = generation;
    const sessions = await client.sessions();
    if (version !== generation || !panel) return;
    post({ type: 'sessions', sessions, selected: sessionId });
  }

  async function capabilities() {
    const health=await client.health();let catalogue={models:[],default_model:''};
    ownedObservation=health.owned_child_observation===true;
    planningObservation=Object.hasOwn(health,'agent_planning');if(planningObservation)await client.planning();
    contextObservation=health.context_controls===true;contextProfileAdmission=health.provider_profile_admission===true;
    if(health.agent_execution) {
      try {catalogue=await client.models();}
      catch(error) {
        if(error.status!==404) throw error;
        if(health.model) catalogue={default_model:health.model,models:[{id:health.model}]};
      }
    }
    return {health,catalogue};
  }
  function chooseModel(catalogue,preferred) {
    return catalogue.models.some(model=>model.id===preferred)?preferred:catalogue.default_model||undefined;
  }

  function configuredOrigin() {
    return workspaceBackend.active?.origin??backendOrigin(machineSetting(vscode,'backendUrl')??'http://127.0.0.1:8765');
  }
  const secretKey = origin => `xmind.auth:${origin}`;
  async function configureToken(initialToken) {
    if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before connecting to xMind Server.');
    const origin = configuredOrigin();
    const token = typeof initialToken === 'string' ? validateToken(initialToken) : await vscode.window.showInputBox({
      title: 'xMind Server authentication', password: true, ignoreFocusOut: true,
      prompt: `Enter the XMIND_AUTH_TOKEN for ${origin}. This authenticates with xMind Server. Configure provider API keys separately on that server.`,
      validateInput: value => { try { validateToken(value); return undefined; } catch (error) { return error.message; } }
    });
    if (token === undefined) return false;
    await context.secrets.store(secretKey(origin), validateToken(token));
    return true;
  }
  async function configureModel(key,profile,route) {
    if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before configuring xMind.');
    const target=client,origin=configuredOrigin(),version=generation;
    if(!target || target.baseUrl!==origin)throw new Error('Connect to xMind Server before configuring a model.');
    if(key!==undefined && (typeof key!=='string' || !/^[\x21-\x7e]{1,32768}$/.test(key)))throw new Error('Enter your provider API key without spaces.');
    providerSelection=undefined;
    if(await profileController?.discover(key,profile,route))return;
    try{
      const current=()=>{if(!panel || client!==target || configuredOrigin()!==origin || version!==generation)throw new Error('Backend or conversation changed during provider setup. Try again.');};
      const setup=await target.providerConfiguration();current();
      const wire=providerEnrollmentWire(setup);post({type:'provider-wire',wire:setup.configured?wire:undefined});
      if(!setup.configured && key===undefined)throw new Error('Open Settings at the top right and enter your OpenAI API key.');
      post({type:'settings-state',busy:true,text:'Fetching models from OpenAI…'});
      const catalogue=await target.discoverProviderModels(key,setup.revision);current();
      if(!Array.isArray(catalogue.models) || catalogue.models.length>4096 || catalogue.models.some(item=>!item || typeof item.id!=='string' || !/^[A-Za-z0-9_.:/-]{1,256}$/.test(item.id)))throw new Error('Backend returned an invalid model list.');
      const ids=[...new Set(catalogue.models.map(item=>item.id))].sort();
      if(!ids.length)throw new Error('OpenAI returned no models for this key. Check the API project access.');
      // A new key stays in host memory only until a real sidebar selection.
      providerSelection={target,origin,revision:setup.revision,ids,key,expires:key===undefined?Infinity:Date.now()+300000};key=undefined;
      const pendingSelection=providerSelection;
      if(pendingSelection.key!==undefined)setTimeout(()=>{if(providerSelection===pendingSelection && pendingSelection.key!==undefined){pendingSelection.key=undefined;providerSelection=undefined;post({type:'capabilities',execution:false,models:[],model:undefined});post({type:'status',text:'Unsaved provider setup expired · fetch models again in Settings'});}},300000);
      post({type:'model-list',models:ids.map(id=>({id})),model:pendingSelection.key===undefined?setup.model:undefined});
      post({type:'settings-state',busy:false,complete:true,text:'Models fetched. Choose a model in the sidebar to save the configuration.'});
      post({type:'status',text:'Choose a model below · provider access is not yet verified'});
    }catch(error){
      const text=error.status===502 && /provider returned HTTP (401|403)$/.test(error.message)?'OpenAI rejected the key. Open Settings and enter a valid API key.':error.message;
      post({type:'settings-state',busy:false,text});throw new Error(text);
    }finally{key=undefined;}
  }
  async function openPanel() {
    if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before using AgentFlow.');
    if (panel) { panel.show(); return; }
    await messages;
    let connection;
    if(previewOrigin||machineSetting(vscode,'backendMode')==='external'){
      const externalOrigin=previewOrigin??configuredOrigin();
      if(!await context.secrets.get(secretKey(externalOrigin))&&!await configureToken())return;
      connection=await workspaceBackend.attach(externalOrigin,await context.secrets.get(secretKey(externalOrigin)));
    }else connection=await workspaceBackend.connect();
    const origin=connection.origin,connectionEpoch=workspaceBackend.epoch;
    const connectionCurrent=()=>workspaceBackend.epoch===connectionEpoch&&workspaceBackend.active?.origin===origin;
    contextController?.dispose();contextController=undefined;profileController?.dispose();client = new BackendClient(origin, () => context.secrets.get(secretKey(origin)));
    client.bindWorkspace(workspaceBackend);
    const profileTarget=client;profileController=new ProviderProfileController(client,post,()=>!!panel&&client===profileTarget&&configuredOrigin()===profileTarget.baseUrl);
    const initial=await capabilities();if(!connectionCurrent())throw new Error('Workspace changed while opening the sidebar.');health=initial.health;modelCatalogue=initial.catalogue;
    const savedModel=context.workspaceState.get(modelStateKey);
    selectedModel=chooseModel(modelCatalogue,savedConnection(savedModel)?savedModel.id:undefined);
    const savedGraph=context.workspaceState.get(graphStateKey);selectedGraph=savedConnection(savedGraph)?savedGraph.id:undefined;
    const saved = context.workspaceState.get(stateKey);
    sessionId = savedConnection(saved) ? saved.id : undefined;
    const availableView=await acquireSidebar();if(!connectionCurrent())throw new Error('Workspace changed while opening the sidebar.');panel=availableView;
    panel.webview.options = { enableScripts: true, localResourceRoots: [context.extensionUri] };
    const assetVersion=crypto.randomBytes(16).toString('hex');
    const asset = (...parts) => panel.webview.asWebviewUri(vscode.Uri.joinPath(context.extensionUri, ...parts)).toString()+'?v='+assetVersion;
    contextReadyView=undefined;pendingEditorContexts=[];
    panel.webview.html = html(assetVersion, {
      source:panel.webview.cspSource,css:asset('media','chat.css'),script:asset('media','chat.js'),
      marked:asset('node_modules','marked','lib','marked.umd.js'),purify:asset('node_modules','dompurify','dist','purify.min.js'),patchReview:asset('patch-review.js')
    });
    const view = panel,workspaceEpoch=workspaceBackend.epoch;
    disposeSubscription?.dispose();receiveSubscription?.dispose();
    disposeSubscription=panel.onDidDispose(() => { if (panel === view) { contextReadyView=undefined;pendingEditorContexts=[];profileController?.dispose();stop(); reviewed.clear(); providerSelection=undefined; panel = undefined; sidebarView = undefined; } }, null, context.subscriptions);
    receiveSubscription=panel.webview.onDidReceiveMessage(message => {
      if(panel!==view||workspaceBackend.epoch!==workspaceEpoch)return;
      if(panel===view&&['model','select-provider'].includes(message?.type))contextController?.invalidate();
      // Selection intent invalidates an in-flight approval immediately, before
      // its queued selection handler can run behind that network request.
      if (panel === view && ['select','select-run','new','refresh'].includes(message?.type)) {
        stop(); reviewed.clear();clearGraph();post({ type: 'operations', operations: [] });
      }
      // Serialize view commands so overlapping selections/submissions cannot
      // overwrite the observed session or display one session's response in another.
      messages = messages.then(async () => { try {
        if (panel !== view||workspaceBackend.epoch!==workspaceEpoch) return;
        if (!message || typeof message.type !== 'string') return;
        if (message.type === 'ready') {
          post({type:'workspace',root:connection.metadata.root,roots:connection.roots,managed:!previewOrigin&&machineSetting(vscode,'backendMode')!=='external',backendChangePending:connection.backendChangePending===true});
          contextReadyView=view;const pending=pendingEditorContexts;pendingEditorContexts=[];
          for(const item of pending)if(item.view===view&&item.client===client&&item.generation===generation&&item.epoch===workspaceBackend.epoch)post(item.message);
          post({ type: 'capabilities', execution: health.agent_execution, renameSessions:health.session_rename===true, model:selectedModel, models:modelCatalogue.models });
          await refresh();
          if (sessionId) await selectSession(sessionId);
          await refreshGraphs();
          // Fetch with the saved backend key; settings handles a rejected key.
          try{await configureModel();}catch{}
          await readSkills();
        } else if(message.type==='skills-refresh'){
          if(!sessionId){const session=await client.createSession('Workspace skills');if(panel!==view)return;await selectSession(session.id);await refresh();}else await readSkills();
        } else if(message.type==='skills-change'){
          try{if(health?.skill_controls!==true||busySession()||!skillController)throw new Error('Wait for the selected conversation to become idle.');await skillController.change(message);}catch(error){post({type:'skills-error',text:error.message});}
        } else if (message.type === 'saveProviderKey') {
          let key=message.key;delete message.key;
          try{await configureModel(key===''?undefined:key,message.profile,message.route);}finally{key=undefined;}
        } else if (message.type === 'discardProviderKey') {
          providerSelection=undefined;profileController?.invalidate();
          const version=generation;await profileController?.refresh();if(panel!==view||version!==generation)return;
          const current=await capabilities();if(panel!==view||version!==generation)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue,selectedModel);post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:selectedModel});profileController?.models(selectedModel);
        } else if(message.type==='select-provider'){
          const version=generation;if(!await profileController.select(message.id)||panel!==view||version!==generation)return;
          const current=await capabilities();if(panel!==view||version!==generation||configuredOrigin()!==client.baseUrl)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);
          post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:selectedModel});
          await context.workspaceState.update(modelStateKey,{...connectionState(),id:selectedModel});await configureModel();
        } else if (message.type === 'refresh') {
          providerSelection=undefined;
          const version=generation;
          post({type:'capabilities',execution:false,models:[],model:undefined});
          post({type:'status',text:'Reconnecting to xMind…'});
          const current=await capabilities();
          if(panel!==view || version!==generation) return;
          health=current.health;modelCatalogue=current.catalogue;
          selectedModel=chooseModel(modelCatalogue,selectedModel);
          post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:selectedModel});
          await refresh();
          if(panel!==view || version!==generation) return;
          if(sessionId) await selectSession(sessionId);
          else if(health.agent_execution) post({type:'status',text:'Ready'});
          try{await configureModel();}catch{}
          await refreshGraphs();
          await readSkills();
        }
        else if (message.type === 'model' && typeof message.id === 'string') {
          const modelVersion=generation;
          let saved;try{saved=await profileController?.save(message.id);}catch(error){
            if(panel===view&&modelVersion===generation&&configuredOrigin()===client.baseUrl){const current=await capabilities();if(panel!==view||modelVersion!==generation||configuredOrigin()!==client.baseUrl)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:selectedModel});}
            throw error;
          }
          if(saved){
            if(panel!==view||modelVersion!==generation||configuredOrigin()!==client.baseUrl)return;
            const current=await capabilities();health=current.health;modelCatalogue=current.catalogue;
            if(panel!==view||modelVersion!==generation||configuredOrigin()!==client.baseUrl)return;
            post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:message.id});profileController.models(message.id);
          }else if(providerSelection){
            const selection=providerSelection;
            if(selection.target!==client || selection.origin!==configuredOrigin() || Date.now()>selection.expires){providerSelection=undefined;throw new Error('Model discovery expired. Fetch models again in Settings.');}
            if(!selection.ids.includes(message.id))throw new Error('Choose a model returned by OpenAI.');
            const configured=await client.configureProvider(message.id,selection.key,selection.revision);
            post({type:'provider-wire',wire:providerEnrollmentWire(configured)});
            selection.key=undefined;selection.revision=configured.revision;selection.expires=Infinity;
            const current=await capabilities();health=current.health;modelCatalogue=current.catalogue;
            post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:selection.ids.map(id=>({id})),model:message.id});
            post({type:'status',text:'Model configured · send a message to verify provider access'});
          }else if (!modelCatalogue.models.some(model => model.id === message.id)) throw new Error('Fetch models in Settings before choosing this model.');
          selectedModel = message.id;
          await context.workspaceState.update(modelStateKey,{...connectionState(),id:selectedModel});
          await refreshGraphs();
        }
        else if(message.type==='graph-select'){
          if(message.id && !graphCatalogue.some(g=>g.id===message.id&&g.executable))throw new Error('Select an executable graph registered by this backend.');
          selectedGraph=message.id||undefined;post({type:'graphs',graphs:graphCatalogue,selected:selectedGraph});
          await context.workspaceState.update(graphStateKey,{...connectionState(),id:selectedGraph});
        }else if(message.type==='graph-input'){
          if(!vscode.workspace.isTrusted || !graphSnapshot || graphSnapshot.run.id!==runId || message.root!==runId || !['paused','running'].includes(graphSnapshot.run.state))throw new Error('Select an active graph before providing input.');
          if(message.revision!==graphSnapshot.checkpoint_revision || !graphSnapshot.checkpoint.nodes.some(n=>n.id===message.node&&n.state==='waiting_human'))throw new Error('Human input changed. Refresh the current graph before answering.');
          if(typeof message.input_json!=='string' || message.input_json.length>65536)throw new Error('Human input must be bounded JSON.');
          await client.graphInput(runId,message.node,message.input_json,message.revision);await poll();
        }
        else if(message.type==='graph-resume'){
          const root=runId,session=sessionId,observed=graphSnapshot,target=client,version=generation;
          const current=()=>panel===view&&client===target&&version===generation&&runId===root&&sessionId===session&&configuredOrigin()===target.baseUrl;
          if(!vscode.workspace.isTrusted||!observed||message.root!==root||observed.run.id!==root||observed.run.session_id!==session||message.revision!==observed.checkpoint_revision||!validateGraphContext(observed.context,observed.run).resumable)throw new Error('Refresh a ready paused graph before resuming.');
          let fresh;try{fresh=await target.graph(root);}catch(error){if(error.status===409&&current()){graphSnapshot=undefined;post({type:'graph-clear'});}throw error;}if(!current())return;
          if(fresh.run.id!==root||fresh.run.session_id!==session||fresh.checkpoint_revision!==message.revision||!validateGraphContext(fresh.context,fresh.run).resumable)throw new Error('Graph changed. Refresh before resuming.');
          const result=await target.resumeGraph(root,message.revision);if(!current())return;if(result.id!==root||result.session_id!==session||result.graph_root!==true)throw new Error('Graph resume ownership changed');await poll();
          watch();
        }
        else if(message.type==='context-compact'){
          if(!vscode.workspace.isTrusted||!contextObservation)throw new Error('The current backend has no available context controls.');await readContext();await contextController.compact(message);
        }
        else if(message.type==='plan-input'||message.type==='plan-resume'){await controlPlan(message);}
        else if (message.type === 'new') {
          const session = await client.createSession('VS Code session');
          await selectSession(session.id);
          await refresh();
        } else if (message.type === 'select' && typeof message.id === 'string') {
          await selectSession(message.id);
        } else if(message.type==='rename-session'){
          const version=generation;
          try{if(health.session_rename!==true||message.id!==sessionId||typeof message.title!=='string'||typeof message.expected_title!=='string')throw new Error('Select a conversation on a backend supporting rename.');await client.renameSession(sessionId,message.title,message.expected_title);if(panel!==view||version!==generation)return;await refresh();if(panel===view&&version===generation)post({type:'rename-result',id:message.id,success:true});}
          catch(error){if(panel===view&&version===generation)post({type:'rename-result',id:message.id,success:false,text:error.status===409?'Conversation title changed. Refresh history and reopen Rename before saving.':error.message});}
        } else if (message.type === 'select-run' && typeof message.id === 'string') {
          await selectRun(message.id);
        } else if (message.type === 'send' && typeof message.prompt === 'string' && message.prompt.trim()) {
          const graph=selectedGraph?graphCatalogue.find(g=>g.id===selectedGraph&&g.executable):undefined;
          if(selectedGraph&&!graph)throw new Error('Graph is not executable on this backend.');
          if (!graph&&!health.agent_execution) throw new Error('Configure a model on xMind Server before submitting an agent run.');
          const admissionVersion=generation;
          const binding=(graph?health.graph_provider_profile_admission:health.provider_profile_admission)===true?await profileController.admission():undefined;
          if(panel!==view||admissionVersion!==generation||configuredOrigin()!==client.baseUrl)return;
          if (!sessionId) {
            const submissionClient=client;
            const session = await submissionClient.createSession(message.prompt.slice(0, 80));
            if (panel !== view||generation!==admissionVersion||client!==submissionClient) return;
            await selectSession(session.id);
            await refresh();
            if(panel!==view||client!==submissionClient)return;
          }
          if (panel !== view) return;
          if(busySession()) throw new Error('This conversation still has an active run. Stop or finish it before submitting another prompt.');
          const admissionClient=client,runVersion=generation;let run;
          try{run=graph?await admissionClient.graphRun(sessionId,graph.id,graph.revision,message.prompt,selectedModel,binding):await admissionClient.run(sessionId,message.prompt,selectedModel,binding);}
          catch(error){
            if(panel!==view||generation!==runVersion||client!==admissionClient)return;
            const changed=await profileController.reconcileAdmission(binding,error).catch(()=>false);
            if(panel!==view||generation!==runVersion||client!==admissionClient)return;
            if(changed){
              const current=await capabilities();if(panel!==view||generation!==runVersion||client!==admissionClient)return;
              health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);
              post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,models:modelCatalogue.models,model:selectedModel});
              await refreshGraphs();await context.workspaceState.update(modelStateKey,{...connectionState(),id:selectedModel});
              if(panel!==view||generation!==runVersion||client!==admissionClient)return;
              throw new Error('Provider settings changed in another view. Review the current profile and submit again. Your draft has been kept.');
            }
            throw error;
          }
          if (panel !== view||generation!==runVersion||client!==admissionClient) return; // Accepted execution survives view/folder changes.
          stop();clearGraph();runId = run.id; cursor = 0;
          sessionRuns=[...sessionRuns,run];presentRuns();
          await context.workspaceState.update(runStateKey,{...connectionState(),session_id:sessionId,id:run.id});
          if(panel!==view||client!==admissionClient) return;
          reviewed.clear(); post({ type: 'operations', operations: [] });
          post({ type: 'user', text: message.prompt });
          watch();
          await poll();
        } else if (message.type === 'cancel' && runId) {
          await client.cancel(runId); await poll();
        } else if (message.type === 'copy' && typeof message.text === 'string' && message.text.length <= 1048576) {
          await vscode.env.clipboard.writeText(message.text);
        } else if (message.type === 'openLink' && typeof message.url === 'string') {
          const link = new URL(message.url);
          if (!['https:','http:'].includes(link.protocol)) throw new Error('Only HTTP/HTTPS links can be opened.');
          await vscode.env.openExternal(vscode.Uri.parse(link.href));
        } else if (message.type === 'inspect-edit' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before inspecting an edit.');
          const proposal=reviewed.get(message.id);
          if(!proposal || !observedOperation(proposal.run_id) || proposal.state!=='uncertain' || proposal.tool!=='replace_file') throw new Error('Select a recorded uncertain file edit before inspecting.');
          const version=generation;
          const inspection=await client.inspectEdit(proposal.id);
          if(panel!==view || version!==generation) return;
          const current=inspection.operation,observed=inspection.observed;
          if(!current || current.id!==proposal.id || current.state!=='uncertain' || current.run_id!==proposal.run_id || current.workspace_id!==proposal.workspace_id || current.tool!==proposal.tool || current.arguments_json!==proposal.arguments_json || current.expires_unix_ms!==proposal.expires_unix_ms ||
             !observed || observed.workspace_id!==proposal.workspace_id || typeof observed.path!=='string' || typeof observed.file_id!=='string' || !/^[a-f0-9]{64}$/.test(observed.content_sha256) || !Number.isSafeInteger(observed.size) || observed.size<0 || observed.size>1048576 ||
             !['before','after','different'].includes(inspection.match) || typeof inspection.same_file!=='boolean' || (!inspection.same_file && inspection.match!=='different') || !Number.isSafeInteger(inspection.observed_unix_ms) || inspection.observed_unix_ms<=0 || inspection.observed_unix_ms>8640000000000000 || inspection.quarantine_released!==false) throw new Error('The inspection does not match the selected uncertain edit. Refresh its current state.');
          post({type:'edit-inspection',id:proposal.id,inspection});
        } else if (['decide','review'].includes(message.type) && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before deciding an operation.');
          if (message.type === 'decide' && message.decision !== 'allow' && message.decision !== 'deny') throw new Error('Decision must be allow or deny.');
          const proposal = reviewed.get(message.id);
          if (!proposal || !observedOperation(proposal.run_id) || proposal.state !== 'awaiting_approval') throw new Error('Inspect a pending operation in the selected run before deciding.');
          const version = generation;
          const current = await client.operation(message.id);
          if (panel !== view || version !== generation) return;
          if (current.id !== proposal.id || current.state !== 'awaiting_approval' || current.run_id !== proposal.run_id || current.workspace_id !== proposal.workspace_id || current.tool !== proposal.tool || current.arguments_json !== proposal.arguments_json || current.expires_unix_ms !== proposal.expires_unix_ms) {
            await poll();
            throw new Error('The operation changed since review. Inspect its current state before deciding.');
          }
          if (message.type === 'review') {await showEditReview(current);return;}
          if(message.decision==='allow'&&current.tool==='patch_file')patchReview(current);
          const decided = await client.decide(message.id, message.decision);
          if (panel !== view || version !== generation) return;
          reviewed.set(decided.id, decided);
          post({ type: 'operations', operations: [...reviewed.values()].map(operation=>({...operation,node_id:graphChildren.get(operation.run_id)?.node_id})) });
          await poll();
        }
      } catch (error) { if (panel === view&&workspaceBackend.epoch===workspaceEpoch) post({ type: 'error', text: error.message }); }
        finally{if(panel===view&&workspaceBackend.epoch===workspaceEpoch)await readContext().catch(error=>{if(workspaceBackend.epoch===workspaceEpoch)post({type:'error',text:error.message});});} });
    }, null, context.subscriptions);
  }
  function open() {
    if (!opening) opening = openPanel().finally(() => { opening = undefined; });
    return opening;
  }

  context.subscriptions.push(vscode.commands.registerCommand('agentflow.open', async () => {
    try { await open(); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.configureToken', async initialToken => {
    try { await configureToken(initialToken); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.openBrowser',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before connecting a browser view.');
      await open();const origin=configuredOrigin();if(!workspaceBackend.active)throw new Error('Connect the opened workspace before opening its browser.');
      let token=await context.secrets.get(secretKey(origin));
      try{const target=new BackendClient(origin,()=>token);await target.health();if(origin!==configuredOrigin())throw new Error('Backend changed while opening the browser. Try again.');await browserViews.open(origin,token);vscode.window.showInformationMessage('xMind Browser uses this server’s models and history. Paste the copied server token into Connect once.');}finally{token=undefined;}
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  function disconnectWorkspace(){
    contextReadyView=undefined;pendingEditorContexts=[];
    workspaceBackend.invalidate();stop();contextController?.dispose();contextController=undefined;profileController?.dispose();profileController=undefined;
    providerSelection=undefined;reviewed.clear();clearGraph();sessionId=undefined;runId=undefined;sessionRuns=[];client=undefined;
    modelCatalogue={models:[],default_model:''};selectedModel=undefined;graphCatalogue=[];selectedGraph=undefined;
    receiveSubscription?.dispose();receiveSubscription=undefined;disposeSubscription?.dispose();disposeSubscription=undefined;
    post({type:'workspace-clear'});post({type:'history',history:[]});post({type:'sessions',sessions:[]});post({type:'runs',runs:[],busy:false});post({type:'graphs',graphs:[]});post({type:'operations',operations:[]});post({type:'capabilities',execution:false,models:[]});post({type:'status',text:'Connecting the opened workspace…'});panel=undefined;
  }
  async function reconnectWorkspace(forcePick=false){
    const shown=!!panel||!!sidebarView||!!opening;disconnectWorkspace();
    if(opening)await opening.catch(()=>{});
    if(forcePick)await workspaceBackend.selectedFolder(true);
    if(shown)await open();
  }
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.selectWorkspaceRoot',async()=>{
    try{await reconnectWorkspace(true);}catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  if(vscode.workspace.onDidChangeWorkspaceFolders)context.subscriptions.push(vscode.workspace.onDidChangeWorkspaceFolders(()=>{
    reconnectWorkspace().catch(error=>vscode.window.showErrorMessage(error.message));
  }));
  if(vscode.workspace.onDidChangeConfiguration)context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event=>{
    if(['backendMode','backendUrl','runtimeDirectory','stdlibSource','providerConfigPath','workspaceEdits'].some(key=>event.affectsConfiguration('agentflow.'+key)))reconnectWorkspace().catch(error=>vscode.window.showErrorMessage(error.message));
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.selection', async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return;
    try {
      await open();
      const target=client,view=panel,epoch=generation,owner=workspaceBackend.active;
      if(!target||!view||!owner)throw new Error('Connect the workspace before adding editor context.');
      const ticket=await workspaceBackend.prepare();workspaceBackend.assert(ticket);
      if(ticket.origin!==target.baseUrl)throw new Error('The backend workspace changed.');
      const isCurrent=()=>client===target&&panel===view&&generation===epoch&&(!vscode.window.activeTextEditor||vscode.window.activeTextEditor===editor);
      const draft=await captureEditorSelection(editor,owner.metadata,{isCurrent});workspaceBackend.assert(ticket);
      if(!isCurrent())throw new Error('The editor or sidebar changed. Select the code again.');
      const message={type:'append-context',text:draft.text,root:owner.metadata.root};
      if(contextReadyView===view)post(message);
      else{if(pendingEditorContexts.length>=8)throw new Error('The sidebar is still opening. Wait before adding more selections.');pendingEditorContexts.push({view,client:target,generation:epoch,epoch:workspaceBackend.epoch,message});}
    } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push({ dispose:()=>{stop();workspaceBackend.dispose();receiveSubscription?.dispose();disposeSubscription?.dispose();} });
  if(previewReady) {
    // View resolution waits for extension activation. Opening synchronously
    // inside activation would wait on itself in a normal development host.
    const bootstrap=setTimeout(()=>{
      open().then(async()=>{
        await vscode.commands.executeCommand('workbench.view.explorer');
        require('node:fs').writeFileSync(previewReady,JSON.stringify({opened:true,location:'secondarySidebar',origin:previewOrigin,time:new Date().toISOString(),storage:'Normal development host; no extension test runner',scope:'Actual sidebar opened; no live inference claimed'})+'\n');
      }).catch(error=>vscode.window.showErrorMessage(error.message));
    },0);
    context.subscriptions.push({dispose:()=>clearTimeout(bootstrap)});
  }
  // Public observation API for real IDE acceptance/reconnection tooling. It
  // only re-reads the authenticated Native DTO; it does not start work or expose
  // owner tokens, provider configuration contents or private storage paths.
  return {workspaceStatus:()=>workspaceBackend.status()};
}

module.exports = { activate };
