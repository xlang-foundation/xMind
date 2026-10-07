'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const { BackendClient } = require('../client');

test('session discovery, URL encoding, error handling and origin boundaries', async () => {
  const server = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json');
    if (req.url === '/v1/sessions') res.end(JSON.stringify([{ id: 'session-1', title: '<script>untrusted</script>' }]));
    else if (req.url === '/v1/runs/run%2F1/events?after=12') res.end(JSON.stringify([{ seq: 13, kind: 'run.completed' }]));
    else { res.statusCode = 409; res.end(JSON.stringify({ detail: 'Session already active' })); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const client = new BackendClient(`http://127.0.0.1:${server.address().port}`);
    assert.equal((await client.sessions())[0].title, '<script>untrusted</script>');
    assert.equal((await client.events('run/1', 12))[0].seq, 13);
    await assert.rejects(client.cancel('active'), /Session already active/);
    assert.throws(() => new BackendClient('https://remote.example'), /loopback/);
    assert.throws(() => new BackendClient('http://user:password@localhost'), /loopback/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});
