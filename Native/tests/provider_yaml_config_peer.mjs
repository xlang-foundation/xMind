// Synthetic Anthropic model catalogue. Native YAML, encryption, SQLite and
// catalogue HTTP are real; this peer performs no inference and reads no user file.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp, rm, writeFile} from 'node:fs/promises';
import {join, resolve, dirname, basename} from 'node:path';
import {tmpdir} from 'node:os';
const [executable, modules, stdlib] = process.argv.slice(2);
assert.ok(executable && modules && stdlib);
const root = await mkdtemp(join(tmpdir(), 'xmind-provider-yaml-'));
let count = 0, failure;
const server = createServer((request, response) => {
  try {
    assert.equal(request.method, 'GET');assert.equal(request.url, '/claude-models?limit=1000');
    assert.equal(request.headers['x-api-key'], 'synthetic-yaml-claude-key-not-live');
    assert.equal(request.headers['anthropic-version'], '2023-06-01');assert.equal(request.headers.authorization, undefined);
    ++count;assert.equal(count, 1, 'One explicit discovery uses the existing encrypted key-only profile');
    response.writeHead(200, {'Content-Type': 'application/json'});
    response.end(JSON.stringify({data: [{type: 'model', id: 'claude-synthetic-b'}, {type: 'model', id: 'claude-synthetic-a'}], has_more: false, last_id: 'claude-synthetic-a'}));
  } catch (error) {failure ??= error;response.destroy();}
});
try {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const result = await promisify(execFile)(executable, [root, modules, stdlib, 'http://127.0.0.1:' + server.address().port], {windowsHide: true, timeout: 60000, maxBuffer: 1024 * 1024});
  if (failure) throw failure;assert.equal(count, 1);process.stdout.write(result.stdout);
  assert.equal(dirname(resolve(root)), resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-provider-yaml-'));
  await rm(root, {recursive: true, force: true});
} catch (error) {
  await writeFile(join(root, 'synthetic-yaml-fixture-failure-private.log'), String(failure?.stack ?? error.stack ?? error));
  process.stderr.write('Synthetic native provider YAML contract failed; owned diagnostics retained at ' + root + '\n');process.exitCode = 1;
} finally {server.closeAllConnections();await new Promise(resolve => server.close(resolve));}
