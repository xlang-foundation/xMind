'use strict';

// These events carry their display data. Durable conversation/tool/lifecycle
// changes still reconcile saved state; unknown kinds conservatively do too.
const streamOnlyEventKinds=new Set(['model.text','model.refusal','model.tool_delta','model.reasoning','model.usage','model.finish','model.done','process.output']);
function eventRequiresSnapshot(event){return !streamOnlyEventKinds.has(event.kind);}

// Queue following is a view selection intent, separate from session activity.
// Keep its epoch so a later explicit inspection cannot be replaced by a claim.
class QueuedRunSelection {
  constructor(){this.observed=new Map();}
  remember(item,session,epoch){
    if(this.observed.has(item.id))return;
    this.observed.set(item.id,{session,epoch});
    while(this.observed.size>128)this.observed.delete(this.observed.keys().next().value);
  }
  observe(items,session,epoch,selected){
    for(const item of items)if(item.state==='queued'){
      const prior=this.observed.get(item.id);
      // Initial session restoration can invalidate metadata before choosing any
      // run. There is no inspected root to retain in that case.
      if(selected===undefined&&prior?.session===session)prior.epoch=epoch;
      else this.remember(item,session,epoch);
    }
  }
  next(items,runs,selected,session,epoch){
    if(runs.some(run=>run.id===selected&&['queued','running','paused'].includes(run.state)))return;
    return items.find(item=>{const intent=this.observed.get(item.id);return item.state==='claimed'&&typeof item.run_id==='string'&&item.run_id&&item.run_id!==selected&&intent?.session===session&&intent.epoch===epoch;});
  }
  advance(id,session,from,to){
    this.observed.delete(id);
    for(const intent of this.observed.values())if(intent.session===session&&intent.epoch===from)intent.epoch=to;
  }
}

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
function validateEditInspection(proposal,inspection) {
  let plan;try{plan=JSON.parse(proposal.arguments_json);}catch{throw new Error('The saved file change proposal is invalid.');}
  const action=proposal.tool==='replace_file'?'update':plan.action;
  if(!['update','add','delete','move'].includes(action)||typeof plan.path!=='string'||(action==='move'&&typeof plan.destination_path!=='string'))throw new Error('The saved file change does not have a supported recovery shape.');
  const expectedPaths=action==='move'?[plan.path,plan.destination_path]:[plan.path],targetWorkspace=plan.target_workspace_id||plan.folder_id||proposal.workspace_id;
  const current=inspection?.operation,observed=inspection?.observed,paths=inspection?.paths;
  const pathsValid=Array.isArray(paths)&&paths.length===expectedPaths.length&&paths.every((entry,index)=>{
    if(!entry||entry.path!==expectedPaths[index]||typeof entry.exists!=='boolean')return false;
    if(!entry.exists)return entry.fingerprint===null;
    const fingerprint=entry.fingerprint;
    return !!fingerprint&&fingerprint.workspace_id===targetWorkspace&&typeof fingerprint.file_id==='string'&&fingerprint.file_id.length>0&&/^[a-f0-9]{64}$/.test(fingerprint.content_sha256)&&Number.isSafeInteger(fingerprint.size)&&fingerprint.size>=0&&fingerprint.size<=1048576;
  });
  const observedPresent=!!observed&&observed.workspace_id===targetWorkspace&&typeof observed.path==='string'&&typeof observed.file_id==='string'&&observed.file_id.length>0&&/^[a-f0-9]{64}$/.test(observed.content_sha256)&&Number.isSafeInteger(observed.size)&&observed.size>=0&&observed.size<=1048576;
  const observedAbsent=!!observed&&expectedPaths.includes(observed.path)&&observed.workspace_id===targetWorkspace&&observed.file_id===''&&observed.content_sha256===''&&observed.size===0;
  if(!current||current.id!==proposal.id||current.state!=='uncertain'||current.run_id!==proposal.run_id||current.workspace_id!==proposal.workspace_id||current.tool!==proposal.tool||current.arguments_json!==proposal.arguments_json||current.expires_unix_ms!==proposal.expires_unix_ms||
    inspection.action!==action||!pathsValid||(!observedPresent&&!observedAbsent)||!['before','after','different'].includes(inspection.match)||typeof inspection.same_file!=='boolean'||(action==='update'&&inspection.match!=='different'&&!inspection.same_file)||
    !Number.isSafeInteger(inspection.observed_unix_ms)||inspection.observed_unix_ms<=0||inspection.observed_unix_ms>8640000000000000||inspection.quarantine_released!==false)throw new Error('The inspection does not match the selected uncertain change. Refresh its current state.');
  return true;
}
class BackendClient {
  #tokenProvider;
  #workspaceGuard;
  #connectionAdmissionSupported=false;
  #connectionGraphAdmissionSupported=false;
  #connectionGraphReadSupported=false;
  #connectionGraphChildrenSupported=false;
  #connectionTreeStreamSupported=false;
  #connectionRunStreamSupported=false;
  #connectionModelsSupported=false;
  #connectionCommandsSupported=false;
  #connectionWorkspaceSupported=false;
  #connectionModelSelectionSupported=false;
  #connectionGraphControlSupported=false;
  #connectionOperationsSupported=false;
  #connectionHistorySupported=false;
  #connectionChildHistorySupported=false;
  #connectionCapture;
  #connectionClosed=false;
  #connectionClosing;
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

