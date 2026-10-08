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
  constructor(baseUrl, tokenProvider, fetchImpl = (...args) => fetch(...args)) {
    this.baseUrl = backendOrigin(baseUrl);
    if (typeof tokenProvider !== 'function') throw new Error('Server authentication is required.');
    this.#tokenProvider = tokenProvider;
    this.fetch = fetchImpl;
  }

  async request(path, body, timeoutMs=15000) {
    const token = validateToken(await this.#tokenProvider());
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
  graphRun(session_id,graph_id,graph_revision,prompt,model_id,binding) { return this.request('/v1/graph-runs',{session_id,graph_id,graph_revision,prompt,...(model_id?{model_id}:{}),...profileBindingFields(binding)}); }
  graph(id) { return this.request(`/v1/graph-runs/${encodeURIComponent(id)}`); }
  graphChildren(id) { return this.request(`/v1/graph-runs/${encodeURIComponent(id)}/children`); }
  graphEvents(id,after) { return this.request(`/v1/graph-runs/${encodeURIComponent(id)}/events?after=${after}`); }
  graphChildHistory(root,child) { return this.request(`/v1/graph-runs/${encodeURIComponent(root)}/children/${encodeURIComponent(child)}/history`); }
  graphInput(root,node,input_json,expected_checkpoint_revision) { return this.request(`/v1/graph-runs/${encodeURIComponent(root)}/human/${encodeURIComponent(node)}`,{input_json,expected_checkpoint_revision}); }
  providerConfiguration() { return this.request('/v1/provider/configuration'); }
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
  createSession(title) { return this.request('/v1/sessions', { title }); }
  renameSession(id,title,expectedTitle) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/title`,{title,expected_title:expectedTitle}); }
  history(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/history`); }
  runs(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/runs`); }
  run(session_id, prompt, model_id,binding) { return this.request('/v1/runs', { session_id, prompt, ...(model_id ? {model_id} : {}),...profileBindingFields(binding) }); }
  events(id, after) { return this.request(`/v1/runs/${encodeURIComponent(id)}/events?after=${after}`); }
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
function executionIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_-]{1,128}$/.test(value))throw new Error('Invalid execution identity');return value;}
function providerCallIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.:-]{1,256}$/.test(value))throw new Error('Invalid provider call identity');return value;}
function planLabel(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.-]{1,32}$/.test(value))throw new Error('Invalid planning node label');return value;}
function planInteger(value,minimum=0){if(!Number.isSafeInteger(value)||value<minimum)throw new Error('Invalid planning revision or counter');return value;}
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

function exactFields(value,fields){if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field)))throw new Error('Invalid provider profile metadata');}
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
class ProviderProfileController {
  constructor(client,post,guard=()=>true){this.client=client;this.post=post;this.guard=guard;this.epoch=0;}
  invalidate(){this.epoch++;clearTimeout(this.timer);if(this.draft)this.draft.key=undefined;this.draft=undefined;}
  dispose(){this.invalidate();this.disposed=true;}
  current(epoch){return !this.disposed&&this.epoch===epoch&&this.guard();}
  present(){if(this.state)this.post({type:'provider-profiles',profiles:this.state.profiles,routes:this.state.routes,active:this.state.active});}
  async refresh(){
    if(typeof this.client.providerProfiles!=='function')return false;
    const epoch=this.epoch;
    try{const state=await this.client.providerProfiles();if(this.current(epoch)){this.state=state;this.present();}return true;}
    catch(error){if(error.status===404)return false;throw error;}
  }
  wire(id){const profile=this.state?.profiles.find(value=>value.id===id);return this.state?.routes.find(value=>value.id===profile?.route_id)?.wire;}
  models(selected){if(this.draft)this.post({type:'model-list',models:this.draft.ids.map(id=>({id})),model:selected});}
  async admission(){if(!this.state&&!await this.refresh())throw new Error('Provider profile admission requires profile metadata');if(this.disposed||!this.state||!this.guard())throw new Error('Backend changed before run admission');return {provider_profile_id:this.state.active,expected_provider_revision:this.state.revision};}
  async reconcileAdmission(binding,error){
    if(error?.status!==409||!binding)return false;
    const epoch=this.epoch;if(!await this.refresh()||!this.current(epoch))return false;
    if(this.state.revision===binding.expected_provider_revision&&this.state.active===binding.provider_profile_id)return false;
    this.invalidate();this.present();const wire=this.wire(this.state.active);if(wire)this.post({type:'provider-wire',wire});return true;
  }
  async discover(key,id,route_id){
    if(!this.state&&!await this.refresh())return false;
    if(this.disposed||!this.state)return true;
    this.invalidate();const epoch=this.epoch;
    try{
      id=id===undefined?this.state.active:id;
      const saved=this.state.profiles.find(value=>value.id===id);
      if(id&&!saved)throw new Error('Choose a saved profile or Add profile');
      route_id=route_id||saved?.route_id||this.state.routes.find(value=>value.id==='openai.responses')?.id||this.state.routes[0].id;
      const route=this.state.routes.find(value=>value.id===route_id&&value.discovery);
      if(!route||saved&&saved.provider!==route.provider)throw new Error('Choose an available route for this provider');
      if(!saved){if(!key)throw new Error('Enter a key for the new provider profile');id=route.provider+'-'+globalThis.crypto.randomUUID();}
      this.post({type:'settings-state',busy:true,text:'Fetching models from '+(route.provider==='anthropic'?'Claude':route.provider)+'…'});
      if(!this.current(epoch))throw new Error('Backend changed during provider setup');
      const catalogue=await this.client.discoverProfileModels(id,saved&&key===undefined?saved.route_id:route_id,key,this.state.revision);
      if(!this.current(epoch))return true;
      if(!catalogue.models.length)throw new Error('The provider returned no models');
      this.draft={id,route_id,key,revision:this.state.revision,ids:catalogue.models.map(value=>value.id),expires:key===undefined?Infinity:Date.now()+300000};key=undefined;
      this.models(saved?.model);this.post({type:'settings-state',busy:false,complete:true,text:'Choose a model below to save this profile'});
      if(this.draft.key!==undefined)this.timer=setTimeout(()=>{if(this.current(epoch)){this.invalidate();this.post({type:'settings-state',busy:false,text:'Unsaved key expired · fetch models again'});}},300000);
      return true;
    }catch(error){if(this.current(epoch))this.post({type:'settings-state',busy:false,text:error.message});throw error;}finally{key=undefined;}
  }
  async save(model){
    if(!this.draft)return false;
    const draft=this.draft,epoch=this.epoch;
    if(Date.now()>draft.expires||!draft.ids.includes(model))throw new Error('Fetch models and select a returned model');
    if(!this.current(epoch))throw new Error('Backend changed during provider setup');
    const state=await this.client.saveProviderProfile(draft.id,draft.route_id,model,draft.key,draft.revision,true);
    draft.key=undefined;if(!this.current(epoch))return true;
    this.state=state;draft.revision=state.revision;draft.expires=Infinity;clearTimeout(this.timer);
    this.present();this.models(model);this.post({type:'provider-wire',wire:this.wire(state.active)});return true;
  }
  async select(id){
    if(!this.state&&!await this.refresh())throw new Error('Update the backend to use provider profiles');
    if(this.disposed||!this.state)return;
    if(!this.state.profiles.some(profile=>profile.id===id))throw new Error('Choose a saved provider profile');
    this.invalidate();const epoch=this.epoch;if(!this.current(epoch))throw new Error('Backend changed during provider setup');const state=await this.client.selectProviderProfile(id,this.state.revision);
    if(!this.current(epoch))return;
    this.state=state;this.present();this.post({type:'provider-wire',wire:this.wire(id)});
    return true;
  }
}
if(typeof module!=='undefined'&&module.exports)module.exports={BackendClient,backendOrigin,validateToken,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanning,validatePlanObservation,validateOwnedChild,validatePlanInputText};
else globalThis.XMindBackend={BackendClient,backendOrigin,validateToken,providerEnrollmentWire,ProviderProfileController,observeOwnedRun,validatePlanning,validatePlanObservation,validateOwnedChild,validatePlanInputText};
