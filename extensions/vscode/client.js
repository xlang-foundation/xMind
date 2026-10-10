'use strict';

function backendOrigin(baseUrl) {
    const url = new URL(baseUrl);
    if (url.protocol !== 'http:' || !['localhost', '127.0.0.1'].includes(url.hostname) || url.username || url.password || url.pathname !== '/' || url.search || url.hash) {
      throw new Error('Configure a loopback HTTP xMind Server origin without credentials, path or query.');
    }
    // The native server binds IPv4 only. Canonicalize localhost before selecting
    // its scoped secret, so DNS/IPv6 resolution does not change the destination.
    url.hostname = '127.0.0.1';
    return url.origin;
}
function validateToken(token) {
  if (typeof token !== 'string' || !/^[\x21-\x7e]{32,256}$/.test(token)) {
    throw new Error('Configure the server authentication token: 32–256 printable characters without spaces.');
  }
  return token;
}
class BackendClient {
  #tokenProvider;
  #workspaceGuard;
  constructor(baseUrl, tokenProvider, fetchImpl = (...args) => fetch(...args)) {
    this.baseUrl = backendOrigin(baseUrl);
    if (typeof tokenProvider !== 'function') throw new Error('Server authentication is required.');
    this.#tokenProvider = tokenProvider;
    this.fetch = fetchImpl;
  }
  bindWorkspace(guard) {
    if(!guard||typeof guard.prepare!=='function'||typeof guard.assert!=='function')throw new Error('Invalid workspace admission guard.');
    this.#workspaceGuard=guard;return this;
  }

