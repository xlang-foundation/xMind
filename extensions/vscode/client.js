'use strict';

class BackendClient {
  constructor(baseUrl, fetchImpl = fetch) {
    const url = new URL(baseUrl);
    if (url.protocol !== 'http:' || !['localhost', '127.0.0.1', '[::1]'].includes(url.hostname) || url.username || url.password) {
      throw new Error('This development client requires a loopback HTTP backend.');
    }
    this.baseUrl = url.origin;
    this.fetch = fetchImpl;
  }

  async request(path, body) {
    const response = await this.fetch(this.baseUrl + path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(15000),
      redirect: 'error'
    });
    const data = await response.json();
    if (!response.ok) throw new Error(typeof data.detail === 'string' ? data.detail : `Backend returned ${response.status}`);
    return data;
  }
  sessions() { return this.request('/v1/sessions'); }
  createSession(title) { return this.request('/v1/sessions', { title }); }
  history(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/history`); }
  runs(id) { return this.request(`/v1/sessions/${encodeURIComponent(id)}/runs`); }
  run(session_id, prompt) { return this.request('/v1/runs', { session_id, prompt }); }
  events(id, after) { return this.request(`/v1/runs/${encodeURIComponent(id)}/events?after=${after}`); }
  status(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}`); }
  cancel(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}/cancel`, {}); }
  approvals(id) { return this.request(`/v1/runs/${encodeURIComponent(id)}/approvals`); }
  decide(id, decision) { return this.request(`/v1/approvals/${encodeURIComponent(id)}`, { decision }); }
}

module.exports = { BackendClient };