  async request(path, body, timeoutMs=15000, method) {
    const ticket=body!==undefined&&this.#workspaceGuard?await this.#workspaceGuard.prepare():undefined;
    const token = validateToken(await this.#tokenProvider());
    if(ticket){
      if(ticket.origin!==this.baseUrl)throw new Error('The selected workspace backend changed before admission.');
      this.#workspaceGuard.assert(ticket);
      if(path==='/v1/runs'||/^\/v1\/connection-bindings\/[a-f0-9]{32}\/runs$/.test(path)||path==='/v1/graph-runs'||/^\/v1\/sessions\/[A-Za-z0-9_-]+\/commands$/.test(path))body={...body,...ticket.fields};
    }
    const response = await this.fetch(this.baseUrl + path, {
      method: method || (body === undefined ? 'GET' : 'POST'),
      headers: { Authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(timeoutMs),
      redirect: 'error'
    });
    const data = response.status===204?{}:await response.json();
    if (!response.ok) { const error=new Error(typeof data.detail === 'string' ? data.detail : `Backend returned ${response.status}`);error.status=response.status;throw error; }
    return data;
  }
  async health() {
    const health=await this.request('/v1/health');
    if(health&&Object.hasOwn(health,'local_connection_admission')){if(health.local_connection_admission!==1)throw new Error('Unsupported captured Local admission protocol. Reconnect to a compatible backend.');this.#connectionAdmissionSupported=true;}
    else if(this.#connectionCapture)throw new Error('The backend connection protocol changed. Reconnect before admitting new work.');
    if(health&&Object.hasOwn(health,'local_connection_operations')){if(health.local_connection_operations!==1||!this.#connectionAdmissionSupported)throw new Error('Unsupported captured operation protocol.');this.#connectionOperationsSupported=true;}
    else if(this.#connectionOperationsSupported)throw new Error('The backend operation protocol changed. Reconnect before deciding work.');
    if(health&&Object.hasOwn(health,'local_connection_history')){if(health.local_connection_history!==1||!this.#connectionAdmissionSupported)throw new Error('Unsupported captured history protocol.');this.#connectionHistorySupported=true;}
    else if(this.#connectionHistorySupported)throw new Error('The backend history protocol changed. Reconnect before reading history.');
    if(health&&Object.hasOwn(health,"local_connection_child_history")){if(health.local_connection_child_history!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured child history protocol.");this.#connectionChildHistorySupported=true;}
    else if(this.#connectionChildHistorySupported)throw new Error("The backend child history protocol changed. Reconnect before reading children.");
    if(health&&Object.hasOwn(health,"local_connection_graph_admission")){if(health.local_connection_graph_admission!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured graph admission protocol.");this.#connectionGraphAdmissionSupported=true;}
    else if(this.#connectionGraphAdmissionSupported)throw new Error("The backend graph admission protocol changed. Reconnect before admitting graph work.");
    if(health&&Object.hasOwn(health,"local_connection_graph_read")){if(health.local_connection_graph_read!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured graph read protocol.");this.#connectionGraphReadSupported=true;}
    else if(this.#connectionGraphReadSupported)throw new Error("The backend graph read protocol changed. Reconnect before reading graphs.");
    if(health&&Object.hasOwn(health,"local_connection_graph_children")){if(health.local_connection_graph_children!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured graph children protocol.");this.#connectionGraphChildrenSupported=true;}
    else if(this.#connectionGraphChildrenSupported)throw new Error("The backend graph children protocol changed. Reconnect before reading graph owners.");
    if(health&&Object.hasOwn(health,"local_connection_workspace")){if(health.local_connection_workspace!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured workspace protocol.");this.#connectionWorkspaceSupported=true;}
    else if(this.#connectionWorkspaceSupported)throw new Error("The backend workspace protocol changed. Reconnect before reading workspace state.");
    if(health&&Object.hasOwn(health,"local_connection_commands")){if(health.local_connection_commands!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured commands protocol.");this.#connectionCommandsSupported=true;}
    else if(this.#connectionCommandsSupported)throw new Error("The backend command protocol changed. Reconnect before invoking commands.");
    if(health&&Object.hasOwn(health,"local_connection_model_selection")){if(health.local_connection_model_selection!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured model selection protocol.");this.#connectionModelSelectionSupported=true;}
    else if(this.#connectionModelSelectionSupported)throw new Error("The backend model selection protocol changed. Reconnect before choosing models.");
    if(health&&Object.hasOwn(health,"local_connection_models")){if(health.local_connection_models!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured models protocol.");this.#connectionModelsSupported=true;}
    else if(this.#connectionModelsSupported)throw new Error("The backend model catalog protocol changed. Reconnect before reading models.");
    if(health&&Object.hasOwn(health,"local_connection_run_stream")){if(health.local_connection_run_stream!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured run stream protocol.");this.#connectionRunStreamSupported=true;}
    else if(this.#connectionRunStreamSupported)throw new Error("The backend run stream protocol changed. Reconnect before observing events.");
    if(health&&Object.hasOwn(health,"local_connection_tree_stream")){if(health.local_connection_tree_stream!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured tree stream protocol.");this.#connectionTreeStreamSupported=true;}
    else if(this.#connectionTreeStreamSupported)throw new Error("The backend tree stream protocol changed. Reconnect before observing events.");
    if(health&&Object.hasOwn(health,"local_connection_graph_control")){if(health.local_connection_graph_control!==1||!this.#connectionAdmissionSupported)throw new Error("Unsupported captured graph control protocol.");this.#connectionGraphControlSupported=true;}
    else if(this.#connectionGraphControlSupported)throw new Error("The backend graph control protocol changed. Reconnect before continuing graphs.");
    return health;
  }
  async #captureConnection(){
    if(this.#connectionClosed)throw new Error('This connection context was closed. Reconnect before admitting new work.');
    if(!this.#connectionCapture){const origin=this.baseUrl;this.#connectionCapture=(async()=>{
      const catalog=await this.connectionProfiles(),profile=catalog.profiles.find(p=>p.id===catalog.active_id);
      if(!profile||profile.kind!=='local'||!profile.authenticated||!profile.command_routing)throw new Error('The selected connection is unavailable. No Local fallback was attempted.');
      if(this.#connectionClosed||this.baseUrl!==origin)throw new Error('The connection changed before capture.');
      const value=await this.request('/v1/connection-bindings',{id:profile.id,expected_profile_revision:profile.revision});exactFields(value,['binding_id','identity']);
      if(typeof value.binding_id!=='string'||!/^[a-f0-9]{32}$/.test(value.binding_id))throw new Error('Invalid native connection handle.');validateConnectionBindingIdentity(value.identity);
      if(value.identity.connection_id!==profile.id||value.identity.profile_revision!==profile.revision)throw new Error('Native connection capture differs from the selected profile.');
      return Object.freeze({id:value.binding_id,origin,identity:Object.freeze({...value.identity})});
    })();}
    return this.#connectionCapture;
  }
  async #admitConnection(resource,body){
    if(!this.#connectionAdmissionSupported)return this.request('/v1/'+resource,body);
    if(resource==='sessions'&&body.title===undefined)body={...body,title:'New session'};
    const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured connection changed before admission.');
    const id=globalThis.crypto.randomUUID().replaceAll('-',''),kind=resource==='sessions'?'session':'run';
    try{
      const value=await this.request('/v1/connection-bindings/'+capture.id+'/'+resource,{...body,id});exactFields(value,['binding',kind]);validateConnectionBindingIdentity(value.binding);
      if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Admission receipt belongs to another connection context.');
      const record=value[kind];if(!record||record.id!==id)throw new Error('Admission receipt does not match its request identity.');
      if(kind==='run'){validateRunDTO(record);if(record.session_id!==body.session_id)throw new Error('Run receipt belongs to another conversation.');if(resource==='graph-runs'&&(!record.graph_root||record.parent_id))throw new Error('Graph receipt is not its admitted root.');}
      else if(record.title!==body.title||record.parent_id!=='')throw new Error('Session receipt differs from its requested conversation.');
      return record;
    }catch(error){error.admissionId=id;error.connectionBinding=capture.identity;throw error;}
  }
  async closeConnection(){
    if(!this.#connectionAdmissionSupported&&!this.#connectionCapture)return;
    if(this.#connectionClosing)return this.#connectionClosing;this.#connectionClosed=true;
    this.#connectionClosing=(async()=>{if(!this.#connectionCapture)return;let capture;try{capture=await this.#connectionCapture;}catch{return;}if(this.baseUrl!==capture.origin)return;await this.request('/v1/connection-bindings/'+capture.id,undefined,15000,'DELETE');})();return this.#connectionClosing;
  }
  async #workspaceRead(membership=false) {
    if(!this.#connectionWorkspaceSupported)return this.request('/v1/workspace'+(membership?'/membership':''));const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured workspace connection changed.');const field=membership?'membership':'workspace',value=await this.request('/v1/connection-bindings/'+capture.id+'/workspace'+(membership?'/membership':''));exactFields(value,['binding',field]);validateConnectionBindingIdentity(value.binding);if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Workspace state belongs to another connection');return value[field];
  }
  async workspace() { return validateWorkspace(await this.#workspaceRead()); }
  async workspaceMembership(workspace) { const identity=workspace||await this.workspace();return validateWorkspaceMembership(await this.#workspaceRead(true),identity); }
  async addWorkspaceFolder(root) {
    if(typeof root!=='string'||!root||root.length>32760||/[\x00-\x1f]/.test(root)||!(/^[A-Za-z]:[\\/]/.test(root)||/^\\\\[^\\/]+\\[^\\/]+(?:\\|$)/.test(root)))throw new Error('Choose an absolute local or UNC workspace folder.');
    const workspace=await this.workspace();return validateWorkspaceMembership(await this.request('/v1/workspaces/add',{workspace_id:workspace.workspace_id,root},30000),workspace);
  }
  async languageServerProfiles() {
    const value=await this.request('/v1/language-servers');exactFields(value,['profiles']);
    if(!Array.isArray(value.profiles)||value.profiles.length>16)throw new Error('Invalid native language-server catalogue.');
    const ids=new Set();for(const profile of value.profiles){exactFields(profile,['id','language_id','revision','extensions']);if(typeof profile.id!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(profile.id)||ids.has(profile.id)||typeof profile.language_id!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(profile.language_id)||!Number.isSafeInteger(profile.revision)||profile.revision<1||!Array.isArray(profile.extensions)||!profile.extensions.length||profile.extensions.length>32||profile.extensions.some(extension=>typeof extension!=='string'||!/^\.[A-Za-z0-9_-]{1,15}$/.test(extension)))throw new Error('Invalid native language-server profile.');ids.add(profile.id);}
    return value.profiles;
  }
  async languageServerQuery(query,binding) {
    if(!query||typeof query!=='object'||Array.isArray(query)||!binding||typeof binding!=='object')throw new Error('Invalid language-server query.');
    const path=query.path,method=query.method,line=query.line??0,character=query.character??0,documentText=query.documentText,formattingOptions=query.formattingOptions,folderId=query.folderId;
    if(typeof path!=='string'||!path||path.length>4096||/[\x00-\x1f]/.test(path)||/^(?:[\\/]|[A-Za-z]:)/.test(path)||path.split(/[\\/]/).some(segment=>segment==='..')||!['hover','definition','references','documentSymbol','diagnostics','formatting'].includes(method)||!Number.isSafeInteger(line)||line<0||line>1000000||!Number.isSafeInteger(character)||character<0||character>1000000||
       (documentText!==undefined&&(typeof documentText!=='string'||Buffer.byteLength(documentText,'utf8')>900*1024))||(folderId!==undefined&&(typeof folderId!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(folderId)))||typeof binding.expected_workspace_id!=='string'||!binding.expected_workspace_id||binding.expected_workspace_id.length>256||typeof binding.expected_workspace_authority_id!=='string'||!/^[a-f0-9]{32}$/.test(binding.expected_workspace_authority_id))throw new Error('Invalid language-server query.');
    const formattingKeys=['tabSize','insertSpaces','trimTrailingWhitespace','insertFinalNewline','trimFinalNewlines'];
    if((method==='formatting')!==(!!formattingOptions)||formattingOptions&&(!Number.isSafeInteger(formattingOptions.tabSize)||formattingOptions.tabSize<1||formattingOptions.tabSize>16||typeof formattingOptions.insertSpaces!=='boolean'||Object.keys(formattingOptions).some(key=>!formattingKeys.includes(key))||formattingKeys.slice(2).some(key=>formattingOptions[key]!==undefined&&typeof formattingOptions[key]!=='boolean')))throw new Error('Invalid language-server formatting options.');
    const options=formattingOptions&&{tab_size:formattingOptions.tabSize,insert_spaces:formattingOptions.insertSpaces,...(formattingOptions.trimTrailingWhitespace===undefined?{}:{trim_trailing_whitespace:formattingOptions.trimTrailingWhitespace}),...(formattingOptions.insertFinalNewline===undefined?{}:{insert_final_newline:formattingOptions.insertFinalNewline}),...(formattingOptions.trimFinalNewlines===undefined?{}:{trim_final_newlines:formattingOptions.trimFinalNewlines})};
    const body={path,method,line,character,...(documentText===undefined?{}:{document_text:documentText}),...(options?{formatting_options:options}:{}),...(folderId===undefined?{}:{folder_id:folderId}),expected_workspace_id:binding.expected_workspace_id,expected_workspace_authority_id:binding.expected_workspace_authority_id};
    if(Buffer.byteLength(JSON.stringify(body),'utf8')>1024*1024)throw new Error('Editor document exceeds the language-server request limit.');
    const value=await this.request('/v1/language-server/query',body,130000);
    exactFields(value,folderId===undefined?['server_id','language_id','path','method','result','diagnostics']:['server_id','language_id','path','method','result','diagnostics','folder_id']);
    if(typeof value.server_id!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(value.server_id)||typeof value.language_id!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(value.language_id)||value.path!==path||value.method!==method||!Array.isArray(value.diagnostics)||value.diagnostics.length>10000||folderId!==undefined&&value.folder_id!==folderId)throw new Error('Invalid native language-server response.');
    return value;
  }
  initializeGitRepository(folderId) { return this.request('/v1/vcs/init',{...(folderId===undefined?{}:{folder_id:validateVcsFolderId(folderId)})}); }
  async vcsStatus(folderId) { return validateVcsStatus(await this.request('/v1/vcs/status'+vcsFolderQuery(folderId))); }
  async vcsBase(folderId) { return validateVcsBase(await this.request('/v1/vcs/base'+vcsFolderQuery(folderId))); }
  async vcsDiff(mode,base,context=3,folderId) {
    if(!['working','branch','committed'].includes(mode)||!Number.isSafeInteger(context)||context<0||context>1000)throw new Error('Invalid Git diff selection.');
    if(base!==undefined&&(typeof base!=='string'||!base||base.length>1024||/[\r\n\0]/.test(base)))throw new Error('Invalid Git base reference.');
    const query=new URLSearchParams({mode,context:String(context)});if(base!==undefined)query.set('base',base);if(folderId!==undefined)query.set('folder_id',validateVcsFolderId(folderId));
    return validateVcsDiff(await this.request('/v1/vcs/diff?'+query.toString()));
  }
  async models() {
    if(!this.#connectionModelsSupported)return this.request('/v1/models');
    const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured model connection changed.');
    const value=await this.request('/v1/connection-bindings/'+capture.id+'/models');exactFields(value,['binding','default_model','models']);validateConnectionBindingIdentity(value.binding);if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Model catalog belongs to another connection');
    if(typeof value.default_model!=='string'||!Array.isArray(value.models)||value.models.length>10000)throw new Error('Invalid model catalog');const ids=new Set();for(const model of value.models){exactFields(model,['id']);if(typeof model.id!=='string'||!model.id||model.id.length>256||/[\x00-\x1f\x7f]/.test(model.id)||ids.has(model.id))throw new Error('Invalid model catalog entry');ids.add(model.id);}if(value.default_model!==(value.models[0]?.id??''))throw new Error('Model catalog default differs');return {default_model:value.default_model,models:value.models};
  }
  async #readCapturedGraph(id){
    if(id!==undefined)executionIdentity(id);const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error("The captured graph connection changed.");
    const detail=id!==undefined,value=await this.request('/v1/connection-bindings/'+capture.id+(detail?'/graph-runs/'+encodeURIComponent(id):'/graphs'));exactFields(value,detail?['binding','graph']:['binding','graphs']);validateConnectionBindingIdentity(value.binding);
    if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error("Graph read receipt belongs to another connection.");
    if(!detail){if(!Array.isArray(value.graphs))throw new Error("Invalid captured graph catalog");return {graphs:value.graphs};}
    validateRunDTO(value.graph?.run);if(value.graph.run.id!==id||!value.graph.run.graph_root)throw new Error("Graph read receipt belongs to another run.");return value.graph;
  }
  graphs() { return this.#connectionGraphReadSupported?this.#readCapturedGraph():this.request('/v1/graphs'); }
  delegation() { return this.request('/v1/agent/delegation'); }
  async planning() { return validatePlanning(await this.request('/v1/agent/planning')); }
  async plan(root) { executionIdentity(root);return validatePlanObservation(await this.request(`/v1/runs/${root}/plan`),root); }
  async planInput(root,request,input_json,expected_revision,expected_state_sequence) {
    executionIdentity(root);executionIdentity(request);planPreconditions(expected_revision,expected_state_sequence);validatePlanInputText(input_json);
    return validatePlanOwner(await this.request(`/v1/runs/${root}/plan/human/${request}`,{input_json,expected_revision,expected_state_sequence}),root);
  }
  async mcpInputs(run) {
    executionIdentity(run);const value=await this.request(`/v1/runs/${run}/mcp-inputs`);
    if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==1||!Array.isArray(value.requests)||value.requests.length>32)throw new Error('Invalid MCP input request list.');
  const seen=new Set();for(const item of value.requests){if(!item||typeof item!=='object'||Array.isArray(item)||Object.keys(item).sort().join(',')!=='config_revision,expires_unix_ms,id,operation_id,peer_tool,published_event_seq,request_json,run_id,server_config_id,state'||item.run_id!==run||item.state!=='waiting'||typeof item.peer_tool!=='string'||!item.peer_tool||item.peer_tool.length>128||!Number.isSafeInteger(item.config_revision)||item.config_revision<1||!Number.isSafeInteger(item.expires_unix_ms)||item.expires_unix_ms<=0||!Number.isSafeInteger(item.published_event_seq)||item.published_event_seq<1||typeof item.request_json!=='string'||new TextEncoder().encode(item.request_json).length>65536)throw new Error('Invalid MCP input request ownership.');executionIdentity(item.id);executionIdentity(item.operation_id);mcpServerIdentity(item.server_config_id);if(seen.has(item.id))throw new Error('Duplicate MCP input request.');seen.add(item.id);let request;try{request=JSON.parse(item.request_json);}catch{throw new Error('Invalid MCP input form.');}if(!request||typeof request!=='object'||Array.isArray(request)||request.method!=='elicitation/create'||!request.params||typeof request.params!=='object'||Array.isArray(request.params)||request.params.mode!==undefined&&request.params.mode!=='form'||typeof request.params.message!=='string'||!request.params.requestedSchema||typeof request.params.requestedSchema!=='object'||Array.isArray(request.params.requestedSchema))throw new Error('Unsupported MCP input form.');}
    return value.requests;
  }
  async answerMcpInput(run,id,response_json) {
    executionIdentity(run);executionIdentity(id);if(typeof response_json!=='string'||new TextEncoder().encode(response_json).length>16384)throw new Error('MCP input response exceeds limits.');const response=JSON.parse(response_json);if(!response||typeof response!=='object'||Array.isArray(response))throw new Error('MCP input response must be an object.');
    const value=await this.request(`/v1/runs/${run}/mcp-inputs/${id}`,{response_json});if(!value||value.id!==id||value.run_id!==run||value.state!=='answered')throw new Error('MCP input response ownership changed.');return value;
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
  async #controlGraph(root,path,body,resume=false){
    executionIdentity(root);const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error("The captured graph control connection changed.");
    const value=await this.request('/v1/connection-bindings/'+capture.id+'/graph-runs/'+encodeURIComponent(root)+path,body);exactFields(value,['binding',resume?'run':'graph']);validateConnectionBindingIdentity(value.binding);
    if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error("Graph control receipt belongs to another connection.");const run=resume?value.run:value.graph?.run;validateRunDTO(run);if(run.id!==root||!run.graph_root||run.parent_id)throw new Error("Graph control receipt belongs to another root.");return resume?value.run:value.graph;
  }
  async resumeGraph(root,expected_checkpoint_revision) {
    executionIdentity(root);planInteger(expected_checkpoint_revision,1);
    const result=await (this.#connectionGraphControlSupported?this.#controlGraph(root,'/resume',{expected_checkpoint_revision},true):this.request(`/v1/graph-runs/${root}/resume`,{expected_checkpoint_revision}));validateRunDTO(result);
    if(result.id!==root||result.parent_id!==''||result.graph_root!==true)throw new Error('Graph resume ownership changed');return result;
  }
  #admitGraph(body){return this.#connectionGraphAdmissionSupported?this.#admitConnection('graph-runs',body):this.request('/v1/graph-runs',body);}
  async graphRun(session_id,graph_id,graph_revision,prompt,model_id,binding,files=[],expectedWorkspaceRevision) {
    if(!Array.isArray(files)||files.length>32)throw new Error('Invalid workspace file attachments.');
    if(!files.length)return this.#admitGraph({session_id,graph_id,graph_revision,prompt,...(model_id?{model_id}:{}),...profileBindingFields(binding)});
    const workspace=await this.workspace();if(!workspace.configured||typeof workspace.workspace_id!=='string'||typeof workspace.authority_id!=='string')throw new Error('Graph file attachments require an opened backend workspace.');const membership=await this.workspaceMembership(workspace);
    if(expectedWorkspaceRevision!==undefined&&membership.revision!==expectedWorkspaceRevision)throw new Error('Workspace membership changed. Remove and reattach these files before sending.');
    for(const file of files)if(!file||typeof file!=='object'||Array.isArray(file)||Object.keys(file).some(key=>!['path','folder_id','name','description','mention'].includes(key))||typeof file.path!=='string'||!file.path||file.path.length>4096||file.path.startsWith('/')||/^[A-Za-z]:/.test(file.path)||file.path.includes('\\')||file.path.split('/').some(part=>!part||part==='.'||part==='..')||file.folder_id!==undefined&&(typeof file.folder_id!=='string'||file.folder_id.length>256))throw new Error('Invalid workspace file attachment.');
    return this.#admitGraph({session_id,graph_id,graph_revision,prompt,files,expected_workspace_id:workspace.workspace_id,expected_workspace_authority_id:workspace.authority_id,expected_workspace_revision:expectedWorkspaceRevision??membership.revision,...(model_id?{model_id}:{}),...profileBindingFields(binding)});
  }
  async graph(id) { const value=await (this.#connectionGraphReadSupported?this.#readCapturedGraph(id):this.request(`/v1/graph-runs/${encodeURIComponent(id)}`));if(value.context!==undefined)validateGraphContext(value.context,value.run);return value; }
  async graphChildren(id) {
    if(!this.#connectionGraphChildrenSupported)return this.request(`/v1/graph-runs/${encodeURIComponent(id)}/children`);
    executionIdentity(id);const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error("The captured graph children connection changed.");
    const value=await this.request("/v1/connection-bindings/"+capture.id+"/graph-runs/"+encodeURIComponent(id)+"/children");exactFields(value,["binding","root_id","session_id","children"]);validateConnectionBindingIdentity(value.binding);
    if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key])||value.root_id!==id)throw new Error("Graph children receipt belongs to another connection or owner.");executionIdentity(value.session_id);if(!Array.isArray(value.children)||value.children.length>4096)throw new Error("Invalid captured graph children");const seen=new Set();for(const child of value.children){validateRunDTO(child);if(child.id===id||seen.has(child.id)||child.parent_id!==id||child.session_id!==value.session_id)throw new Error("Graph child ownership changed.");seen.add(child.id);}return value.children;
  }
  graphEvents(id,after) { return this.request(`/v1/graph-runs/${encodeURIComponent(id)}/events?after=${after}`); }
  graphChildHistory(root,child,session) { return this.#connectionChildHistorySupported?this.#capturedChildHistory(root,child,session):this.request(`/v1/graph-runs/${encodeURIComponent(root)}/children/${encodeURIComponent(child)}/history`); }
  graphInput(root,node,input_json,expected_checkpoint_revision) { if(this.#connectionGraphControlSupported)return this.#controlGraph(root,'/human/'+encodeURIComponent(node),{input_json,expected_checkpoint_revision});return this.request(`/v1/graph-runs/${encodeURIComponent(root)}/human/${encodeURIComponent(node)}`,{input_json,expected_checkpoint_revision}); }
  graphTreeInput(root,owner,node,input_json,expected_checkpoint_revision,expected_owner_checkpoint_revision) { if(this.#connectionGraphControlSupported)return this.#controlGraph(root,'/invocations/'+encodeURIComponent(owner)+'/human/'+encodeURIComponent(node),{input_json,expected_checkpoint_revision,expected_owner_checkpoint_revision});return this.request(`/v1/graph-runs/${encodeURIComponent(root)}/invocations/${encodeURIComponent(owner)}/human/${encodeURIComponent(node)}`,{input_json,expected_checkpoint_revision,expected_owner_checkpoint_revision}); }
  providerConfiguration() { return this.request('/v1/provider/configuration'); }
  async importProviderConfiguration(path,expected_revision) {
    if(typeof path!=='string'||!path||path.length>32768||/[\x00-\x1f]/.test(path)||!(/^[A-Za-z]:[\\/]/.test(path)||/^\\\\[^\\/]+[\\/][^\\/]+/.test(path)))throw new Error('Provider configuration must be an absolute local path');
    planInteger(expected_revision);return validateProviderProfiles(await this.request('/v1/provider/configuration/import',{path,expected_revision}),true);
  }
  async mcpAuthorizationServers() { return validateMcpAuthorizationServers(await this.request('/v1/mcp/authorization/servers')); }
  async mcpResources() { return validateMcpResourceCatalog(await this.request('/v1/mcp/resources')); }
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
  async connectionProfiles() { return validateConnectionProfiles(await this.request('/v1/connections')); }
  async saveConnectionProfile(id,name,origin) {
    connectionIdentity(id);connectionName(name);connectionOrigin(origin);
    if(id==='local')throw new Error('The Local connection profile cannot be replaced.');
    const current=await this.connectionProfiles(),profiles=current.profiles.map(({id,name,kind,origin})=>({id,name,kind,origin})),profile={id,name,kind:'nexus',origin};
    const existing=profiles.findIndex(value=>value.id===id);if(existing<0)profiles.push(profile);else profiles[existing]=profile;
    if(profiles.length>32)throw new Error('Connection catalog is full.');
    return validateConnectionProfiles(await this.request('/v1/connections',{profiles,expected_revision:current.revision}));
  }
  async removeConnectionProfile(id) {
    connectionIdentity(id);if(id==='local')throw new Error('The Local connection profile cannot be removed.');
    const current=await this.connectionProfiles(),profiles=current.profiles.filter(value=>value.id!==id).map(({id,name,kind,origin})=>({id,name,kind,origin}));
    if(profiles.length===current.profiles.length)throw new Error('Nexus connection profile was not found.');
    return validateConnectionProfiles(await this.request('/v1/connections',{profiles,expected_revision:current.revision}));
  }
  sessions() { return this.request('/v1/sessions'); }
  deleteSession(id) { executionIdentity(id);return this.request(`/v1/sessions/${encodeURIComponent(id)}`,undefined,15000,'DELETE'); }
  activeSessions() { return this.request('/v1/sessions/active'); }
  skills() { return this.request('/v1/workspace/skills'); }
  async commands() {
    if(!this.#connectionCommandsSupported)return this.request('/v1/workspace/commands');const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured command connection changed.');const value=await this.request('/v1/connection-bindings/'+capture.id+'/commands');exactFields(value,['binding','commands','workspace_id','authority_id']);validateConnectionBindingIdentity(value.binding);if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Commands belong to another connection');if(!Array.isArray(value.commands)||typeof value.workspace_id!=='string'||!value.workspace_id||typeof value.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(value.authority_id))throw new Error('Invalid command catalog');return {commands:value.commands,workspace_id:value.workspace_id,authority_id:value.authority_id};
  }
  async #invokeCommand(session,body) {
    if(!this.#connectionCommandsSupported)return this.request('/v1/sessions/'+encodeURIComponent(session)+'/commands',body);const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured command connection changed.');const value=await this.request('/v1/connection-bindings/'+capture.id+'/sessions/'+encodeURIComponent(session)+'/commands',body);exactFields(value,['binding','run']);validateConnectionBindingIdentity(value.binding);if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Command run belongs to another connection');validateRunDTO(value.run);if(value.run.session_id!==session)throw new Error('Command run belongs to another session');return value.run;
  }
  command(sessionId,commandId,args,modelId,binding,workspace,graph) {
    executionIdentity(sessionId);if(typeof commandId!=='string'||!commandId||commandId.length>256||typeof args!=='string'||new TextEncoder().encode(args).length>8192||!workspace||typeof workspace.workspace_id!=='string'||!/^[a-f0-9]{32}$/.test(workspace.authority_id))throw new Error('Invalid workspace command request.');
    if(graph&&(!graph||typeof graph.id!=='string'||!Number.isSafeInteger(graph.revision)||graph.revision<1))throw new Error('Invalid workspace command graph selection.');
    return this.#invokeCommand(sessionId,{command_id:commandId,arguments:args,...(modelId?{model_id:modelId}:{}),...profileBindingFields(binding),...(graph?{graph_id:graph.id,graph_revision:graph.revision}:{}),expected_workspace_id:workspace.workspace_id,expected_workspace_authority_id:workspace.authority_id});
  }
  async runCommand(sessionId,commandId,args,modelId,binding,graph) {
    const workspace=await this.workspace();if(!workspace.configured)throw new Error('Workspace commands require an opened workspace.');
    const value=await this.command(sessionId,commandId,args,modelId,binding,workspace,graph);validateRunDTO(value);return value;
  }
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
  async #sessionModelRequest(id,body) {
    if(!this.#connectionModelSelectionSupported)return this.request('/v1/sessions/'+encodeURIComponent(id)+'/model',body);
    const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured model selection connection changed.');const value=await this.request('/v1/connection-bindings/'+capture.id+'/sessions/'+encodeURIComponent(id)+'/model',body);exactFields(value,['binding','model']);validateConnectionBindingIdentity(value.binding);if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Model selection belongs to another connection');return value.model;
  }
  async sessionModel(id) { executionIdentity(id);return validateSessionModel(await this.#sessionModelRequest(id),id); }
  async replaceSessionModel(id,model,providerBinding,observed) {
    executionIdentity(id);profileIdentity(model);validateSessionModel(observed,id);
    if(!observed.editable)throw new Error('Wait for this conversation to become idle before changing its model.');
    if(!providerBinding||typeof providerBinding.provider_profile_id!=='string'||!Number.isSafeInteger(providerBinding.expected_provider_revision)||providerBinding.expected_provider_revision<0)throw new Error('Refresh the provider profile before choosing a conversation model.');
    return validateSessionModel(await this.#sessionModelRequest(id,{model_id:model,provider_profile_id:providerBinding.provider_profile_id,expected_provider_revision:providerBinding.expected_provider_revision,expected_revision:observed.revision}),id);
  }
  createSession(title) { return this.#admitConnection('sessions', { title }); }
  sessionInbox(session_id) { return this.request(`/v1/sessions/${encodeURIComponent(session_id)}/inbox`); }
  async enqueueSessionInput(session_id,content,id,files=[],expectedWorkspaceRevision) { const value={...(id?{id}:{}),content};if(files.length){const workspace=await this.workspace();if(!workspace.configured||typeof workspace.workspace_id!=='string'||typeof workspace.authority_id!=='string')throw new Error('Queued file attachments require an opened workspace.');const membership=await this.workspaceMembership(workspace);if(expectedWorkspaceRevision!==undefined&&membership.revision!==expectedWorkspaceRevision)throw new Error('Workspace membership changed. Remove and reattach these files before queuing.');value.files=files;value.expected_workspace_id=workspace.workspace_id;value.expected_workspace_authority_id=workspace.authority_id;value.expected_workspace_revision=expectedWorkspaceRevision??membership.revision;}return this.request(`/v1/sessions/${encodeURIComponent(session_id)}/inbox`,value); }
  cancelSessionInput(session_id,input_id) { return this.request(`/v1/sessions/${encodeURIComponent(session_id)}/inbox/${encodeURIComponent(input_id)}`,undefined,15000,'DELETE'); }
  async terminals(session) {
    executionIdentity(session);
    const value=await this.request(`/v1/sessions/${encodeURIComponent(session)}/terminals`);
    exactFields(value,['terminals']);
    if(!Array.isArray(value.terminals)||value.terminals.length>256)throw new Error('Invalid terminal catalogue');
    for(const terminal of value.terminals)validateTerminalMetadata(terminal,session);
    return value.terminals;
  }
  async createTerminal(session,title,rows,columns) {
    executionIdentity(session);terminalDimensions(rows,columns);
    if(typeof title!=='string'||!title.trim()||title.length>128||/[\x00-\x1f\x7f]/.test(title))throw new Error('Invalid terminal title');
    return validateTerminalMetadata(await this.request(`/v1/sessions/${encodeURIComponent(session)}/terminals`,{title,rows,columns}),session);
  }
  async terminal(id) { terminalIdentity(id);return validateTerminalMetadata(await this.request(`/v1/terminals/${id}`)); }
  async terminalOutput(id,after) {
    terminalIdentity(id);planInteger(after);
    return validateTerminalOutput(await this.request(`/v1/terminals/${id}/output?after=${after}`));
  }
  async terminalInput(id,data) {
    terminalIdentity(id);
    if(typeof data!=='string'||!data||data.length>65536||data.includes('\0'))throw new Error('Invalid terminal input');
    const value=await this.request(`/v1/terminals/${id}/input`,{data});exactFields(value,['written']);if(value.written!==true)throw new Error('Terminal input was not accepted');
  }
  async resizeTerminal(id,rows,columns) {
    terminalIdentity(id);terminalDimensions(rows,columns);
    return validateTerminalMetadata(await this.request(`/v1/terminals/${id}/resize`,{rows,columns}));
  }
  async closeTerminal(id) {
    terminalIdentity(id);const value=await this.request(`/v1/terminals/${id}/close`,{});exactFields(value,['closed']);if(value.closed!==true)throw new Error('Terminal did not close');
  }
  renameSession(id,title,expectedTitle) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/title`,{title,expected_title:expectedTitle}); }
  async forkSession(id,before) {
    executionIdentity(id);
    if(before!==null&&before!==undefined&&(typeof before!=='string'||!/^msg_[1-9][0-9]*$/.test(before)))throw new Error('Invalid fork boundary message ID');
    const value=await this.request(`/v1/sessions/${encodeURIComponent(id)}/fork`,{before:before??null});
    if(!value||typeof value.id!=='string'||!value.id||value.parent_id!==id||!value.fork||value.fork.session_id!==id||value.fork.boundary_type!==(before==null?'through':'before')||value.fork.message_id!==(before??null))throw new Error('Invalid native fork session receipt');
    return value;
  }
  async exportSession(id) { executionIdentity(id);return validateSessionArchive(await this.request(`/v1/sessions/${encodeURIComponent(id)}/export`)); }
  async importSession(archive) {
    validateSessionArchive(archive);const value=await this.request('/v1/sessions/import',{archive});exactFields(value,['id','title']);executionIdentity(value.id);if(typeof value.title!=='string'||!value.title)throw new Error('Invalid imported session receipt');return value;
  }
  history(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/history`); }
  async historyPage(id,limit=40,before) {
    executionIdentity(id);if(!Number.isSafeInteger(limit)||limit<1||limit>1000)throw new Error('Invalid session history page size');
    if(before!==undefined&&(typeof before!=='string'||!/^msg_[1-9][0-9]*$/.test(before)||!Number.isSafeInteger(Number(before.slice(4)))))throw new Error('Invalid session history cursor');
    const query=new URLSearchParams({limit:String(limit)});if(before!==undefined)query.set('before',before);
    let value;
    if(this.#connectionHistorySupported){
      const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured history connection changed.');
      const receipt=await this.request('/v1/connection-bindings/'+capture.id+'/sessions/'+encodeURIComponent(id)+'/messages',{before_sequence:before===undefined?0:Number(before.slice(4)),count:limit});
      exactFields(receipt,['binding','session_id','messages','has_more','next_before']);validateConnectionBindingIdentity(receipt.binding);
      if(Object.keys(capture.identity).some(key=>receipt.binding[key]!==capture.identity[key])||receipt.session_id!==id)throw new Error('History receipt belongs to another connection or conversation.');
      value={messages:receipt.messages,has_more:receipt.has_more,next_before:receipt.next_before};
    }else value=await this.request(`/v1/sessions/${encodeURIComponent(id)}/messages?${query}`);
    if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).sort().join(',')!=='has_more,messages,next_before'||!Array.isArray(value.messages)||value.messages.length>limit||typeof value.has_more!=='boolean'||(value.next_before!==null&&(typeof value.next_before!=='string'||!/^msg_[1-9][0-9]*$/.test(value.next_before)))||value.has_more!==(value.next_before!==null)||(value.has_more&&(!value.messages.length||value.next_before!==value.messages[0].id)))throw new Error('Invalid session history page');
    let prior=0;for(const item of value.messages){if(!item||typeof item!=='object'||!Number.isSafeInteger(item.seq)||item.seq<=prior||item.id!==`msg_${item.seq}`||typeof item.role!=='string'||!item.data||typeof item.data!=='object'||Array.isArray(item.data))throw new Error('Invalid session history message');prior=item.seq;}
    return value;
  }
  async sessionMessage(id,messageId) {
    executionIdentity(id);if(typeof messageId!=='string'||!/^msg_[1-9][0-9]*$/.test(messageId)||!Number.isSafeInteger(Number(messageId.slice(4))))throw new Error('Invalid session message ID');
    const value=await this.request(`/v1/sessions/${encodeURIComponent(id)}/messages/${messageId}`),sequence=Number(messageId.slice(4));
    if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).sort().join(',')!=='data,id,role,seq'||value.id!==messageId||value.seq!==sequence||typeof value.role!=='string'||!value.data||typeof value.data!=='object'||Array.isArray(value.data))throw new Error('Invalid native session message');
    return value;
  }
  runs(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/runs`); }
  run(session_id, prompt, model_id,binding,inbox_id,files=[],expectedWorkspaceRevision) {
    if(!Array.isArray(files)||files.length>32)throw new Error('Invalid workspace file attachments.');
    if(!files.length)return this.#admitConnection('runs', { session_id, prompt,...(model_id ? {model_id} : {}),...(inbox_id?{inbox_id}:{}),...profileBindingFields(binding) });
    return this.runWithAttachments(session_id,prompt,model_id,binding,inbox_id,files,expectedWorkspaceRevision);
  }
  async runWithAttachments(session_id,prompt,model_id,binding,inbox_id,files,expectedWorkspaceRevision){
    const workspace=await this.workspace();if(!workspace.configured||typeof workspace.workspace_id!=='string'||typeof workspace.authority_id!=='string')throw new Error('File attachments require an opened backend workspace.');const membership=await this.workspaceMembership(workspace);
    if(expectedWorkspaceRevision!==undefined&&membership.revision!==expectedWorkspaceRevision)throw new Error('Workspace membership changed. Remove and reattach these files before sending.');
    const workspaceFields={expected_workspace_id:workspace.workspace_id,expected_workspace_authority_id:workspace.authority_id,expected_workspace_revision:expectedWorkspaceRevision??membership.revision};
    for(const file of files)if(!file||typeof file!=='object'||Array.isArray(file)||Object.keys(file).some(key=>!['path','folder_id','name','description','mention'].includes(key))||typeof file.path!=='string'||!file.path||file.path.length>4096||file.path.startsWith('/')||/^[A-Za-z]:/.test(file.path)||file.path.includes('\\')||file.path.split('/').some(part=>!part||part==='.'||part==='..')||file.folder_id!==undefined&&(typeof file.folder_id!=='string'||file.folder_id.length>256))throw new Error('Invalid workspace file attachment.');
    return this.#admitConnection('runs', { session_id, prompt,files,...workspaceFields,...(model_id ? {model_id} : {}),...(inbox_id?{inbox_id}:{}),...profileBindingFields(binding) });
  }
  events(id, after) { return this.request(`/v1/runs/${encodeURIComponent(id)}/events?after=${after}`); }
  async openEventStream(path,signal){
    signal?.throwIfAborted();const token=validateToken(await this.#tokenProvider());signal?.throwIfAborted();
    return this.fetch(this.baseUrl+path,{method:'GET',headers:{Authorization:`Bearer ${token}`,Accept:'text/event-stream'},signal:signal?AbortSignal.any([signal,AbortSignal.timeout(35000)]):AbortSignal.timeout(35000),redirect:'error'});
  }
  async eventStream(id,{after=0,scope='run',session_id,signal,onEvent,onObservation}={}) {
    executionIdentity(id);executionIdentity(session_id);planInteger(after);
    if(!['run','tree','graph'].includes(scope)||typeof onEvent!=='function'||onObservation!==undefined&&typeof onObservation!=='function')throw new Error('Invalid event stream observer');
    signal?.throwIfAborted();
    let capture;let route=scope==='graph'?`/v1/graph-runs/${id}/events/stream`:`/v1/runs/${id}/${scope==='tree'?'tree-events':'events'}/stream`;
    if(scope==="run"?this.#connectionRunStreamSupported:this.#connectionTreeStreamSupported){capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error("The captured tree stream connection changed.");route="/v1/connection-bindings/"+capture.id+"/sessions/"+session_id+"/runs/"+id+"/"+(scope==="graph"?"graph-events":scope==="tree"?"tree-events":"events")+"/stream";}
    let response;try{response=await this.openEventStream(route+'?after='+after,signal);}catch(error){if(error?.name==='TypeError'||error?.name==='TimeoutError')error.eventTransportUnavailable=true;throw error;}
    if(!response.ok){const value=await response.json();const error=new Error(typeof value.detail==='string'?value.detail:'Native event observation unavailable');error.status=response.status;throw error;}
    const known=new Set([id]);
    return readCommittedEventStream(response,{root:id,session:session_id,scope,after,binding:capture?.identity,signal,onEvent,onObservation,authorizeEvent:async event=>{
      if(known.has(event.run_id))return true;
      if(scope==='graph'){const root=await this.graph(id);if(root.run.id!==id)throw new Error('Graph stream root changed');const tree=await observeGraphTree(this,root,session_id);signal?.throwIfAborted();for(const child of tree.children)known.add(child.id);return known.has(event.run_id);}
      const children=await this.ownedChildren(id);signal?.throwIfAborted();
      if(!Array.isArray(children)||children.length>4096)throw new Error('Invalid owned stream children');
      const seen=new Set();for(const record of children){const child=scope==='graph'?record:record.run;validateRunDTO(child);if(child.id===id||seen.has(child.id)||child.parent_id!==id||child.session_id!==session_id||child.graph_root)throw new Error('Owned stream child identity changed');seen.add(child.id);known.add(child.id);}
      return known.has(event.run_id);
    }});
  }
  ownedChildren(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}/children`); }
  async #capturedChildHistory(parent,child,session){
    executionIdentity(parent);executionIdentity(child);executionIdentity(session);const capture=await this.#captureConnection();
    if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error("The captured child history connection changed.");
    const value=await this.request('/v1/connection-bindings/'+capture.id+'/sessions/'+encodeURIComponent(session)+'/runs/'+encodeURIComponent(parent)+'/children/'+encodeURIComponent(child)+'/history',{});
    exactFields(value,['binding','session_id','parent_id','child_id','messages']);validateConnectionBindingIdentity(value.binding);
    if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key])||value.session_id!==session||value.parent_id!==parent||value.child_id!==child)throw new Error("Child history receipt belongs to another connection or owner.");
    if(!Array.isArray(value.messages))throw new Error("Invalid captured child history");let sequence=0;for(const message of value.messages){if(!message||!Number.isSafeInteger(message.seq)||message.seq<=sequence||message.id!==`msg_${message.seq}`||typeof message.role!=='string'||!message.data||typeof message.data!=='object'||Array.isArray(message.data))throw new Error("Invalid captured child history message");sequence=message.seq;}
    return value.messages;
  }
  ownedChildHistory(parent,child,session) { return this.#connectionChildHistorySupported?this.#capturedChildHistory(parent,child,session):this.request(`/v1/runs/${encodeURIComponent(parent)}/children/${encodeURIComponent(child)}/history`); }
  treeEvents(id,after=0) { if(!Number.isSafeInteger(after)||after<0)throw new Error('Invalid tree event cursor');return this.request(`/v1/runs/${encodeURIComponent(id)}/tree-events?after=${after}`); }
  async #boundRun(id,cancel=false){
    executionIdentity(id);const capture=await this.#captureConnection();
    if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured run connection changed.');
    const value=await this.request('/v1/connection-bindings/'+capture.id+'/runs/'+encodeURIComponent(id)+(cancel?'/cancel':''),cancel?{}:undefined);
    if(cancel){exactFields(value,['cancel_requested']);if(value.cancel_requested!==true)throw new Error('Cancellation was not acknowledged.');return value;}
    exactFields(value,['binding','run']);validateConnectionBindingIdentity(value.binding);
    if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Run receipt belongs to another connection context.');
    validateRunDTO(value.run);if(value.run.id!==id)throw new Error('Run receipt differs from its requested identity.');return value.run;
  }
  status(id) { return this.#connectionAdmissionSupported?this.#boundRun(id):this.request(`/v1/runs/${encodeURIComponent(id)}`); }
  cancel(id) { return this.#connectionAdmissionSupported?this.#boundRun(id,true):this.request(`/v1/runs/${encodeURIComponent(id)}/cancel`, {}); }
  async #scopedOperations(run,session,id,decision){
    executionIdentity(run);executionIdentity(session);if(id!==undefined)executionIdentity(id);
    const capture=await this.#captureConnection();if(this.#connectionClosed||this.baseUrl!==capture.origin)throw new Error('The captured operation connection changed.');
    const path='/v1/connection-bindings/'+capture.id+'/sessions/'+encodeURIComponent(session)+'/runs/'+encodeURIComponent(run)+'/operations'+(id===undefined?'':'/'+encodeURIComponent(id))+(decision===undefined?'':'/decision');
    const value=await this.request(path,decision===undefined?undefined:{decision});
    exactFields(value,id===undefined?['binding','session_id','run_id','operations']:['binding','operation']);validateConnectionBindingIdentity(value.binding);
    if(Object.keys(capture.identity).some(key=>value.binding[key]!==capture.identity[key]))throw new Error('Operation receipt belongs to another connection context.');
    if(id===undefined){if(value.session_id!==session||value.run_id!==run||!Array.isArray(value.operations)||value.operations.some(op=>!op||op.run_id!==run))throw new Error('Operation collection belongs to another run.');return value.operations;}
    const operation=value.operation;if(!operation||operation.id!==id||operation.run_id!==run)throw new Error('Operation receipt belongs to another run.');return operation;
  }
  operations(id,session) { return this.#connectionOperationsSupported?this.#scopedOperations(id,session):this.request(`/v1/runs/${encodeURIComponent(id)}/operations`); }
  operation(id,scope) { return this.#connectionOperationsSupported?this.#scopedOperations(scope?.run_id,scope?.session_id,id):this.request(`/v1/operations/${encodeURIComponent(id)}`); }
  async sessionPermissions(id) {
    executionIdentity(id);const value=await this.request(`/v1/sessions/${encodeURIComponent(id)}/permissions`);
    if(!value||value.session_id!==id||!Array.isArray(value.requests)||value.requests.length>256||value.requests.some(item=>!item||typeof item.id!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(item.id)||typeof item.run_id!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(item.run_id)||typeof item.workspace_id!=='string'||!item.workspace_id||item.workspace_id.length>32768||typeof item.tool!=='string'||!item.tool||item.tool.length>128||typeof item.arguments_json!=='string'||typeof item.expires_unix_ms!=='number'||!Number.isSafeInteger(item.expires_unix_ms)||item.state!=='awaiting_approval'))throw new Error('Invalid pending session permission requests.');
    return value;
  }
  async replySessionPermission(sessionId,requestId,decision,grantResource=false) {
    executionIdentity(sessionId);executionIdentity(requestId);if(decision!=='allow'&&decision!=='deny')throw new Error('Permission decision must be allow or deny.');
    if(typeof grantResource!=='boolean'||(grantResource&&decision!=='allow'))throw new Error('Invalid permission grant choice.');
    const result=await this.request(`/v1/sessions/${encodeURIComponent(sessionId)}/permissions/${encodeURIComponent(requestId)}/reply`,{decision,...(grantResource?{grant_resource:true}:{})});
    if(!result||result.id!==requestId||!['ready','denied'].includes(result.state)||result.state!==(decision==='allow'?'ready':'denied'))throw new Error('Permission reply did not settle the requested operation.');
    return result;
  }
  async sessionPermissionGrants(sessionId) {
    executionIdentity(sessionId);const value=await this.request(`/v1/sessions/${encodeURIComponent(sessionId)}/permission-grants`);
    if(!value||value.session_id!==sessionId||!Number.isSafeInteger(value.revision)||value.revision<0||!Array.isArray(value.grants)||value.grants.length>256||value.grants.some(item=>!item||typeof item.id!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(item.id)||typeof item.workspace_id!=='string'||!item.workspace_id||typeof item.action!=='string'||!item.action||typeof item.resource!=='string'||!item.resource||typeof item.policy_binding!=='string'||!item.policy_binding||!Number.isSafeInteger(item.created_unix_ms)))throw new Error('Invalid saved session permission grants.');
    return value;
  }
  async revokeSessionPermissionGrant(sessionId,grantId,expectedRevision) {
    executionIdentity(sessionId);executionIdentity(grantId);if(!Number.isSafeInteger(expectedRevision)||expectedRevision<0)throw new Error('Invalid saved permission revision.');
    const value=await this.request(`/v1/sessions/${encodeURIComponent(sessionId)}/permission-grants/${encodeURIComponent(grantId)}`,{expected_revision:expectedRevision},15000,'DELETE');
    if(!value||value.session_id!==sessionId||!Number.isSafeInteger(value.revision)||value.revision!==expectedRevision+1||!Array.isArray(value.grants)||value.grants.some(item=>item?.id===grantId))throw new Error('Saved permission grant revocation was not confirmed.');return value;
  }
  async processOutput(id,channel,after=0,limit=65536) {
    executionIdentity(id);if(!['stdout','stderr'].includes(channel)||!Number.isSafeInteger(after)||after<0||!Number.isSafeInteger(limit)||limit<1||limit>65536)throw new Error('Invalid process output page request.');
    const value=await this.request(`/v1/operations/${encodeURIComponent(id)}/output?channel=${channel}&after=${after}&limit=${limit}`);
    if(!value||value.operation_id!==id||!/^[-A-Za-z0-9_]{1,128}$/.test(value.run_id)||value.channel!==channel||value.encoding!=='hex'||!['capturing','complete','incomplete'].includes(value.state)||typeof value.complete!=='boolean'||value.complete!==(value.state==='complete')||!Number.isSafeInteger(value.offset)||value.offset!==after||!Number.isSafeInteger(value.total_bytes)||value.total_bytes<0||value.total_bytes>64*1024*1024||!Number.isSafeInteger(value.next_offset)||typeof value.data!=='string'||!/^(?:[0-9a-f]{2})*$/.test(value.data)||value.data.length/2>limit||value.next_offset!==after+value.data.length/2||value.next_offset>value.total_bytes||value.complete&&value.next_offset<value.total_bytes)throw new Error('Invalid native process output page.');
    return value;
  }
  inspectEdit(id) { return this.request(`/v1/operations/${encodeURIComponent(id)}/inspection`); }
  async restoreOperation(id,sessionId) {
    executionIdentity(id);executionIdentity(sessionId);
    const run=await this.request(`/v1/operations/${encodeURIComponent(id)}/restore`,{session_id:sessionId});validateRunDTO(run);
    if(run.session_id!==sessionId||run.parent_id||run.graph_root!==true)throw new Error('Native restore run was not bound to this conversation.');return run;
  }
  reconcileProcess(id,note) {
    executionIdentity(id);const bytes=typeof note==='string'?new TextEncoder().encode(note).length:0;
    if(bytes<16||bytes>1024||/[\r\n\0]/.test(note))throw new Error('Enter a one-line reconciliation note of 16–1,024 UTF-8 bytes.');
    return this.request(`/v1/operations/${encodeURIComponent(id)}/reconcile`,{note,acknowledge_possible_side_effects:true});
  }
  inspectA2aTask(id, includeArtifacts = false) { return this.request(`/v1/operations/${encodeURIComponent(id)}/a2a/inspect`, includeArtifacts ? {include_artifacts:true} : {}); }
  resubscribeA2aTask(id) { return this.request(`/v1/operations/${encodeURIComponent(id)}/a2a/resubscribe`, {}, 30000); }
  cancelA2aTask(id) { return this.request(`/v1/operations/${encodeURIComponent(id)}/a2a/cancel`, {}); }
  followUpA2aTask(id,sequence,message) {
    executionIdentity(id);if(!Number.isSafeInteger(sequence)||sequence<1||sequence>64||typeof message!=='string'||!message.trim()||message.length>32768||message.includes('\0'))throw new Error('Invalid A2A follow-up message');
    return this.request(`/v1/operations/${encodeURIComponent(id)}/a2a/follow-up`,{sequence,message},45000);
  }
  decide(id, decision, scope) {
    if (decision !== 'allow' && decision !== 'deny') throw new Error('Decision must be allow or deny.');
    return this.#connectionOperationsSupported?this.#scopedOperations(scope?.run_id,scope?.session_id,id,decision):this.request(`/v1/operations/${encodeURIComponent(id)}/decision`, { decision });
  }
}

// Keeps the rendered transcript bounded while allowing both browser and
// Browser and VS Code views to load older records on demand. Snapshot refreshes merge new
// messages into the already loaded range without moving its older cursor.
class SessionHistoryPager {
  constructor(client,limit=40){if(!client||typeof client!=='object'||!Number.isSafeInteger(limit)||limit<1||limit>1000)throw new Error('Invalid session history pager');this.client=client;this.limit=limit;this.session=undefined;this.messages=[];this.cursor=undefined;this.hasMore=false;this.busy=false;this.epoch=0;}
  async page(session,before){if(typeof this.client.historyPage==='function')return this.client.historyPage(session,this.limit,before);if(before!==undefined||typeof this.client.history!=='function')return{messages:[],has_more:false,next_before:null};const messages=await this.client.history(session);if(!Array.isArray(messages))throw new Error('Invalid session history');return{messages,has_more:false,next_before:null};}
  snapshot(){return{history:this.messages,historyHasMore:this.hasMore,historyLoading:this.busy};}
  clear(){this.epoch++;this.session=undefined;this.messages=[];this.cursor=undefined;this.hasMore=false;this.busy=false;return this.snapshot();}
  async select(session){executionIdentity(session);const epoch=++this.epoch;this.session=session;this.messages=[];this.cursor=undefined;this.hasMore=false;this.busy=true;try{const page=await this.page(session);if(epoch!==this.epoch)return this.snapshot();this.messages=page.messages;this.hasMore=page.has_more;this.cursor=page.next_before||this.messages[0]?.id;this.busy=false;return this.snapshot();}finally{if(epoch===this.epoch)this.busy=false;}}
  async refresh(){const session=this.session,epoch=this.epoch;if(!session||this.busy)return this.snapshot();this.busy=true;try{const page=await this.page(session);if(epoch!==this.epoch||session!==this.session)return this.snapshot();if(typeof this.client.historyPage!=='function')this.messages=page.messages;else{const previousHasMore=this.hasMore,merged=new Map(this.messages.map(item=>[item.id,item]));for(const item of page.messages)merged.set(item.id,item);this.messages=[...merged.values()].sort((a,b)=>a.seq-b.seq);this.hasMore=previousHasMore;this.cursor=previousHasMore?this.messages[0]?.id:undefined;}this.busy=false;return this.snapshot();}finally{if(epoch===this.epoch)this.busy=false;}}
  async older(){if(!this.session||!this.hasMore||this.busy)return this.snapshot();const session=this.session,epoch=this.epoch,before=this.cursor||this.messages[0]?.id;if(!before){this.hasMore=false;return this.snapshot();}this.busy=true;try{const page=await this.page(session,before);if(epoch!==this.epoch||session!==this.session)return this.snapshot();const merged=new Map(this.messages.map(item=>[item.id,item]));for(const item of page.messages)merged.set(item.id,item);this.messages=[...merged.values()].sort((a,b)=>a.seq-b.seq);this.hasMore=page.has_more;this.cursor=page.next_before||this.messages[0]?.id;this.busy=false;return this.snapshot();}finally{if(epoch===this.epoch)this.busy=false;}}
}

function terminalIdentity(value){if(typeof value!=='string'||!/^[a-f0-9]{32}$/.test(value))throw new Error('Invalid terminal identity');}
function terminalDimensions(rows,columns){if(!Number.isSafeInteger(rows)||rows<5||rows>300||!Number.isSafeInteger(columns)||columns<20||columns>500)throw new Error('Invalid terminal dimensions');}
function validateTerminalMetadata(value,session){
  exactFields(value,['id','session_id','title','rows','columns','running','exit_code','output_sequence']);
  terminalIdentity(value.id);
  if(session!==undefined&&value.session_id!==session||typeof value.session_id!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(value.session_id)||
    typeof value.title!=='string'||!value.title||value.title.length>128||/[\x00-\x1f\x7f]/.test(value.title)||
    !Number.isSafeInteger(value.rows)||value.rows<5||value.rows>300||!Number.isSafeInteger(value.columns)||value.columns<20||value.columns>500||
    typeof value.running!=='boolean'||value.running&&value.exit_code!==null||!value.running&&(!Number.isSafeInteger(value.exit_code)||value.exit_code< -2147483648||value.exit_code>2147483647)||
    !Number.isSafeInteger(value.output_sequence)||value.output_sequence<0)throw new Error('Invalid terminal metadata');
  return value;
}
function validateTerminalOutput(value){
  exactFields(value,['encoding','data','next_sequence','truncated','running','exit_code']);
  if(value.encoding!=='hex'||typeof value.data!=='string'||value.data.length>131072||value.data.length%2||!/^(?:[a-f0-9]{2})*$/.test(value.data)||
    !Number.isSafeInteger(value.next_sequence)||value.next_sequence<0||typeof value.truncated!=='boolean'||typeof value.running!=='boolean'||
    value.running&&value.exit_code!==null||!value.running&&(!Number.isSafeInteger(value.exit_code)||value.exit_code< -2147483648||value.exit_code>2147483647))throw new Error('Invalid terminal output');
  return value;
}

function validateWorkspace(value){
  exactFields(value,['configured','root','workspace_id','authority_id']);
  if(typeof value.configured!=='boolean')throw new Error('Invalid backend workspace metadata.');
  if(!value.configured){if(value.root!==null||value.workspace_id!==null||value.authority_id!==null)throw new Error('Invalid backend workspace metadata.');}
  else if(typeof value.root!=='string'||!value.root||value.root.length>32760||/[\x00-\x1f]/.test(value.root)||typeof value.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(value.workspace_id)||typeof value.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(value.authority_id))throw new Error('Invalid backend workspace identity.');
  return value;
}
function parseWorkspaceCommand(input,catalogue){
  if(typeof input!=='string'||!input.startsWith('/'))return undefined;
  const match=/^\/([A-Za-z0-9_-]+(?:\/[A-Za-z0-9_-]+)*)(?:[ \t]+([\s\S]*))?$/.exec(input);
  if(!match)throw new Error('Use /command-name followed by optional arguments.');
  if(!catalogue||!Array.isArray(catalogue.commands)||catalogue.commands.length>128)throw new Error('The native workspace command catalogue is invalid.');
  const command=catalogue.commands.find(item=>item?.id===match[1]);if(!command)throw new Error(`Workspace command /${match[1]} was not found.`);
  return {id:command.id,arguments:match[2]??''};
}
function validateWorkspaceMembership(value,workspace){
  const baseFields=['workspace_id','revision','primary_folder_id','folders'],enrichedFields=[...baseFields,'name','root'];
  if(!value||typeof value!=='object'||Array.isArray(value)||![baseFields,enrichedFields].some(fields=>Object.keys(value).length===fields.length&&fields.every(field=>Object.hasOwn(value,field))))throw new Error('Invalid metadata field set');
  if(value.workspace_id!==workspace.workspace_id||!Number.isSafeInteger(value.revision)||value.revision<1||value.primary_folder_id!==workspace.workspace_id||!Array.isArray(value.folders)||!value.folders.length||value.folders.length>256)throw new Error('The native host returned invalid workspace membership.');
  const nameOf=root=>root.split(/[\\/]+/).filter(Boolean).at(-1)||root,ids=new Set(),roots=new Set();
  if(Object.hasOwn(value,'name')&&(value.root!==workspace.root||value.name!==nameOf(workspace.root)))throw new Error('The native host changed the workspace identity.');
  const folders=value.folders.map((folder,ordinal)=>{
    const fields=['folder_id','root'],enriched=['folder_id','root','name','ordinal'];
    if(!folder||typeof folder!=='object'||Array.isArray(folder)||![fields,enriched].some(expected=>Object.keys(folder).length===expected.length&&expected.every(field=>Object.hasOwn(folder,field))))throw new Error('Invalid metadata field set');
    if(typeof folder.folder_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(folder.folder_id)||ids.has(folder.folder_id)||typeof folder.root!=='string'||!folder.root||folder.root.length>32760||/[\x00-\x1f]/.test(folder.root)||(!/^[A-Za-z]:[\\/]/.test(folder.root)&&!/^\\\\[^\\/]+[\\/][^\\/]+(?:[\\/]|$)/.test(folder.root))||roots.has(folder.root))throw new Error('The native host returned an invalid workspace folder.');
    if(Object.hasOwn(folder,'name')&&(folder.name!==nameOf(folder.root)||folder.ordinal!==ordinal))throw new Error('The native host returned inconsistent workspace folder metadata.');
    ids.add(folder.folder_id);roots.add(folder.root);return {...folder,name:nameOf(folder.root),ordinal};
  });
  if(folders[0].folder_id!==value.primary_folder_id||folders[0].root!==workspace.root)throw new Error('The native host changed the workspace primary folder.');
  return {...value,name:nameOf(workspace.root),root:workspace.root,folders};
}
function validateVcsFolderId(value){if(typeof value!=='string'||!value||value.length>256||/[\x00-\x1f]/.test(value))throw new Error('Invalid Git workspace folder selection.');return value;}
function vcsFolderQuery(folderId){return folderId===undefined?'':'?'+new URLSearchParams({folder_id:validateVcsFolderId(folderId)}).toString();}
function validateVcsStatus(value){
  if(!Array.isArray(value)||value.length>4096)throw new Error('Invalid native Git status');
  for(const item of value){
    if(!item||typeof item.file!=='string'||!item.file||item.file.length>32768||/[\x00]/.test(item.file)||
      !Number.isSafeInteger(item.additions)||item.additions<0||!Number.isSafeInteger(item.deletions)||item.deletions<0||
      !['added','deleted','modified'].includes(item.status))throw new Error('Invalid native Git status');
  }
  return value;
}
function validateVcsBase(value){
  if(value===null)return null;
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==3||typeof value.name!=='string'||!value.name||value.name.length>1024||/[\r\n\0]/.test(value.name)||typeof value.ref!=='string'||!/^[a-f0-9]{40,64}$/.test(value.ref)||!['default','reflog'].includes(value.source))throw new Error('Invalid native Git review base.');
  return value;
}
function validateVcsDiff(value){
  if(!Array.isArray(value)||value.length>4096)throw new Error('Invalid native Git diff.');
  for(const item of value)if(!item||typeof item!=='object'||Array.isArray(item)||Object.keys(item).length!==5||typeof item.file!=='string'||!item.file||item.file.length>32768||/[\0]/.test(item.file)||typeof item.patch!=='string'||new TextEncoder().encode(item.patch).length>4*1024*1024||!Number.isSafeInteger(item.additions)||item.additions<0||!Number.isSafeInteger(item.deletions)||item.deletions<0||!['added','deleted','modified'].includes(item.status))throw new Error('Invalid native Git diff entry.');
  return value;
}

// Observation only. Events are read before owners so concurrent admission is
// covered; bounded pages preserve the exact committed cursor without replaying
// execution. Both hosts share the same identity and operation checks.
// Traverse only native-declared invocation edges. A nested controller is not
// an independent root; IDs and immediate parents remain backend authority.
async function observeGraphTree(client,root,session){
  if(!root?.run||root.run.session_id!==session||root.run.graph_root!==true||root.run.parent_id)throw new Error('Graph root observation changed');
  const children=[],graphs=[],definitions=new Map(),paths=new Map(),seen=new Set([root.run.id]);
  const canonical=value=>{if(Array.isArray(value))return value.map(canonical);if(value&&typeof value==='object')return Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])]));return value;};
  const owners=[{record:root,path:[],depth:0}];let declaredCount=0;
  for(let at=0;at<owners.length;++at){
    const {record,path,depth}=owners[at];
    if(!record.spec||!Array.isArray(record.spec.nodes)||record.spec.nodes.length>100)throw new Error('Invalid graph specification observation');
    declaredCount+=record.spec.nodes.length;if(declaredCount>256)throw new Error('Expanded graph observation exceeds limits');
    const declared=new Map();for(const node of record.spec.nodes){if(!node||typeof node.id!=='string'||declared.has(node.id))throw new Error('Invalid declared graph node');declared.set(node.id,node);}
    const batch=await client.graphChildren(record.run.id);if(!Array.isArray(batch)||batch.length>100)throw new Error('Invalid graph child observation');
    const claimed=new Set();
    for(const child of batch){
      validateRunDTO(child);const definition=declared.get(child.node_id);
      if(child.parent_id!==record.run.id||child.session_id!==session||seen.has(child.id)||claimed.has(child.node_id)||!definition||!['agent','tool','graph'].includes(definition.type)||child.graph_root!==(definition.type==='graph'))throw new Error('Graph child ownership identity changed');
      if(children.length>=256)throw new Error('Graph descendant observation exceeds limits');
      seen.add(child.id);claimed.add(child.node_id);children.push(child);definitions.set(child.id,definition);const nodePath=[...path,child.node_id];paths.set(child.id,nodePath);
      if(child.graph_root){
        if(depth>=3)throw new Error('Graph invocation observation exceeds depth limit');
        const nested=await client.graph(child.id);
        if(!nested?.run||nested.run.id!==child.id||nested.run.node_id!==child.node_id||nested.run.parent_id!==record.run.id||nested.run.session_id!==session||nested.run.graph_root!==true||nested.graph_id!==definition.graph_id||nested.graph_revision!==definition.graph_revision||JSON.stringify(canonical(nested.spec))!==JSON.stringify(canonical(definition.graph_spec)))throw new Error('Graph invocation snapshot changed');
        graphs.push({record:nested,path:nodePath});owners.push({record:nested,path:nodePath,depth:depth+1});
      }
    }
  }
  return {children,graphs,definitions,paths};
}

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
  for(const child of children){const history=await client.ownedChildHistory(run.id,child.run.id,session);if(!Array.isArray(history))throw new Error('Invalid owned child history');histories[child.run.id]=history;}
  for(const id of owned){const batch=await client.operations(id,session);if(!Array.isArray(batch)||batch.some(value=>!value||value.run_id!==id))throw new Error('Owned operation identity changed');operations.push(...batch);}
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
function agentPermissions(value){
  if(value===undefined)return [];
  if(!Array.isArray(value)||value.length>128)throw new Error('Invalid backend agent permissions.');
  for(const rule of value){if(!rule||typeof rule!=='object'||Array.isArray(rule)||Object.keys(rule).length!==3||typeof rule.action!=='string'||!rule.action||rule.action.length>128||rule.action!=='*'&&!/^[A-Za-z0-9_.-]+$/.test(rule.action)||typeof rule.resource!=='string'||!rule.resource||new TextEncoder().encode(rule.resource).length>1024||rule.resource.includes('\\')||rule.resource.includes(':')||rule.resource.startsWith('/')||rule.resource.split('/').includes('..')||!['allow','ask','deny'].includes(rule.effect))throw new Error('Invalid backend agent permission rule.');}
  return value;
}
function sameAgentPermissions(a,b){return JSON.stringify(agentPermissions(a))===JSON.stringify(agentPermissions(b));}
function agentPlacement(agent){if(Object.hasOwn(agent,'placement')&&(!['thread','process'].includes(agent.placement)||agent.placement==='process'&&agent.mode!=='primary'))throw new Error('Invalid backend agent placement.');}
function validateAgentCatalogue(value){
  if(!value||typeof value!=='object'||Object.keys(value).length!==1||!Array.isArray(value.agents)||value.agents.length>64)throw new Error('Invalid backend agent catalogue.');
  const ids=new Set();let defaults=0;for(const agent of value.agents){if(!agent||typeof agent!=='object'||Object.keys(agent).length!==(Object.hasOwn(agent,'permissions')?9:8)+(Object.hasOwn(agent,'placement')?1:0)||typeof agent.name!=='string'||!agent.name.trim()||new TextEncoder().encode(agent.name).length>128||typeof agent.description!=='string'||new TextEncoder().encode(agent.description).length>2048||!['all','primary','subagent'].includes(agent.mode)||typeof agent.hidden!=='boolean'||typeof agent.default!=='boolean'||agent.default&&(agent.hidden||agent.mode==='subagent'))throw new Error('Invalid backend agent metadata.');agentPermissions(agent.permissions);agentPlacement(agent);agentIdentity(agent.id);if(ids.has(agent.id)||!Number.isSafeInteger(agent.revision)||agent.revision<1||typeof agent.model_id!=='string'||agent.model_id.length>256||agent.model_id.startsWith('sk-'))throw new Error('Invalid or duplicate backend agent metadata.');ids.add(agent.id);if(agent.default&&++defaults>1)throw new Error('Multiple default backend agents.');}
  return value;
}
function validateSessionAgent(value,session,catalogue){
  if(!value||typeof value!=='object'||Object.keys(value).length!==4||value.session_id!==session||!Number.isSafeInteger(value.revision)||value.revision<0||typeof value.editable!=='boolean'||!(value.selected===null||value.selected&&typeof value.selected==='object'&&Object.keys(value.selected).length===(Object.hasOwn(value.selected,'permissions')?9:8)+(Object.hasOwn(value.selected,'placement')?1:0)))throw new Error('Invalid backend conversation agent state.');
  if(value.selected){const selected=value.selected;agentPermissions(selected.permissions);agentPlacement(selected);agentIdentity(selected.id);if(typeof selected.name!=='string'||!selected.name.trim()||new TextEncoder().encode(selected.name).length>128||typeof selected.description!=='string'||new TextEncoder().encode(selected.description).length>2048||!['all','primary','subagent'].includes(selected.mode)||typeof selected.hidden!=='boolean'||typeof selected.default!=='boolean'||!Number.isSafeInteger(selected.revision)||selected.revision<1||typeof selected.model_id!=='string'||selected.model_id.length>256||selected.model_id.startsWith('sk-'))throw new Error('Invalid selected backend agent metadata.');if(catalogue&&!catalogue.agents.some(item=>item.id===selected.id&&item.revision===selected.revision&&item.model_id===selected.model_id&&item.name===selected.name&&item.description===selected.description&&item.mode===selected.mode&&item.hidden===selected.hidden&&item.default===selected.default&&item.placement===selected.placement&&sameAgentPermissions(item.permissions,selected.permissions)))throw new Error('Selected agent differs from its backend catalogue.');}
  return value;
}
function validateSessionModel(value,session){
  if(!value||typeof value!=='object'||Object.keys(value).length!==4||value.session_id!==session||!Number.isSafeInteger(value.revision)||value.revision<0||typeof value.editable!=='boolean'||!(value.selected===null||value.selected&&typeof value.selected==='object'&&!Array.isArray(value.selected)&&Object.keys(value.selected).length===3))throw new Error('Invalid backend conversation model state.');
  if(value.selected){profileIdentity(value.selected.model_id);profileIdentity(value.selected.provider_profile_id);if(typeof value.selected.provider_route_id!=='string'||!/^[A-Za-z0-9_.-]{1,256}$/.test(value.selected.provider_route_id))throw new Error('Invalid provider route identity.');}
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
    if(selection.selected&&!catalogue.agents.some(item=>item.id===selection.selected.id&&item.revision===selection.selected.revision&&item.model_id===selection.selected.model_id&&item.name===selection.selected.name&&item.description===selection.selected.description&&item.mode===selection.selected.mode&&item.hidden===selection.selected.hidden&&item.default===selection.selected.default&&sameAgentPermissions(item.permissions,selection.selected.permissions)))throw new Error('The selected agent revision is no longer in the current catalogue; clear or replace it.');
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
async function readCommittedEventStream(response,{root,session,scope,after,binding,signal,onEvent,onObservation,authorizeEvent}){
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
      planFields(data,binding?['run','after','scope','binding']:['run','after','scope']);validateRunDTO(data.run);
      if(binding){validateConnectionBindingIdentity(data.binding);if(Object.keys(binding).some(key=>data.binding[key]!==binding[key]))throw new Error('Native event stream belongs to another connection');}
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
      // EOF can split a frame or even a UTF-8 character. Neither is a committed
      // event: discard the unfinished bytes and resume the read-only stream at
      // the last accepted cursor. Complete malformed frames/invalid UTF-8 still
      // fail above/below, and the subscription bounds transport retries.
      if(chunk.done){const error=new Error('Native event stream ended without a complete end frame');error.eventTransportUnavailable=true;throw error;}
      if(!ArrayBuffer.isView(chunk.value))throw new Error('Invalid event stream bytes');total+=chunk.value.byteLength;if(total>64*1024*1024)throw new Error('Event stream exceeds its connection limit');
      pending+=decoder.decode(chunk.value,{stream:true});pending=pending.replace(/\r\n/g,'\n');
      let end;while((end=pending.indexOf('\n\n'))!==-1){if(end>16*1024*1024+1024)throw new Error('Event stream frame exceeds its limit');const value=await frame(pending.slice(0,end));pending=pending.slice(end+2);if(value)return value;}
      if(pending.length>16*1024*1024+1024)throw new Error('Event stream frame exceeds its limit');
    }
  }catch(error){if(error&&typeof error==='object')error.eventCursor=cursor;throw error;}
  finally{signal?.removeEventListener('abort',detach);try{await reader.cancel();}catch{}reader.releaseLock();}
}
class EventStreamSubscription {
  constructor({current,onEvent,onObservation,onEnd,onError,schedule=(callback,delay)=>globalThis.setTimeout(callback,delay),unschedule=timer=>globalThis.clearTimeout(timer)}){
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
    this.pin=pin;this.cursor=pin.after;this.failedAttempts=0;this.busyAttempts=0;this.finished=false;this.connect(pin,this.epoch);
  }
  stop(){this.epoch++;this.unschedule(this.timer);this.timer=undefined;this.abort?.abort();this.abort=undefined;this.pin=undefined;}
  dispose(){this.stop();this.disposed=true;}
  async connect(pin,epoch){
    if(!this.valid(pin,epoch)||this.finished)return;const abort=new AbortController();this.abort=abort;
    try{
      const consume=async(callback,...values)=>{try{return await callback?.(...values);}catch(error){const rejected=new Error('Event observer rejected delivery',{cause:error});rejected.eventConsumerFailure=true;throw rejected;}};
      const result=await pin.client.eventStream(pin.root,{scope:pin.scope,session_id:pin.session,after:this.cursor,signal:abort.signal,onObservation:async value=>{if(!this.valid(pin,epoch))throw new Error('Event subscription retired');await consume(this.onObservation,value,pin);if(!this.valid(pin,epoch))throw new Error('Event subscription retired');this.busyAttempts=0;},onEvent:async event=>{if(!this.valid(pin,epoch))throw new Error('Event subscription retired');await consume(this.onEvent,event,pin);if(!this.valid(pin,epoch))throw new Error('Event subscription retired');this.cursor=event.seq;this.failedAttempts=0;this.busyAttempts=0;}});
      if(!this.valid(pin,epoch))return;
      if(!result||result.cursor!==this.cursor)throw new Error('Event subscription acknowledgement changed');
      if(result.reason==='terminal'){this.finished=true;await this.onEnd?.(result,pin);return;}
      if(result.reason==='reauthenticate'){this.finished=true;const error=new Error('Reconnect this profile to continue event observation');error.status=401;this.notifyError(error,pin);return;}
      if(result.reason!=='reconnect'){this.finished=true;this.notifyError(new Error('Native event observation was interrupted; reconnect this profile'),pin);return;}
      this.retry(pin,epoch,0);
    }catch(error){
      if(!this.valid(pin,epoch)||abort.signal.aborted)return;
      // A full stream lease is released asynchronously. Keep its retry bounded
      // in rate, while protocol/identity and consumer failures still stop.
      if(!this.finished&&!error.eventConsumerFailure&&error.status===503){
        this.busyAttempts=Math.min(5,this.busyAttempts+1);this.retry(pin,epoch,Math.min(8000,500*2**(this.busyAttempts-1)));return;
      }
      if(!this.finished&&!error.eventConsumerFailure&&error.eventTransportUnavailable===true){
        this.failedAttempts++;if(this.failedAttempts<=3){this.retry(pin,epoch,Math.min(4000,500*2**(this.failedAttempts-1)));return;}
      }
      this.finished=true;this.notifyError(error,pin);
    }finally{if(this.abort===abort)this.abort=undefined;}
  }
  retry(pin,epoch,delay){if(!this.valid(pin,epoch))return;this.timer=this.schedule(()=>{this.timer=undefined;if(this.valid(pin,epoch))this.connect(pin,epoch);},delay);}
  notifyError(error,pin){try{Promise.resolve(this.onError?.(error,pin)).catch(()=>{});}catch{} }
}
function executionIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(value))throw new Error('Invalid execution identity');return value;}
// Node metadata is not a route/run identity. Graph labels allow periods up to
// 64 bytes, delegated nodes use d.<id>, and dynamic execution IDs allow 128.
function runNodeIdentity(value){if(typeof value!=='string'||!(/^[A-Za-z0-9_-]{1,128}$/.test(value)||/^[A-Za-z0-9_.-]{1,64}$/.test(value)))throw new Error('Invalid native run node identity');return value;}
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
  if(value.parent_id!=='')executionIdentity(value.parent_id);if(value.node_id!=='')runNodeIdentity(value.node_id);if(typeof value.graph_root!=='boolean'||!['queued','running','paused','completed','failed','cancelled'].includes(value.state))throw new Error('Invalid actual run metadata');
  if(value.provider_context!==undefined){planFields(value.provider_context,['profile_id','route_id','provider','wire','model_id','profile_revision']);for(const key of ['profile_id','route_id','provider','model_id'])profileIdentity(value.provider_context[key]);if(!['chat-completions','responses','anthropic-messages','gemini-generate-content'].includes(value.provider_context.wire))throw new Error('Invalid public provider identity');planInteger(value.provider_context.profile_revision,1);}
  return value;
}
function validatePlanOwner(value,root){validateRunDTO(value);if(value.id!==root||value.parent_id!==''||value.node_id!==''||value.graph_root!==false)throw new Error('Planning root ownership changed');return value;}
function validateNestedPlanOwner(value,root,parentPlan,parentDepth=0){
  planFields(value,['owner_run_id','parent_plan_id','parent_node_label','depth','run','plan','questions','revisions','calls']);executionIdentity(value.owner_run_id);executionIdentity(value.parent_plan_id);planLabel(value.parent_node_label);planInteger(value.depth,1);validateRunDTO(value.run);
  if(value.depth!==parentDepth+1||value.depth>8||value.parent_plan_id!==parentPlan.id||value.run.id!==value.owner_run_id||value.run.parent_id!==root||value.run.graph_root)throw new Error('Nested planning owner changed');
  const parent=parentPlan.nodes.find(node=>node.id===value.parent_node_label);if(!parent||parent.type!=='agent'||parent.child_run_id!==value.owner_run_id||parent.backend_node_id!==value.run.node_id)throw new Error('Nested plan parent node changed');
  const plan=value.plan;planFields(plan,['id','root_run_id','revision','state_sequence','state','ready','blocked','claimed','waiting_human','finished','report_ready','halted','retired_labels','planned_children_reserved','planned_humans_reserved','humans_published','nodes']);executionIdentity(plan.id);planPreconditions(plan.revision,plan.state_sequence);
  if(plan.root_run_id!==root||!['active','waiting_human','completed','failed','cancelled','uncertain','interrupted'].includes(plan.state)||!Array.isArray(plan.nodes)||!plan.nodes.length||plan.nodes.length>32)throw new Error('Nested plan changed');
  for(const key of ['finished','report_ready','halted'])if(typeof plan[key]!=='boolean')throw new Error('Invalid nested plan readiness');for(const key of ['planned_children_reserved','planned_humans_reserved','humans_published'])planInteger(plan[key]);
  const nodes=new Map();for(const node of plan.nodes){planDefinition(node,true);if(nodes.has(node.id)||node.definition_revision>plan.revision)throw new Error('Nested plan node revision changed');nodes.set(node.id,node);}for(const node of nodes.values())for(const edge of node.depends_on)if(!nodes.has(edge.task))throw new Error('Nested plan dependency is unowned');
  for(const key of ['ready','blocked','claimed','waiting_human','retired_labels'])if(!Array.isArray(plan[key])||new Set(plan[key]).size!==plan[key].length||plan[key].some(label=>!nodes.has(label)))throw new Error('Invalid nested plan readiness labels');
  if(!Array.isArray(value.questions)||value.questions.length>8||!Array.isArray(value.calls)||value.calls.length>16||!Array.isArray(value.revisions)||value.revisions.length>16)throw new Error('Invalid nested plan ledgers');
  const questions=new Set();for(const question of value.questions){planFields(question,['id','plan_id','root_run_id','label','backend_node_id','question','state','definition_revision','published_event_seq','expires_unix_ms','input_json','input_event_seq']);executionIdentity(question.id);planLabel(question.label);planText(question.question,8192);planText(question.input_json,16384,true);for(const key of ['definition_revision','published_event_seq','expires_unix_ms'])planInteger(question[key],1);if(question.input_event_seq!==null)planInteger(question.input_event_seq,1);const node=nodes.get(question.label);if(questions.has(question.id)||question.plan_id!==plan.id||question.root_run_id!==root||!node||node.type!=='human'||node.backend_node_id!==question.backend_node_id||node.definition_revision!==question.definition_revision||node.human_request_id!==question.id||node.question!==question.question||!['waiting','answered','expired','cancelled'].includes(question.state))throw new Error('Nested planning question ownership changed');questions.add(question.id);}
  const calls=new Map();let revision=0;for(const call of value.calls){planFields(call,['id','plan_id','root_run_id','provider_tool_call_id','origin_attempt_id','name','state','accepted_revision','accepted_event_seq','result_event_seq','conversation_commit_seq','continuation_attempt_id','pending'],['response']);for(const key of ['id','plan_id','root_run_id','origin_attempt_id'])executionIdentity(call[key]);providerCallIdentity(call.provider_tool_call_id);if(call.continuation_attempt_id!=='')executionIdentity(call.continuation_attempt_id);planInteger(call.accepted_revision,1);planInteger(call.accepted_event_seq,1);for(const key of ['result_event_seq','conversation_commit_seq'])if(call[key]!==null)planInteger(call[key],1);if(calls.has(call.id)||call.plan_id!==plan.id||call.root_run_id!==root||call.accepted_revision<=revision||call.accepted_revision>plan.revision||!['plan_tasks','revise_plan'].includes(call.name)||!['accepted','report_ready','turn_committed','interrupted','cancelled'].includes(call.state)||call.pending!==(['accepted','report_ready'].includes(call.state)&&call.conversation_commit_seq===null)||Object.hasOwn(call,'response')!==call.pending)throw new Error('Nested plan call ownership changed');if(call.pending)planResponse(call.response);calls.set(call.id,call);revision=call.accepted_revision;}
  revision=0;for(const saved of value.revisions){planFields(saved,['plan_id','revision','accepted_event_seq','call_id','spec']);planInteger(saved.revision,1);planInteger(saved.accepted_event_seq,1);const call=calls.get(saved.call_id);if(saved.plan_id!==plan.id||saved.revision<=revision||!call||call.accepted_revision!==saved.revision||call.accepted_event_seq!==saved.accepted_event_seq)throw new Error('Nested plan revision ownership changed');planFields(saved.spec,['nodes']);if(!Array.isArray(saved.spec.nodes)||!saved.spec.nodes.length||saved.spec.nodes.length>32)throw new Error('Invalid nested accepted topology');const labels=new Set();for(const node of saved.spec.nodes){planDefinition(node);if(labels.has(node.id))throw new Error('Duplicate nested accepted planning label');labels.add(node.id);}for(const node of saved.spec.nodes)for(const edge of node.depends_on)if(!labels.has(edge.task))throw new Error('Unowned nested accepted dependency');revision=saved.revision;}
  if(calls.size!==value.revisions.length||revision!==plan.revision)throw new Error('Nested plan revision ledger changed');return value;
}
function validatePlanning(value){planFields(value,['enabled','tools']);if(typeof value.enabled!=='boolean'||!Array.isArray(value.tools)||JSON.stringify(value.tools)!==JSON.stringify(value.enabled?['inspect_plan','plan_tasks','revise_plan']:[]))throw new Error('Invalid planning capability');return value;}
function validateOwnedChild(child,root,session){
  const value=child?.run;
  if(!value||value.parent_id!==root||value.session_id!==session||value.graph_root||typeof value.id!=='string'||typeof child.batch_id!=='string'||typeof child.task_id!=='string'||typeof child.preset_id!=='string')throw new Error('Owned child identity changed');
  executionIdentity(value.id);
  if(child.kind==='delegated_leaf'){planInteger(child.preset_revision,1);if(!child.batch_id||!child.task_id||child.preset_id!=='workspace.inspect')throw new Error('Owned child identity changed');}
  else if(child.kind==='dynamic_agent'){
    validateDynamicOwnedChild(child,value);
    if(!['workspace.inspect','workspace.coding'].includes(child.preset_id))throw new Error('Owned dynamic child claim changed');
  }else if(child.kind==='dynamic_tool'){
    validateDynamicOwnedChild(child,value);
    if(child.preset_id!==''||child.preset_revision!==0)throw new Error('Owned dynamic tool claim changed');
  }else throw new Error('Owned child identity changed');
  return child;
}
function validateDynamicOwnedChild(child,value){
  planFields(child,['run','kind','batch_id','task_id','preset_id','preset_revision','plan_id','node_label','claim_id','definition_revision','claim_revision']);validateRunDTO(value);executionIdentity(child.plan_id);executionIdentity(child.claim_id);executionIdentity(value.node_id);planLabel(child.node_label);planInteger(child.definition_revision,1);
  if(child.claim_revision!==child.definition_revision||child.batch_id!==''||child.task_id!=='')throw new Error('Owned dynamic child claim changed');
  if(child.kind==='dynamic_agent')planInteger(child.preset_revision,1);
  else if(child.preset_revision!==0)throw new Error('Owned dynamic tool claim changed');
}
function planUsage(value){
  const numbers=['input_tokens','output_tokens','total_tokens','prompt_tokens','completion_tokens','cached_tokens','reasoning_tokens','cache_read_input_tokens','cache_creation_input_tokens','promptTokenCount','candidatesTokenCount','totalTokenCount','thoughtsTokenCount','cachedContentTokenCount'];
  const nested=['input_tokens_details','output_tokens_details','prompt_tokens_details','completion_tokens_details'];planFields(value,[],[...numbers,...nested]);
  for(const [key,count] of Object.entries(value))if(numbers.includes(key))planInteger(count);else {planFields(count,[],['cached_tokens','reasoning_tokens','audio_tokens','accepted_prediction_tokens','rejected_prediction_tokens']);for(const number of Object.values(count))planInteger(number);}
}
function planResponse(value){planFields(value,[],['model','usage','elapsed_ms','first_token_ms']);if(value.model!==undefined)planText(value.model,256,true);if(value.usage!==undefined)planUsage(value.usage);for(const key of ['elapsed_ms','first_token_ms'])if(value[key]!==undefined)planInteger(value[key]);}
const dynamicPlanReadTools=new Set(['read_repository_instructions','read_file','list_files','search_files','find_files','glob_files']);
function isDynamicPlanTool(value){
  return dynamicPlanReadTools.has(value)||value==='run_process'||/^mcp_[a-f0-9]{48}$/.test(value)||/^a2a_delegate_[a-f0-9]{24}$/.test(value);
}
function planDefinition(value,runtime=false){
  const fields=['id','type','depends_on'];if(value?.type==='agent')fields.push('objective','preset');else if(value?.type==='human')fields.push('question');else if(value?.type==='tool')fields.push('tool','arguments_json');else throw new Error('Invalid planning node type');
  if(runtime)fields.push('backend_node_id','definition_revision','state','protected','effect_state',...(value.type==='agent'?['preset_revision']:[]));
  planFields(value,fields,runtime?['claim_revision','claim_id','child_run_id','child_state','human_request_id','outcome_json','settled_event_seq']:[]);planLabel(value.id);
  if(value.type==='agent'){planText(value.objective,8192);if(!['workspace.inspect','workspace.coding'].includes(value.preset))throw new Error('Invalid planning preset');}
  else if(value.type==='human')planText(value.question,8192);
  else {if(typeof value.tool!=='string'||!isDynamicPlanTool(value.tool))throw new Error('Invalid deterministic planning tool');planText(value.arguments_json,65536);const args=JSON.parse(value.arguments_json);if(!args||typeof args!=='object'||Array.isArray(args))throw new Error('Invalid deterministic planning arguments');}
  if(!Array.isArray(value.depends_on)||value.depends_on.length>32)throw new Error('Invalid planning dependencies');const unique=new Set();for(const edge of value.depends_on){planFields(edge,['task','require']);planLabel(edge.task);if(unique.has(edge.task)||edge.task===value.id||!['success','observed'].includes(edge.require))throw new Error('Invalid planning dependency');unique.add(edge.task);}
  if(runtime){executionIdentity(value.backend_node_id);planInteger(value.definition_revision,1);if(typeof value.protected!=='boolean'||!['pending','blocked','claimed','waiting_human','settled','skipped','cancelled','uncertain'].includes(value.state)||!['none','succeeded','failed','cancelled','uncertain'].includes(value.effect_state))throw new Error('Invalid actual planning node state');if(value.type==='agent')planInteger(value.preset_revision,1);
    if(value.claim_revision!==undefined){if(value.claim_revision!==value.definition_revision)throw new Error('Planning claim revision changed');executionIdentity(value.claim_id);}else if(value.claim_id!==undefined)throw new Error('Planning claim is unbound');
    for(const key of ['child_run_id','human_request_id'])if(value[key]!==undefined)executionIdentity(value[key]);if(value.child_state!==undefined&&!['queued','running','paused','completed','failed','cancelled'].includes(value.child_state))throw new Error('Invalid planning child state');if(value.settled_event_seq!==undefined)planInteger(value.settled_event_seq,1);
    if(value.outcome_json!==undefined){planText(value.outcome_json,32768);const outcome=JSON.parse(value.outcome_json);if(!outcome||typeof outcome!=='object'||Array.isArray(outcome))throw new Error('Invalid actual planning outcome');}
  }
}
function validatePlanObservation(value,root){
  planFields(value,['run','enabled','plan','questions','revisions','calls','nested_plans','policy','budget']);validatePlanOwner(value.run,root);if(typeof value.enabled!=='boolean')throw new Error('Invalid planning capability');
  if(value.policy!==null){const policy=value.policy;planFields(policy,['revision','catalogue_finalized','limits','presets']);planInteger(policy.revision,1);if(typeof policy.catalogue_finalized!=='boolean'||!Array.isArray(policy.presets)||policy.presets.length>2)throw new Error('Invalid captured planning policy');planFields(policy.limits,['max_nodes','max_revisions','max_humans','change_bytes','human_expiry_ms','max_parent_turns']);for(const counter of Object.values(policy.limits))planInteger(counter,1);const ids=new Set();for(const preset of policy.presets){planFields(preset,['id','revision','readonly','turn_limit']);if(!['workspace.inspect','workspace.coding'].includes(preset.id)||ids.has(preset.id)||preset.readonly!==(preset.id==='workspace.inspect'))throw new Error('Invalid captured planning preset');ids.add(preset.id);planInteger(preset.revision,1);planInteger(preset.turn_limit,1);}}
  if(value.budget!==null){const budget=value.budget;planFields(budget,['policy_id','policy_revision','revision','max_children','max_parallel','max_model_calls','wall_limit_ms','children_admitted','planned_children_reserved','model_calls_reserved','parent_calls_held','parent_model_calls_reserved']);if(budget.policy_id!=='native.dynamic-plan')throw new Error('Invalid captured planning budget');for(const [key,counter] of Object.entries(budget))if(key!=='policy_id')planInteger(counter,['children_admitted','planned_children_reserved','model_calls_reserved','parent_calls_held','parent_model_calls_reserved'].includes(key)?0:1);}
  if(!Array.isArray(value.questions)||value.questions.length>8||!Array.isArray(value.revisions)||value.revisions.length>16||!Array.isArray(value.calls)||value.calls.length>16)throw new Error('Invalid planning ledgers');
  if(!Array.isArray(value.nested_plans)||value.nested_plans.length>8)throw new Error('Invalid nested planning owners');
  if(value.plan===null){if(value.questions.length||value.revisions.length||value.calls.length||value.nested_plans.length)throw new Error('Planning ledger lacks its owner');return value;}
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
  if(calls.size!==value.revisions.length||revision!==plan.revision)throw new Error('Planning revision ledger changed');
  const owners=new Set(),plans=new Map([[plan.id,{plan,depth:0}]]);let pending=[...value.nested_plans],totalQuestions=value.questions.length;
  while(pending.length){let progressed=false;const next=[];for(const nested of pending){const parent=plans.get(nested.parent_plan_id);if(!parent){next.push(nested);continue;}validateNestedPlanOwner(nested,root,parent.plan,parent.depth);if(owners.has(nested.owner_run_id)||plans.has(nested.plan.id))throw new Error('Duplicate nested planning owner');owners.add(nested.owner_run_id);plans.set(nested.plan.id,{plan:nested.plan,depth:nested.depth});totalQuestions+=nested.questions.length;progressed=true;}if(!progressed)throw new Error('Nested planning owner chain is incomplete');pending=next;}
  if(totalQuestions>value.policy.limits.max_humans)throw new Error('Planning human-question limit changed');return value;
}

class McpAuthorizationController {
  constructor(client,post,{current=()=>true,save=()=>{},requestId=()=>globalThis.crypto.randomUUID(),schedule=(fn)=>globalThis.setTimeout(fn,1000),unschedule=(timer)=>globalThis.clearTimeout(timer)}={}){
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
function exactFields(value,fields,message='Invalid metadata field set'){if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field)))throw new Error(message);}
function validateSessionArchive(value){
  exactFields(value,['format','version','session','messages']);if(value.format!=='xmind.session'||![1,2].includes(value.version)||!value.session||typeof value.session!=='object'||Array.isArray(value.session)||!Array.isArray(value.messages)||value.messages.length>1000)throw new Error('Invalid xMind session archive');
  exactFields(value.session,value.version===1?['title']:['title','agent']);if(typeof value.session.title!=='string'||!value.session.title||value.session.title.length>512)throw new Error('Invalid xMind session title');
  if(value.version===2&&value.session.agent!==null){const agent=value.session.agent,fields=['id','revision','model_id','instructions','tools','permissions','name','description','mode','hidden','default'];if(!agent||typeof agent!=='object'||Array.isArray(agent)||Object.keys(agent).some(key=>!fields.includes(key))||(agent.name===undefined)!==(agent.description===undefined)||(agent.mode!==undefined&&!['all','primary','subagent'].includes(agent.mode))||(agent.hidden!==undefined&&typeof agent.hidden!=='boolean')||(agent.default!==undefined&&typeof agent.default!=='boolean')||typeof agent.id!=='string'||!agent.id||!Number.isSafeInteger(agent.revision)||agent.revision<1||typeof agent.model_id!=='string'||agent.model_id.length>256||typeof agent.instructions!=='string'||!agent.instructions||agent.instructions.length>32768||(agent.name!==undefined&&(typeof agent.name!=='string'||!agent.name.trim()||new TextEncoder().encode(agent.name).length>128))||(agent.description!==undefined&&(typeof agent.description!=='string'||new TextEncoder().encode(agent.description).length>2048))||(agent.tools!==undefined&&(!Array.isArray(agent.tools)||agent.tools.length>64||agent.tools.some(tool=>typeof tool!=='string'||!tool||tool.length>128)))||(agent.permissions!==undefined&&(!Array.isArray(agent.permissions)||agent.permissions.length>128||agent.permissions.some(rule=>!rule||typeof rule!=='object'||Array.isArray(rule)||Object.keys(rule).length!==3||!['action','resource','effect'].every(field=>Object.hasOwn(rule,field))||typeof rule.action!=='string'||!rule.action||rule.action.length>128||rule.action!=='*'&&!/^[A-Za-z0-9_.*-]+$/.test(rule.action)||typeof rule.resource!=='string'||!rule.resource||new TextEncoder().encode(rule.resource).length>1024||rule.resource.includes('\\')||rule.resource.includes(':')||rule.resource.startsWith('/')||rule.resource.split('/').includes('..')||!['deny','allow'].includes(rule.effect)))))throw new Error('Invalid archived agent selection');}
  let total=0;const encoder=new TextEncoder();for(const message of value.messages){exactFields(message,['role','data']);if(!['user','assistant','tool'].includes(message.role)||!message.data||typeof message.data!=='object'||Array.isArray(message.data))throw new Error('Invalid archived conversation message');const size=encoder.encode(JSON.stringify(message.data)).length;if(size>262144||total>4194304-size)throw new Error('Session archive exceeds limits');total+=size;}return value;
}
function connectionIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(value))throw new Error('Invalid connection profile identity.');return value;}
function connectionName(value){if(typeof value!=='string'||!value.trim()||value.length>128||/[\r\n\0]/.test(value))throw new Error('Enter a connection name (up to 128 characters).');return value;}
function connectionOrigin(value){if(typeof value!=='string'||value.length>8192||!value.startsWith('https://')||/[\\?#@\s\0-\x1f]/.test(value))throw new Error('Enter an HTTPS server URL without credentials, query or fragment.');return value;}
function validateConnectionBindingIdentity(value){
  exactFields(value,['connection_id','profile_revision','server_id','backend_generation','protocol_version','kind']);
  if(value.connection_id!=='local'||value.kind!=='local'||!Number.isSafeInteger(value.profile_revision)||value.profile_revision<1||typeof value.server_id!=='string'||!/^local-[a-f0-9]{64}$/.test(value.server_id)||typeof value.backend_generation!=='string'||!value.backend_generation||value.backend_generation.length>256||/[\x00-\x1f\x7f]/.test(value.backend_generation)||value.protocol_version!==1)throw new Error('Invalid captured Local connection identity.');return value;
}
function validateConnectionProfiles(value){
  const fields=['revision','active_id','profiles'];if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field))||!Number.isSafeInteger(value.revision)||value.revision<1||!Array.isArray(value.profiles)||!value.profiles.length||value.profiles.length>32)throw new Error('Invalid connection profile catalog.');
  connectionIdentity(value.active_id);const ids=new Set();let local=false,active=false;
  for(const profile of value.profiles){
    const profileFields=['id','name','kind','origin','revision','status','command_routing','authenticated'];if(!profile||typeof profile!=='object'||Array.isArray(profile)||Object.keys(profile).length!==profileFields.length||profileFields.some(field=>!Object.hasOwn(profile,field)))throw new Error('Invalid connection profile.');
    connectionIdentity(profile.id);connectionName(profile.name);if(ids.has(profile.id)||!Number.isSafeInteger(profile.revision)||profile.revision<1||profile.revision>value.revision||typeof profile.command_routing!=='boolean'||typeof profile.authenticated!=='boolean')throw new Error('Invalid connection profile identity or revision.');ids.add(profile.id);
    if(profile.kind==='local'){if(profile.id!=='local'||profile.origin!==''||profile.status!=='connected'||!profile.command_routing||!profile.authenticated)throw new Error('Invalid Local connection state.');local=true;}
    else if(profile.kind==='nexus'){connectionOrigin(profile.origin);if(profile.id==='local'||profile.status!=='not_connected'||profile.command_routing||profile.authenticated)throw new Error('Invalid Nexus connection state.');}
    else throw new Error('Unsupported connection profile kind.');active||=profile.id===value.active_id;
  }
  if(!local||!active)throw new Error('Connection catalog must contain Local and its active profile.');return value;
}
function mcpFields(value,fields){if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field)))throw new Error('Invalid MCP authorization metadata');}
function mcpServerIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.-]{1,64}$/.test(value))throw new Error('Invalid MCP server identity');return value;}
function validateMcpResourceCatalog(value){
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==3||!Object.hasOwn(value,'servers')||!Object.hasOwn(value,'resources')||!Object.hasOwn(value,'templates')||!Array.isArray(value.servers)||!Array.isArray(value.resources)||!Array.isArray(value.templates)||value.servers.length>16||value.resources.length>1024||value.templates.length>1024)throw new Error('Invalid MCP resource catalog');
  const servers=new Map();for(const server of value.servers){
    if(!server||typeof server!=='object'||Array.isArray(server)||!['id','revision','enabled','state'].every(key=>Object.hasOwn(server,key)))throw new Error('Invalid MCP resource server');
    const allowed=server.state==='connected'?['id','revision','enabled','state','resource_count','template_count']:['id','revision','enabled','state'];if(Object.keys(server).length!==allowed.length||allowed.some(key=>!Object.hasOwn(server,key)))throw new Error('Invalid MCP resource server');
    mcpServerIdentity(server.id);planInteger(server.revision,1);if(typeof server.enabled!=='boolean'||!['disabled','unsupported','connected','authorization_required','unavailable'].includes(server.state)||server.state==='connected'&&!server.enabled)throw new Error('Invalid MCP resource server state');
    if(server.state==='connected'&&(!Number.isSafeInteger(server.resource_count)||server.resource_count<0||server.resource_count>1024||!Number.isSafeInteger(server.template_count)||server.template_count<0||server.template_count>1024))throw new Error('Invalid MCP resource count');if(servers.has(server.id))throw new Error('Duplicate MCP resource server');servers.set(server.id,server);
  }
  const identities=new Set();for(const item of value.resources){
    if(!item||typeof item!=='object'||Array.isArray(item)||Object.keys(item).length!==3||!Object.hasOwn(item,'server_id')||!Object.hasOwn(item,'config_revision')||!Object.hasOwn(item,'resource'))throw new Error('Invalid MCP resource entry');
    const server=servers.get(mcpServerIdentity(item.server_id));planInteger(item.config_revision,1);if(!server||server.state!=='connected'||server.revision!==item.config_revision||!item.resource||typeof item.resource!=='object'||Array.isArray(item.resource)||typeof item.resource.uri!=='string'||!item.resource.uri||item.resource.uri.length>16384)throw new Error('Invalid MCP resource binding');
    if(item.resource.name!==undefined&&(typeof item.resource.name!=='string'||item.resource.name.length>4096)||item.resource.title!==undefined&&(typeof item.resource.title!=='string'||item.resource.title.length>4096)||item.resource.description!==undefined&&(typeof item.resource.description!=='string'||item.resource.description.length>16384)||item.resource.mimeType!==undefined&&(typeof item.resource.mimeType!=='string'||item.resource.mimeType.length>256))throw new Error('Invalid MCP resource metadata');
    const identity=item.server_id+'\0'+item.resource.uri;if(identities.has(identity))throw new Error('Duplicate MCP resource URI');identities.add(identity);
  }
  const templateIdentities=new Set();for(const item of value.templates){
    if(!item||typeof item!=='object'||Array.isArray(item)||Object.keys(item).length!==3||!Object.hasOwn(item,'server_id')||!Object.hasOwn(item,'config_revision')||!Object.hasOwn(item,'template'))throw new Error('Invalid MCP resource-template entry');
    const server=servers.get(mcpServerIdentity(item.server_id));planInteger(item.config_revision,1);if(!server||server.state!=='connected'||server.revision!==item.config_revision||!item.template||typeof item.template!=='object'||Array.isArray(item.template)||typeof item.template.name!=='string'||!item.template.name||item.template.name.length>4096||typeof item.template.uriTemplate!=='string'||!item.template.uriTemplate||item.template.uriTemplate.length>16384)throw new Error('Invalid MCP resource-template binding');
    if(item.template.title!==undefined&&(typeof item.template.title!=='string'||item.template.title.length>4096)||item.template.description!==undefined&&(typeof item.template.description!=='string'||item.template.description.length>16384)||item.template.mimeType!==undefined&&(typeof item.template.mimeType!=='string'||item.template.mimeType.length>256))throw new Error('Invalid MCP resource-template metadata');
    const identity=item.server_id+'\0'+item.template.uriTemplate;if(templateIdentities.has(identity))throw new Error('Duplicate MCP resource URI template');templateIdentities.add(identity);
  }
  for(const [id,server] of servers)if(server.state==='connected'&&(server.resource_count!==value.resources.filter(item=>item.server_id===id).length||server.template_count!==value.templates.filter(item=>item.server_id===id).length))throw new Error('MCP resource catalog changed');return value;
}
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
  exactFields(value,withRoutes?['revision','active','profiles','routes']:['revision','active','profiles'],'Invalid provider profile metadata');profileRevision(value.revision);
  if(typeof value.active!=='string'||!Array.isArray(value.profiles)||value.profiles.length>32)throw new Error('Invalid provider profile metadata');
  if(value.revision===0?(value.profiles.length!==0||value.active!==''):value.profiles.length===0)throw new Error('Invalid provider profile metadata');
  const ids=new Set();for(const profile of value.profiles){exactFields(profile,['id','route_id','provider','model','revision'],'Invalid provider profile metadata');profileIdentity(profile.id);profileIdentity(profile.route_id);profileIdentity(profile.provider);if(profile.model!=='')profileIdentity(profile.model);profileRevision(profile.revision);if(profile.revision<1||profile.revision>value.revision||ids.has(profile.id))throw new Error('Invalid provider profile metadata');ids.add(profile.id);}
  if(value.active&&!ids.has(value.active))throw new Error('Invalid active provider profile');
  if(withRoutes){if(!Array.isArray(value.routes)||!value.routes.length||value.routes.length>64)throw new Error('Invalid provider profile routes');
    const routes=new Map();for(const route of value.routes){exactFields(route,['id','provider','wire','discovery'],'Invalid provider profile routes');profileIdentity(route.id);profileIdentity(route.provider);if(routes.has(route.id)||!['chat-completions','responses','anthropic-messages','gemini-generate-content'].includes(route.wire)||typeof route.discovery!=='boolean')throw new Error('Invalid provider profile routes');routes.set(route.id,route);}
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
  async admission(){if(!this.state&&!await this.refresh())throw new Error('Provider profile admission requires profile metadata');if(this.disposed||!this.state||!this.guard())throw new Error('Backend changed before run admission');if(!this.state.active)return undefined;return {provider_profile_id:this.state.active,expected_provider_revision:this.state.revision};}
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
      if(id===undefined&&route_id===undefined&&key===undefined&&!this.state.active){
        const text=this.state.profiles.length?'Choose a saved provider below, then choose a model. Your saved key will be used.':'Open Settings to add a provider and choose a model.';
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
      let captured=this.binding(id);const savedCredential=!!saved&&key===undefined;let catalogue;
      try{catalogue=await this.client.discoverProfileModels(id,savedCredential?saved.route_id:route_id,key,this.state.revision);}
      catch(error){
        // Another xMind view can update provider metadata while this page is
        // open. Saved-key discovery is read-only, so refresh once and retry
        // only while the selected profile identity and route are unchanged.
        if(error.status===409&&savedCredential){
          const active=captured.active;
          const refreshed=await this.refresh();if(!this.current(epoch))return true;if(!refreshed)throw error;
          const latest=this.state.profiles.find(value=>value.id===id);
          if(this.state.active!==active||!latest||latest.route_id!==saved.route_id||latest.provider!==saved.provider)
            throw new Error('Provider settings changed. Review the selected profile and fetch models again.');
          captured=this.binding(id);
          catalogue=await this.client.discoverProfileModels(id,latest.route_id,undefined,this.state.revision);
        }else if(savedCredential&&(!Number.isInteger(error.status)||[502,503,504].includes(error.status))){
          // A just-selected saved profile can race native/provider readiness on
          // first use. Retry transient discovery failures with bounded backoff,
          // and only while the same backend and profile remain selected.
          let retryError=error;
          for(const delay of [250,500,1000]){
            await new Promise(resolve=>setTimeout(resolve,delay));
            if(!this.current(epoch))return true;
            if(!this.bound(captured))throw new Error('Provider settings changed. Fetch models again.');
            try{catalogue=await this.client.discoverProfileModels(id,saved.route_id,undefined,this.state.revision);break;}
            catch(retry){retryError=retry;if(Number.isInteger(retry.status)&&![502,503,504].includes(retry.status))throw retry;}
          }
          if(!catalogue)throw retryError;
        }else throw error;
      }
      if(!this.current(epoch))return true;
      if(!this.bound(captured))throw new Error('Provider settings changed. Fetch models again.');
      if(!catalogue.models.length)throw new Error('The provider returned no models');
      this.draft={...captured,route_id,key,savedCredential,ids:catalogue.models.map(value=>value.id),expires:key===undefined?Infinity:Date.now()+300000};key=undefined;
      if(savedCredential)this.savedCatalogue={...captured,route_id:saved.route_id,ids:[...this.draft.ids],expires:Infinity,savedCredential:true};
      this.models(saved?.model);const confirmation=savedCredential?`Fetched ${this.draft.ids.length} ${saved.provider} models using this profile's saved key. The key stays in local xMind.`:'Choose a model below to save this profile';this.post({type:'settings-state',busy:false,complete:true,text:confirmation});if(savedCredential)this.post({type:'status',text:confirmation});
      if(this.draft.key!==undefined)this.timer=setTimeout(()=>{if(this.current(epoch)){this.invalidate();this.post({type:'settings-state',busy:false,text:'Unsaved key expired · fetch models again'});}},300000);
      return true;
    }catch(error){if(this.current(epoch)){this.post({type:'settings-state',busy:false,text:error.message});this.post({type:'status',text:`Model discovery failed: ${error.message} Open Settings to retry.`});}throw error;}finally{key=undefined;}
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
  activeBinding(){
    const profile=this.state?.profiles.find(value=>value.id===this.state.active),route=this.state?.routes.find(value=>value.id===profile?.route_id);
    return JSON.stringify([this.state?.active,profile?.revision,profile?.route_id,profile?.provider,profile?.model,route?.wire,route?.provider,route?.discovery]);
  }
  async selectAndRefresh(id,refresh){
    if(!this.state&&!await this.refresh())throw new Error('Update the backend to use provider profiles');
    const before=this.activeBinding();
    try{if(!await this.select(id))return;}
    catch(error){
      // Another client may have changed the active binding despite our rejected
      // selection. Refresh view capabilities, but never replay that mutation.
      // An unrelated registry revision or a busy rejection keeps its model.
      if(error.status===409&&this.current(this.epoch)&&before!==this.activeBinding())await refresh();
      throw error;
    }
    if(this.current(this.epoch))await refresh();
    return true;
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
if(typeof module!=='undefined'&&module.exports)module.exports={BackendClient,SessionHistoryPager,EventStreamSubscription,eventRequiresSnapshot,backendOrigin,validateToken,validateEditInspection,validateVcsStatus,validateVcsBase,validateVcsDiff,validateConnectionProfiles,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,observeGraphTree,validatePlanning,validatePlanObservation,validateOwnedChild,validatePlanInputText,validateContextObservation,validateContextRequest,validateGraphContext,ContextViewController,SkillViewController,validateSessionSkills,validateSkillCatalogue,AgentSelectionController,validateAgentCatalogue,validateSessionAgent,validateSessionModel,validateMcpAuthorizationServers,validateMcpResourceCatalog,validateMcpAuthorizationAttempt,McpAuthorizationController,parseWorkspaceCommand,QueuedRunSelection};
else globalThis.XMindBackend={BackendClient,SessionHistoryPager,EventStreamSubscription,eventRequiresSnapshot,backendOrigin,validateToken,validateEditInspection,validateVcsStatus,validateVcsBase,validateVcsDiff,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,observeGraphTree,validatePlanning,validatePlanObservation,validateOwnedChild,validatePlanInputText,validateContextObservation,validateContextRequest,validateGraphContext,ContextViewController,SkillViewController,validateSessionSkills,validateSkillCatalogue,AgentSelectionController,validateAgentCatalogue,validateSessionAgent,validateSessionModel,validateMcpAuthorizationServers,validateMcpResourceCatalog,validateMcpAuthorizationAttempt,McpAuthorizationController,parseWorkspaceCommand,QueuedRunSelection};
