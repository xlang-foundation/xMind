'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const { BackendClient } = require('../client');
const token = 'native-client-contract-token-32-bytes';
test('graph access adapter preserves backend identity, revision and raw human JSON',async()=>{
 const requests=[];const client=new BackendClient('http://127.0.0.1:8765',()=>token,async(url,options)=>{requests.push({url,body:options.body?JSON.parse(options.body):undefined});return {ok:true,json:async()=>({})};});
 await client.graphRun('session','workflow',3,'Task');await client.graphInput('root','answer.step','{"answer":1,"answer":2}',7);await client.graphChildHistory('root','child/opaque');
 assert.deepEqual(requests[0].body,{session_id:'session',graph_id:'workflow',graph_revision:3,prompt:'Task'});assert.deepEqual(requests[1].body,{input_json:'{"answer":1,"answer":2}',expected_checkpoint_revision:7});assert.ok(requests[2].url.endsWith('/v1/graph-runs/root/children/child%2Fopaque/history'));
});

test('session discovery, URL encoding, error handling and origin boundaries', async () => {
  const server = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json');
    if (req.headers.authorization !== `Bearer ${token}`) { res.statusCode = 401; res.end('{"detail":"Authentication required"}'); return; }
    if (req.url === '/v1/sessions') res.end(JSON.stringify([{ id: 'session-1', title: '<script>untrusted</script>' }]));
    else if (req.url === '/v1/runs/run%2F1/events?after=12') res.end(JSON.stringify([{ seq: 13, kind: 'run.completed' }]));
    else { res.statusCode = 409; res.end(JSON.stringify({ detail: 'Session already active' })); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const client = new BackendClient(`http://127.0.0.1:${server.address().port}`, () => token);
    assert.equal((await client.sessions())[0].title, '<script>untrusted</script>');
    assert.equal((await client.events('run/1', 12))[0].seq, 13);
    await assert.rejects(client.cancel('active'), /Session already active/);
    assert.throws(() => new BackendClient('https://remote.example'), /loopback/);
    assert.throws(() => new BackendClient('http://user:password@localhost'), /loopback/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('origin-scoped authentication and native request bodies', async () => {
  let current = token;
  const requests = [];
  const client = new BackendClient('http://localhost:8765', async () => current, async (url,options) => {
    requests.push({url,options});return {ok:true,json:async()=>({state:'queued'})};
  });
  await client.run('session','Real prompt');
  assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/runs');
  assert.equal(requests[0].options.headers.Authorization,`Bearer ${token}`);
  assert.deepEqual(JSON.parse(requests[0].options.body),{session_id:'session',prompt:'Real prompt'});
  current = 'rotated-native-client-token-32-bytes';
  await client.cancel('run');
  assert.equal(requests[1].options.headers.Authorization,`Bearer ${current}`);
  assert.equal(requests[1].options.body,'{}');
  assert.equal(requests[1].options.redirect,'error');
  current = 'invalid';
  await assert.rejects(client.sessions(),/authentication token/);
  assert.equal(requests.length,2,'Invalid token must not leave the extension host');
  for (const url of ['http://localhost/path','http://localhost?query=1','http://localhost/#fragment','http://[::1]:8765']) {
    assert.throws(()=>new BackendClient(url,()=>token),/origin/);
  }
});
