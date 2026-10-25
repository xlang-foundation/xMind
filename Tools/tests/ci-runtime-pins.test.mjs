import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
import test from 'node:test';

test('CI runtime staging accepts exactly the SDK and library source checked out by the workflow', async () => {
 const root=new URL('../../',import.meta.url);
 const workflow=await readFile(new URL('.github/workflows/native-windows.yml',root),'utf8');
 const staging=await readFile(new URL('Tools/ci-stage-runtime.mjs',root),'utf8');
 const native=await readFile(new URL('Tools/ci-native.ps1',root),'utf8');
 for(const [repository,pin,guard] of [['CantorAI/xlang3','sdkRevision','ciRevision'],['python/cpython','stdlibRevision','ciStdlibRevision']]){
  const checkout=new RegExp('repository: '+repository+'\\s+ref: ([a-f0-9]{40})').exec(workflow);
  const expected=new RegExp(pin+"='([a-f0-9]{40})'").exec(staging);
  assert.ok(checkout,repository+' checkout pin missing');
  assert.ok(expected,pin+' staging pin missing');
  assert.equal(expected[1],checkout[1],repository+' staging/checkout mismatch');
  const build=new RegExp('\\$'+guard+" -ne '([a-f0-9]{40})'").exec(native);assert.ok(build,guard+' native build pin missing');assert.equal(build[1],checkout[1],repository+' native build/checkout mismatch');
 }
});
