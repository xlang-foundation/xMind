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
  constructor(baseUrl, tokenProvider, fetchImpl = fetch) {
    this.baseUrl = backendOrigin(baseUrl);
    if (typeof tokenProvider !== 'function') throw new Error('Server authentication is required.');
    this.#tokenProvider = tokenProvider;
    this.fetch = fetchImpl;
  }

  async request(path, body) {
    const token = validateToken(await this.#tokenProvider());
    const response = await this.fetch(this.baseUrl + path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { Authorization: `Bearer ${token}`, ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(15000),
      redirect: 'error'
    });
    const data = await response.json();
    if (!response.ok) { const error=new Error(typeof data.detail === 'string' ? data.detail : `Backend returned ${response.status}`);error.status=response.status;throw error; }
    return data;
  }
  health() { return this.request('/v1/health'); }
  models() { return this.request('/v1/models'); }
  sessions() { return this.request('/v1/sessions'); }
  createSession(title) { return this.request('/v1/sessions', { title }); }
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

module.exports = { BackendClient, backendOrigin, validateToken };
