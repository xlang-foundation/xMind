import test from 'node:test';
import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdir,mkdtemp,readdir,readFile,stat} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

test('instruction peer retains the original failed namespace and command output',{timeout:15000},async()=>{
  const parent=await mkdtemp(join(tmpdir(),'xmind-instruction-retention-'));await mkdir(join(parent,'tmp'));
  const temp=join(parent,'tmp'),peer=fileURLToPath(new URL('../../Native/tests/instruction_configuration_peer.mjs',import.meta.url));
  // Node refuses the fresh directory as an entry point: deterministic nonzero,
  // no native model/tool task and no modification to an existing namespace.
  await assert.rejects(promisify(execFile)(process.execPath,[peer,process.execPath,'unused-modules','unused-library'],{windowsHide:true,env:{...process.env,TEMP:temp,TMP:temp,GITHUB_ACTIONS:'false'},timeout:10000}),error=>{
    assert.match(error.stderr,/Failed instruction fixture retained at/);assert.match(error.stderr,/MODULE_NOT_FOUND/);return true;
  });
  const roots=await readdir(temp);assert.equal(roots.length,1);assert.match(roots[0],/^xmind-instructions-/);
  const root=join(temp,roots[0]);assert.equal((await stat(root)).isDirectory(),true);
  const receipt=JSON.parse(await readFile(join(root,'failure.json'),'utf8'));
  assert.equal(receipt.deadlineMs,30000);assert.equal(receipt.code,1);assert.match(receipt.stderr,/MODULE_NOT_FOUND/);
  console.log('Original synthetic failure retained at '+root);
});
