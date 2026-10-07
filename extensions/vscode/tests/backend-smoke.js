'use strict';
// Run against an actual native xMind Server with a real model configured.
const assert = require('node:assert/strict');
const { BackendClient } = require('../client');

async function main() {
  const client = new BackendClient(process.argv[2] || 'http://127.0.0.1:8765', () => process.env.XMIND_AUTH_TOKEN);
  assert.equal((await client.health()).agent_execution,true,'Configure a model on the native server');
  const session = await client.createSession('VS Code client integration');
  const run = await client.run(session.id,process.argv[3] || 'Describe what you can do with the tools offered in this session.');
  assert.equal((await client.runs(session.id)).at(-1).id, run.id);
  const deadline=Date.now()+120000;
  let final;
  while(Date.now()<deadline) {
    final=await client.status(run.id);
    if(['completed','failed','cancelled'].includes(final.state)) break;
    await new Promise(resolve=>setTimeout(resolve,250));
  }
  assert.equal(final.state,'completed','Real provider run must complete; inspect persisted events if it fails');
  const reconnected = new BackendClient(client.baseUrl, () => process.env.XMIND_AUTH_TOKEN);
  assert.equal((await reconnected.sessions()).find(s => s.id === session.id).title, session.title);
  const events = await reconnected.events(run.id,0);
  assert.ok(events.some(event=>event.kind==='run.completed'));
  assert.deepEqual(await reconnected.events(run.id,events.at(-1).seq),[]);
  const history=await reconnected.history(session.id);
  assert.equal(history[0].role,'user');assert.equal(history.at(-1).role,'assistant');
  console.log('VS Code host client live-provider smoke passed: actual native execution, durable transcript and reconnect. IDE rendering is not tested.');
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
