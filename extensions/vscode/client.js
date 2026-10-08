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
  graphRun(session_id,graph_id,graph_revision,prompt,model_id) { return this.request('/v1/graph-runs',{session_id,graph_id,graph_revision,prompt,...(model_id?{model_id}:{})}); }
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
  run(session_id, prompt, model_id) { return this.request('/v1/runs', { session_id, prompt, ...(model_id ? {model_id} : {}) }); }
  events(id, after) { return this.request(`/v1/runs/${encodeURIComponent(id)}/events?after=${after}`); }
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

function exactFields(value,fields){if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field)))throw new Error('Invalid provider profile metadata');}
function profileIdentity(value){if(typeof value!=='string'||!/^[A-Za-z0-9_.:/-]{1,256}$/.test(value)||value.startsWith('sk-'))throw new Error('Invalid provider profile identity');return value;}
function profileRevision(value){if(!Number.isSafeInteger(value)||value<0)throw new Error('Invalid provider profile revision');}
function profileKey(value){if(value!==undefined&&(typeof value!=='string'||!/^[\x21-\x7e]{1,32768}$/.test(value)))throw new Error('Enter a provider API key without spaces');}
function validateProviderProfiles(value,withRoutes=true){
  exactFields(value,withRoutes?['revision','active','profiles','routes']:['revision','active','profiles']);profileRevision(value.revision);
  if(typeof value.active!=='string'||!Array.isArray(value.profiles)||value.profiles.length>32)throw new Error('Invalid provider profile metadata');
  if(value.revision===0?(value.profiles.length!==0||value.active!==''):value.profiles.length===0)throw new Error('Invalid provider profile metadata');
  const ids=new Set();for(const profile of value.profiles){exactFields(profile,['id','route_id','provider','model','revision']);profileIdentity(profile.id);profileIdentity(profile.route_id);profileIdentity(profile.provider);if(profile.model!=='')profileIdentity(profile.model);profileRevision(profile.revision);if(profile.revision<1||profile.revision>value.revision||ids.has(profile.id))throw new Error('Invalid provider profile metadata');ids.add(profile.id);}
  if(value.active&&!ids.has(value.active))throw new Error('Invalid active provider profile');
  if(withRoutes){if(!Array.isArray(value.routes)||!value.routes.length||value.routes.length>64)throw new Error('Invalid provider profile routes');
    const routes=new Map();for(const route of value.routes){exactFields(route,['id','provider','wire','discovery']);profileIdentity(route.id);profileIdentity(route.provider);if(routes.has(route.id)||!['chat-completions','responses','anthropic-messages'].includes(route.wire)||typeof route.discovery!=='boolean')throw new Error('Invalid provider profile routes');routes.set(route.id,route);}
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
if(typeof module!=='undefined'&&module.exports)module.exports={BackendClient,backendOrigin,validateToken,providerEnrollmentWire};
else globalThis.XMindBackend={BackendClient,backendOrigin,validateToken,providerEnrollmentWire};
