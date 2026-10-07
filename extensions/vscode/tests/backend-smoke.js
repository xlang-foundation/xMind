'use strict';
// Run against an actual local xlang3 backend started for this smoke check.
const assert = require('node:assert/strict');
const { BackendClient } = require('../client');

async function main() {
  const client = new BackendClient(process.argv[2] || 'http://127.0.0.1:18765');
  const session = await client.createSession('VS Code client integration');
  const run = await client.request('/v1/runs', { session_id: session.id });
  assert.equal((await client.runs(session.id)).at(-1).id, run.id);
  const cursor = (await client.events(run.id, 0)).at(-1).seq;
  await client.cancel(run.id);
  const reconnected = new BackendClient(client.baseUrl);
  assert.equal((await reconnected.sessions()).find(s => s.id === session.id).title, session.title);
  const events = await reconnected.events(run.id, cursor);
  assert.equal(events.length, 1);
  assert.equal(events[0].kind, 'run.cancelled');
  assert.deepEqual(await reconnected.history(session.id), []);
  console.log('VS Code backend client: shared session, cancellation and cursor reconnect passed');
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
