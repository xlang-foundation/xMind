'use strict';
const vscode = require('vscode');
const crypto = require('node:crypto');
const { BackendClient,SessionHistoryPager,backendOrigin,validateToken,validateEditInspection,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanObservation,validatePlanInputText,ContextViewController,validateGraphContext,SkillViewController,AgentSelectionController,EventStreamSubscription,McpAuthorizationController,parseWorkspaceCommand } = require('./client');
const { html } = require('./webview');
const { editReview } = require('./edit-review');
const {patchReview,canRestoreFileOperation}=require('./patch-review');
const { browserViewLauncher } = require('./browser-view');
const { WorkspaceBackend,machineSetting } = require('./workspace-backend');
const { resolveNativeRuntime } = require('./native-runtime');
const { captureEditorSelection } = require('./editor-selection');
const { XMindPseudoterminal } = require('./terminal');
const { LanguageServerEditor } = require('./language-server-editor');
const { GitChangesTreeProvider } = require('./git-changes-tree');

async function activate(context) {
  const browserViews=browserViewLauncher(vscode,context);
  const workspaceBackend=new WorkspaceBackend(vscode,context,resolveNativeRuntime);
  const languageServerEditor=new LanguageServerEditor(vscode,workspaceBackend);
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
  const terminalBridges=new Set();
  let resolveSidebar;
  let client;
  let historyPager;
  let health;
  let sessionId;
  let runId;
  let sessionRuns=[];
  let cursor = 0;
  let eventSubscription,snapshotTimer,snapshotPromise,permissionTimer,sessionRefreshTimer,sessionCatalogueSignature;
  let pendingTreeEvents=false;
  let generation = 0;
  let opening;
  let receiveSubscription,disposeSubscription;
  let contextReadyView,pendingEditorContexts=[];
  let messages = Promise.resolve();
  let reviewed = new Map();
  let sessionPermissionRequests = new Map();
  let sessionPermissionsLoaded = false;
  let modelCatalogue = {models:[],default_model:''};
  let selectedModel;
  let sessionModelState;
  let providerSelection;
  let profileController;
  let mcpController;
  let graphCatalogue=[],selectedGraph,graphSnapshot;
  let graphChildren=new Map(),childHistory=new Map();
  let ownedObservation=false;
  let planningObservation=false,planSnapshot,pendingPlanRead=false,planReadConflicts=0;
  let contextObservation=false,contextController;
  let skillController;
  let agentController;
  const stateKey = 'agentflow.session';
  const modelStateKey = 'xmind.model';
  const runStateKey = 'xmind.observedRun';
  const graphStateKey = 'xmind.workflow';
  const connectionState=()=>({url:client.baseUrl,...(workspaceBackend.stateFields?.()||{})});
  const savedConnection=record=>{const scope=workspaceBackend.stateFields?.();return scope?record?.workspace_id===scope.workspace_id&&record?.profile_directory===scope.profile_directory:record?.url===client.baseUrl;};
  const gitChanges = new GitChangesTreeProvider(vscode);
  if(vscode.window.registerTreeDataProvider)context.subscriptions.push(vscode.window.registerTreeDataProvider('xmind.gitChanges',gitChanges));

  const post = message => panel?.webview.postMessage(message.type==='capabilities'?{...message,fileEditProposals:client?health?.file_edit_proposals:undefined,sessionDeletion:!!client}:message);
  async function readSkills(){
    if(!panel||!client)return;const target=client,version=generation;
    if(health?.skill_controls!==true){if(skillController)skillController.invalidate();return;}
    if(!skillController||skillController.client!==target)skillController=new SkillViewController(target,post,()=>({session:sessionId,generation,enabled:!!panel&&client===target&&health?.skill_controls===true}));
    try{await skillController.read();}catch(error){if(client===target&&version===generation)post({type:'skills-error',text:error.message});}
  }
  async function readAgentSelection(){
    if(!panel||!client)return;const target=client,version=generation;
    if(health?.agent_selection!==true){if(agentController)agentController.invalidate();return;}
    if(!agentController||agentController.client!==target)agentController=new AgentSelectionController(target,post,()=>({session:sessionId,generation,enabled:!!panel&&client===target&&health?.agent_selection===true}));
    try{await agentController.read();}catch(error){if(client===target&&version===generation)post({type:'agent-selection-error',text:error.message});}
  }
  const stop = () => { clearTimeout(snapshotTimer);snapshotTimer=undefined;clearTimeout(permissionTimer);permissionTimer=undefined;eventSubscription?.stop();generation++;contextController?.invalidate();skillController?.invalidate();agentController?.invalidate();profileController?.invalidate(); };
  function scheduleSessionRefresh(){clearTimeout(sessionRefreshTimer);if(!panel||health?.active_session_map!==true)return;sessionRefreshTimer=setTimeout(async()=>{sessionRefreshTimer=undefined;if(!panel)return;try{await refresh();}catch{}if(panel)scheduleSessionRefresh();},5000);}
  const streamCurrent=pin=>!!panel&&panel===pin.view&&generation===pin.generation&&client===pin.client&&sessionId===pin.session&&configuredOrigin()===pin.origin;
  function watch(){
    schedulePermissionRefresh();
    const pin=eventSubscription?.pin,selected=sessionRuns.find(value=>value.id===runId);
    if(pin&&selected&&['completed','failed','cancelled'].includes(selected.state)&&!eventSubscription.finished&&pin.root===selected.id&&streamCurrent(pin))return;
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
  function publishOperations(items){const base=items?new Map(items.map(operation=>[operation.id,operation])):reviewed;reviewed=new Map([...base].filter(([id,operation])=>!sessionPermissionsLoaded||operation.state!=='awaiting_approval'||sessionPermissionRequests.has(id)||observedOperation(operation.run_id)));for(const [id,operation] of sessionPermissionRequests)reviewed.set(id,operation);post({type:'operations',operations:[...reviewed.values()].map(operation=>({...operation,node_id:graphChildren.get(operation.run_id)?.node_id}))});}
  async function refreshSessionPermissions(target=client,session=sessionId,version=generation){if(!target||!session)return;const observed=await target.sessionPermissions(session);if(target!==client||session!==sessionId||version!==generation||!panel)return;if(!observed||observed.session_id!==session||!Array.isArray(observed.requests)||observed.requests.length>256||observed.requests.some(operation=>!operation||typeof operation.id!=='string'||typeof operation.run_id!=='string'||operation.state!=='awaiting_approval'||!Number.isSafeInteger(operation.expires_unix_ms)))throw new Error('Invalid session permission request list.');sessionPermissionRequests=new Map(observed.requests.map(operation=>[operation.id,operation]));sessionPermissionsLoaded=true;publishOperations();}
  function schedulePermissionRefresh(version=generation){clearTimeout(permissionTimer);if(!panel||!client||!sessionId)return;permissionTimer=setTimeout(async()=>{permissionTimer=undefined;if(!panel||version!==generation)return;try{await refreshSessionPermissions(client,sessionId,version);}catch(error){if(panel&&version===generation)post({type:'error',text:error.message});}if(panel&&version===generation)schedulePermissionRefresh(version);},2500);}
  const clearGraph=()=>{pendingTreeEvents=false;planReadConflicts=0;pendingPlanRead=false;planSnapshot=undefined;post({type:'plan-clear'});graphSnapshot=undefined;graphChildren.clear();childHistory.clear();post({type:'graph-clear'});post({type:'owned-clear'});};
  let inboxDispatching=false;
  async function refreshInbox(target=client,session=sessionId,version=generation){
    if(!target||!session)return [];
    const observed=await target.sessionInbox(session);if(target!==client||session!==sessionId||version!==generation||!panel)return [];
    if(!observed||!Array.isArray(observed.items)||observed.items.length>128||observed.items.some(item=>!item||item.session_id!==session||typeof item.id!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(item.id)||typeof item.content!=='string'||!['queued','claimed'].includes(item.state)))throw new Error('Invalid native session inbox.');
    post({type:'inbox',items:observed.items});return observed.items;
  }
  async function queuePrompt(prompt){
    const target=client,session=sessionId,version=generation;if(!target||!session)throw new Error('Select a conversation before queuing a prompt.');
    const id='inbox_'+crypto.randomBytes(16).toString('hex'),item=await target.enqueueSessionInput(session,prompt,id);
    if(target!==client||session!==sessionId||version!==generation||!panel)return;
    if(!item||item.id!==id||item.session_id!==session||item.content!==prompt||!['queued','claimed'].includes(item.state))throw new Error('Native inbox acknowledgement changed.');
    await refreshInbox(target,session,version);if(target!==client||session!==sessionId||version!==generation||!panel)return;
    post({type:'queued-input',item});if(!busySession())await drainInbox();
  }
  async function cancelQueuedPrompt(id){
    const target=client,session=sessionId,version=generation;if(!target||!session||typeof id!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(id))throw new Error('Select a queued prompt in this conversation.');
    const items=await refreshInbox(target,session,version);if(!items.some(item=>item.id===id&&item.state==='queued'))throw new Error('This prompt is no longer queued.');
    await target.cancelSessionInput(session,id);if(target!==client||session!==sessionId||version!==generation||!panel)return;
    await refreshInbox(target,session,version);post({type:'queued-input-cancelled',id});
  }
  async function drainInbox(){
    if(inboxDispatching||!panel||!client||!sessionId||busySession()||health?.agent_execution!==true)return;
    inboxDispatching=true;const target=client,session=sessionId,version=generation;
    try{
      const items=await refreshInbox(target,session,version);if(target!==client||session!==sessionId||version!==generation||busySession())return;
      const item=items.find(value=>value.state==='queued');if(!item)return;
      const binding=health.provider_profile_admission===true?await profileController.admission():undefined;
      if(target!==client||session!==sessionId||version!==generation||busySession())return;
      const run=await target.run(session,item.content,selectedModel,binding,item.id);if(target!==client||session!==sessionId||version!==generation||busySession())return;
      stop();clearGraph();runId=run.id;cursor=0;sessionRuns=[...sessionRuns.filter(value=>value.id!==run.id),run];presentRuns();
      await context.workspaceState.update(runStateKey,{...connectionState(),session_id:session,id:run.id});
      if(!panel||target!==client||session!==sessionId)return;
      reviewed.clear();post({type:'operations',operations:[]});post({type:'user',text:item.content});post({type:'queued-started',item,run});
      await refreshInbox(target,session,generation);watch();
    }catch(error){if(target===client&&session===sessionId&&panel){if(error.status===409)await refreshInbox(target,session,generation).catch(()=>{});else post({type:'error',text:`Queued prompt is still saved. ${error.message}`});}}
    finally{inboxDispatching=false;}
  }
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
    graphSnapshot=root;graphChildren=owned;
    post({type:'graph',record:root,children,histories:Object.fromEntries([...childHistory].filter(([child])=>owned.has(child)).map(([child,value])=>[child,value.history]))});
    for(const event of events){if(event.seq<=cursor)continue;post({type:event.run_id===id?'event':'graph-event',event,node_id:owned.get(event.run_id)?.node_id});cursor=event.seq;}
    publishOperations(operations);post({type:'status',text:root.run.state});
    sessionRuns=await client.runs(sessionId);if(version!==generation || !panel)return;presentRuns();
    if(['completed','failed','cancelled'].includes(root.run.state)){
      const finalEvents=await client.graphEvents(id,cursor);if(version!==generation || !panel)return;
      for(const event of finalEvents){if(event.seq<=cursor)continue;if(event.run_id!==id&&!owned.has(event.run_id))throw new Error('Final event does not belong to the observed graph.');post({type:event.run_id===id?'event':'graph-event',event,node_id:owned.get(event.run_id)?.node_id});cursor=event.seq;}
      const history=await historyPager.refresh();if(version!==generation || !panel)return;post({type:'transcript',...history});
      await refreshGitChangesSnapshot().catch(()=>{});
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
        const history = await historyPager.refresh();
        if (version !== generation) return;
        post({ type:'transcript',...history,preserveLive:true });
      }
      const run = await client.status(id);
      if (version !== generation) return;
      const runs=await client.runs(sessionId);
      if(version!==generation) return;
      sessionRuns=runs;presentRuns();
      post({ type: 'status', text: run.state });
      const operations = await client.operations(id);
      if (version !== generation) return;
      publishOperations(operations);
      if (['completed', 'cancelled', 'failed'].includes(run.state)) {
        // A terminal transition can occur between the event query and status query.
        const finalEvents = await client.events(id, cursor);
        if (version !== generation) return;
        for (const event of finalEvents) { if(event.seq<=cursor)continue;post({ type: 'event', event }); cursor = event.seq; }
        const history = await historyPager.refresh();
        if (version !== generation) return;
        post({ type: 'transcript', ...history });
        await refreshGitChangesSnapshot().catch(()=>{});

      }
    } catch (error) { if (version === generation) post({ type: 'error', text: error.message }); }
    finally { if(version===generation&&panel){if(!busySession()){await readSkills();await drainInbox();}await readContext().catch(error=>{if(version===generation)post({type:'error',text:error.message});});watch();} }
  }

  async function pollOwned(id,version){
    let run=await client.status(id);if(version!==generation||!panel)return;
    const validRoot=value=>{if(value.id!==id||value.session_id!==sessionId||value.parent_id||value.graph_root)throw new Error('Owned run observation identity changed');};validRoot(run);
    const publish=snapshot=>{
      graphChildren=new Map(snapshot.children.map(child=>[child.run.id,child.run]));
      post({type:'owned-children',parent:run,children:snapshot.children,histories:snapshot.histories});
      for(const event of snapshot.events){if(event.seq<=cursor)continue;post({type:event.run_id===id?'event':'owned-event',event,child_id:event.run_id});cursor=event.seq;}
      publishOperations(snapshot.operations);
    };
    let snapshot=await observeOwnedRun(client,run,sessionId,cursor);if(version!==generation||!panel)return;publish(snapshot);
    run=await client.status(id);if(version!==generation||!panel)return;
    validRoot(run);
    if(['completed','failed','cancelled'].includes(run.state)){snapshot=await observeOwnedRun(client,run,sessionId,cursor);if(version!==generation||!panel)return;publish(snapshot);await refreshGitChangesSnapshot().catch(()=>{});}
    await readPlan(id,version);if(version!==generation||!panel)return;
    const history=await historyPager.refresh(),runs=await client.runs(sessionId);if(version!==generation||!panel)return;
    sessionRuns=runs;presentRuns();post({type:'transcript',...history,preserveLive:['queued','running','paused'].includes(run.state)});post({type:'status',text:run.state});
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
  function planOwner(record,id){if(record?.plan?.id===id)return record;return record?.nested_plans?.find(owner=>owner.plan.id===id);}
  function readyPlan(record){const owners=[record,...(record?.nested_plans||[])];return record?.enabled&&record.run.state==='paused'&&record.plan&&!record.plan.halted&&owners.some(owner=>!owner.plan.halted&&(owner.plan.ready.length>0||owner.plan.report_ready))&&owners.some(owner=>owner.calls.some(call=>call.pending))&&owners.some(owner=>owner.questions.some(question=>question.state==='answered'));}
  async function controlPlan(message){
    const version=generation,target=client,view=panel,root=runId,session=sessionId,observed=planSnapshot;
    const current=()=>panel===view&&version===generation&&client===target&&runId===root&&sessionId===session&&configuredOrigin()===target.baseUrl;
    const answer=message.type==='plan-input',observedOwner=answer?planOwner(observed,message.plan_id):observed;
    if(!vscode.workspace.isTrusted||!observed?.plan||!observed.enabled||message.root!==root||observed.run.id!==root||observed.run.session_id!==session||!observedOwner||message.expected_revision!==observedOwner.plan.revision||message.expected_state_sequence!==observedOwner.plan.state_sequence)throw new Error('Refresh the selected plan before providing input or resuming.');
    let question;
    if(answer){question=observedOwner.questions.find(value=>value.id===message.request);if(!['paused','running'].includes(observed.run.state)||!question||question.state!=='waiting'||question.backend_node_id!==message.backend_node_id||question.definition_revision!==message.definition_revision||question.published_event_seq!==message.published_event_seq||Date.now()>=question.expires_unix_ms)throw new Error('Refresh the current owned human question.');validatePlanInputText(message.input_json);}
    else if(!readyPlan(observed))throw new Error('The selected owner has no ready paused plan.');
    let fresh;try{fresh=validatePlanObservation(await target.plan(root),root);}catch(error){if(error.status===409&&current()){planSnapshot=undefined;post({type:'plan-clear'});}throw error;}if(!current())return;
    if(fresh.run.session_id!==session)throw new Error('Plan conversation identity changed');planSnapshot=JSON.parse(JSON.stringify(fresh));post({type:'plan',record:planSnapshot});
    const freshOwner=answer?planOwner(fresh,message.plan_id):fresh;if(!fresh.enabled||!freshOwner||freshOwner.plan.revision!==message.expected_revision||freshOwner.plan.state_sequence!==message.expected_state_sequence)throw new Error('Plan changed. Review the refreshed plan before submitting again.');
    if(answer){const actual=freshOwner.questions.find(value=>value.id===question.id);for(const field of ['id','plan_id','root_run_id','backend_node_id','definition_revision','published_event_seq','question','state','expires_unix_ms'])if(!actual||actual[field]!==question[field])throw new Error('Human question changed. Review the refreshed plan.');if(!['paused','running'].includes(fresh.run.state)||Date.now()>=actual.expires_unix_ms)throw new Error('Human question is no longer awaiting input.');}
    else if(!readyPlan(fresh))throw new Error('The selected owner is no longer ready to resume.');
    const result=answer?await target.planInput(root,question.id,message.input_json,message.expected_revision,message.expected_state_sequence):await target.resumePlan(root,message.expected_revision,message.expected_state_sequence);
    if(!current())return;if(result.id!==root||result.session_id!==session||result.parent_id||result.graph_root)throw new Error('Plan controller result identity changed');await poll();
    watch();
  }

  async function persistSessionModel(model) {
    if(!sessionId||health?.session_model_selection!==true||typeof client.sessionModel!=='function'||!profileController)return;
    const binding=await profileController.admission();if(!binding?.provider_profile_id)return;
    const observed=sessionModelState||await client.sessionModel(sessionId);
    const saved=await client.replaceSessionModel(sessionId,model,binding,observed);
    if(saved.session_id!==sessionId||saved.revision!==observed.revision+1||saved.selected?.model_id!==model||saved.selected.provider_profile_id!==binding.provider_profile_id)throw new Error('Backend conversation model acknowledgement changed.');
    sessionModelState=saved;
  }
  async function selectSession(id) {
    stop();
    const version = generation;
    clearGraph();
    sessionId = id;
    sessionPermissionRequests.clear();
    sessionPermissionsLoaded=false;
    sessionModelState=undefined;
    runId = undefined;
    sessionRuns=[];presentRuns();
    reviewed.clear();
    post({ type: 'operations', operations: [] });
    cursor = 0;
    await context.workspaceState.update(stateKey, { ...connectionState(), id });
    if (version !== generation || !panel) return;
    const history = await historyPager.select(id);
    if (version !== generation || !panel) return;
    post({ type: 'history', ...history });
    await refreshInbox(client,id,version);
    if(version!==generation||!panel)return;
    const runs = await client.runs(id);
    if (version !== generation || !panel) return;
    await refreshSessionPermissions(client,id,version);schedulePermissionRefresh(version);
    const previousProfile=profileController?.state;await profileController?.refresh();
    if(version!==generation||!panel)return;
    if(previousProfile&&(previousProfile.revision!==profileController.state?.revision||previousProfile.active!==profileController.state?.active)){
      const current=await capabilities();if(version!==generation||!panel)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:selectedModel});
    }
    const activeProfile=profileController?.state?.profiles?.find(profile=>profile.id===profileController.state.active);if(activeProfile?.model)selectedModel=activeProfile.model;
    const retainedCatalogue=profileController?.models(selectedModel);
    if(health?.session_model_selection===true&&typeof client.sessionModel==='function')try{const state=await client.sessionModel(id);if(version!==generation||!panel)return;sessionModelState=state;if(state.selected&&activeProfile&&state.selected.provider_profile_id===activeProfile.id&&state.selected.provider_route_id===activeProfile.route_id)selectedModel=state.selected.model_id;else if(activeProfile?.model)selectedModel=activeProfile.model;profileController?.models(selectedModel);}catch(error){if(error.status!==404)throw error;}
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
    } else {presentRuns();post({ type: 'status', text: 'Ready' });await drainInbox();}
    await readSkills();await readAgentSelection();
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
    let sessions = await client.sessions();
    if (version !== generation || !panel) return;
    if(!Array.isArray(sessions)||sessions.some(item=>!item||typeof item.id!=='string'||!item.id))throw new Error('Invalid native session catalogue');
    if(health?.active_session_map===true&&typeof client.activeSessions==='function'){
      let result;try{result=await client.activeSessions();}catch(error){if(error.status!==404)throw error;}
      if(result){const data=result.data;if(!data||typeof data!=='object'||Array.isArray(data)||Object.entries(data).some(([id,value])=>!id||!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==1||value.type!=='running'))throw new Error('Invalid active session map');const active=new Set(Object.keys(data));sessions=sessions.map(item=>({...item,active:active.has(item.id)}));}
    }
    if (version !== generation || !panel) return;
    const signature=JSON.stringify([sessionId,sessions.map(item=>[item.id,item.title,item.active===true])]);if(signature!==sessionCatalogueSignature){sessionCatalogueSignature=signature;post({ type: 'sessions', sessions, selected: sessionId });}
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
    mcpController?.dispose();mcpController=undefined;contextController?.dispose();contextController=undefined;profileController?.dispose();client = new BackendClient(origin, () => context.secrets.get(secretKey(origin)));historyPager=new SessionHistoryPager(client);
    client.bindWorkspace(workspaceBackend);
    await languageServerEditor.connect(client);
    if(!connectionCurrent())throw new Error('Workspace changed while connecting language servers.');
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
    const mcpClient=client,mcpScope={...connectionState()};let mcpRestored=false;
    mcpController=new McpAuthorizationController(mcpClient,post,{current:()=>panel===view&&client===mcpClient&&workspaceBackend.epoch===workspaceEpoch&&configuredOrigin()===mcpClient.baseUrl,requestId:()=>crypto.randomUUID(),save:attempts=>context.workspaceState.update('xmind.mcp.login',{...mcpScope,attempts})});
    const readMcp=()=>{if(mcpRestored)return mcpController.read();mcpRestored=true;const saved=context.workspaceState.get('xmind.mcp.login');return mcpController.read(savedConnection(saved)?saved.attempts:[]);};
    disposeSubscription?.dispose();receiveSubscription?.dispose();
    disposeSubscription=panel.onDidDispose(() => { if (panel === view) { clearTimeout(sessionRefreshTimer);sessionRefreshTimer=undefined;sessionCatalogueSignature=undefined;contextReadyView=undefined;pendingEditorContexts=[];mcpController?.dispose();profileController?.dispose();stop(); reviewed.clear(); providerSelection=undefined; panel = undefined; sidebarView = undefined; } }, null, context.subscriptions);
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
          post({ type: 'capabilities', execution: health.agent_execution, renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,agentSelection:health.agent_selection===true, model:selectedModel, models:modelCatalogue.models });
          await refresh();scheduleSessionRefresh();
          if (sessionId) await selectSession(sessionId);
          await refreshGraphs();
          // Fetch with the saved backend key; settings handles a rejected key.
          try{await configureModel();}catch{}
          await readSkills();
          await readAgentSelection();
        } else if(message.type==='history-older'){
          const target=client,session=sessionId,version=generation;
          if(!historyPager||!session)return;
          const history=await historyPager.older();
          if(panel===view&&client===target&&sessionId===session&&version===generation)post({type:'transcript',...history,preserveOlder:true,preserveLive:true});
        } else if(message.type==='open-terminal'){
          try{await openTerminalView();}catch(error){post({type:'status',text:error.message});vscode.window.showErrorMessage(error.message);}
        } else if(message.type==='git-menu'){
          if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before using Git with xMind.');
          const choice=await vscode.window.showQuickPick([
            {label:'Git status',description:'List changed files in a workspace folder',command:'agentflow.gitStatus'},
            {label:'Review Git diff',description:'Inspect a native workspace patch in VS Code',command:'agentflow.gitDiff'},
            {label:'Initialize Git repository',description:'Create .git metadata in a selected workspace folder',command:'agentflow.initializeGit'}
          ],{title:'xMind: Workspace Git',placeHolder:'Choose a Git operation'});
          if(!choice||panel!==view)return;
          await vscode.commands.executeCommand(choice.command);
        } else if(message.type==='mcp-refresh'){
          await readMcp();
        } else if(message.type==='mcp-resources-refresh'){
          const resourceClient=client,resourceGeneration=generation,resourceEpoch=workspaceBackend.epoch;
          const current=()=>panel===view&&client===resourceClient&&generation===resourceGeneration&&workspaceBackend.epoch===resourceEpoch&&configuredOrigin()===resourceClient.baseUrl;
          post({type:'mcp-resources',busy:true});
          try{const catalog=await resourceClient.mcpResources();if(current())post({type:'mcp-resources',busy:false,catalog});}
          catch(error){if(current())post({type:'mcp-resources',busy:false,error:error.message});}
        } else if(['connections-refresh','connection-save','connection-remove'].includes(message.type)){
          const connectionClient=client,connectionGeneration=generation,connectionEpoch=workspaceBackend.epoch;
          const current=()=>panel===view&&client===connectionClient&&generation===connectionGeneration&&workspaceBackend.epoch===connectionEpoch&&configuredOrigin()===connectionClient.baseUrl;
          const report=(state,completed)=>{if(current())post({type:'connections-state',state,...(completed?{completed}:{})});};
          try{
            if(message.type!=='connections-refresh'&&current())post({type:'connections-state',busy:true,text:message.type==='connection-save'?'Saving Nexus address…':'Removing connection…'});
            if(message.type==='connection-save')await connectionClient.saveConnectionProfile(message.id,message.name,message.origin);
            else if(message.type==='connection-remove')await connectionClient.removeConnectionProfile(message.id);
            report(await connectionClient.connectionProfiles(),message.type==='connections-refresh'?undefined:message.type);
          }catch(error){if(current())post({type:'connections-state',error:error.message});}
        } else if(['mcp-start','mcp-renew','mcp-cancel','mcp-open'].includes(message.type)){
          try{if(!mcpRestored)await readMcp();if(message.type==='mcp-start')await mcpController.start(message.server);else if(message.type==='mcp-renew')await mcpController.start(message.server,true);else if(message.type==='mcp-cancel')await mcpController.cancel(message.server);else await mcpController.open(message.server,url=>vscode.env.openExternal(vscode.Uri.parse(url)));}
          catch(error){post({type:'mcp-error',text:error.message});}
        } else if(message.type==='permission-grants-load'||message.type==='permission-grant-revoke'){
          try{
            const selected=sessionId,version=generation,target=client;if(!selected||message.session!==selected)throw new Error('Select the conversation whose saved permissions you want to inspect.');
            let snapshot=await target.sessionPermissionGrants(selected);if(panel!==view||target!==client||version!==generation||selected!==sessionId)return;
            if(message.type==='permission-grant-revoke'){
              if(typeof message.id!=='string'||!Number.isSafeInteger(message.revision)||message.revision!==snapshot.revision||!snapshot.grants.some(item=>item.id===message.id))throw new Error('Saved permissions changed. Refresh the conversation permissions and try again.');
              snapshot=await target.revokeSessionPermissionGrant(selected,message.id,snapshot.revision);if(panel!==view||target!==client||version!==generation||selected!==sessionId)return;
            }
            post({type:'permission-grants',snapshot:{...snapshot,session_id:selected}});
          }catch(error){if(panel===view)post({type:'permission-grants-error',text:error.message});}
        } else if(message.type==='skills-refresh'){
          if(!sessionId){const session=await client.createSession('Workspace skills');if(panel!==view)return;await selectSession(session.id);await refresh();}else await readSkills();
        } else if(message.type==='skills-change'){
          try{if(health?.skill_controls!==true||busySession()||!skillController)throw new Error('Wait for the selected conversation to become idle.');await skillController.change(message);}catch(error){post({type:'skills-error',text:error.message});}
        } else if(message.type==='agent-select'){
          try{if(health?.agent_selection!==true||busySession()||!agentController)throw new Error('Wait for this conversation to become idle before changing its agent.');await agentController.change(message);}catch(error){post({type:'agent-selection-error',text:error.message});}
        } else if (message.type === 'saveProviderKey') {
          let key=message.key;delete message.key;
          try{await configureModel(key===''?undefined:key,message.profile,message.route);}finally{key=undefined;}
        } else if (message.type === 'discardProviderKey') {
          providerSelection=undefined;profileController?.invalidate();
          const version=generation;await profileController?.refresh();if(panel!==view||version!==generation)return;
          const current=await capabilities();if(panel!==view||version!==generation)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue,selectedModel);post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:selectedModel});profileController?.models(selectedModel);
        } else if(message.type==='select-provider'){
          const version=generation;if(!await profileController.select(message.id)||panel!==view||version!==generation)return;
          const current=await capabilities();if(panel!==view||version!==generation||configuredOrigin()!==client.baseUrl)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);
          post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:selectedModel});
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
          post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:selectedModel});
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
            if(panel===view&&modelVersion===generation&&configuredOrigin()===client.baseUrl){const current=await capabilities();if(panel!==view||modelVersion!==generation||configuredOrigin()!==client.baseUrl)return;health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:selectedModel});}
            throw error;
          }
          if(saved){
            if(panel!==view||modelVersion!==generation||configuredOrigin()!==client.baseUrl)return;
            const current=await capabilities();health=current.health;modelCatalogue=current.catalogue;
            if(panel!==view||modelVersion!==generation||configuredOrigin()!==client.baseUrl)return;
            post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:message.id});profileController.models(message.id);
          }else if(providerSelection){
            const selection=providerSelection;
            if(selection.target!==client || selection.origin!==configuredOrigin() || Date.now()>selection.expires){providerSelection=undefined;throw new Error('Model discovery expired. Fetch models again in Settings.');}
            if(!selection.ids.includes(message.id))throw new Error('Choose a model returned by OpenAI.');
            const configured=await client.configureProvider(message.id,selection.key,selection.revision);
            post({type:'provider-wire',wire:providerEnrollmentWire(configured)});
            selection.key=undefined;selection.revision=configured.revision;selection.expires=Infinity;
            const current=await capabilities();health=current.health;modelCatalogue=current.catalogue;
            post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:selection.ids.map(id=>({id})),model:message.id});
            post({type:'status',text:'Model configured · send a message to verify provider access'});
          }else if (!modelCatalogue.models.some(model => model.id === message.id)) throw new Error('Fetch models in Settings before choosing this model.');
          selectedModel = message.id;
          await persistSessionModel(selectedModel);
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
        } else if(message.type==='delete-session'){
          const version=generation,target=client,deletedId=sessionId;
          try{
            if(!deletedId||message.id!==deletedId)throw new Error('Select the conversation you want to remove.');
            if(busySession())throw new Error('Wait for this conversation to finish before removing it.');
            const sessions=await target.sessions();if(panel!==view||client!==target||version!==generation)return;
            const selected=sessions.find(item=>item.id===deletedId);if(!selected)throw new Error('The selected conversation is no longer available. Refresh the session list.');
            const answer=await vscode.window.showWarningMessage(`Remove “${selected.title}” from the conversation list? Immutable context and audit records remain stored. Active work prevents removal.`,{modal:true},'Remove conversation');
            if(panel!==view||client!==target||version!==generation)return;
            if(answer!=='Remove conversation'){post({type:'session-delete-result',id:deletedId,cancelled:true});return;}
            await target.deleteSession(deletedId);if(panel!==view||client!==target||version!==generation)return;
            const remaining=await target.sessions();if(panel!==view||client!==target||version!==generation)return;
            if(remaining.length){await selectSession(remaining.at(-1).id);if(panel!==view||client!==target)return;await refresh();}
            else{
              stop();sessionId=undefined;runId=undefined;sessionRuns=[];sessionModelState=undefined;historyPager.clear();reviewed.clear();sessionPermissionRequests.clear();sessionPermissionsLoaded=false;clearGraph();contextController?.invalidate();presentRuns();
              await context.workspaceState.update(stateKey,{...connectionState()});post({type:'history',...historyPager.snapshot()});post({type:'inbox',items:[]});post({type:'operations',operations:[]});await refresh();if(panel!==view||client!==target)return;post({type:'status',text:'Ready · conversation removed'});
            }
            post({type:'session-delete-result',id:deletedId,success:true});
          }catch(error){if(panel===view&&client===target&&version===generation)post({type:'session-delete-result',id:deletedId,success:false,text:error.message});}
        } else if(message.type==='rename-session'){
          const version=generation;
          try{if(health.session_rename!==true||message.id!==sessionId||typeof message.title!=='string'||typeof message.expected_title!=='string')throw new Error('Select a conversation on a backend supporting rename.');await client.renameSession(sessionId,message.title,message.expected_title);if(panel!==view||version!==generation)return;await refresh();if(panel===view&&version===generation)post({type:'rename-result',id:message.id,success:true});}
          catch(error){if(panel===view&&version===generation)post({type:'rename-result',id:message.id,success:false,text:error.status===409?'Conversation title changed. Refresh history and reopen Rename before saving.':error.message});}
        } else if(message.type==='fork-session'){
          const version=generation,target=client;
          try{
            if(health?.session_fork!==true||message.id!==sessionId||typeof message.before!=='string'||!/^msg_[1-9][0-9]*$/.test(message.before))throw new Error('Select a saved message in a conversation on a backend supporting fork.');
            if(busySession())throw new Error('Wait for this conversation run to finish before forking it.');
            const fork=await target.forkSession(sessionId,message.before);if(panel!==view||client!==target||version!==generation)return;
            if(fork.parent_id!==message.id||fork.fork.message_id!==message.before)throw new Error('Native fork receipt does not match the selected source message.');
            await selectSession(fork.id);if(panel!==view||client!==target||sessionId!==fork.id)return;await refresh();if(panel===view&&client===target&&sessionId===fork.id)post({type:'fork-result',id:message.id,before:message.before,success:true,session_id:fork.id});
          }catch(error){if(panel===view&&version===generation)post({type:'fork-result',id:message.id,before:message.before,success:false,text:error.status===409?'This conversation changed or has an active run. Refresh it before forking.':error.message});}
        } else if (message.type === 'select-run' && typeof message.id === 'string') {
          await selectRun(message.id);
        } else if(message.type==='cancel-inbox'){
          await cancelQueuedPrompt(message.id);
        } else if (message.type === 'send' && typeof message.prompt === 'string' && message.prompt.trim()) {
          if(sessionId&&busySession()){
            if(message.prompt.startsWith('/'))throw new Error('Workspace commands cannot be queued yet. Wait for the active run to finish, then send the command.');
            const selected=sessionRuns.find(run=>run.id===runId);
            if(selected&&!['queued','running','paused'].includes(selected.state))throw new Error('Select the active run before queuing another prompt.');
            await queuePrompt(message.prompt);return;
          }
          const graph=selectedGraph?graphCatalogue.find(g=>g.id===selectedGraph&&g.executable):undefined;
          if(selectedGraph&&!graph)throw new Error('Graph is not executable on this backend.');
          if (!graph&&!health.agent_execution) throw new Error('Configure a model on xMind Server before submitting an agent run.');
          const commandGeneration=generation,submissionClient=client;
          const slashCommand=message.prompt.startsWith('/')?parseWorkspaceCommand(message.prompt,await client.commands()):undefined;
          if(panel!==view||generation!==commandGeneration||client!==submissionClient||configuredOrigin()!==client.baseUrl)return;
          const admissionVersion=generation;
          const binding=(graph?health.graph_provider_profile_admission:health.provider_profile_admission)===true?await profileController.admission():undefined;
          if(panel!==view||admissionVersion!==generation||configuredOrigin()!==client.baseUrl)return;
          if (!sessionId) {
            const session = await submissionClient.createSession(message.prompt.slice(0, 80));
            if (panel !== view||generation!==admissionVersion||client!==submissionClient) return;
            await selectSession(session.id);
            await refresh();
            if(panel!==view||client!==submissionClient)return;
          }
          if (panel !== view) return;
          const admissionClient=client,runVersion=generation;let run;
          try{run=slashCommand?await admissionClient.runCommand(sessionId,slashCommand.id,slashCommand.arguments,selectedModel,binding,graph):graph?await admissionClient.graphRun(sessionId,graph.id,graph.revision,message.prompt,selectedModel,binding):await admissionClient.run(sessionId,message.prompt,selectedModel,binding);}
          catch(error){
            if(panel!==view||generation!==runVersion||client!==admissionClient)return;
            const changed=await profileController.reconcileAdmission(binding,error).catch(()=>false);
            if(panel!==view||generation!==runVersion||client!==admissionClient)return;
            if(changed){
              const current=await capabilities();if(panel!==view||generation!==runVersion||client!==admissionClient)return;
              health=current.health;modelCatalogue=current.catalogue;selectedModel=chooseModel(modelCatalogue);
              post({type:'capabilities',execution:health.agent_execution,renameSessions:health.session_rename===true,forkSessions:health.session_fork===true,models:modelCatalogue.models,model:selectedModel});
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
        } else if (message.type === 'process-output' && typeof message.id === 'string') {
          const proposal=reviewed.get(message.id);if(!proposal||!observedOperation(proposal.run_id)||proposal.state!=='succeeded'||proposal.tool!=='run_process'||!['stdout','stderr'].includes(message.channel)||!Number.isSafeInteger(message.after)||message.after<0)throw new Error('Select a completed command output artifact in the selected run.');
          let result;try{result=JSON.parse(proposal.result_json);}catch{throw new Error('The saved process result is invalid.');}const artifact=result?.output_artifact;
          if(!artifact||artifact.operation_id!==proposal.id||artifact.state!=='complete'||!Number.isSafeInteger(artifact.stdout_bytes)||!Number.isSafeInteger(artifact.stderr_bytes)||artifact.stdout_bytes+artifact.stderr_bytes>64*1024*1024)throw new Error('The saved process output artifact is unavailable.');
          const version=generation,page=await client.processOutput(proposal.id,message.channel,message.after,65536);
          if(panel!==view||version!==generation||reviewed.get(proposal.id)!==proposal)return;
          if(page.run_id!==proposal.run_id||page.total_bytes!==artifact[message.channel+'_bytes'])throw new Error('The process output artifact changed.');
          post({type:'process-output-page',id:proposal.id,channel:message.channel,page});
        } else if (message.type === 'reconcile-process' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before reconciling a process effect.');
          const proposal=reviewed.get(message.id);
          if(!proposal||!observedOperation(proposal.run_id)||proposal.state!=='uncertain'||proposal.tool!=='run_process'||typeof message.note!=='string')throw new Error('Select a recorded uncertain process in the selected run.');
          const version=generation,result=await client.reconcileProcess(proposal.id,message.note);
          if(panel!==view||version!==generation||reviewed.get(proposal.id)!==proposal)return;
          if(!result||result.id!==proposal.id||result.run_id!==proposal.run_id||result.state!=='failed'||JSON.parse(result.result_json)?.reconciliation?.acknowledges_possible_side_effects!==true)throw new Error('The native host did not confirm process reconciliation.');
          await poll();
        } else if (message.type === 'inspect-edit' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before inspecting an edit.');
          const proposal=reviewed.get(message.id);
          if(!proposal || !observedOperation(proposal.run_id) || proposal.state!=='uncertain' || !['replace_file','patch_file'].includes(proposal.tool)) throw new Error('Select a recorded uncertain file change before inspecting.');
          const version=generation;
          const inspection=await client.inspectEdit(proposal.id);
          if(panel!==view || version!==generation) return;
          validateEditInspection(proposal,inspection);
          post({type:'edit-inspection',id:proposal.id,inspection});
        } else if(message.type==='restore-operation'&&typeof message.id==='string') {
          const proposal=reviewed.get(message.id);
          if(!proposal||!observedOperation(proposal.run_id))throw new Error('Select a successful file change in the selected conversation.');
          const version=generation,target=client,current=await target.operation(proposal.id);
          if(panel!==view||version!==generation||client!==target)return;
          for(const field of ['id','run_id','workspace_id','tool','arguments_json','result_json','expires_unix_ms','state'])if(current[field]!==proposal[field])throw new Error('Operation changed. Refresh it before restoring.');
          if(!canRestoreFileOperation(current))throw new Error('Only a successful saved file change in this conversation can be restored.');
          const run=await target.restoreOperation(current.id,sessionId);
          if(panel!==view||version!==generation||client!==target)return;
          stop();clearGraph();runId=run.id;cursor=0;sessionRuns=[...sessionRuns.filter(value=>value.id!==run.id),run];presentRuns();
          await context.workspaceState.update(runStateKey,{...connectionState(),session_id:sessionId,id:run.id});
          if(panel!==view||client!==target)return;
          reviewed.clear();post({type:'operations',operations:[]});post({type:'status',text:'Restore started · review the proposed inverse in Operations'});watch();await poll();
        } else if (message.type === 'inspect-a2a-task' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before contacting a registered A2A peer.');
          const proposal=reviewed.get(message.id);
          if(!proposal || !observedOperation(proposal.run_id) || !['succeeded','uncertain'].includes(proposal.state) || proposal.tool!=='a2a_delegate') throw new Error('Select an acknowledged A2A task or interrupted send in the selected run before refreshing it.');
          let receipt;try{receipt=JSON.parse(proposal.result_json);}catch{throw new Error('The saved A2A task receipt is invalid.');}
          const acknowledged=proposal.state==='succeeded'&&receipt?.acknowledged_by_peer===true,observedBeforeFailure=proposal.state==='uncertain'&&receipt?.reason==='a2a_dispatch_outcome_unknown';
          if((!acknowledged&&!observedBeforeFailure)||typeof receipt.agent_id!=='string'||typeof receipt.task_id!=='string'||!receipt.task_id)throw new Error('This A2A operation has no inspectable remote task ID.');
          const version=generation,inspection=await client.inspectA2aTask(proposal.id,message.includeArtifacts===true);
          if(panel!==view || version!==generation) return;
          if(!inspection||inspection.operation_id!==proposal.id||inspection.agent_id!==receipt.agent_id||inspection.task_id!==receipt.task_id||!inspection.task||typeof inspection.task!=='object'||Array.isArray(inspection.task)||JSON.stringify(inspection.task).length>524288||!['not_attempted','outcome_unknown','acknowledged'].includes(inspection.cancel_state)||!Number.isSafeInteger(inspection.inspected_unix_ms)||inspection.inspected_unix_ms<=0||inspection.inspected_unix_ms>8640000000000000)throw new Error('The remote task snapshot does not match the saved A2A operation.');
          post({type:'a2a-task-inspection',id:proposal.id,inspection});
        } else if (message.type === 'resubscribe-a2a-task' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before reconnecting to a registered A2A peer.');
          const proposal=reviewed.get(message.id);
          if(!proposal || !observedOperation(proposal.run_id) || !['succeeded','uncertain'].includes(proposal.state) || proposal.tool!=='a2a_delegate') throw new Error('Select an acknowledged A2A task or interrupted send in the selected run before reconnecting.');
          let receipt;try{receipt=JSON.parse(proposal.result_json);}catch{throw new Error('The saved A2A task receipt is invalid.');}
          const acknowledged=proposal.state==='succeeded'&&receipt?.acknowledged_by_peer===true,observedBeforeFailure=proposal.state==='uncertain'&&receipt?.reason==='a2a_dispatch_outcome_unknown';
          if((!acknowledged&&!observedBeforeFailure)||typeof receipt.agent_id!=='string'||typeof receipt.task_id!=='string'||!receipt.task_id)throw new Error('This A2A operation has no resumable remote task ID.');
          const version=generation,subscription=await client.resubscribeA2aTask(proposal.id);
          if(panel!==view || version!==generation) return;
          if(!subscription||subscription.operation_id!==proposal.id||subscription.agent_id!==receipt.agent_id||subscription.task_id!==receipt.task_id||typeof subscription.subscription_id!=='string'||!subscription.subscription_id||subscription.subscription_state!=='closed'||!Array.isArray(subscription.events)||subscription.events.length>64||JSON.stringify(subscription.events).length>524288)throw new Error('The resubscribed events do not match the saved A2A operation.');
          post({type:'a2a-task-resubscription',id:proposal.id,subscription});
        } else if (message.type === 'cancel-a2a-task' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before cancelling a registered A2A task.');
          const proposal=reviewed.get(message.id);
          if(!proposal||!observedOperation(proposal.run_id)||proposal.state!=='succeeded'||proposal.tool!=='a2a_delegate')throw new Error('Select an acknowledged A2A task in the selected run before cancelling it.');
          let receipt;try{receipt=JSON.parse(proposal.result_json);}catch{throw new Error('The saved A2A task receipt is invalid.');}
          if(receipt?.acknowledged_by_peer!==true||typeof receipt.agent_id!=='string'||typeof receipt.task_id!=='string'||!receipt.task_id)throw new Error('This A2A operation has no cancellable remote task ID.');
          const choice=await vscode.window.showWarningMessage('Send a cancellation request to remote agent '+receipt.agent_id+' for task '+receipt.task_id+'? If its outcome is uncertain, xMind will not replay it.',{modal:true},'Cancel remote task');
          if(choice!=='Cancel remote task'||panel!==view)return;
          const version=generation,cancellation=await client.cancelA2aTask(proposal.id);
          if(panel!==view||version!==generation)return;
          if(!cancellation||cancellation.operation_id!==proposal.id||cancellation.agent_id!==receipt.agent_id||cancellation.task_id!==receipt.task_id||cancellation.cancel_state!=='acknowledged'||!cancellation.task||typeof cancellation.task!=='object'||Array.isArray(cancellation.task)||cancellation.task.id!==receipt.task_id||!Number.isSafeInteger(cancellation.requested_unix_ms)||cancellation.requested_unix_ms<=0||cancellation.requested_unix_ms>8640000000000000)throw new Error('The remote cancellation response does not match the saved A2A operation.');
          post({type:'a2a-task-cancellation',id:proposal.id,cancellation});
        } else if (message.type === 'follow-up-a2a-task' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before replying to a registered A2A peer.');
          const proposal=reviewed.get(message.id);
          if(!proposal||!observedOperation(proposal.run_id)||proposal.state!=='succeeded'||proposal.tool!=='a2a_delegate')throw new Error('Select an acknowledged A2A task in the selected run before replying.');
          let receipt;try{receipt=JSON.parse(proposal.result_json);}catch{throw new Error('The saved A2A task receipt is invalid.');}
          if(receipt?.acknowledged_by_peer!==true||typeof receipt.agent_id!=='string'||typeof receipt.task_id!=='string'||!receipt.task_id)throw new Error('This A2A operation has no acknowledged remote task.');
          const sequence=message.sequence;if(!Number.isSafeInteger(sequence)||sequence<1||sequence>64||typeof message.message!=='string'||!message.message.trim()||message.message.length>32768)throw new Error('Invalid A2A follow-up message.');
          const version=generation,followUp=await client.followUpA2aTask(proposal.id,sequence,message.message);
          if(panel!==view||version!==generation)return;
          if(!followUp||followUp.operation_id!==proposal.id||followUp.agent_id!==receipt.agent_id||followUp.task_id!==receipt.task_id||followUp.sequence!==sequence||followUp.follow_up_state!=='acknowledged'||!followUp.task||typeof followUp.task!=='object'||Array.isArray(followUp.task)||followUp.task.id!==receipt.task_id||!Number.isSafeInteger(followUp.requested_unix_ms)||followUp.requested_unix_ms<=0||followUp.requested_unix_ms>8640000000000000)throw new Error('The remote follow-up acknowledgement does not match the saved A2A operation.');
          post({type:'a2a-task-follow-up',id:proposal.id,followUp});
          const inspection=await client.inspectA2aTask(proposal.id);
          if(panel!==view||version!==generation)return;
          if(!inspection||inspection.operation_id!==proposal.id||inspection.agent_id!==receipt.agent_id||inspection.task_id!==receipt.task_id||!inspection.task||typeof inspection.task!=='object'||Array.isArray(inspection.task)||!Number.isSafeInteger(inspection.follow_up_sequence)||inspection.follow_up_sequence<0||inspection.follow_up_sequence>64||typeof inspection.follow_up_state!=='string'||!Number.isSafeInteger(inspection.inspected_unix_ms))throw new Error('The remote task refresh did not match the saved A2A operation.');
          post({type:'a2a-task-inspection',id:proposal.id,inspection});
        } else if (['decide','review'].includes(message.type) && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before deciding an operation.');
          if (message.type === 'decide' && message.decision !== 'allow' && message.decision !== 'deny') throw new Error('Decision must be allow or deny.');
          const proposal = reviewed.get(message.id),sessionApproval=sessionPermissionRequests.has(message.id);
          if (!proposal || (!observedOperation(proposal.run_id)&&!(sessionApproval&&['decide','review'].includes(message.type))) || proposal.state !== 'awaiting_approval') throw new Error('Inspect a pending operation in the selected conversation before deciding.');
          const version = generation;
          const current = await client.operation(message.id);
          if (panel !== view || version !== generation) return;
          if (current.id !== proposal.id || current.state !== 'awaiting_approval' || current.run_id !== proposal.run_id || current.workspace_id !== proposal.workspace_id || current.tool !== proposal.tool || current.arguments_json !== proposal.arguments_json || current.expires_unix_ms !== proposal.expires_unix_ms) {
            await poll();
            throw new Error('The operation changed since review. Inspect its current state before deciding.');
          }
          if (message.type === 'review') {await showEditReview(current);return;}
          if(message.decision==='allow'&&current.tool==='patch_file')patchReview(current);
          const sessionScopedApproval=sessionApproval&&!observedOperation(proposal.run_id),grantResource=message.grantResource===true;
          if(message.grantResource!==undefined&&typeof message.grantResource!=='boolean')throw new Error('Invalid saved permission choice.');
          let decided;if(grantResource)decided=await client.replySessionPermission(sessionId,message.id,message.decision,true);else if(sessionScopedApproval)decided=await client.replySessionPermission(sessionId,message.id,message.decision);else decided=await client.decide(message.id, message.decision);
          if (panel !== view || version !== generation) return;
          reviewed.set(decided.id, decided);
          if(sessionApproval){sessionPermissionRequests.delete(decided.id);publishOperations();await refreshSessionPermissions(client,sessionId,version);}else publishOperations();
          await poll();
        }
      } catch (error) { if (panel === view&&workspaceBackend.epoch===workspaceEpoch) post(message?.type==='process-output'?{type:'process-output-error',id:message.id,channel:message.channel,text:error.message}:{type:'error',text:error.message}); }
        finally{if(panel===view&&workspaceBackend.epoch===workspaceEpoch)await readContext().catch(error=>{if(workspaceBackend.epoch===workspaceEpoch)post({type:'error',text:error.message});});} });
    }, null, context.subscriptions);
  }
  function open() {
    if (!opening) opening = openPanel().finally(() => { opening = undefined; });
    return opening;
  }

  async function openTerminalView(){
    if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before opening a coding terminal.');
    await open();
    const target=client,view=panel,workspaceEpoch=workspaceBackend.epoch;
    if(!target||!view)throw new Error('Wait for xMind to load this workspace before opening a terminal.');
    if(!sessionId){
      const created=await target.createSession('Workspace terminal');
      if(client!==target||panel!==view||workspaceBackend.epoch!==workspaceEpoch)throw new Error('Workspace changed while opening a terminal.');
      await refresh();await selectSession(created.id);
    }
    const selectedSession=sessionId,origin=target.baseUrl;
    const bridge=new XMindPseudoterminal(vscode,target,selectedSession,{
      title:'xMind · '+(workspaceBackend.active?.metadata?.root?.split(/[\\/]/).filter(Boolean).pop()||'Workspace'),
      current:()=>client===target&&panel===view&&workspaceBackend.epoch===workspaceEpoch&&workspaceBackend.active?.origin===origin,
      onDispose:value=>terminalBridges.delete(value)
    });
    terminalBridges.add(bridge);
    try{
      const terminal=vscode.window.createTerminal({name:'xMind Terminal',pty:bridge});
      terminal.show();
    }catch(error){bridge.dispose();throw error;}
  }

  context.subscriptions.push(vscode.commands.registerCommand('agentflow.open', async () => {
    try { await open(); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.openTerminal',async()=>{
    try{await openTerminalView();}catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.configureToken', async initialToken => {
    try { await configureToken(initialToken); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.applyRuntimeConfiguration',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before updating its managed xMind backend.');
      if(previewOrigin||machineSetting(vscode,'backendMode')==='external')throw new Error('Apply trusted runtime configuration from the machine that owns the managed local backend.');
      const selection=await workspaceBackend.selectedFolder(false),root=selection?.folder?.uri?.fsPath;
      if(typeof root!=='string'||!root)throw new Error('Open a workspace folder before updating the managed xMind backend.');
      const epoch=workspaceBackend.epoch,signature=workspaceBackend.signature();
      const answer=await vscode.window.showWarningMessage(`Apply the current trusted runtime, process configuration, and workspace edit policy to the local xMind backend for ${root}? The backend will restart while keeping saved sessions and history. File changes still require approval. Active work is not replayed.`,{modal:true},'Apply and reconnect');
      if(answer!=='Apply and reconnect')return;
      if(epoch!==workspaceBackend.epoch||signature!==workspaceBackend.signature())throw new Error('The selected workspace changed before the backend update.');
      workspaceBackend.requestBackendUpgrade();
      await reconnectWorkspace();
      await open();
      vscode.window.showInformationMessage('Trusted xMind runtime configuration applied. Saved sessions and history remain on the backend.');
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.openBrowser',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before connecting a browser view.');
      await open();const origin=configuredOrigin();if(!workspaceBackend.active)throw new Error('Connect the opened workspace before opening its browser.');
      let token=await context.secrets.get(secretKey(origin));
      try{const target=new BackendClient(origin,()=>token);await target.health();if(origin!==configuredOrigin())throw new Error('Backend changed while opening the browser. Try again.');await browserViews.open(origin,token);vscode.window.showInformationMessage('xMind Browser is connected to this workspace and uses its models and history.');}finally{token=undefined;}
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  function disconnectWorkspace(){
    gitChanges.clear('Connect xMind and refresh to load Git changes.');
    for(const terminal of [...terminalBridges])terminal.dispose();terminalBridges.clear();
    languageServerEditor.disconnect();
    contextReadyView=undefined;pendingEditorContexts=[];
    workspaceBackend.invalidate();stop();mcpController?.dispose();mcpController=undefined;contextController?.dispose();contextController=undefined;profileController?.dispose();profileController=undefined;
    providerSelection=undefined;reviewed.clear();clearGraph();sessionId=undefined;runId=undefined;sessionRuns=[];client=undefined;
    modelCatalogue={models:[],default_model:''};selectedModel=undefined;graphCatalogue=[];selectedGraph=undefined;
    clearTimeout(sessionRefreshTimer);sessionRefreshTimer=undefined;sessionCatalogueSignature=undefined;receiveSubscription?.dispose();receiveSubscription=undefined;disposeSubscription?.dispose();disposeSubscription=undefined;
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
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.addWorkspaceFolder',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the opened workspace before attaching a folder to xMind.');
      await open();const target=client,owner=workspaceBackend.active;
      if(!target||!owner)throw new Error('Connect the opened workspace before adding a folder.');
      const ticket=await workspaceBackend.prepare();workspaceBackend.assert(ticket);
      if(ticket.origin!==target.baseUrl||owner.origin!==ticket.origin)throw new Error('The selected xMind workspace changed.');
      const roots=owner.roots||[],primary=owner.canonical.toLowerCase(),candidates=roots.filter(folder=>typeof folder.fsPath==='string'&&folder.fsPath.toLowerCase()!==primary);
      if(!candidates.length){vscode.window.showInformationMessage('Open another folder in this VS Code window, then run xMind: Add Opened Folder to Workspace.');return;}
      const selected=await vscode.window.showQuickPick(candidates.map(folder=>({label:folder.name,description:folder.fsPath,folder})),{title:'xMind: Add Opened Folder to Workspace',placeHolder:'Choose a folder already open in this VS Code window'});
      if(!selected)return;if(client!==target||workspaceBackend.active!==owner)throw new Error('The xMind workspace changed before the folder was attached.');workspaceBackend.assert(ticket);
      const group=await target.addWorkspaceFolder(selected.folder.fsPath);workspaceBackend.assert(ticket);
      if(client!==target||workspaceBackend.active!==owner)throw new Error('The xMind workspace changed while the folder was attached.');
      await languageServerEditor.connect(target);workspaceBackend.assert(ticket);
      vscode.window.showInformationMessage(`Added ${selected.folder.name} to the xMind workspace · ${group.folders.length} folders.`);
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  async function chooseGitFolder(target,owner,ticket,title){
    const group=await target.workspaceMembership();workspaceBackend.assert(ticket);
    if(group.workspace_id!==owner.metadata.workspace_id||group.root.toLowerCase()!==owner.metadata.root.toLowerCase())throw new Error('The native workspace membership changed. Reconnect before using Git.');
    if(group.folders.length===1)return group.folders[0];
    const selected=await vscode.window.showQuickPick(group.folders.map(folder=>({label:folder.name,description:folder.root,folder})),{title,placeHolder:'Choose the registered folder whose Git repository to use'});
    if(!selected)return undefined;workspaceBackend.assert(ticket);return selected.folder;
  }
  async function refreshGitChangesSnapshot(){
    if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before reading Git status.');
    const target=client,view=panel,owner=workspaceBackend.active;
    if(!target||!owner||!view)throw new Error('Connect the opened workspace before refreshing Git Changes.');
    const ticket=await workspaceBackend.prepare();workspaceBackend.assert(ticket);
    if(ticket.origin!==target.baseUrl||owner.origin!==ticket.origin)throw new Error('The selected xMind workspace changed.');
    const group=await target.workspaceMembership();workspaceBackend.assert(ticket);
    if(group.workspace_id!==owner.metadata.workspace_id||group.root.toLowerCase()!==owner.metadata.root.toLowerCase())throw new Error('The native workspace membership changed. Reconnect before refreshing Git Changes.');
    const folders=await Promise.all(group.folders.map(async folder=>({
      name:folder.name,root:folder.root,folder_id:folder.folder_id,ordinal:folder.ordinal,
      changes:await target.vcsStatus(folder.ordinal===0?undefined:folder.folder_id)
    })));
    workspaceBackend.assert(ticket);
    if(panel!==view||client!==target||workspaceBackend.active!==owner)throw new Error('The xMind workspace changed while refreshing Git Changes.');
    gitChanges.setFolders(folders);
    return folders;
  }
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.refreshGitChanges',async()=>{
    try{if(!client)await open();await refreshGitChangesSnapshot();}
    catch(error){gitChanges.clear(`Git status unavailable: ${error.message}`);vscode.window.showErrorMessage(error.message);}
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.initializeGit',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before initializing Git.');
      await open();const target=client,view=panel,owner=workspaceBackend.active;
      if(!target||!owner||!view)throw new Error('Connect the opened workspace before initializing Git.');
      const ticket=await workspaceBackend.prepare();workspaceBackend.assert(ticket);
      if(ticket.origin!==target.baseUrl||owner.origin!==ticket.origin)throw new Error('The selected workspace backend changed.');
      const folder=await chooseGitFolder(target,owner,ticket,'xMind: Choose Git Folder');if(!folder)return;
      const answer=await vscode.window.showWarningMessage(`Initialize a Git repository in ${folder.root}? This creates .git metadata in that folder.`,{modal:true},'Initialize Git');
      if(answer!=='Initialize Git')return;
      if(panel!==view||client!==target)throw new Error('The xMind view changed before Git initialization.');workspaceBackend.assert(ticket);
      await target.initializeGitRepository(folder.ordinal===0?undefined:folder.folder_id);
      await refreshGitChangesSnapshot();
      vscode.window.showInformationMessage(`Git repository initialized in ${folder.root}.`);
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.gitStatus',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before reading Git status.');
      await open();const target=client,view=panel,owner=workspaceBackend.active;
      if(!target||!owner||!view)throw new Error('Connect the opened workspace before reading Git status.');
      const ticket=await workspaceBackend.prepare();workspaceBackend.assert(ticket);
      if(ticket.origin!==target.baseUrl||owner.origin!==ticket.origin)throw new Error('The selected workspace backend changed.');
      const folder=await chooseGitFolder(target,owner,ticket,'xMind: Git Status Folder');if(!folder)return;
      const values=await target.vcsStatus(folder.ordinal===0?undefined:folder.folder_id);workspaceBackend.assert(ticket);
      if(panel!==view||client!==target)throw new Error('The xMind view changed while reading Git status.');
      await refreshGitChangesSnapshot();
      if(!values.length){vscode.window.showInformationMessage(`No changed files reported by Git for ${folder.root}. The folder may not be a Git repository.`);return;}
      const items=values.map(item=>({label:`${item.status}  ${JSON.stringify(item.file)}`,description:`+${item.additions} −${item.deletions}`}));
      await vscode.window.showQuickPick(items,{title:`Git status · ${folder.root}`,placeHolder:`${values.length} changed file${values.length===1?'':'s'} · line counts appear beside each path`});
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.gitDiff',async()=>{
    try{
      if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before reviewing Git changes.');
      await open();const target=client,view=panel,owner=workspaceBackend.active;
      if(!target||!owner||!view)throw new Error('Connect the opened workspace before reviewing Git changes.');
      const ticket=await workspaceBackend.prepare();workspaceBackend.assert(ticket);
      if(ticket.origin!==target.baseUrl||owner.origin!==ticket.origin)throw new Error('The selected workspace backend changed.');
      const folder=await chooseGitFolder(target,owner,ticket,'xMind: Git Review Folder');if(!folder)return;
      const modeChoice=await vscode.window.showQuickPick([
        {label:'Working tree',description:'Compare HEAD with tracked and untracked workspace changes',mode:'working'},
        {label:'Branch changes',description:'Compare the review base with the current working tree',mode:'branch'},
        {label:'Committed changes',description:'Compare the review base with the current branch commits',mode:'committed'}
      ],{title:`Git comparison · ${folder.root}`,placeHolder:'Choose what to compare'});
      if(!modeChoice)return;if(panel!==view||client!==target)throw new Error('The xMind workspace changed before the Git comparison.');workspaceBackend.assert(ticket);
      let base;
      const folderId=folder.ordinal===0?undefined:folder.folder_id;
      if(modeChoice.mode!=='working'){
        const inferred=await target.vcsBase(folderId);workspaceBackend.assert(ticket);base=inferred?.ref;
        if(!base){base=await vscode.window.showInputBox({title:'xMind: Git base reference',prompt:'xMind could not infer this branch’s review base. Enter a branch name or commit ID.',placeHolder:'main or commit SHA',ignoreFocusOut:true});if(base===undefined)return;if(panel!==view||client!==target)throw new Error('The xMind workspace changed while selecting a Git base.');workspaceBackend.assert(ticket);}
      }
      const diffs=await target.vcsDiff(modeChoice.mode,base,3,folderId);workspaceBackend.assert(ticket);
      if(panel!==view||client!==target)throw new Error('The xMind workspace changed while reading Git changes.');
      if(!diffs.length){vscode.window.showInformationMessage(`No ${modeChoice.mode==='working'?'working-tree':modeChoice.mode} Git changes in ${folder.root}.`);return;}
      const selected=await vscode.window.showQuickPick(diffs.map(item=>({label:`${item.status}  ${JSON.stringify(item.file)}`,description:`+${item.additions} −${item.deletions}`,item})),{title:`Git diff · ${folder.root}`,placeHolder:`${diffs.length} file${diffs.length===1?'':'s'} · select a patch to inspect`});
      if(!selected)return;if(panel!==view||client!==target)throw new Error('The xMind workspace changed before opening the Git patch.');workspaceBackend.assert(ticket);
      const document=await vscode.workspace.openTextDocument({language:'diff',content:selected.item.patch});
      if(panel!==view||client!==target)throw new Error('The xMind workspace changed before displaying the Git patch.');workspaceBackend.assert(ticket);
      await vscode.window.showTextDocument(document,{preview:true});
    }catch(error){vscode.window.showErrorMessage(error.message);}
  }));
  if(vscode.workspace.onDidChangeWorkspaceFolders)context.subscriptions.push(vscode.workspace.onDidChangeWorkspaceFolders(()=>{
    reconnectWorkspace().catch(error=>vscode.window.showErrorMessage(error.message));
  }));
  if(vscode.workspace.onDidSaveTextDocument)context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(()=>{
    if(client&&panel&&workspaceBackend.active)refreshGitChangesSnapshot().catch(()=>{});
  }));
  if(vscode.workspace.onDidChangeConfiguration)context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event=>{
    const policyChanged=event.affectsConfiguration('agentflow.workspaceEdits');
    const runtimeChanged=['backendMode','backendUrl','runtimeDirectory','stdlibSource','providerConfigPath','languageServerConfigPath','processConfigPath'].some(key=>event.affectsConfiguration('agentflow.'+key));
    if(policyChanged){
      vscode.window.showInformationMessage('Workspace edit policy is stored with this local profile. Apply the policy to this backend to enable or disable approved file changes.','Apply now').then(choice=>{if(choice==='Apply now')vscode.commands.executeCommand('agentflow.applyRuntimeConfiguration');});
    }else if(runtimeChanged)reconnectWorkspace().catch(error=>vscode.window.showErrorMessage(error.message));
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
  context.subscriptions.push({ dispose:()=>{clearTimeout(sessionRefreshTimer);sessionRefreshTimer=undefined;sessionCatalogueSignature=undefined;mcpController?.dispose();languageServerEditor.dispose();stop();workspaceBackend.dispose();receiveSubscription?.dispose();disposeSubscription?.dispose();} });
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