  async request(path, body, timeoutMs=15000) {
    const ticket=body!==undefined&&this.#workspaceGuard?await this.#workspaceGuard.prepare():undefined;
    const token = validateToken(await this.#tokenProvider());
    if(ticket){
      if(ticket.origin!==this.baseUrl)throw new Error('The selected workspace backend changed before admission.');
      this.#workspaceGuard.assert(ticket);
      if(path==='/v1/runs'||path==='/v1/graph-runs')body={...body,...ticket.fields};
    }
    const response = await this.fetch(this.baseUrl + path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { Authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(timeoutMs),
      redirect: 'error'
    });
    const data = await response.json();
    if (!response.ok) { const error=new Error(typeof data.detail === 'string' ? data.detail : `Backend returned ${response.status}`);error.status=response.status;throw error; }
    return data;
  }
  health() { return this.request('/v1/health'); }
  async workspace() { return validateWorkspace(await this.request('/v1/workspace')); }
  models() { return this.request('/v1/models'); }
  graphs() { return this.request('/v1/graphs'); }
  delegation() { return this.request('/v1/agent/delegation'); }
  async planning() { return validatePlanning(await this.request('/v1/agent/planning')); }
  async plan(root) { executionIdentity(root);return validatePlanObservation(await this.request(`/v1/runs/${root}/plan`),root); }
  async planInput(root,request,input_json,expected_revision,expected_state_sequence) {
    executionIdentity(root);executionIdentity(request);planPreconditions(expected_revision,expected_state_sequence);validatePlanInputText(input_json);
    return validatePlanOwner(await this.request(`/v1/runs/${root}/plan/human/${request}`,{input_json,expected_revision,expected_state_sequence}),root);
  }
  async resumePlan(root,expected_revision,expected_state_sequence) {
    executionIdentity(root);planPreconditions(expected_revision,expected_state_sequence);
    return validatePlanOwner(await this.request(`/v1/runs/${root}/plan/resume`,{expected_revision,expected_state_sequence}),root);
  }
  async context(session,model) {
    executionIdentity(session);if(model!==undefined)profileIdentity(model);
    return validateContextObservation(await this.request(`/v1/sessions/${session}/context${model?`?model_id=${encodeURIComponent(model)}`:''}`),session,model);
  }
  async contextRequest(session,id,model) {
    executionIdentity(session);executionIdentity(id);if(model!==undefined)profileIdentity(model);
    return validateContextRequest(await this.request(`/v1/sessions/${session}/context/requests/${id}${model?`?model_id=${encodeURIComponent(model)}`:''}`),id);
  }
  async compactContext(session,id,expected_head_revision,model,binding) {
    executionIdentity(session);executionIdentity(id);planInteger(expected_head_revision);if(model!==undefined)profileIdentity(model);
    return validateContextRequest(await this.request(`/v1/sessions/${session}/context/compact`,{id,expected_head_revision,...(model?{model_id:model}:{}),...profileBindingFields(binding)}),id);
  }
  async resumeGraph(root,expected_checkpoint_revision) {
    executionIdentity(root);planInteger(expected_checkpoint_revision,1);
    const result=await this.request(`/v1/graph-runs/${root}/resume`,{expected_checkpoint_revision});validateRunDTO(result);
    if(result.id!==root||result.parent_id!==''||result.graph_root!==true)throw new Error('Graph resume ownership changed');return result;
  }
  graphRun(session_id,graph_id,graph_revision,prompt,model_id,binding) { return this.request('/v1/graph-runs',{session_id,graph_id,graph_revision,prompt,...(model_id?{model_id}:{}),...profileBindingFields(binding)}); }
  async graph(id) { const value=await this.request(`/v1/graph-runs/${encodeURIComponent(id)}`);if(value.context!==undefined)validateGraphContext(value.context,value.run);return value; }
  graphChildren(id) { return this.request(`/v1/graph-runs/${encodeURIComponent(id)}/children`); }
  graphEvents(id,after) { return this.request(`/v1/graph-runs/${encodeURIComponent(id)}/events?after=${after}`); }
  graphChildHistory(root,child) { return this.request(`/v1/graph-runs/${encodeURIComponent(root)}/children/${encodeURIComponent(child)}/history`); }
  graphInput(root,node,input_json,expected_checkpoint_revision) { return this.request(`/v1/graph-runs/${encodeURIComponent(root)}/human/${encodeURIComponent(node)}`,{input_json,expected_checkpoint_revision}); }
  providerConfiguration() { return this.request('/v1/provider/configuration'); }
  async importProviderConfiguration(path,expected_revision) {
    if(typeof path!=='string'||!path||path.length>32768||/[\x00-\x1f]/.test(path)||!(/^[A-Za-z]:[\\/]/.test(path)||/^\\\\[^\\/]+[\\/][^\\/]+/.test(path)))throw new Error('Provider configuration must be an absolute local path');
    planInteger(expected_revision);return validateProviderProfiles(await this.request('/v1/provider/configuration/import',{path,expected_revision}),true);
  }
  async mcpAuthorizationServers() { return validateMcpAuthorizationServers(await this.request('/v1/mcp/authorization/servers')); }
  async startMcpAuthorization(server_id,expected_config_revision,expected_credential_revision,request_id) {
    mcpServerIdentity(server_id);planInteger(expected_config_revision,1);planInteger(expected_credential_revision);executionIdentity(request_id);
    return validateMcpAuthorizationAttempt(await this.request('/v1/mcp/authorization/attempts',{server_id,expected_config_revision,expected_credential_revision,request_id}),{id:request_id,server_id,config_revision:expected_config_revision,credential_revision:expected_credential_revision});
  }
  async renewMcpAuthorization(server_id,expected_config_revision,expected_credential_revision,request_id) {
    mcpServerIdentity(server_id);planInteger(expected_config_revision,1);planInteger(expected_credential_revision,1);executionIdentity(request_id);
    return validateMcpAuthorizationAttempt(await this.request('/v1/mcp/authorization/renewals',{server_id,expected_config_revision,expected_credential_revision,request_id}),{id:request_id,server_id,config_revision:expected_config_revision,credential_revision:expected_credential_revision});
  }
  async mcpAuthorization(id,binding) {
    executionIdentity(id);return validateMcpAuthorizationAttempt(await this.request(`/v1/mcp/authorization/attempts/${id}`),{...binding,id});
  }
  async cancelMcpAuthorization(id,binding) {
    executionIdentity(id);return validateMcpAuthorizationAttempt(await this.request(`/v1/mcp/authorization/attempts/${id}/cancel`,{}),{...binding,id});
  }
  discoverProviderModels(api_key,expected_revision) { return this.request('/v1/provider/models',{api_key,expected_revision}); }
  configureProvider(model,api_key,expected_revision) { return this.request('/v1/provider/configuration',{model,api_key,expected_revision}); }
  async providerProfiles() { return validateProviderProfiles(await this.request('/v1/provider/profiles'),true); }
  async discoverProfileModels(id,route_id,api_key,expected_revision) {
    profileIdentity(id);profileIdentity(route_id);profileRevision(expected_revision);profileKey(api_key);
    const catalogue=await this.request('/v1/provider/profiles/models',{id,route_id,api_key,expected_revision},35000);
    exactFields(catalogue,['models']);
    if(!Array.isArray(catalogue.models)||catalogue.models.length>4096)throw new Error('Invalid provider model catalogue');
    const ids=new Set();for(const model of catalogue.models){exactFields(model,['id']);profileIdentity(model.id);if(ids.has(model.id)||model.id===api_key||(api_key&&model.id.includes(api_key)))throw new Error('Invalid provider model catalogue');ids.add(model.id);}
    return catalogue;
  }
  async saveProviderProfile(id,route_id,model,api_key,expected_revision,activate=false) {
    profileIdentity(id);profileIdentity(route_id);profileIdentity(model);profileRevision(expected_revision);profileKey(api_key);
    if(typeof activate!=='boolean'||model===api_key)throw new Error('Invalid provider profile enrollment');
    return validateProviderProfiles(await this.request('/v1/provider/profiles',{id,route_id,model,api_key,expected_revision,activate}));
  }
  async selectProviderProfile(id,expected_revision) {
    profileIdentity(id);profileRevision(expected_revision);
    return validateProviderProfiles(await this.request('/v1/provider/profiles/select',{id,expected_revision}));
  }
  sessions() { return this.request('/v1/sessions'); }
  skills() { return this.request('/v1/workspace/skills'); }
  async sessionSkills(id) { executionIdentity(id);return validateSessionSkills(await this.request(`/v1/sessions/${encodeURIComponent(id)}/skills`),id); }
  async replaceSessionSkills(id,ids,observed) {
    executionIdentity(id);skillIds(ids);validateSessionSkills(observed,id);
    if(!observed.editable)throw new Error('Wait for this conversation to become idle before changing skills.');
    return validateSessionSkills(await this.request(`/v1/sessions/${encodeURIComponent(id)}/skills`,{ids,expected_revision:observed.revision,expected_workspace_id:observed.workspace_id,expected_workspace_authority_id:observed.authority_id}),id);
  }
  async agentDefinitions() { return validateAgentCatalogue(await this.request('/v1/agents')); }
  async sessionAgent(id,catalogue) { executionIdentity(id);return validateSessionAgent(await this.request(`/v1/sessions/${encodeURIComponent(id)}/agent`),id,catalogue); }
  async replaceSessionAgent(id,definition,observed,catalogue) {
    executionIdentity(id);if(!observed?.editable||!Number.isSafeInteger(observed.revision)||observed.revision<0)throw new Error('Wait for this conversation to become idle before changing its agent.');
    const selected=definition===undefined?undefined:catalogue?.agents?.find(item=>item.id===definition.id&&item.revision===definition.revision);
    if(definition!==undefined&&!selected)throw new Error('Choose an agent from the current backend catalogue.');
    const result=await this.request(`/v1/sessions/${encodeURIComponent(id)}/agent`,{agent_id:selected?.id??null,agent_revision:selected?.revision??0,expected_revision:observed.revision});
    return validateSessionAgent(result,id,catalogue);
  }
  createSession(title) { return this.request('/v1/sessions', { title }); }
  renameSession(id,title,expectedTitle) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/title`,{title,expected_title:expectedTitle}); }
  history(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/history`); }
  runs(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/runs`); }
  run(session_id, prompt, model_id,binding) { return this.request('/v1/runs', { session_id, prompt, ...(model_id ? {model_id} : {}),...profileBindingFields(binding) }); }
  events(id, after) { return this.request(`/v1/runs/${encodeURIComponent(id)}/events?after=${after}`); }
  async openEventStream(path,signal){
    signal?.throwIfAborted();const token=validateToken(await this.#tokenProvider());signal?.throwIfAborted();
    return this.fetch(this.baseUrl+path,{method:'GET',headers:{Authorization:`Bearer ${token}`,Accept:'text/event-stream'},signal:signal?AbortSignal.any([signal,AbortSignal.timeout(35000)]):AbortSignal.timeout(35000),redirect:'error'});
  }
  async eventStream(id,{after=0,scope='run',session_id,signal,onEvent,onObservation}={}) {
    executionIdentity(id);executionIdentity(session_id);planInteger(after);
    if(!['run','tree','graph'].includes(scope)||typeof onEvent!=='function'||onObservation!==undefined&&typeof onObservation!=='function')throw new Error('Invalid event stream observer');
    signal?.throwIfAborted();
    const route=scope==='graph'?`/v1/graph-runs/${id}/events/stream`:`/v1/runs/${id}/${scope==='tree'?'tree-events':'events'}/stream`;
    let response;try{response=await this.openEventStream(route+'?after='+after,signal);}catch(error){if(error?.name==='TypeError'||error?.name==='TimeoutError')error.eventTransportUnavailable=true;throw error;}
    if(!response.ok){const value=await response.json();const error=new Error(typeof value.detail==='string'?value.detail:'Native event observation unavailable');error.status=response.status;throw error;}
    const known=new Set([id]);
    return readCommittedEventStream(response,{root:id,session:session_id,scope,after,signal,onEvent,onObservation,authorizeEvent:async event=>{
      if(known.has(event.run_id))return true;
      const children=scope==='graph'?await this.graphChildren(id):await this.ownedChildren(id);signal?.throwIfAborted();
      if(!Array.isArray(children)||children.length>4096)throw new Error('Invalid owned stream children');
      const seen=new Set();for(const record of children){const child=scope==='graph'?record:record.run;validateRunDTO(child);if(child.id===id||seen.has(child.id)||child.parent_id!==id||child.session_id!==session_id||child.graph_root)throw new Error('Owned stream child identity changed');seen.add(child.id);known.add(child.id);}
      return known.has(event.run_id);
    }});
  }
  ownedChildren(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}/children`); }
  ownedChildHistory(parent,child) { return this.request(`/v1/runs/${encodeURIComponent(parent)}/children/${encodeURIComponent(child)}/history`); }
  treeEvents(id,after=0) { if(!Number.isSafeInteger(after)||after<0)throw new Error('Invalid tree event cursor');return this.request(`/v1/runs/${encodeURIComponent(id)}/tree-events?after=${after}`); }
  status(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}`); }
  cancel(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}/cancel`, {}); }
  operations(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}/operations`); }
  operation(id) { return this.request(`/v1/operations/${encodeURIComponent(id)}`); }
  inspectEdit(id) { return this.request(`/v1/operations/${encodeURIComponent(id)}/inspection`); }
  decide(id, decision) {
    if (decision !== 'allow' && decision !== 'deny') throw new Error('Decision must be allow or deny.');
    return this.request(`/v1/operations/${encodeURIComponent(id)}/decision`, { decision });
  }
}

function validateWorkspace(value){
  exactFields(value,['configured','root','workspace_id','authority_id']);
  if(typeof value.configured!=='boolean')throw new Error('Invalid backend workspace metadata.');
  if(!value.configured){if(value.root!==null||value.workspace_id!==null||value.authority_id!==null)throw new Error('Invalid backend workspace metadata.');}
  else if(typeof value.root!=='string'||!value.root||value.root.length>32760||/[\x00-\x1f]/.test(value.root)||typeof value.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(value.workspace_id)||typeof value.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(value.authority_id))throw new Error('Invalid backend workspace identity.');
  return value;
}

// Observation only. Events are read before owners so concurrent admission is
// covered; bounded pages preserve the exact committed cursor without replaying
// execution. Both hosts share the same identity and operation checks.
async function observeOwnedRun(client,run,session,after){
  if(!run||run.session_id!==session||run.parent_id||run.graph_root||!Number.isSafeInteger(after)||after<0)throw new Error('Owned run observation changed');
  const events=[];let cursor=after,caughtUp=false;
  for(let page=0;page<16;++page){
    const batch=await client.treeEvents(run.id,cursor);
    if(!Array.isArray(batch)||batch.length>256)throw new Error('Invalid tree event page');
    for(const event of batch){if(!event||!Number.isSafeInteger(event.seq)||event.seq<=cursor||typeof event.run_id!=='string'||typeof event.kind!=='string')throw new Error('Invalid tree event identity or cursor');events.push(event);cursor=event.seq;}
    if(batch.length<256){caughtUp=true;break;}
  }
  const children=await client.ownedChildren(run.id),owned=new Set([run.id]);
  if(!Array.isArray(children)||children.length>8)throw new Error('Invalid owned child batch');
  for(const child of children){validateOwnedChild(child,run.id,session);if(owned.has(child.run.id))throw new Error('Owned child identity changed');owned.add(child.run.id);}
  for(const event of events)if(!owned.has(event.run_id))throw new Error('Unowned tree event');
  const histories={},operations=[];
  for(const child of children){const history=await client.ownedChildHistory(run.id,child.run.id);if(!Array.isArray(history))throw new Error('Invalid owned child history');histories[child.run.id]=history;}
  for(const id of owned){const batch=await client.operations(id);if(!Array.isArray(batch)||batch.some(value=>!value||value.run_id!==id))throw new Error('Owned operation identity changed');operations.push(...batch);}
  return {children,histories,events,operations,cursor,caughtUp};
}

// Public observation schemas only. Native owns topology, admission, effects and
// transitions. These validators never manufacture grants or metric totals.
function planFields(value,required,optional=[]){if(!value||typeof value!=='object'||Array.isArray(value)||required.some(field=>!Object.hasOwn(value,field))||Object.keys(value).some(field=>!required.includes(field)&&!optional.includes(field)))throw new Error('Invalid public planning metadata');}
function skillIds(ids){if(!Array.isArray(ids)||ids.length>8||new Set(ids).size!==ids.length||ids.some(id=>typeof id!=='string'||!id||new TextEncoder().encode(id).length>256||id==='.'||id==='..'||/[\/\\:\x00]/.test(id)))throw new Error('Choose at most eight distinct catalogue skills.');return ids;}
function validateSessionSkills(value,session){
  if(!value||typeof value!=='object'||Object.keys(value).length!==7||value.session_id!==session||typeof value.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(value.workspace_id)||typeof value.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(value.authority_id)||!Number.isSafeInteger(value.revision)||value.revision<0||typeof value.editable!=='boolean')throw new Error('Invalid backend session skill scope.');
  skillIds(value.ids);skillIds(value.manual_ids);if(value.manual_ids.some(id=>!value.ids.includes(id)))throw new Error('Invalid backend skill attachment provenance.');return value;
}
function validateSkillCatalogue(value){
  if(!value||typeof value.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(value.workspace_id)||typeof value.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(value.authority_id)||!Array.isArray(value.skills)||value.skills.length>64||new Set(value.skills.map(s=>s?.id)).size!==value.skills.length)throw new Error('Invalid backend skill catalogue.');
  for(const skill of value.skills){skillIds([skill?.id]);if(typeof skill.name!=='string'||new TextEncoder().encode(skill.name).length>1024||typeof skill.path!=='string'||!skill.path.startsWith('.agents/skills/')||typeof skill.model_invocable!=='boolean'||Object.keys(skill).some(key=>!['id','name','path','model_invocable','description','autoinvoke'].includes(key))||Object.hasOwn(skill,'description')&&typeof skill.description!=='string'||Object.hasOwn(skill,'autoinvoke')&&typeof skill.autoinvoke!=='boolean')throw new Error('Invalid backend skill catalogue entry.');}return value;
}
function agentIdentity(id){if(typeof id!=='string'||!id||id.length>64||!/^[A-Za-z0-9_.-]+$/.test(id))throw new Error('Invalid named agent identity.');return id;}
function validateAgentCatalogue(value){
  if(!value||typeof value!=='object'||Object.keys(value).length!==1||!Array.isArray(value.agents)||value.agents.length>64)throw new Error('Invalid backend agent catalogue.');
  const ids=new Set();for(const agent of value.agents){if(!agent||typeof agent!=='object'||Object.keys(agent).length!==3)throw new Error('Invalid backend agent metadata.');agentIdentity(agent.id);if(ids.has(agent.id)||!Number.isSafeInteger(agent.revision)||agent.revision<1||typeof agent.model_id!=='string'||agent.model_id.length>256||agent.model_id.startsWith('sk-'))throw new Error('Invalid or duplicate backend agent metadata.');ids.add(agent.id);}
  return value;
}
function validateSessionAgent(value,session,catalogue){
  if(!value||typeof value!=='object'||Object.keys(value).length!==4||value.session_id!==session||!Number.isSafeInteger(value.revision)||value.revision<0||typeof value.editable!=='boolean'||!(value.selected===null||value.selected&&typeof value.selected==='object'&&Object.keys(value.selected).length===3))throw new Error('Invalid backend conversation agent state.');
  if(value.selected){const selected=value.selected;agentIdentity(selected.id);if(!Number.isSafeInteger(selected.revision)||selected.revision<1||typeof selected.model_id!=='string'||selected.model_id.length>256||selected.model_id.startsWith('sk-'))throw new Error('Invalid selected backend agent metadata.');if(catalogue&&!catalogue.agents.some(item=>item.id===selected.id&&item.revision===selected.revision&&item.model_id===selected.model_id))throw new Error('Selected agent differs from its backend catalogue.');}
  return value;
}
class AgentSelectionController {
  constructor(client,post,scope){this.client=client;this.post=post;this.scope=scope;this.serial=0;}
  invalidate(){this.serial++;this.record=undefined;this.post({type:'agent-selection-clear'});}
  current(scope,serial){const now=this.scope();return serial===this.serial&&now.enabled&&now.session===scope.session&&now.generation===scope.generation;}
  async read(){
    const scope=this.scope(),serial=++this.serial;if(!scope.enabled||!scope.session){this.record=undefined;this.post({type:'agent-selection-clear'});return;}
    this.record=undefined;const catalogue=validateAgentCatalogue(await this.client.agentDefinitions());if(!this.current(scope,serial))return;
    const selection=validateSessionAgent(await this.client.sessionAgent(scope.session),scope.session);
    if(!this.current(scope,serial))return;
    if(selection.selected&&!catalogue.agents.some(item=>item.id===selection.selected.id&&item.revision===selection.selected.revision&&item.model_id===selection.selected.model_id))throw new Error('The selected agent revision is no longer in the current catalogue; clear or replace it.');
    this.record={scope,catalogue,selection};this.post({type:'agent-selection',catalogue,selection});return this.record;
  }
  async change(message){
    const observed=this.record,scope=this.scope();if(!observed||!scope.enabled||!observed.selection.editable||scope.session!==observed.scope.session||scope.generation!==observed.scope.generation||message.session!==scope.session||message.revision!==observed.selection.revision)throw new Error('Refresh this idle conversation before changing its agent.');
    const definition=message.agent_id===null?undefined:observed.catalogue.agents.find(item=>item.id===message.agent_id);if(message.agent_id!==null&&!definition)throw new Error('Choose an agent from the current catalogue.');
    const serial=++this.serial,selection=await this.client.replaceSessionAgent(scope.session,definition,observed.selection,observed.catalogue);if(!this.current(scope,serial))return;
    if(selection.revision!==observed.selection.revision+1||selection.selected?.id!==definition?.id||selection.selected?.revision!==definition?.revision)throw new Error('Backend agent acknowledgement changed the requested selection.');
    this.record={scope,catalogue:observed.catalogue,selection};this.post({type:'agent-selection',catalogue:observed.catalogue,selection});return this.record;
  }
}
class SkillViewController {
  constructor(client,post,scope){this.client=client;this.post=post;this.scope=scope;this.serial=0;}
  invalidate(){this.serial++;this.record=undefined;this.post({type:'skills-clear'});}
  current(scope,serial){const now=this.scope();return serial===this.serial&&now.enabled&&now.session===scope.session&&now.generation===scope.generation;}
  async read(){
    const scope=this.scope(),serial=++this.serial;if(!scope.enabled){this.record=undefined;this.post({type:'skills-clear'});return;}
    this.record=undefined;const selection=scope.session?await this.client.sessionSkills(scope.session):undefined;if(!this.current(scope,serial))return;
    let catalogue,catalogueError;try{catalogue=validateSkillCatalogue(await this.client.skills());}catch(error){if(!selection||error.status!==409)throw error;catalogue={workspace_id:selection.workspace_id,authority_id:selection.authority_id,skills:[]};catalogueError=error.message;}if(!this.current(scope,serial))return;
    if(selection&&(selection.workspace_id!==catalogue.workspace_id||selection.authority_id!==catalogue.authority_id))throw new Error('Workspace changed while reading skills. Refresh before editing.');
    this.record={scope,catalogue,selection,catalogueError};this.post({type:'skills',catalogue,selection,catalogueError});return this.record;
  }
  async change(message){
    const observed=this.record,scope=this.scope();if(!observed||!observed.selection||!scope.enabled||scope.session!==observed.scope.session||scope.generation!==observed.scope.generation||message.session!==scope.session||message.revision!==observed.selection.revision)throw new Error('Refresh the selected conversation skills before editing.');
    skillIds(message.ids);if(message.ids.some(id=>!observed.catalogue.skills.some(skill=>skill.id===id)))throw new Error('Choose skills from the current catalogue.');
    const serial=++this.serial;const selection=await this.client.replaceSessionSkills(scope.session,message.ids,observed.selection);if(!this.current(scope,serial))return;
    if(selection.workspace_id!==observed.catalogue.workspace_id||selection.authority_id!==observed.catalogue.authority_id||selection.revision!==observed.selection.revision+1||selection.ids.length!==message.ids.length||message.ids.some(id=>!selection.ids.includes(id))||selection.manual_ids.length!==message.ids.length||message.ids.some(id=>!selection.manual_ids.includes(id)))throw new Error('Backend skill acknowledgement changed scope or selection.');
    this.record={scope,catalogue:observed.catalogue,selection,catalogueError:observed.catalogueError};this.post({type:'skills',catalogue:observed.catalogue,selection,catalogueError:observed.catalogueError});return this.record;
  }
}
async function readCommittedEventStream(response,{root,session,scope,after,signal,onEvent,onObservation,authorizeEvent}){
  if(response.headers?.get('content-type')!=='text/event-stream'||!response.body?.getReader){try{await response.body?.cancel();}catch{}throw new Error('Invalid native event stream response');}
  const reader=response.body.getReader(),decoder=new TextDecoder('utf-8',{fatal:true});let pending='',cursor=after,observation,total=0;
  const detach=()=>{reader.cancel().catch(()=>{});};signal?.addEventListener('abort',detach,{once:true});
  const frame=async text=>{
    if(new TextEncoder().encode(text).byteLength>16*1024*1024+1024)throw new Error('Event stream frame exceeds its limit');
    const fields={data:[]};for(const line of text.split('\n')){
      if(!line||line.startsWith(':'))continue;const split=line.indexOf(':'),key=split<0?line:line.slice(0,split);let value=split<0?'':line.slice(split+1);if(value.startsWith(' '))value=value.slice(1);
      if(!['id','event','data'].includes(key))throw new Error('Invalid native event stream field');
      if(key==='data')fields.data.push(value);else {if(Object.hasOwn(fields,key))throw new Error('Duplicate native event stream field');fields[key]=value;}
    }
    if(!fields.data.length){if(fields.id!==undefined||fields.event!==undefined)throw new Error('Incomplete native event stream frame');return;}
    const data=JSON.parse(fields.data.join('\n'));signal?.throwIfAborted();
    if(fields.event==='observation'){
      planFields(data,['run','after','scope']);validateRunDTO(data.run);
      if(observation||fields.id!==undefined||data.run.id!==root||data.run.session_id!==session||data.scope!==scope||data.after!==after||scope!=='run'&&data.run.parent_id!==''||scope==='graph'&&!data.run.graph_root)throw new Error('Native event stream identity changed');
      observation=data;await onObservation?.(data);signal?.throwIfAborted();return;
    }
    if(!observation)throw new Error('Native event stream observation is missing');
    if(fields.event==='committed'){
      planFields(data,['seq','run_id','kind','data']);planInteger(data.seq,1);executionIdentity(data.run_id);
      if(fields.id!==String(data.seq)||data.seq<=cursor||typeof data.kind!=='string'||!data.kind.length||data.kind.length>256)throw new Error('Invalid committed event cursor or payload');
      if(scope==='run'&&data.run_id!==root||data.run_id!==root&&!await authorizeEvent?.(data))throw new Error('Unowned committed stream event');
      signal?.throwIfAborted();await onEvent(data,observation);cursor=data.seq;signal?.throwIfAborted();return;
    }
    if(fields.event==='end'){
      planFields(data,['reason','after']);if(fields.id!==undefined||data.after!==cursor||!['terminal','reconnect','reauthenticate','interrupted'].includes(data.reason))throw new Error('Invalid event stream end cursor');
      return {reason:data.reason,cursor,observation};
    }
    throw new Error('Unknown native event stream frame');
  };
  try{
    while(true){
      signal?.throwIfAborted();let chunk;try{chunk=await reader.read();}catch(error){if(error?.name==='TypeError'||error?.name==='TimeoutError')error.eventTransportUnavailable=true;throw error;}signal?.throwIfAborted();
      if(chunk.done){pending+=decoder.decode();const error=new Error('Native event stream ended without a complete end frame');if(!pending.trim())error.eventTransportUnavailable=true;throw error;}
      if(!ArrayBuffer.isView(chunk.value))throw new Error('Invalid event stream bytes');total+=chunk.value.byteLength;if(total>64*1024*1024)throw new Error('Event stream exceeds its connection limit');
      pending+=decoder.decode(chunk.value,{stream:true});pending=pending.replace(/\r\n/g,'\n');
      let end;while((end=pending.indexOf('\n\n'))!==-1){if(end>16*1024*1024+1024)throw new Error('Event stream frame exceeds its limit');const value=await frame(pending.slice(0,end));pending=pending.slice(end+2);if(value)return value;}
      if(pending.length>16*1024*1024+1024)throw new Error('Event stream frame exceeds its limit');
    }
  }catch(error){if(error&&typeof error==='object')error.eventCursor=cursor;throw error;}
  finally{signal?.removeEventListener('abort',detach);try{await reader.cancel();}catch{}reader.releaseLock();}
}
class EventStreamSubscription {
  constructor({current,onEvent,onObservation,onEnd,onError,schedule=setTimeout,unschedule=clearTimeout}){
    if(typeof current!=='function'||typeof onEvent!=='function')throw new Error('Invalid event subscription callbacks');
    this.current=current;this.onEvent=onEvent;this.onObservation=onObservation;this.onEnd=onEnd;this.onError=onError;this.schedule=schedule;this.unschedule=unschedule;this.epoch=0;this.disposed=false;
  }
  matches(pin){return this.pin&&this.pin.client===pin.client&&this.pin.origin===pin.client.baseUrl&&this.pin.root===pin.root&&this.pin.session===pin.session&&this.pin.scope===pin.scope&&this.pin.generation===pin.generation;}
  valid(pin,epoch){return !this.disposed&&this.pin===pin&&this.epoch===epoch&&pin.client.baseUrl===pin.origin&&this.current(pin);}
  watch(value){
    if(this.disposed)return;executionIdentity(value.root);executionIdentity(value.session);planInteger(value.after??0);
    if(!value.client||typeof value.client.eventStream!=='function'||!['run','tree','graph'].includes(value.scope)||!Number.isSafeInteger(value.generation)||value.generation<0)throw new Error('Invalid event subscription binding');
    if(this.matches(value))return;
    this.stop();const pin=Object.freeze({...value,origin:backendOrigin(value.client.baseUrl),after:value.after??0});if(!this.current(pin))return;
    this.pin=pin;this.cursor=pin.after;this.failedAttempts=0;this.finished=false;this.connect(pin,this.epoch);
  }
  stop(){this.epoch++;this.unschedule(this.timer);this.timer=undefined;this.abort?.abort();this.abort=undefined;this.pin=undefined;}
  dispose(){this.stop();this.disposed=true;}
  async connect(pin,epoch){
    if(!this.valid(pin,epoch)||this.finished)return;const abort=new AbortController();this.abort=abort;
    try{
      const consume=async(callback,...values)=>{try{return await callback?.(...values);}catch(error){const rejected=new Error('Event observer rejected delivery',{cause:error});rejected.eventConsumerFailure=true;throw rejected;}};
      const result=await pin.client.eventStream(pin.root,{scope:pin.scope,session_id:pin.session,after:this.cursor,signal:abort.signal,onObservation:async value=>{if(!this.valid(pin,epoch))throw new Error('Event subscription retired');await consume(this.onObservation,value,pin);if(!this.valid(pin,epoch))throw new Error('Event subscription retired');},onEvent:async event=>{if(!this.valid(pin,epoch))throw new Error('Event subscription retired');await consume(this.onEvent,event,pin);if(!this.valid(pin,epoch))throw new Error('Event subscription retired');this.cursor=event.seq;this.failedAttempts=0;}});
      if(!this.valid(pin,epoch))return;
      if(!result||result.cursor!==this.cursor)throw new Error('Event subscription acknowledgement changed');
      if(result.reason==='terminal'){this.finished=true;await this.onEnd?.(result,pin);return;}
      if(result.reason==='reauthenticate'){this.finished=true;const error=new Error('Reconnect this profile to continue event observation');error.status=401;this.notifyError(error,pin);return;}
      if(result.reason!=='reconnect'){this.finished=true;this.notifyError(new Error('Native event observation was interrupted; reconnect this profile'),pin);return;}
      this.retry(pin,epoch,0);
    }catch(error){
      if(!this.valid(pin,epoch)||abort.signal.aborted)return;
      // Retry only bounded transport unavailability. Protocol/identity and
      // consumer failures stop explicitly; a cursor cannot be silently skipped.
      if(!this.finished&&!error.eventConsumerFailure&&(error.status===503||error.eventTransportUnavailable===true)){
        this.failedAttempts++;if(this.failedAttempts<=3){this.retry(pin,epoch,Math.min(4000,500*2**(this.failedAttempts-1)));return;}
      }
      this.finished=true;this.notifyError(error,pin);
    }finally{if(this.abort===abort)this.abort=undefined;}
  }
  retry(pin,epoch,delay){if(!this.valid(pin,epoch))return;this.timer=this.schedule(()=>{this.timer=undefined;if(this.valid(pin,epoch))this.connect(pin,epoch);},delay);}
  notifyError(error,pin){try{Promise.resolve(this.onError?.(error,pin)).catch(()=>{});}catch{} }
}
function executionIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(value))throw new Error('Invalid execution identity');return value;}
function providerCallIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.:-]{1,256}$/.test(value))throw new Error('Invalid provider call identity');return value;}
function planLabel(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.-]{1,32}$/.test(value))throw new Error('Invalid planning node label');return value;}
function planInteger(value,minimum=0){if(!Number.isSafeInteger(value)||value<minimum)throw new Error('Invalid planning revision or counter');return value;}
function validateContextRequest(value,id){
  planFields(value,['id','state']);executionIdentity(value.id);if(id!==undefined&&value.id!==id)throw new Error('Context request identity changed');
  if(!['pending','claimed','completed','failed','interrupted'].includes(value.state))throw new Error('Invalid context request state');return value;
}
function validateContextObservation(value,session,model){
  planFields(value,['session_id','model_id','enabled','automatic','head_revision','source_watermark','manual','checkpoint']);executionIdentity(value.session_id);profileIdentity(value.model_id);
  if(value.session_id!==session||(model!==undefined&&value.model_id!==model))throw new Error('Context conversation/model identity changed');
  for(const key of ['enabled','automatic'])if(typeof value[key]!=='boolean')throw new Error('Invalid context capability');
  planInteger(value.head_revision);planInteger(value.source_watermark);
  if(value.manual!==null)validateContextRequest(value.manual);
  if(value.checkpoint!==null){const c=value.checkpoint;planFields(c,['id','provider_elapsed_ms','preparation_elapsed_ms','usage']);executionIdentity(c.id);planInteger(c.provider_elapsed_ms);if(c.preparation_elapsed_ms!==null)planInteger(c.preparation_elapsed_ms);if(c.usage!==null)planUsage(c.usage);}
  return value;
}
function validateGraphContext(value,run){
  planFields(value,['enabled','resumable','remaining_active_ms']);for(const key of ['enabled','resumable'])if(typeof value[key]!=='boolean')throw new Error('Invalid graph context capability');
  if(value.remaining_active_ms!==null)planInteger(value.remaining_active_ms);
  if(value.resumable&&(!value.enabled||run?.state!=='paused'||value.remaining_active_ms===null||value.remaining_active_ms<1))throw new Error('Graph has no ready closed context owner');
  if(!value.enabled&&(value.resumable||value.remaining_active_ms!==null))throw new Error('Disabled graph context cannot own an allowance');return value;
}
// Thin observation/control helper shared by browser and VS Code hosts. It never
// compacts locally, edits history, retries a mutation or estimates token usage.
class ContextViewController {
  constructor(client,post,selection,admission=async()=>undefined,id=()=>globalThis.crypto.randomUUID().replaceAll('-','')){this.client=client;this.post=post;this.selection=selection;this.admission=admission;this.id=id;this.epoch=0;this.readOrdinal=0;}
  invalidate(){clearTimeout(this.timer);this.timer=undefined;this.epoch++;this.record=undefined;this.request=undefined;this.post({type:'context-clear'});}
  dispose(){this.disposed=true;this.invalidate();}
  current(pin,epoch){const now=this.selection();return !this.disposed&&epoch===this.epoch&&now.enabled===true&&now.session===pin.session&&now.model===pin.model&&now.generation===pin.generation;}
  async read(){
    const pin=this.selection(),epoch=this.epoch,ordinal=++this.readOrdinal,current=()=>this.current(pin,epoch)&&ordinal===this.readOrdinal;
    if(!pin.enabled||!pin.session||!pin.model){if(this.record)this.invalidate();return;}
    try{
      let value=await this.client.context(pin.session,pin.model);if(!current())return;
      if(this.request?.session===pin.session&&this.request.model===pin.model){
        const state=await this.client.contextRequest(pin.session,this.request.id,pin.model);if(!current())return;
        if(['completed','failed','interrupted'].includes(state.state)){value=await this.client.context(pin.session,pin.model);if(!current())return;this.request=undefined;}
        if(value.manual===null||value.manual.id===state.id)value={...value,manual:state};
      }
      this.record=value;this.post({type:'context',record:value});clearTimeout(this.timer);
      if(value.manual&&['pending','claimed'].includes(value.manual.state))this.timer=setTimeout(()=>this.read().catch(error=>{if(this.current(pin,epoch))this.post({type:'error',text:error.message});}),500);
      return value;
    }catch(error){if(!current())return;if(error.status===409)this.invalidate();throw error;}
  }
  async compact(message){
    const pin=this.selection(),epoch=this.epoch,observed=this.record;
    if(!this.current(pin,epoch)||!observed?.enabled||message.session!==pin.session||message.model!==pin.model||message.expected_head_revision!==observed.head_revision)throw new Error('Refresh the current model context before compacting.');
    const fresh=await this.read();if(!this.current(pin,epoch)||!fresh)return;
    if(!fresh.enabled||fresh.head_revision!==message.expected_head_revision||fresh.manual&&['pending','claimed'].includes(fresh.manual.state))throw new Error('Context changed or already has a request. Review the current observation.');
    const binding=await this.admission();if(!this.current(pin,epoch))return;
    const id=this.id();executionIdentity(id);
    const result=await this.client.compactContext(pin.session,id,message.expected_head_revision,pin.model,binding);if(!this.current(pin,epoch))return;
    this.request={session:pin.session,model:pin.model,id:result.id};await this.read();
  }
}
function planText(value,limit,empty=false){if(typeof value!=='string'||(!empty&&!value.length)||value.includes('\0')||new TextEncoder().encode(value).length>limit)throw new Error('Invalid planning text');}
function planPreconditions(revision,sequence){planInteger(revision,1);planInteger(sequence,1);}
function validatePlanInputText(source){
  planText(source,16384);let position=0;
  const invalid=()=>{throw new Error('Plan input requires strict JSON without duplicate fields');};
  const whitespace=()=>{while(/[\x20\t\r\n]/.test(source[position]||'!'))position++;};
  const unicode=value=>{for(let i=0;i<value.length;i++){const unit=value.charCodeAt(i);if(unit>=0xd800&&unit<=0xdbff){const next=value.charCodeAt(++i);if(!(next>=0xdc00&&next<=0xdfff))invalid();}else if(unit>=0xdc00&&unit<=0xdfff)invalid();}return value;};
  function string(){if(source[position]!=='"')invalid();const start=position++;let escaped=false;while(position<source.length){const character=source[position++];if(character==='"'&&!escaped){try{return unicode(JSON.parse(source.slice(start,position)));}catch{invalid();}}if(character==='\\'&&!escaped)escaped=true;else escaped=false;}invalid();}
  function value(depth){if(depth>64)invalid();whitespace();const character=source[position];
    if(character==='{'){position++;whitespace();const keys=new Set();if(source[position]==='}'){position++;return;}for(;;){const key=string();if(keys.has(key))invalid();keys.add(key);whitespace();if(source[position++]!==':')invalid();value(depth+1);whitespace();const next=source[position++];if(next==='}')return;if(next!==',')invalid();whitespace();}}
    if(character==='['){position++;whitespace();if(source[position]===']'){position++;return;}for(;;){value(depth+1);whitespace();const next=source[position++];if(next===']')return;if(next!==',')invalid();}}
    if(character==='"'){string();return;}
    for(const literal of ['true','false','null'])if(source.startsWith(literal,position)){position+=literal.length;return;}
    const number=source.slice(position).match(/^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?/);if(!number)invalid();position+=number[0].length;
  }
  whitespace();if(source[position]!=='{')throw new Error('Plan input must be a JSON object');value(0);whitespace();if(position!==source.length)invalid();return source;
}
function validateRunDTO(value){
  planFields(value,['id','session_id','state','parent_id','node_id','graph_root'],['provider_context']);executionIdentity(value.id);executionIdentity(value.session_id);
  if(value.parent_id!=='')executionIdentity(value.parent_id);if(value.node_id!=='')executionIdentity(value.node_id);if(typeof value.graph_root!=='boolean'||!['queued','running','paused','completed','failed','cancelled'].includes(value.state))throw new Error('Invalid actual run metadata');
  if(value.provider_context!==undefined){planFields(value.provider_context,['profile_id','route_id','provider','wire','model_id','profile_revision']);for(const key of ['profile_id','route_id','provider','model_id'])profileIdentity(value.provider_context[key]);if(!['chat-completions','responses','anthropic-messages','gemini-generate-content'].includes(value.provider_context.wire))throw new Error('Invalid public provider identity');planInteger(value.provider_context.profile_revision,1);}
  return value;
}
function validatePlanOwner(value,root){validateRunDTO(value);if(value.id!==root||value.parent_id!==''||value.node_id!==''||value.graph_root!==false)throw new Error('Planning root ownership changed');return value;}
function validatePlanning(value){planFields(value,['enabled','tools']);if(typeof value.enabled!=='boolean'||!Array.isArray(value.tools)||JSON.stringify(value.tools)!==JSON.stringify(value.enabled?['inspect_plan','plan_tasks','revise_plan']:[]))throw new Error('Invalid planning capability');return value;}
function validateOwnedChild(child,root,session){
  const value=child?.run;
  if(!value||value.parent_id!==root||value.session_id!==session||value.graph_root||typeof value.id!=='string'||typeof child.batch_id!=='string'||typeof child.task_id!=='string'||typeof child.preset_id!=='string')throw new Error('Owned child identity changed');
  executionIdentity(value.id);planInteger(child.preset_revision,1);
  if(child.kind==='delegated_leaf'){if(!child.batch_id||!child.task_id||child.preset_id!=='workspace.inspect')throw new Error('Owned child identity changed');}
  else if(child.kind==='dynamic_agent'){
    planFields(child,['run','kind','batch_id','task_id','preset_id','preset_revision','plan_id','node_label','claim_id','definition_revision','claim_revision']);validateRunDTO(value);executionIdentity(child.plan_id);executionIdentity(child.claim_id);executionIdentity(value.node_id);planLabel(child.node_label);planInteger(child.definition_revision,1);
    if(child.claim_revision!==child.definition_revision||child.batch_id!==''||child.task_id!==''||!['workspace.inspect','workspace.coding'].includes(child.preset_id))throw new Error('Owned dynamic child claim changed');
  }else throw new Error('Owned child identity changed');
  return child;
}
function planUsage(value){
  const numbers=['input_tokens','output_tokens','total_tokens','prompt_tokens','completion_tokens','cached_tokens','reasoning_tokens','cache_read_input_tokens','cache_creation_input_tokens','promptTokenCount','candidatesTokenCount','totalTokenCount','thoughtsTokenCount','cachedContentTokenCount'];
  const nested=['input_tokens_details','output_tokens_details','prompt_tokens_details','completion_tokens_details'];planFields(value,[],[...numbers,...nested]);
  for(const [key,count] of Object.entries(value))if(numbers.includes(key))planInteger(count);else {planFields(count,[],['cached_tokens','reasoning_tokens','audio_tokens','accepted_prediction_tokens','rejected_prediction_tokens']);for(const number of Object.values(count))planInteger(number);}
}
function planResponse(value){planFields(value,[],['model','usage','elapsed_ms','first_token_ms']);if(value.model!==undefined)planText(value.model,256,true);if(value.usage!==undefined)planUsage(value.usage);for(const key of ['elapsed_ms','first_token_ms'])if(value[key]!==undefined)planInteger(value[key]);}
function planDefinition(value,runtime=false){
  const fields=['id','type','depends_on'];if(value?.type==='agent')fields.push('objective','preset');else if(value?.type==='human')fields.push('question');else throw new Error('Invalid planning node type');
  if(runtime)fields.push('backend_node_id','definition_revision','state','protected','effect_state',...(value.type==='agent'?['preset_revision']:[]));
  planFields(value,fields,runtime?['claim_revision','claim_id','child_run_id','child_state','human_request_id','outcome_json','settled_event_seq']:[]);planLabel(value.id);
  if(value.type==='agent'){planText(value.objective,8192);if(!['workspace.inspect','workspace.coding'].includes(value.preset))throw new Error('Invalid planning preset');}else planText(value.question,8192);
  if(!Array.isArray(value.depends_on)||value.depends_on.length>32)throw new Error('Invalid planning dependencies');const unique=new Set();for(const edge of value.depends_on){planFields(edge,['task','require']);planLabel(edge.task);if(unique.has(edge.task)||edge.task===value.id||!['success','observed'].includes(edge.require))throw new Error('Invalid planning dependency');unique.add(edge.task);}
  if(runtime){executionIdentity(value.backend_node_id);planInteger(value.definition_revision,1);if(typeof value.protected!=='boolean'||!['pending','blocked','claimed','waiting_human','settled','skipped','cancelled','uncertain'].includes(value.state)||!['none','succeeded','failed','cancelled','uncertain'].includes(value.effect_state))throw new Error('Invalid actual planning node state');if(value.type==='agent')planInteger(value.preset_revision,1);
    if(value.claim_revision!==undefined){if(value.claim_revision!==value.definition_revision)throw new Error('Planning claim revision changed');executionIdentity(value.claim_id);}else if(value.claim_id!==undefined)throw new Error('Planning claim is unbound');
    for(const key of ['child_run_id','human_request_id'])if(value[key]!==undefined)executionIdentity(value[key]);if(value.child_state!==undefined&&!['queued','running','paused','completed','failed','cancelled'].includes(value.child_state))throw new Error('Invalid planning child state');if(value.settled_event_seq!==undefined)planInteger(value.settled_event_seq,1);
    if(value.outcome_json!==undefined){planText(value.outcome_json,32768);const outcome=JSON.parse(value.outcome_json);if(!outcome||typeof outcome!=='object'||Array.isArray(outcome))throw new Error('Invalid actual planning outcome');}
  }
}
function validatePlanObservation(value,root){
  planFields(value,['run','enabled','plan','questions','revisions','calls','policy','budget']);validatePlanOwner(value.run,root);if(typeof value.enabled!=='boolean')throw new Error('Invalid planning capability');
  if(value.policy!==null){const policy=value.policy;planFields(policy,['revision','catalogue_finalized','limits','presets']);planInteger(policy.revision,1);if(typeof policy.catalogue_finalized!=='boolean'||!Array.isArray(policy.presets)||policy.presets.length>2)throw new Error('Invalid captured planning policy');planFields(policy.limits,['max_nodes','max_revisions','max_humans','change_bytes','human_expiry_ms','max_parent_turns']);for(const counter of Object.values(policy.limits))planInteger(counter,1);const ids=new Set();for(const preset of policy.presets){planFields(preset,['id','revision','readonly','turn_limit']);if(!['workspace.inspect','workspace.coding'].includes(preset.id)||ids.has(preset.id)||preset.readonly!==(preset.id==='workspace.inspect'))throw new Error('Invalid captured planning preset');ids.add(preset.id);planInteger(preset.revision,1);planInteger(preset.turn_limit,1);}}
  if(value.budget!==null){const budget=value.budget;planFields(budget,['policy_id','policy_revision','revision','max_children','max_parallel','max_model_calls','wall_limit_ms','children_admitted','planned_children_reserved','model_calls_reserved','parent_calls_held','parent_model_calls_reserved']);if(budget.policy_id!=='native.dynamic-plan')throw new Error('Invalid captured planning budget');for(const [key,counter] of Object.entries(budget))if(key!=='policy_id')planInteger(counter,['children_admitted','planned_children_reserved','model_calls_reserved','parent_calls_held','parent_model_calls_reserved'].includes(key)?0:1);}
  if(!Array.isArray(value.questions)||value.questions.length>8||!Array.isArray(value.revisions)||value.revisions.length>16||!Array.isArray(value.calls)||value.calls.length>16)throw new Error('Invalid planning ledgers');
  if(value.plan===null){if(value.questions.length||value.revisions.length||value.calls.length)throw new Error('Planning ledger lacks its owner');return value;}
  const plan=value.plan;planFields(plan,['id','root_run_id','revision','state_sequence','state','ready','blocked','claimed','waiting_human','finished','report_ready','halted','retired_labels','planned_children_reserved','planned_humans_reserved','humans_published','nodes']);executionIdentity(plan.id);planPreconditions(plan.revision,plan.state_sequence);
  if(plan.root_run_id!==root||!value.policy||!value.budget||!['active','waiting_human','completed','failed','cancelled','uncertain','interrupted'].includes(plan.state)||!Array.isArray(plan.nodes)||!plan.nodes.length||plan.nodes.length>32)throw new Error('Planning owner changed');
  for(const key of ['finished','report_ready','halted'])if(typeof plan[key]!=='boolean')throw new Error('Invalid planning readiness');for(const key of ['planned_children_reserved','planned_humans_reserved','humans_published'])planInteger(plan[key]);
  const nodes=new Map();for(const node of plan.nodes){planDefinition(node,true);if(nodes.has(node.id)||node.definition_revision>plan.revision)throw new Error('Planning node revision changed');nodes.set(node.id,node);}
  for(const node of nodes.values())for(const dependency of node.depends_on)if(!nodes.has(dependency.task))throw new Error('Planning dependency is unowned');
  for(const key of ['ready','blocked','claimed','waiting_human','retired_labels']){if(!Array.isArray(plan[key])||new Set(plan[key]).size!==plan[key].length||plan[key].some(label=>!nodes.has(label)))throw new Error('Invalid planning readiness labels');}
  const questions=new Set();for(const question of value.questions){planFields(question,['id','plan_id','root_run_id','label','backend_node_id','question','state','definition_revision','published_event_seq','expires_unix_ms','input_json','input_event_seq']);executionIdentity(question.id);planLabel(question.label);planText(question.question,8192);planText(question.input_json,16384,true);for(const key of ['definition_revision','published_event_seq','expires_unix_ms'])planInteger(question[key],1);if(question.input_event_seq!==null)planInteger(question.input_event_seq,1);const node=nodes.get(question.label);if(questions.has(question.id)||question.plan_id!==plan.id||question.root_run_id!==root||!node||node.type!=='human'||node.backend_node_id!==question.backend_node_id||node.definition_revision!==question.definition_revision||node.human_request_id!==question.id||node.question!==question.question||!['waiting','answered','expired','cancelled'].includes(question.state))throw new Error('Planning question ownership changed');questions.add(question.id);}
  const calls=new Map();let revision=0;for(const call of value.calls){planFields(call,['id','plan_id','root_run_id','provider_tool_call_id','origin_attempt_id','name','state','accepted_revision','accepted_event_seq','result_event_seq','conversation_commit_seq','continuation_attempt_id','pending'],['response']);for(const key of ['id','plan_id','root_run_id','origin_attempt_id'])executionIdentity(call[key]);providerCallIdentity(call.provider_tool_call_id);if(call.continuation_attempt_id!=='')executionIdentity(call.continuation_attempt_id);planInteger(call.accepted_revision,1);planInteger(call.accepted_event_seq,1);for(const key of ['result_event_seq','conversation_commit_seq'])if(call[key]!==null)planInteger(call[key],1);
    if(calls.has(call.id)||call.plan_id!==plan.id||call.root_run_id!==root||call.accepted_revision<=revision||call.accepted_revision>plan.revision||!['plan_tasks','revise_plan'].includes(call.name)||!['accepted','report_ready','turn_committed','interrupted','cancelled'].includes(call.state)||call.pending!==(['accepted','report_ready'].includes(call.state)&&call.conversation_commit_seq===null)||Object.hasOwn(call,'response')!==call.pending)throw new Error('Planning call ownership changed');if(call.pending)planResponse(call.response);calls.set(call.id,call);revision=call.accepted_revision;}
  revision=0;for(const saved of value.revisions){planFields(saved,['plan_id','revision','accepted_event_seq','call_id','spec']);planInteger(saved.revision,1);planInteger(saved.accepted_event_seq,1);const call=calls.get(saved.call_id);if(saved.plan_id!==plan.id||saved.revision<=revision||!call||call.accepted_revision!==saved.revision||call.accepted_event_seq!==saved.accepted_event_seq)throw new Error('Planning revision ownership changed');planFields(saved.spec,['nodes']);if(!Array.isArray(saved.spec.nodes)||!saved.spec.nodes.length||saved.spec.nodes.length>32)throw new Error('Invalid accepted topology');const labels=new Set();for(const node of saved.spec.nodes){planDefinition(node);if(labels.has(node.id))throw new Error('Duplicate accepted planning label');labels.add(node.id);}for(const node of saved.spec.nodes)for(const edge of node.depends_on)if(!labels.has(edge.task))throw new Error('Unowned accepted dependency');revision=saved.revision;}
  if(calls.size!==value.revisions.length||revision!==plan.revision)throw new Error('Planning revision ledger changed');return value;
}

class McpAuthorizationController {
  constructor(client,post,{current=()=>true,save=()=>{},requestId=()=>globalThis.crypto.randomUUID(),schedule=(fn)=>setTimeout(fn,1000),unschedule=clearTimeout}={}){
    this.client=client;this.post=post;this.current=current;this.save=save;this.requestId=requestId;this.schedule=schedule;this.unschedule=unschedule;this.attempts=new Map();this.servers=[];this.epoch=0;this.available=false;this.busy=false;this.error='';
  }
  active(epoch=this.epoch){return !this.disposed&&epoch===this.epoch&&this.current();}
  pending(value){return !value||['discovering','awaiting_callback','exchanging'].includes(value.state);}
  present(){if(this.active())this.post({type:'mcp-authorization',available:this.available,servers:this.servers,attempts:[...this.attempts.values()].filter(entry=>entry.value).map(entry=>entry.value),busy:this.busy||!!this.loading,error:this.error});}
  persist(){return this.save([...this.attempts.values()].filter(entry=>this.pending(entry.value)).map(entry=>({...entry.binding})));}
  arm(){this.unschedule(this.timer);this.timer=undefined;if(this.active()&&this.available&&[...this.attempts.values()].some(entry=>this.pending(entry.value)))this.timer=this.schedule(()=>{this.timer=undefined;this.read().catch(()=>{});});}
  dispose(){this.disposed=true;this.epoch++;this.unschedule(this.timer);this.timer=undefined;this.attempts.clear();}
  async read(saved){
    const epoch=this.epoch;if(!this.active(epoch)||this.loading)return;if(this.busy){this.arm();return;}this.loading=true;
    try{
      const result=await this.client.mcpAuthorizationServers();if(!this.active(epoch))return;this.servers=result.servers;this.available=true;this.error='';
      if(saved!==undefined){if(!Array.isArray(saved)||saved.length>16)throw new Error('Invalid saved MCP login observation');for(const binding of saved){mcpFields(binding,['id','server_id','config_revision','credential_revision']);executionIdentity(binding.id);mcpServerIdentity(binding.server_id);planInteger(binding.config_revision,1);planInteger(binding.credential_revision);if(this.attempts.has(binding.server_id))throw new Error('Duplicate saved MCP login observation');if(this.servers.some(server=>server.id===binding.server_id&&server.config_revision===binding.config_revision))this.attempts.set(binding.server_id,{binding:{...binding}});}}
      for(const [serverId,entry] of [...this.attempts]){
        if(!this.servers.some(server=>server.id===serverId&&server.config_revision===entry.binding.config_revision)){this.attempts.delete(serverId);continue;}
        if(!this.pending(entry.value))continue;
        try{const value=await this.client.mcpAuthorization(entry.binding.id,entry.binding);if(!this.active(epoch))return;entry.value=value;}
        catch(error){if(!this.active(epoch))return;if(error.status===404){this.attempts.delete(serverId);this.error='The backend no longer has this login attempt. Start a new login if needed.';}else throw error;}
      }
      await this.persist();if(this.active(epoch))this.present();
    }catch(error){if(!this.active(epoch))return;if(error.status===404){this.available=false;this.servers=[];this.error='MCP login setup is unavailable on this backend.';}else this.error=error.message;this.present();}
    finally{this.loading=false;if(this.active(epoch)){this.present();this.arm();}}
  }
  async start(serverId,renewal=false){
    mcpServerIdentity(serverId);if(!this.active()||this.busy)return;if(this.loading)throw new Error('MCP settings are refreshing. Try again.');const server=this.servers.find(item=>item.id===serverId);
    if(!server||!server.enabled||!server.configured||(renewal?server.credential_revision<1||!['authorized','needs_login'].includes(server.state):server.state!=='needs_login')||this.pending(this.attempts.get(serverId)?.value)&&this.attempts.has(serverId))throw new Error(renewal?'Refresh MCP settings and select a server with an existing grant.':'Refresh MCP settings and select a server requiring login.');
    const epoch=this.epoch,binding={id:this.requestId(),server_id:server.id,config_revision:server.config_revision,credential_revision:server.credential_revision};executionIdentity(binding.id);this.busy=true;this.error='';this.attempts.set(serverId,{binding});this.present();
    try{
      await this.persist();if(!this.active(epoch))return;
      const value=await (renewal?this.client.renewMcpAuthorization(serverId,binding.config_revision,binding.credential_revision,binding.id):this.client.startMcpAuthorization(serverId,binding.config_revision,binding.credential_revision,binding.id));if(!this.active(epoch))return;this.attempts.get(serverId).value=value;
    }catch(error){if(this.active(epoch)){this.error=error.message;if([400,401,403,404,409,429,503].includes(error.status))this.attempts.delete(serverId);}}
    finally{if(this.active(epoch)){this.busy=false;await this.persist();this.present();this.arm();}}
  }
  async cancel(serverId){
    mcpServerIdentity(serverId);const entry=this.attempts.get(serverId);if(!this.active()||this.busy||!entry||!this.pending(entry.value))return;if(this.loading)throw new Error('MCP settings are refreshing. Try again.');
    const epoch=this.epoch;this.busy=true;this.error='';this.present();
    try{const value=await this.client.cancelMcpAuthorization(entry.binding.id,entry.binding);if(this.active(epoch))entry.value=value;}
    catch(error){if(this.active(epoch))this.error=error.message;}
    finally{if(this.active(epoch)){this.busy=false;await this.persist();this.present();this.arm();}}
  }
  open(serverId,link){mcpServerIdentity(serverId);const value=this.attempts.get(serverId)?.value;if(!this.active()||!value||value.state!=='awaiting_callback'||value.cancellation_requested||Date.now()>=value.expires_unix_ms)throw new Error('Wait for the current MCP authorization link.');validateMcpAuthorizationAttempt(value);return link(value.authorization_url);}
}
function exactFields(value,fields){if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field)))throw new Error('Invalid provider profile metadata');}
function mcpFields(value,fields){if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field)))throw new Error('Invalid MCP authorization metadata');}
function mcpServerIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(value))throw new Error('Invalid MCP server identity');return value;}
function validateMcpAuthorizationServers(value){
  mcpFields(value,['servers']);if(!Array.isArray(value.servers)||value.servers.length>16)throw new Error('Invalid MCP authorization servers');
  const ids=new Set();for(const server of value.servers){
    mcpFields(server,['id','config_revision','credential_revision','enabled','configured','state','expires_unix_ms']);mcpServerIdentity(server.id);planInteger(server.config_revision,1);planInteger(server.credential_revision);
    if(ids.has(server.id)||typeof server.enabled!=='boolean'||typeof server.configured!=='boolean'||!['not_configured','disabled','authorized','needs_login','unavailable'].includes(server.state))throw new Error('Invalid MCP authorization server');
    if(server.expires_unix_ms!==null)planInteger(server.expires_unix_ms,1);ids.add(server.id);
  }return value;
}
function validateMcpAuthorizationAttempt(value,binding={}){
  mcpFields(value,['id','server_id','state','config_revision','credential_revision','authorization_url','expires_unix_ms','reason','cancellation_requested']);executionIdentity(value.id);mcpServerIdentity(value.server_id);planInteger(value.config_revision,1);planInteger(value.credential_revision);planInteger(value.expires_unix_ms,1);
  if(!['discovering','awaiting_callback','exchanging','connected','failed','denied','cancelled','expired'].includes(value.state)||typeof value.cancellation_requested!=='boolean')throw new Error('Invalid MCP authorization attempt');
  for(const field of ['id','server_id','config_revision'])if(binding[field]!==undefined&&value[field]!==binding[field])throw new Error('MCP authorization ownership changed');
  if(binding.credential_revision!==undefined&&(value.state==='connected'?value.credential_revision<=binding.credential_revision:value.credential_revision!==binding.credential_revision))throw new Error('MCP authorization credential revision changed');
  const reasons={failed:['backend_quiesced','configuration_or_credential_changed','protocol_rejected','authorization_failed','refresh_uncertain','refresh_recovery_required'],denied:['authorization_failed','access_denied','interaction_required','login_required','consent_required','temporarily_unavailable','server_error','invalid_request','unauthorized_client','unsupported_response_type','invalid_scope'],cancelled:['cancelled'],expired:['deadline_exceeded']};
  if(reasons[value.state]? !reasons[value.state].includes(value.reason):value.reason!==null)throw new Error('Invalid MCP authorization reason');
  if(value.state==='awaiting_callback'){
    if(typeof value.authorization_url!=='string'||value.authorization_url.length>16384||/[\u0000-\u0020\u007f]/.test(value.authorization_url))throw new Error('Invalid MCP authorization URL');
    const url=new URL(value.authorization_url);if(url.protocol!=='https:'||url.username||url.password||url.hash)throw new Error('Invalid MCP authorization URL');
  }else if(value.authorization_url!==null)throw new Error('Unexpected MCP authorization URL');
  return value;
}
function profileIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.:/-]{1,256}$/.test(value)||value.startsWith('sk-'))throw new Error('Invalid provider profile identity');return value;}
function profileRevision(value){if(!Number.isSafeInteger(value)||value<0)throw new Error('Invalid provider profile revision');}
function profileKey(value){if(value!==undefined&&(typeof value!=='string'||!/^[\x21-\x7e]{1,32768}$/.test(value)))throw new Error('Enter a provider API key without spaces');}
function profileBindingFields(binding){if(binding===undefined)return {};exactFields(binding,['provider_profile_id','expected_provider_revision']);if(binding.provider_profile_id!=='')profileIdentity(binding.provider_profile_id);profileRevision(binding.expected_provider_revision);return {...binding};}
function validateProviderProfiles(value,withRoutes=true){
  exactFields(value,withRoutes?['revision','active','profiles','routes']:['revision','active','profiles']);profileRevision(value.revision);
  if(typeof value.active!=='string'||!Array.isArray(value.profiles)||value.profiles.length>32)throw new Error('Invalid provider profile metadata');
  if(value.revision===0?(value.profiles.length!==0||value.active!==''):value.profiles.length===0)throw new Error('Invalid provider profile metadata');
  const ids=new Set();for(const profile of value.profiles){exactFields(profile,['id','route_id','provider','model','revision']);profileIdentity(profile.id);profileIdentity(profile.route_id);profileIdentity(profile.provider);if(profile.model!=='')profileIdentity(profile.model);profileRevision(profile.revision);if(profile.revision<1||profile.revision>value.revision||ids.has(profile.id))throw new Error('Invalid provider profile metadata');ids.add(profile.id);}
  if(value.active&&!ids.has(value.active))throw new Error('Invalid active provider profile');
  if(withRoutes){if(!Array.isArray(value.routes)||!value.routes.length||value.routes.length>64)throw new Error('Invalid provider profile routes');
    const routes=new Map();for(const route of value.routes){exactFields(route,['id','provider','wire','discovery']);profileIdentity(route.id);profileIdentity(route.provider);if(routes.has(route.id)||!['chat-completions','responses','anthropic-messages','gemini-generate-content'].includes(route.wire)||typeof route.discovery!=='boolean')throw new Error('Invalid provider profile routes');routes.set(route.id,route);}
    for(const profile of value.profiles)if(routes.get(profile.route_id)?.provider!==profile.provider)throw new Error('Provider profile route changed');
  }
  return value;
}

function providerEnrollmentWire(setup){
  if(!setup||setup.provider!=='openai'||!Number.isSafeInteger(setup.revision)||setup.revision<0)throw new Error('Backend provider setup policy is unsupported.');
  const wire=setup.wire??'chat-completions';
  if(!((wire==='chat-completions'&&setup.endpoint==='https://api.openai.com/v1/chat/completions')||(wire==='responses'&&setup.endpoint==='https://api.openai.com/v1/responses')))throw new Error('Backend provider setup policy is unsupported.');
  return wire;
}
// Thin setup controller shared by both hosts. The native service owns profile
// publication and execution; unsaved keys live only in this host's bounded draft.
// Saved-key discovery retains only model IDs and public profile/route revisions
// across conversation changes. A fresh native metadata read precedes every save;
// external revisions retire it, while our acknowledged model-only CAS rebases it.
class ProviderProfileController {
  constructor(client,post,guard=()=>true){this.client=client;this.post=post;this.guard=guard;this.epoch=0;this.metadataRead=0;}
  clearDraft(){clearTimeout(this.timer);if(this.draft)this.draft.key=undefined;this.draft=undefined;}
  invalidate({catalogue=false}={}){this.epoch++;this.clearDraft();if(catalogue)this.savedCatalogue=undefined;else if(!this.disposed&&this.guard()&&!this.models())this.configuredModels();}
  dispose(){this.disposed=true;this.invalidate({catalogue:true});}
  current(epoch){return !this.disposed&&this.epoch===epoch&&this.guard();}
  present(){if(this.state)this.post({type:'provider-profiles',profiles:this.state.profiles,routes:this.state.routes,active:this.state.active});}
  binding(id){const profile=this.state.profiles.find(value=>value.id===id),route=this.state.routes.find(value=>value.id===profile?.route_id);return {client:this.client,origin:this.client.baseUrl,id,revision:this.state.revision,active:this.state.active,profile_revision:profile?.revision,owned_route:profile?.route_id,provider:profile?.provider,wire:route?.wire,route_provider:route?.provider,route_discovery:route?.discovery};}
  bound(value,state=this.state){
    if(!value||!state||value.client!==this.client||value.origin!==this.client.baseUrl||value.revision!==state.revision||value.active!==state.active)return false;
    const profile=state.profiles.find(item=>item.id===value.id);
    if(value.profile_revision===undefined)return !profile;
    const route=state.routes.find(item=>item.id===profile?.route_id);
    return !!profile&&profile.revision===value.profile_revision&&profile.route_id===value.owned_route&&profile.provider===value.provider&&route?.wire===value.wire&&route?.provider===value.route_provider&&route?.discovery===value.route_discovery;
  }
  configuredModels(){if(!this.state||!this.current(this.epoch))return;const profile=this.state.profiles.find(value=>value.id===this.state.active);this.post({type:'model-list',models:profile?.model?[{id:profile.model}]:[],model:profile?.model||undefined});}
  async refresh(){
    if(typeof this.client.providerProfiles!=='function')return false;
    const epoch=this.epoch,read=++this.metadataRead;
    try{const state=await this.client.providerProfiles();if(this.current(epoch)&&read===this.metadataRead){
      const changed=this.state&&(state.revision!==this.state.revision||state.active!==this.state.active)||(this.draft&&!this.bound(this.draft,state))||(this.savedCatalogue&&!this.bound(this.savedCatalogue,state));
      if(changed){this.clearDraft();this.savedCatalogue=undefined;}
      this.state=state;this.present();if(changed){if(!this.models())this.configuredModels();this.post({type:'provider-wire',wire:this.wire(state.active)});}
    }return true;}
    catch(error){if(error.status===404)return false;throw error;}
  }
  wire(id){const profile=this.state?.profiles.find(value=>value.id===id);return this.state?.routes.find(value=>value.id===profile?.route_id)?.wire;}
  choice(){if(this.bound(this.draft))return this.draft;if(this.bound(this.savedCatalogue))return this.savedCatalogue;return undefined;}
  models(selected){const choice=this.choice();if(!choice||!this.current(this.epoch))return false;const profile=this.state.profiles.find(value=>value.id===choice.id);this.post({type:'model-list',models:choice.ids.map(id=>({id})),model:selected??profile?.model});return true;}
  async admission(){if(!this.state&&!await this.refresh())throw new Error('Provider profile admission requires profile metadata');if(this.disposed||!this.state||!this.guard())throw new Error('Backend changed before run admission');return {provider_profile_id:this.state.active,expected_provider_revision:this.state.revision};}
  async reconcileAdmission(binding,error){
    if(error?.status!==409||!binding)return false;
    const epoch=this.epoch;if(!await this.refresh()||!this.current(epoch))return false;
    if(this.state.revision===binding.expected_provider_revision&&this.state.active===binding.provider_profile_id)return false;
    this.invalidate({catalogue:true});this.present();const wire=this.wire(this.state.active);if(wire)this.post({type:'provider-wire',wire});return true;
  }
  async discover(key,id,route_id){
    if(!this.state&&!await this.refresh())return false;
    if(this.disposed||!this.state)return true;
    this.invalidate({catalogue:true});const epoch=this.epoch;
    try{
      if(!this.current(epoch))return true;
      if(id===undefined&&route_id===undefined&&key===undefined&&!this.state.active&&this.state.profiles.length){
        const text='Choose a saved provider below, then choose a model. Your saved key will be used.';
        this.configuredModels();this.post({type:'settings-state',busy:false,text});this.post({type:'status',text});return true;
      }
      id=id===undefined?this.state.active:id;
      const saved=this.state.profiles.find(value=>value.id===id);
      if(id&&!saved)throw new Error('Choose a saved profile or Add profile');
      route_id=route_id||saved?.route_id||this.state.routes.find(value=>value.id==='openai.responses')?.id||this.state.routes[0].id;
      const route=this.state.routes.find(value=>value.id===route_id&&value.discovery);
      if(!route||saved&&saved.provider!==route.provider)throw new Error('Choose an available route for this provider');
      if(!saved){if(!key)throw new Error('Enter a key for the new provider profile');id=route.provider+'-'+globalThis.crypto.randomUUID();}
      this.post({type:'settings-state',busy:true,text:'Fetching models from '+(route.provider==='anthropic'?'Claude':route.provider)+'…'});
      if(!this.current(epoch))throw new Error('Backend changed during provider setup');
      const captured=this.binding(id),savedCredential=!!saved&&key===undefined;
      const catalogue=await this.client.discoverProfileModels(id,savedCredential?saved.route_id:route_id,key,this.state.revision);
      if(!this.current(epoch))return true;
      if(!this.bound(captured))throw new Error('Provider settings changed. Fetch models again.');
      if(!catalogue.models.length)throw new Error('The provider returned no models');
      this.draft={...captured,route_id,key,savedCredential,ids:catalogue.models.map(value=>value.id),expires:key===undefined?Infinity:Date.now()+300000};key=undefined;
      if(savedCredential)this.savedCatalogue={...captured,route_id:saved.route_id,ids:[...this.draft.ids],expires:Infinity,savedCredential:true};
      this.models(saved?.model);this.post({type:'settings-state',busy:false,complete:true,text:'Choose a model below to save this profile'});
      if(this.draft.key!==undefined)this.timer=setTimeout(()=>{if(this.current(epoch)){this.invalidate();this.post({type:'settings-state',busy:false,text:'Unsaved key expired · fetch models again'});}},300000);
      return true;
    }catch(error){if(this.current(epoch))this.post({type:'settings-state',busy:false,text:error.message});throw error;}finally{key=undefined;}
  }
  async save(model){
    const draft=this.choice();if(!draft)return false;
    const epoch=this.epoch;
    if(Date.now()>draft.expires||!draft.ids.includes(model))throw new Error('Fetch models and select a returned model');
    if(!this.current(epoch))throw new Error('Backend changed during provider setup');
    if(!await this.refresh()){this.invalidate({catalogue:true});throw new Error('Provider profile metadata is unavailable. Reconnect before selecting.');}
    if(!this.current(epoch)||!this.bound(draft))throw new Error('Provider settings changed. Fetch models again before selecting.');
    let state;try{state=await this.client.saveProviderProfile(draft.id,draft.route_id,model,draft.key,draft.revision,true);}
    catch(error){if(error.status===409)await this.refresh();throw error;}
    draft.key=undefined;if(!this.current(epoch))return true;
    this.state=state;const saved=this.binding(draft.id),routeChanged=draft.profile_revision!==undefined&&(saved.owned_route!==draft.owned_route||saved.provider!==draft.provider||saved.wire!==draft.wire||saved.route_provider!==draft.route_provider||saved.route_discovery!==draft.route_discovery);
    this.draft=routeChanged?undefined:{...saved,route_id:draft.route_id,key:undefined,savedCredential:draft.savedCredential,ids:[...draft.ids],expires:Infinity};clearTimeout(this.timer);
    this.savedCatalogue=draft.savedCredential&&!routeChanged?{...saved,route_id:saved.owned_route,ids:[...draft.ids],expires:Infinity,savedCredential:true}:undefined;
    this.present();if(!this.models(model))this.configuredModels();this.post({type:'provider-wire',wire:this.wire(state.active)});return true;
  }
  async select(id){
    if(!this.state&&!await this.refresh())throw new Error('Update the backend to use provider profiles');
    if(this.disposed||!this.state)return;
    if(!this.state.profiles.some(profile=>profile.id===id))throw new Error('Choose a saved provider profile');
    this.invalidate({catalogue:true});const epoch=this.epoch;if(!this.current(epoch))throw new Error('Backend changed during provider setup');let state;
    try{state=await this.client.selectProviderProfile(id,this.state.revision);}catch(error){if(error.status===409&&this.current(epoch))await this.refresh();throw error;}
    if(!this.current(epoch))return;
    this.state=state;this.present();this.post({type:'provider-wire',wire:this.wire(id)});
    return true;
  }
}
if(typeof module!=='undefined'&&module.exports)module.exports={BackendClient,EventStreamSubscription,backendOrigin,validateToken,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanning,validatePlanObservation,validateOwnedChild,validatePlanInputText,validateContextObservation,validateContextRequest,validateGraphContext,ContextViewController,SkillViewController,validateSessionSkills,validateSkillCatalogue,AgentSelectionController,validateAgentCatalogue,validateSessionAgent,validateMcpAuthorizationServers,validateMcpAuthorizationAttempt,McpAuthorizationController};
else globalThis.XMindBackend={BackendClient,EventStreamSubscription,backendOrigin,validateToken,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanning,validatePlanObservation,validateOwnedChild,validatePlanInputText,validateContextObservation,validateContextRequest,validateGraphContext,ContextViewController,SkillViewController,validateSessionSkills,validateSkillCatalogue,AgentSelectionController,validateAgentCatalogue,validateSessionAgent,validateMcpAuthorizationServers,validateMcpAuthorizationAttempt,McpAuthorizationController};
