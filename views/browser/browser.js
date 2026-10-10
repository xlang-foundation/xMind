'use strict';
(() => {
const fileReview=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/patch-review').fileReview:globalThis.XMindPatchReview.fileReview;
const viewEnrollmentWire=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').providerEnrollmentWire:globalThis.XMindBackend.providerEnrollmentWire;
const ProfileController=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').ProviderProfileController:globalThis.XMindBackend.ProviderProfileController;
const observeOwnedRun=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').observeOwnedRun:globalThis.XMindBackend.observeOwnedRun;
const validatePlanObservation=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').validatePlanObservation:globalThis.XMindBackend.validatePlanObservation;
const validatePlanInputText=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').validatePlanInputText:globalThis.XMindBackend.validatePlanInputText;
const ContextViewController=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').ContextViewController:globalThis.XMindBackend.ContextViewController;
const SkillViewController=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').SkillViewController:globalThis.XMindBackend.SkillViewController;
const validateGraphContext=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').validateGraphContext:globalThis.XMindBackend.validateGraphContext;
const EventStreamSubscription=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').EventStreamSubscription:globalThis.XMindBackend.EventStreamSubscription;
const BackendClient=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').BackendClient:globalThis.XMindBackend.BackendClient;
const McpAuthorizationController=typeof module!=='undefined'&&module.exports?require('../../extensions/vscode/client').McpAuthorizationController:globalThis.XMindBackend.McpAuthorizationController;
function browserRuntime(){return typeof window==='undefined'?globalThis:window;}
async function browserFetch(input,options={}){const runtime=browserRuntime();try{return await runtime.fetch(input,options);}catch(error){if(!options?.signal||!/signal[\s\S]*AbortSignal/i.test(String(error?.message)))throw error;const compatible={...options};delete compatible.signal;return runtime.fetch(input,compatible);}}
// Browser authentication stays in an HttpOnly cookie. Domain validation and
// committed-event parsing remain shared with the native editor client.
class BrowserSessionClient extends BackendClient {
  constructor(origin,{fetchImpl=browserFetch,requestSignal=()=>{const signals=browserRuntime().AbortSignal;return signals?.timeout?.(15000);}}={}){super(origin,()=>{throw new Error('Browser views use session authentication');},fetchImpl);this.requestSignal=requestSignal;}
  async request(path,body){const init={method:body===undefined?'GET':'POST',headers:body===undefined?{}:{'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),credentials:'same-origin',redirect:'error'};const signal=this.requestSignal();if(signal)init.signal=signal;const response=await this.fetch(this.baseUrl+path,init);const data=await response.json();if(!response.ok){const error=new Error(typeof data.detail==='string'?data.detail:`Backend returned ${response.status}`);error.status=response.status;throw error;}return data;}
  async openEventStream(path,signal){signal?.throwIfAborted();const init={method:'GET',headers:{Accept:'text/event-stream'},credentials:'same-origin',redirect:'error'};if(signal){const signals=browserRuntime().AbortSignal;init.signal=signals?.any&&signals?.timeout?signals.any([signal,signals.timeout(35000)]):signal;}return this.fetch(this.baseUrl+path,init);}
}
// Thin view controller. All execution, permissions and persistence stay native.
class BrowserController {
  async readSkills(){
    if(this.disposed)return;const version=this.generation;
    if(this.health?.skill_controls!==true){if(this.skillController)this.skillController.invalidate();return;}
    this.skillController??=new SkillViewController(this.client,m=>{if(!this.disposed)this.post(m);},()=>({session:this.session,generation:this.generation,enabled:!this.disposed&&this.health?.skill_controls===true}));
    try{await this.skillController.read();}catch(error){if(this.current(version))this.post({type:'skills-error',text:error.message});}
  }
  constructor(client,post,{save=()=>{},review=()=>{},copy=()=>{},link=()=>{},subscriptionFactory=options=>new EventStreamSubscription(options)}={}){this.client=client;this.post=post;this.save=save;this.review=review;this.copy=copy;this.link=link;this.subscriptionFactory=subscriptionFactory;this.generation=0;this.cursor=0;this.runs=[];this.graphs=[];this.children=new Map();this.operations=new Map();this.models={models:[]};this.queue=Promise.resolve();}
  stop(){clearTimeout(this.snapshotTimer);this.snapshotTimer=undefined;this.subscription?.stop();this.contextController?.invalidate();this.skillController?.invalidate();this.pendingTreeEvents=false;const hadPlan=!!this.planObservation||this.pendingPlanObservation;this.pendingPlanObservation=false;this.planReadConflicts=0;this.planObservation=undefined;if(hadPlan)this.post({type:'plan-clear'});this.generation++;this.profileController?.invalidate();}
  dispose(){this.disposed=true;this.mcpController?.dispose();this.profileController?.dispose();this.stop();this.contextController?.dispose();clearTimeout(this.keyTimer);this.selection=undefined;}
  async readContext(){
    if(this.disposed)return;
    if(this.health?.context_controls!==true){if(this.contextController?.record)this.contextController.invalidate();return;}
    this.contextController??=new ContextViewController(this.client,this.post,()=>({session:this.session,model:this.model,generation:this.generation,enabled:this.health?.context_controls===true&&!this.runs.find(run=>run.id===this.runId)?.graph_root}),async()=>{
      if(this.health.provider_profile_admission!==true)return;
      this.profileController??=new ProfileController(this.client,this.post);return this.profileController.admission();
    });return this.contextController.read();
  }
  current(version){return !this.disposed&&version===this.generation;}
  busy(){return this.runs.some(run=>['queued','running','paused'].includes(run.state));}
  remember(){this.save({session:this.session,run:this.runId,model:this.model,graph:this.graphId,...(this.mcpBindings!==undefined?{mcp:this.mcpBindings}:{})});}
  async readMcp(){
    if(this.disposed)return;
    if(!this.mcpController){this.mcpController=new McpAuthorizationController(this.client,this.post,{current:()=>!this.disposed,save:bindings=>{this.mcpBindings=bindings;this.remember();}});return this.mcpController.read(this.mcpBindings||[]);}
    return this.mcpController.read();
  }
  present(){this.post({type:'runs',runs:this.runs,selected:this.runId,busy:this.busy()});}
  async refreshCapabilities(){const version=this.generation,health=await this.client.health();if(Object.hasOwn(health,'agent_planning'))await this.client.planning();let catalogue={models:[]};if(health.agent_execution)catalogue=await this.client.models();let graphs={graphs:[]};try{graphs=await this.client.graphs();}catch(error){if(error.status!==404)throw error;}if(!this.current(version))return;if(!Array.isArray(catalogue.models)||!Array.isArray(graphs.graphs)||graphs.graphs.some(g=>typeof g.id!=='string'||!Number.isSafeInteger(g.revision)||g.revision<1||typeof g.executable!=='boolean'))throw new Error('Invalid backend capabilities');this.health=health;this.models=catalogue;this.model=catalogue.models.some(m=>m.id===this.model)?this.model:catalogue.default_model;this.graphs=graphs.graphs;if(!this.graphs.some(g=>g.id===this.graphId&&g.executable))this.graphId=undefined;this.post({type:'capabilities',execution:health.agent_execution,fileEditProposals:health.file_edit_proposals,renameSessions:health.session_rename===true,models:catalogue.models,model:this.model});this.post({type:'graphs',graphs:this.graphs,selected:this.graphId});if(this.profileController){await this.profileController.refresh();if(this.current(version))this.profileController.models(this.model);}}
  async initialize(saved){this.model=saved?.model;this.graphId=saved?.graph;this.mcpBindings=saved?.mcp;await this.refreshCapabilities();if(this.disposed)return;const version=this.generation,sessions=await this.client.sessions();if(!this.current(version))return;if(saved?.session&&sessions.some(s=>s.id===saved.session))await this.select(saved.session,saved.run);else if(sessions.length)await this.select(sessions.at(-1).id);if(this.disposed)return;this.post({type:'sessions',sessions,selected:this.session});if(!this.session)this.post({type:'status',text:this.health.agent_execution?'Ready':'Backend connected · open Settings to configure a model'});await this.readSkills();this.discover().catch(()=>{}); }
  async select(id,preferred){this.stop();const version=this.generation;this.session=id;this.runId=undefined;this.children.clear();this.graph=undefined;this.operations.clear();this.post({type:'graph-clear'});this.post({type:'operations',operations:[]});this.post({type:'reset-run'});const history=await this.client.history(id),runs=await this.client.runs(id);if(!this.current(version))return;
    const previousProfile=this.profileController?.state;await this.profileController?.refresh();if(!this.current(version))return;
    if(previousProfile&&(previousProfile.revision!==this.profileController.state?.revision||previousProfile.active!==this.profileController.state?.active))await this.refreshCapabilities();
    if(!this.current(version))return;const retainedCatalogue=this.profileController?.models(this.model);
    this.runs=runs;this.post({type:'history',history});const selected=runs.find(run=>run.id===preferred)||runs.at(-1);this.runId=selected?.id;this.cursor=0;this.present();this.remember();await this.refreshSessions();
    // A selection acknowledgement or discovery can be retired by the immediate
    // conversation intent guard after Native has already changed profiles.
    // Recover with the freshly observed saved profile; never replay the CAS or
    // carry an unsaved key across the conversation change.
    if(this.current(version)&&previousProfile&&!retainedCatalogue&&this.profileController?.state?.active){try{await this.discover();}catch{}}
    if(!this.current(version))return;if(this.runId){await this.poll();this.watch();}else this.post({type:'status',text:'Ready'});await this.readSkills();}
  watch(){
    // A faster command snapshot may already show completion while the owned
    // stream still has committed frames in flight. Drain that stream through
    // its terminal acknowledgement before retiring observation or switching.
    const pin=this.subscription?.pin,selected=this.runs.find(value=>value.id===this.runId);
    if(pin&&selected&&['completed','failed','cancelled'].includes(selected.state)&&!this.subscription.finished&&pin.root===selected.id&&pin.session===this.session&&pin.client===this.client&&pin.generation===this.generation)return;
    const run=this.runs.find(value=>value.id===this.runId&&['queued','running','paused'].includes(value.state))||this.runs.find(value=>['queued','running','paused'].includes(value.state))||((this.pendingTreeEvents||this.pendingPlanObservation)&&this.runs.find(value=>value.id===this.runId));
    if(!run||!this.session||this.disposed){this.subscription?.stop();return;}
    this.subscription??=this.subscriptionFactory({current:pin=>this.current(pin.generation)&&this.client===pin.client&&this.session===pin.session,onObservation:async(value,pin)=>{if(this.current(pin.generation))await this.poll();},onEvent:async(event,pin)=>{
      if(!this.current(pin.generation))return;
      if(pin.root===this.runId&&event.seq>this.cursor){
        if(event.run_id!==this.runId&&!this.children.has(event.run_id))await this.poll();
        if(!this.current(pin.generation))return;
        if(event.seq>this.cursor){const child=this.children.get(event.run_id);if(event.run_id!==this.runId&&!child)throw new Error('Refresh the owned child before displaying its event');this.post({type:event.run_id===this.runId?'event':pin.scope==='graph'?'graph-event':'owned-event',event,node_id:child?.node_id,child_id:event.run_id});this.cursor=event.seq;}
      }
      this.scheduleSnapshot(pin);
    },onEnd:async(result,pin)=>{if(!this.current(pin.generation))return;await this.poll();if(this.current(pin.generation))this.watch();},onError:(error,pin)=>{if(this.current(pin.generation))this.post({type:'error',text:error.message});}});
    this.subscription.watch({client:this.client,root:run.id,session:this.session,scope:run.graph_root?'graph':this.health?.owned_child_observation===true?'tree':'run',generation:this.generation,after:run.id===this.runId?this.cursor:0});
  }
  scheduleSnapshot(pin){if(this.snapshotTimer||!this.current(pin.generation))return;this.snapshotTimer=setTimeout(()=>{this.snapshotTimer=undefined;if(this.current(pin.generation))this.poll().then(()=>{if(this.current(pin.generation))this.watch();});},100);}
  async refreshSessions(){const version=this.generation,sessions=await this.client.sessions();if(this.current(version))this.post({type:'sessions',sessions,selected:this.session});}
  async poll(){
    const version=this.generation;
    while(this.snapshotPromise){await this.snapshotPromise;if(!this.current(version))return;}
    if(!this.runId||!this.current(version))return;
    const pending=this.pollSnapshot();this.snapshotPromise=pending;
    try{await pending;}finally{if(this.snapshotPromise===pending)this.snapshotPromise=undefined;}
  }
  async pollSnapshot(){const version=this.generation,id=this.runId;try{
    const run=await this.client.status(id);if(!this.current(version))return;
    if(!run.graph_root&&this.health?.owned_child_observation===true){await this.pollOwned(run,version);return;}
    if(run.graph_root){const events=await this.client.graphEvents(id,this.cursor),root=await this.client.graph(id),children=await this.client.graphChildren(id);if(!this.current(version))return;if(root.run.id!==id||root.run.session_id!==this.session||children.some(child=>child.parent_id!==id||child.session_id!==this.session))throw new Error('Graph observation changed');this.children=new Map(children.map(child=>[child.id,child]));this.graph=root;const histories={},operations=[];
      for(const child of children){if(root.spec.nodes.find(node=>node.id===child.node_id)?.type==='agent')histories[child.id]=await this.client.graphChildHistory(id,child.id);operations.push(...await this.client.operations(child.id));if(!this.current(version))return;}
      this.post({type:'graph',record:root,children,histories});for(const event of events){if(event.seq<=this.cursor)continue;if(event.run_id!==id&&!this.children.has(event.run_id))throw new Error('Unowned graph event');this.post({type:event.run_id===id?'event':'graph-event',event,node_id:this.children.get(event.run_id)?.node_id});this.cursor=event.seq;}this.operations=new Map(operations.map(item=>[item.id,item]));this.post({type:'operations',operations:operations.map(item=>({...item,node_id:this.children.get(item.run_id)?.node_id}))});
    }else{const events=await this.client.events(id,this.cursor),operations=await this.client.operations(id);if(!this.current(version))return;for(const event of events){if(event.seq<=this.cursor)continue;if(event.run_id!==id)throw new Error('Unowned run event');this.post({type:'event',event});this.cursor=event.seq;}this.operations=new Map(operations.map(item=>[item.id,item]));this.post({type:'operations',operations});}
    const latest=await this.client.status(id);if(!this.current(version))return;
    if(['completed','failed','cancelled'].includes(latest.state)){
      const final=run.graph_root?await this.client.graphEvents(id,this.cursor):await this.client.events(id,this.cursor);if(!this.current(version))return;
      if(run.graph_root){const root=await this.client.graph(id),children=await this.client.graphChildren(id),histories={},operations=[];if(root.run.id!==id||root.run.session_id!==this.session||children.some(child=>child.parent_id!==id||child.session_id!==this.session))throw new Error('Graph observation changed');for(const child of children){if(root.spec.nodes.find(node=>node.id===child.node_id)?.type==='agent')histories[child.id]=await this.client.graphChildHistory(id,child.id);operations.push(...await this.client.operations(child.id));}if(!this.current(version))return;this.children=new Map(children.map(child=>[child.id,child]));this.graph=root;this.post({type:'graph',record:root,children,histories});this.operations=new Map(operations.map(item=>[item.id,item]));this.post({type:'operations',operations:operations.map(item=>({...item,node_id:this.children.get(item.run_id)?.node_id}))});}
      for(const event of final){if(event.seq<=this.cursor)continue;if(event.run_id!==id&&!this.children.has(event.run_id))throw new Error('Unowned final event');this.post({type:event.run_id===id?'event':'graph-event',event,node_id:this.children.get(event.run_id)?.node_id});this.cursor=event.seq;}
    }
    const history=await this.client.history(this.session),runs=await this.client.runs(this.session);if(!this.current(version))return;this.runs=runs;this.present();this.post({type:'transcript',history,preserveLive:['queued','running','paused'].includes(latest.state)});this.post({type:'status',text:latest.state});
  }catch(error){if(error.status===409&&this.current(version)){this.graph=undefined;this.post({type:'graph-clear'});}if(this.current(version))this.post({type:'error',text:error.message});}finally{if(this.current(version)){if(!this.busy())await this.readSkills();await this.readContext().catch(error=>{if(this.current(version))this.post({type:'error',text:error.message});});}}}
  async pollOwned(run,version){
    const id=this.runId,validRoot=value=>{if(value.id!==id||value.session_id!==this.session||value.parent_id||value.graph_root)throw new Error('Owned run observation identity changed');};validRoot(run);
    const publish=snapshot=>{
      this.children=new Map(snapshot.children.map(child=>[child.run.id,child.run]));this.operations=new Map(snapshot.operations.map(operation=>[operation.id,operation]));
      this.post({type:'owned-children',parent:run,children:snapshot.children,histories:snapshot.histories});
      for(const event of snapshot.events){if(event.seq<=this.cursor)continue;this.post({type:event.run_id===id?'event':'owned-event',event,child_id:event.run_id});this.cursor=event.seq;}
      this.post({type:'operations',operations:snapshot.operations.map(operation=>({...operation,node_id:this.children.get(operation.run_id)?.node_id}))});
    };
    let snapshot=await observeOwnedRun(this.client,run,this.session,this.cursor);if(!this.current(version))return;publish(snapshot);
    run=await this.client.status(id);if(!this.current(version))return;
    validRoot(run);
    if(['completed','failed','cancelled'].includes(run.state)){snapshot=await observeOwnedRun(this.client,run,this.session,this.cursor);if(!this.current(version))return;publish(snapshot);}
    await this.readPlan(id,version);if(!this.current(version))return;
    const history=await this.client.history(this.session),runs=await this.client.runs(this.session);if(!this.current(version))return;
    this.runs=runs;this.pendingTreeEvents=!snapshot.caughtUp;this.present();this.post({type:'transcript',history,preserveLive:['queued','running','paused'].includes(run.state)});this.post({type:'status',text:run.state});
    this.watch();
  }
  async readPlan(root,version){
    if(!Object.hasOwn(this.health||{},'agent_planning'))return;
    const target=this.client;let record;try{record=validatePlanObservation(await target.plan(root),root);}catch(error){
      if(error.status!==409)throw error;if(!this.current(version)||this.client!==target||this.runId!==root)return;
      this.planObservation=undefined;this.post({type:'plan-clear'});this.planReadConflicts=(this.planReadConflicts||0)+1;this.pendingPlanObservation=this.planReadConflicts<3;
      if(!this.pendingPlanObservation)this.post({type:'error',text:'Plan is changing. Refresh its observation before answering or resuming.'});return;
    }if(!this.current(version)||this.client!==target||this.runId!==root)return;
    if(record.run.session_id!==this.session)throw new Error('Plan conversation identity changed');
    this.planReadConflicts=0;this.pendingPlanObservation=false;this.planObservation=JSON.parse(JSON.stringify(record));this.post({type:'plan',record:this.planObservation});return this.planObservation;
  }
  planReady(record){return record?.enabled&&record.run.state==='paused'&&record.plan&&!record.plan.halted&&record.plan.claimed.length===0&&!record.questions.some(question=>question.state==='waiting')&&(record.plan.ready.length>0||record.plan.report_ready)&&record.calls.some(call=>call.pending);}
  async planCommand(message,version){
    const observed=this.planObservation,root=this.runId,session=this.session,target=this.client;
    if(!observed?.plan||!observed.enabled||message.root!==root||observed.run.id!==root||observed.run.session_id!==session||message.plan_id!==observed.plan.id||message.expected_revision!==observed.plan.revision||message.expected_state_sequence!==observed.plan.state_sequence)throw new Error('Refresh the selected plan before providing input or resuming');
    const answer=message.type==='plan-input';let question;
    if(answer){question=observed.questions.find(question=>question.id===message.request);if(!['paused','running'].includes(observed.run.state)||!question||question.state!=='waiting'||question.backend_node_id!==message.backend_node_id||question.definition_revision!==message.definition_revision||question.published_event_seq!==message.published_event_seq||Date.now()>=question.expires_unix_ms)throw new Error('Refresh the current owned human question');validatePlanInputText(message.input_json);}
    else if(!this.planReady(observed))throw new Error('The selected owner has no ready paused plan');
    let fresh;try{fresh=validatePlanObservation(await target.plan(root),root);}catch(error){if(error.status===409&&this.current(version)&&target===this.client&&root===this.runId){this.planObservation=undefined;this.post({type:'plan-clear'});}throw error;}if(!this.current(version)||target!==this.client||root!==this.runId||session!==this.session)return;
    if(fresh.run.session_id!==session)throw new Error('Plan conversation identity changed');
    this.planObservation=JSON.parse(JSON.stringify(fresh));this.post({type:'plan',record:this.planObservation});
    if(!fresh.enabled||fresh.plan?.id!==observed.plan.id||fresh.plan.revision!==message.expected_revision||fresh.plan.state_sequence!==message.expected_state_sequence)throw new Error('Plan changed. Review the refreshed plan before submitting again.');
    if(answer){const actual=fresh.questions.find(value=>value.id===question.id);for(const field of ['id','plan_id','root_run_id','backend_node_id','definition_revision','published_event_seq','question','state','expires_unix_ms'])if(!actual||actual[field]!==question[field])throw new Error('Human question changed. Review the refreshed plan.');if(!['paused','running'].includes(fresh.run.state)||Date.now()>=actual.expires_unix_ms)throw new Error('Human question is no longer awaiting input');}
    else if(!this.planReady(fresh))throw new Error('The selected owner is no longer ready to resume');
    const result=answer?await target.planInput(root,question.id,message.input_json,message.expected_revision,message.expected_state_sequence):await target.resumePlan(root,message.expected_revision,message.expected_state_sequence);
    if(!this.current(version)||target!==this.client||root!==this.runId||session!==this.session)return;
    if(result.id!==root||result.session_id!==session||result.parent_id||result.graph_root)throw new Error('Plan controller result identity changed');
    await this.poll();this.watch();
  }
  async discover(key,id,route){const version=this.generation;clearTimeout(this.keyTimer);this.selection=undefined;try{
    this.profileController??=new ProfileController(this.client,this.post);
    if(await this.profileController.discover(key,id,route))return;
    if(key!==undefined&&(typeof key!=='string'||!/^[\x21-\x7e]{1,32768}$/.test(key)))throw new Error('Enter your provider API key without spaces');const setup=await this.client.providerConfiguration();if(!this.current(version))return;const wire=viewEnrollmentWire(setup);this.post({type:'provider-wire',wire:setup.configured?wire:undefined});if(!setup.configured&&key===undefined)throw new Error('Open Settings and enter your OpenAI API key');this.post({type:'settings-state',busy:true,text:'Fetching models from OpenAI…'});const catalogue=await this.client.discoverProviderModels(key,setup.revision);if(!this.current(version))return;if(!Array.isArray(catalogue.models)||catalogue.models.length>4096||catalogue.models.some(m=>!m||typeof m.id!=='string'||!/^[A-Za-z0-9_.:/-]{1,256}$/.test(m.id)))throw new Error('Invalid model catalogue');const ids=[...new Set(catalogue.models.map(m=>m.id))];if(!ids.length)throw new Error('No models returned');this.selection={key,revision:setup.revision,ids,expires:key===undefined?Infinity:Date.now()+300000};key=undefined;this.post({type:'model-list',models:ids.map(id=>({id})),model:this.selection.key===undefined?setup.model:undefined});this.post({type:'settings-state',busy:false,complete:true,text:'Choose a model below to save configuration'});if(this.selection.key!==undefined)this.keyTimer=setTimeout(()=>{this.selection=undefined;this.post({type:'settings-state',busy:false,text:'Unsaved key expired · fetch models again'});},300000);
  }catch(error){if(this.current(version))this.post({type:'settings-state',busy:false,text:error.message});throw error;}finally{key=undefined;}}
  handle(message){if(['new','select','select-run','refresh','model','select-provider'].includes(message?.type)){this.contextController?.invalidate();if(['new','select','select-run','refresh'].includes(message?.type)){this.stop();this.operations.clear();}}if(message?.type==='saveProviderKey'){const key=message.key;delete message.key;message={type:message.type,key,profile:message.profile,route:message.route};}this.queue=this.queue.then(()=>this.command(message)).then(()=>this.readContext()).catch(error=>this.post({type:'error',text:error.message}));return this.queue;}
  async admit(graph,prompt,binding,version){
    try{return graph?await this.client.graphRun(this.session,graph.id,graph.revision,prompt,this.model,binding):await this.client.run(this.session,prompt,this.model,binding);}
    catch(error){
      if(!this.current(version))return;
      const changed=await this.profileController?.reconcileAdmission(binding,error).catch(()=>false);
      if(!this.current(version))return;
      if(changed){await this.refreshCapabilities();if(!this.current(version))return;this.remember();throw new Error('Provider settings changed in another view. Review the current profile and submit again. Your draft has been kept.');}
      throw error;
    }
  }
  async command(message){if(this.disposed||!message||typeof message.type!=='string')return;const version=this.generation;
    if(message.type==='mcp-refresh')return this.readMcp();
    if(['mcp-start','mcp-renew','mcp-cancel','mcp-open'].includes(message.type)){
      try{if(!this.mcpController)await this.readMcp();if(message.type==='mcp-start')await this.mcpController.start(message.server);else if(message.type==='mcp-renew')await this.mcpController.start(message.server,true);else if(message.type==='mcp-cancel')await this.mcpController.cancel(message.server);else this.mcpController.open(message.server,this.link);}
      catch(error){if(!this.disposed)this.post({type:'mcp-error',text:error.message});}return;
    }
    if(message.type==='ready')return;
    if(message.type==='skills-refresh'){if(!this.session){const session=await this.client.createSession('Workspace skills');if(this.current(version))await this.select(session.id);}await this.readSkills();return;}
    if(message.type==='skills-change'){try{if(this.health?.skill_controls!==true||this.busy()||!this.skillController)throw new Error('Wait for the selected conversation to become idle.');await this.skillController.change(message);}catch(error){if(this.current(version))this.post({type:'skills-error',text:error.message});}return;}
    if(message.type==='saveProviderKey'){let key=message.key;delete message.key;try{await this.discover(key||undefined,message.profile,message.route);}finally{key=undefined;}return;}
    if(message.type==='discardProviderKey'){this.selection=undefined;clearTimeout(this.keyTimer);this.profileController?.invalidate();await this.refreshCapabilities();return;}
    if(message.type==='select-provider'){this.profileController??=new ProfileController(this.client,this.post);if(!await this.profileController.select(message.id)||!this.current(version))return;await this.refreshCapabilities();if(!this.current(version))return;this.model=this.models.default_model;this.remember();this.discover().catch(()=>{});return;}
    if(message.type==='refresh'){this.selection=undefined;clearTimeout(this.keyTimer);await this.refreshCapabilities();await this.refreshSessions();if(this.session)await this.select(this.session,this.runId);try{await this.discover();}catch{}return;}
    if(message.type==='new'){const session=await this.client.createSession('Browser conversation');if(this.current(version))await this.select(session.id);return;}
    if(message.type==='select'){await this.select(message.id);return;}
    if(message.type==='rename-session'){
      try{if(this.health.session_rename!==true||message.id!==this.session||typeof message.title!=='string'||typeof message.expected_title!=='string')throw new Error('Select a conversation on a backend supporting rename');await this.client.renameSession(this.session,message.title,message.expected_title);if(!this.current(version))return;await this.refreshSessions();if(this.current(version))this.post({type:'rename-result',id:message.id,success:true});}
      catch(error){if(this.current(version))this.post({type:'rename-result',id:message.id,success:false,text:error.status===409?'Conversation title changed. Refresh history and reopen Rename before saving.':error.message});}
      return;
    }
    if(message.type==='select-run'){if(!this.session)throw new Error('Select a conversation');const runs=await this.client.runs(this.session);if(!this.current(version))return;if(!runs.some(run=>run.id===message.id))throw new Error('Run belongs to another conversation');await this.select(this.session,message.id);return;}
    if(message.type==='model'){let saved;try{saved=await this.profileController?.save(message.id);}catch(error){if(this.current(version))await this.refreshCapabilities();throw error;}if(saved){if(!this.current(version))return;await this.refreshCapabilities();this.profileController.models(message.id);this.model=message.id;this.remember();return;}if(this.selection){if(Date.now()>this.selection.expires||!this.selection.ids.includes(message.id))throw new Error('Fetch models and select a returned ID');const selection=this.selection,configured=await this.client.configureProvider(message.id,selection.key,selection.revision);selection.key=undefined;if(!this.current(version))return;this.post({type:'provider-wire',wire:viewEnrollmentWire(configured)});selection.revision=configured.revision;selection.expires=Infinity;clearTimeout(this.keyTimer);await this.refreshCapabilities();this.post({type:'model-list',models:selection.ids.map(id=>({id})),model:message.id});}else if(!this.models.models.some(model=>model.id===message.id))throw new Error('Select an advertised model');this.model=message.id;this.remember();return;}
    if(message.type==='graph-select'){if(message.id&&!this.graphs.some(g=>g.id===message.id&&g.executable))throw new Error('Select an executable workflow');this.graphId=message.id||undefined;this.remember();this.post({type:'graphs',graphs:this.graphs,selected:this.graphId});return;}
    if(message.type==='graph-input'){if(!this.graph||message.root!==this.runId||message.revision!==this.graph.checkpoint_revision||!this.graph.checkpoint.nodes.some(n=>n.id===message.node&&n.state==='waiting_human'))throw new Error('Refresh the current human step');if(typeof message.input_json!=='string'||message.input_json.length>65536)throw new Error('Use bounded JSON input');await this.client.graphInput(this.runId,message.node,message.input_json,message.revision);await this.poll();this.watch();return;}
    if(message.type==='graph-resume'){
      const observed=this.graph,root=this.runId,session=this.session;
      if(!observed||observed.run.id!==root||observed.run.session_id!==session||message.root!==root||message.revision!==observed.checkpoint_revision||!validateGraphContext(observed.context,observed.run).resumable)throw new Error('Refresh a ready paused graph before resuming.');
      let fresh;try{fresh=await this.client.graph(root);}catch(error){if(error.status===409&&this.current(version)){this.graph=undefined;this.post({type:'graph-clear'});}throw error;}
      if(!this.current(version)||this.runId!==root||this.session!==session)return;
      if(fresh.run.id!==root||fresh.run.session_id!==session||fresh.checkpoint_revision!==message.revision||!validateGraphContext(fresh.context,fresh.run).resumable)throw new Error('Graph changed. Refresh before resuming.');
      const result=await this.client.resumeGraph(root,message.revision);if(!this.current(version)||this.runId!==root||this.session!==session)return;
      if(result.id!==root||result.session_id!==session||result.graph_root!==true)throw new Error('Graph resume ownership changed');await this.poll();this.watch();return;
    }
    if(message.type==='context-compact'){await this.readContext();await this.contextController.compact(message);return;}
    if(message.type==='plan-input'||message.type==='plan-resume'){await this.planCommand(message,version);return;}
    if(message.type==='send'){if(this.busy())throw new Error('Wait for the active conversation run');if(typeof message.prompt!=='string'||!message.prompt.trim())return;const graph=this.graphs.find(g=>g.id===this.graphId&&g.executable);if(!graph&&!this.health.agent_execution)throw new Error('Configure a model in Settings');let binding;if((graph?this.health.graph_provider_profile_admission:this.health.provider_profile_admission)===true){this.profileController??=new ProfileController(this.client,this.post);binding=await this.profileController.admission();if(!this.current(version))return;}if(!this.session){const session=await this.client.createSession(message.prompt.slice(0,80));if(!this.current(version))return;this.session=session.id;}const run=await this.admit(graph,message.prompt,binding,version);if(!this.current(version))return;this.runId=run.id;this.cursor=0;this.children.clear();this.graph=undefined;this.operations.clear();this.post({type:'graph-clear'});this.post({type:'operations',operations:[]});this.post({type:'user',text:message.prompt});this.remember();await this.refreshSessions();await this.poll();this.watch();return;}
    if(message.type==='cancel'){if(this.runId){await this.client.cancel(this.runId);await this.poll();}return;}
    if(message.type==='copy'){if(typeof message.text==='string'&&message.text.length<=1048576)await this.copy(message.text);return;}
    if(message.type==='openLink'){const url=new URL(message.url);if(!['http:','https:'].includes(url.protocol))throw new Error('Only HTTP/HTTPS links are supported');this.link(url.href);return;}
    if(['decide','review','inspect-edit'].includes(message.type)){const observed=this.operations.get(message.id);if(!observed||observed.run_id!==this.runId&&!this.children.has(observed.run_id))throw new Error('Select an observed operation');const current=await this.client.operation(message.id);if(!this.current(version))return;for(const field of ['id','run_id','workspace_id','tool','arguments_json','expires_unix_ms','state'])if(current[field]!==observed[field])throw new Error('Operation changed · refresh before reviewing');
      if(message.type==='inspect-edit'){if(current.state!=='uncertain'||current.tool!=='replace_file')throw new Error('Select an uncertain edit');const inspection=await this.client.inspectEdit(current.id);if(this.current(version))this.post({type:'edit-inspection',id:current.id,inspection});return;}
      if(current.state!=='awaiting_approval'||Date.now()>=current.expires_unix_ms)throw new Error('Operation is no longer awaiting approval');if(message.type==='review'){const plan=fileReview(current);this.review(plan);return;}if(!['allow','deny'].includes(message.decision))throw new Error('Invalid operation decision');if(message.decision==='allow'&&current.tool==='patch_file')fileReview(current);await this.client.decide(current.id,message.decision);await this.poll();return;
    }
  }
}
if(typeof module!=='undefined'&&module.exports)module.exports={BrowserController,BrowserSessionClient};
else {
  let controller,connectionAttempt,connectionGeneration=0,workspaceBusy=false,selectedWorkspaceId;const element=id=>document.getElementById(id),dialog=element('connection');
  const post=data=>window.dispatchEvent(new MessageEvent('message',{data,origin:location.origin,source:window}));
  const divider=element('sidebar-divider');let drag;
  function resizeSidebar(width,remember=false){const minimum=280,maximum=Math.max(minimum,innerWidth-320),value=Math.round(Math.min(maximum,Math.max(minimum,width)));document.documentElement.style.setProperty('--xmind-sidebar-width',value+'px');divider.setAttribute('aria-valuemin',minimum);divider.setAttribute('aria-valuemax',maximum);divider.setAttribute('aria-valuenow',value);if(remember)try{localStorage.setItem('xmind.view.sidebarWidth',String(value));}catch{}return value;}
  let preferredWidth=420;try{const saved=Number(localStorage.getItem('xmind.view.sidebarWidth'));if(Number.isFinite(saved)&&saved>=280)preferredWidth=saved;}catch{}resizeSidebar(preferredWidth);
  window.addEventListener('resize',()=>resizeSidebar(preferredWidth));
  divider.onpointerdown=event=>{if(event.button!==0)return;event.preventDefault();drag={pointer:event.pointerId,x:event.clientX,width:Number(divider.getAttribute('aria-valuenow'))};divider.setPointerCapture(event.pointerId);divider.classList.add('dragging');};
  divider.onpointermove=event=>{if(!drag||drag.pointer!==event.pointerId)return;preferredWidth=resizeSidebar(drag.width+drag.x-event.clientX);};
  const finishResize=()=>{if(!drag)return;drag=undefined;divider.classList.remove('dragging');resizeSidebar(preferredWidth,true);};divider.onpointerup=finishResize;divider.onpointercancel=finishResize;divider.onlostpointercapture=finishResize;
  divider.onkeydown=event=>{let width=Number(divider.getAttribute('aria-valuenow')),step=event.shiftKey?40:16;if(event.key==='ArrowLeft')width+=step;else if(event.key==='ArrowRight')width-=step;else if(event.key==='Home')width=280;else if(event.key==='End')width=innerWidth-320;else return;event.preventDefault();preferredWidth=resizeSidebar(width,true);};
  const abortConnection=()=>{connectionGeneration++;connectionAttempt?.abort.abort();connectionAttempt=undefined;dialog.querySelector('button[type="submit"]').disabled=false;};
  const cancelConnection=()=>{abortConnection();element('server-token').value='';element('connection-error').textContent='';dialog.close();if(!controller)post({type:'status',text:'View disconnected · connect when ready'});};
  element('connection-cancel').onclick=cancelConnection;element('connection-close').onclick=cancelConnection;dialog.addEventListener('cancel',event=>{event.preventDefault();cancelConnection();});
  globalThis.xMindView={postMessage(message){if(message.type==='ready'&&!controller){post({type:'capabilities',execution:false,models:[]});connect();return;}controller?.handle(message);}};
  element('connect-view').onclick=()=>{element('server-token').value='';dialog.showModal();};
  const picker=element('workspace-picker'),workspaceControl=element('workspace-control');
  function readSaved(key){try{return JSON.parse(sessionStorage.getItem(key)||'null');}catch{return null;}}
  async function workspaceCatalogue(signal){const response=await browserFetch('/ui/workspaces',{credentials:'same-origin',redirect:'error',signal});if(!response.ok)throw new Error(response.status===404?'Update xMind to enable native workspace selection.':`The native workspace catalogue is unavailable (HTTP ${response.status}).`);const value=await response.json();if(!value||!Array.isArray(value.workspaces)||value.workspaces.length>256||typeof value.selected_workspace_id!=='string'||value.workspaces.some(item=>!item||typeof item.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(item.workspace_id)||typeof item.name!=='string'||typeof item.root!=='string'))throw new Error('The native host returned an invalid workspace catalogue.');if(!value.workspaces.some(item=>item.workspace_id===value.selected_workspace_id))throw new Error('The selected workspace is absent from the native catalogue.');return value;}
  function showWorkspaceCatalogue(value){while(picker.firstChild)picker.removeChild(picker.firstChild);for(const item of value.workspaces){const option=document.createElement('option');option.value=item.workspace_id;option.textContent=item.name+' · '+item.root;picker.appendChild(option);}selectedWorkspaceId=value.selected_workspace_id;picker.value=selectedWorkspaceId;picker.disabled=value.workspaces.length<2;workspaceControl.hidden=false;}
  function clearWorkspaceUi(){selectedWorkspaceId=undefined;picker.disabled=true;picker.replaceChildren();workspaceControl.hidden=true;}
  function resetObservedWorkspace(){post({type:'provider-clear'});post({type:'capabilities',execution:false,models:[]});post({type:'graph-clear'});post({type:'operations',operations:[]});post({type:'history',history:[]});post({type:'sessions',sessions:[],selected:undefined});post({type:'status',text:'Workspace changed · runs continue on their original backend'});}
  function makeController(client,generation,pending){let published=false;const next=new BrowserController(client,data=>published?post(data):pending.push(data),{save:value=>sessionStorage.setItem('xmind.view.selection:'+selectedWorkspaceId,JSON.stringify(value)),copy:text=>navigator.clipboard.writeText(text),link:url=>window.open(url,'_blank','noopener,noreferrer'),review:plan=>{element('browser-review').hidden=false;element('review-title').textContent='Proposed '+(plan.action||'file change')+': '+plan.path+(plan.destination_path?' → '+plan.destination_path:'');element('review-before-label').textContent=plan.before_exists===false?'Before · absent':'Before';element('review-after-label').textContent=plan.after_exists===false?'After · absent':'After';element('review-before').textContent=plan.before_content;element('review-after').textContent=plan.after_content;}});return {next,publish(){published=true;pending.forEach(post);}};}
  async function activateWorkspace(generation,signal){const client=new BrowserSessionClient(location.origin,{requestSignal:signal});await client.health();if(generation!==connectionGeneration)return;const catalogue=await workspaceCatalogue(signal);if(generation!==connectionGeneration)return;const workspace=await client.workspace();if(!workspace.configured||workspace.workspace_id!==catalogue.selected_workspace_id)throw new Error('The browser session and native workspace catalogue disagree.');showWorkspaceCatalogue(catalogue);let pending=[];const made=makeController(client,generation,pending),saved=readSaved('xmind.view.selection:'+workspace.workspace_id);await made.next.initialize(saved);if(generation!==connectionGeneration){made.next.dispose();return;}controller?.dispose();controller=made.next;made.publish();dialog.close();element('connection-error').textContent='';element('disconnect-view').hidden=false;element('workspace-info').textContent=`Connected to the native xMind runtime. Server workspace: ${workspace.root}. The right sidebar shows durable conversations, runs, workflows and approval proposals. Closing this view leaves backend execution running.`;}
  element('disconnect-view').onclick=async()=>{abortConnection();controller?.dispose();controller=undefined;post({type:'provider-clear'});post({type:'capabilities',execution:false,models:[]});post({type:'graph-clear'});post({type:'operations',operations:[]});post({type:'history',history:[]});post({type:'status',text:'View disconnected · backend runs continue'});element('disconnect-view').hidden=true;element('workspace-info').textContent='Connect to your native xMind Server.';clearWorkspaceUi();try{await browserFetch('/ui/session/disconnect',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}',credentials:'same-origin'});}catch{element('connection-error').textContent='Connection ended; server session cleanup could not be confirmed.';}};
  picker.onchange=async()=>{const requested=picker.value;if(!controller||workspaceBusy||requested===selectedWorkspaceId)return;const previousWorkspaceId=selectedWorkspaceId;workspaceBusy=true;picker.disabled=true;const generation=++connectionGeneration;const signal=()=>window.AbortSignal.timeout(30000);try{const response=await browserFetch('/ui/workspaces/select',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({workspace_id:requested}),credentials:'same-origin',redirect:'error',signal:signal()});if(!response.ok){const failure=await response.json().catch(()=>({}));throw new Error(failure.detail||'Native workspace switch failed.');}const selected=await response.json();if(generation!==connectionGeneration)return;if(!selected.connected||selected.workspace?.workspace_id!==requested)throw new Error('The native host selected a different workspace.');controller?.dispose();controller=undefined;resetObservedWorkspace();const signalNow=()=>generation===connectionGeneration?window.AbortSignal.timeout(15000):window.AbortSignal.abort();await activateWorkspace(generation,signalNow);}catch(error){if(generation===connectionGeneration){element('connection-error').textContent=error.message;post({type:'error',text:'Workspace switch failed · '+error.message});try{const catalogue=await workspaceCatalogue(signal());if(catalogue.selected_workspace_id!==previousWorkspaceId){controller?.dispose();controller=undefined;resetObservedWorkspace();await activateWorkspace(generation,()=>window.AbortSignal.timeout(15000));}else showWorkspaceCatalogue(catalogue);}catch{picker.disabled=false;}}}finally{workspaceBusy=false;if(controller)picker.disabled=false;}};
  async function connect(entered){if(connectionAttempt)return;const attempt={abort:new window.AbortController()},generation=++connectionGeneration;connectionAttempt=attempt;dialog.querySelector('button[type="submit"]').disabled=true;const signal=()=>attempt.abort.signal;try{
    const headers={'Content-Type':'application/json'};if(entered!==undefined)headers.Authorization='Bearer '+XMindBackend.validateToken(entered);entered=undefined;
    const response=await browserFetch('/ui/session',{method:'POST',headers,body:'{}',credentials:'same-origin',redirect:'error',signal:signal()});delete headers.Authorization;if(!response.ok){if(response.status===401)throw new Error('Enter the native server access token to connect');const failure=await response.json().catch(()=>({}));throw new Error(failure.error_code==='invalid_view_session'?'The server and browser adapter access-session versions do not match. Update them together.':'Browser session connection failed');}if(generation!==connectionGeneration)return;
    await activateWorkspace(generation,signal);
  }catch(error){controller?.dispose();controller=undefined;clearWorkspaceUi();if(generation===connectionGeneration){element('connection-error').textContent=error.message;if(!dialog.open)dialog.showModal();}}finally{entered=undefined;if(connectionAttempt===attempt){connectionAttempt=undefined;dialog.querySelector('button[type="submit"]').disabled=false;}}}
  element('connection-form').onsubmit=event=>{event.preventDefault();if(connectionAttempt)return;const entered=element('server-token').value;element('server-token').value='';connect(entered);};
  window.addEventListener('pagehide',()=>{abortConnection();controller?.dispose();});
}
})();
