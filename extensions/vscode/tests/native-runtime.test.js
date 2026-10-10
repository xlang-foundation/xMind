'use strict';
// Synthetic filesystem inventory tests only. Dummy EXE/DLL bytes are never run;
// no SDK, user provider configuration, actual backend or SQLite is accessed.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { resolveNativeRuntime, retainNativeRuntime, verifyNativeRuntime, validateManifest, REQUIRED_NATIVE, nativeProgramEntry, REQUIRED_STDLIB, MANIFEST_NAME } = require('../native-runtime');
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function fixture(t) {
  // Positive inputs must be canonical even when a Windows runner's TEMP uses
  // a short-name alias. Production canonical-path rejection is preserved.
  const temp = await fs.realpath(os.tmpdir()), root = await fs.mkdtemp(path.join(temp, 'xmind-native-runtime-files-'));
  t.after(async () => { const resolved = path.resolve(root); assert.equal(path.dirname(resolved).toLowerCase(), temp.toLowerCase()); assert.match(path.basename(resolved), /^xmind-native-runtime-files-/); await fs.rm(resolved, { recursive: true }); });
  const runtime = path.join(root, 'extension/native-runtime'), files = {};
  await fs.mkdir(runtime, { recursive: true });
  for (const name of [...REQUIRED_NATIVE, ...REQUIRED_STDLIB.map(name => 'stdlib/' + name), 'licenses/SYNTHETIC-LICENSE']) {
    const bytes = Buffer.from('Synthetic inventory only: ' + name); await fs.mkdir(path.dirname(path.join(runtime, name)), { recursive: true }); await fs.writeFile(path.join(runtime, name), bytes); files[name] = digest(bytes);
  }
  const manifest = { schemaVersion:1, platform: 'win32', arch: 'x64', bridgeEnabled: false, nativeRevision: 'a'.repeat(40), sdkRevision: 'b'.repeat(40), sourceManifestSha256: 'c'.repeat(64), files };
  const save = () => fs.writeFile(path.join(runtime, MANIFEST_NAME), JSON.stringify(manifest)); await save();
  return { root, runtime, manifest, save, context: { extensionUri: { fsPath: path.join(root, 'extension') }, globalStorageUri: { fsPath: path.join(root, 'private') } }, host: { platform: 'win32', arch: 'x64', remoteName: undefined } };
}
test('packaged native discovery binds every listed byte and returns paths without launching', async t => {
  const f = await fixture(t), config = path.join(f.root, 'not-created/.config/providers.yaml');
  const resolved = await resolveNativeRuntime(f.context, f.host, { providerConfigPath: config });
  assert.equal(resolved.nativeProgram, path.join(f.runtime, 'xmind.exe')); assert.equal(resolved.modules, path.join(f.runtime, 'modules'));
  assert.equal(resolved.stdlib, path.join(f.runtime, 'stdlib')); assert.equal(resolved.providerConfig, config); assert.equal(resolved.privateStateRoot, f.context.globalStorageUri.fsPath);
  assert.equal(resolved.manifestSha256, digest(await fs.readFile(path.join(f.runtime, MANIFEST_NAME))));
  await assert.rejects(fs.lstat(config), { code: 'ENOENT' });
});

test('unified package resolves fixed entry modes and retains the exact primary executable',async t=>{
 const f=await fixture(t);await fs.mkdir(f.context.globalStorageUri.fsPath);
 const runtime=await resolveNativeRuntime(f.context,f.host);assert.equal((await verifyNativeRuntime(f.runtime)).manifest.schemaVersion,1);
 assert.equal(runtime.nativeProgram,path.join(f.runtime,'xmind.exe'));assert.equal(runtime.serverSha256,f.manifest.files['xmind.exe']);
 for(const role of ['serve','admin','console','schema-worker'])assert.deepEqual(nativeProgramEntry(runtime,role),{program:runtime.nativeProgram,arguments:role==='console'?[]:[role]});
 const retained=await retainNativeRuntime(runtime,f.context.globalStorageUri.fsPath);
 assert.equal(retained.nativeProgram,path.join(retained.runtimeRoot,'xmind.exe'));
 assert.deepEqual(await fs.readFile(retained.nativeProgram),await fs.readFile(runtime.nativeProgram));
 assert.equal((await retainNativeRuntime(runtime,f.context.globalStorageUri.fsPath)).runtimeRoot,retained.runtimeRoot);
 await fs.appendFile(retained.nativeProgram,'changed fixture');await assert.rejects(verifyNativeRuntime(retained.runtimeRoot),/verification failed/);
});

test('one format rejects missing primary executables, legacy launchers and unsupported schemas',async t=>{
 const f=await fixture(t);
 for(const name of ['xmind_server.exe','xmind_cli.exe','xmind_admin.exe','xmind_schema_worker.exe'])assert.throws(()=>validateManifest({...f.manifest,files:{...f.manifest.files,[name]:'d'.repeat(64)}}),/retired multi-executable layout.*current unified xMind package.*migration is unsupported/);
 const missing=structuredClone(f.manifest);delete missing.files['xmind.exe'];assert.throws(()=>validateManifest(missing),/incomplete|inventory/);
 assert.throws(()=>validateManifest({...f.manifest,schemaVersion:2}),/incompatible/);
 assert.throws(()=>nativeProgramEntry({runtimeRoot:f.runtime},'worker'),/Unsupported/);
});

test('retained generation uses verified private inventory and reuses exact bytes without overwriting',async t=>{
 const f=await fixture(t);await fs.mkdir(f.context.globalStorageUri.fsPath);const runtime=await resolveNativeRuntime(f.context,f.host);
 const retained=await retainNativeRuntime(runtime,f.context.globalStorageUri.fsPath);assert.equal(retained.manifestSha256,runtime.manifestSha256);assert.notEqual(retained.runtimeRoot,runtime.runtimeRoot);assert.equal(path.dirname(retained.runtimeRoot),path.join(f.context.globalStorageUri.fsPath,'runtime-generations'));
 assert.equal((await retainNativeRuntime(runtime,f.context.globalStorageUri.fsPath)).runtimeRoot,retained.runtimeRoot);
 await fs.appendFile(path.join(retained.runtimeRoot,'xmind.exe'),'changed fixture');await assert.rejects(retainNativeRuntime(runtime,f.context.globalStorageUri.fsPath),/verification failed/);
 assert.ok((await fs.readFile(path.join(retained.runtimeRoot,'xmind.exe'),'utf8')).endsWith('changed fixture'));
});

test('managed generation refuses unqualified pure-source override without creating runtime storage',async t=>{
 const f=await fixture(t);await fs.mkdir(f.context.globalStorageUri.fsPath);const runtime=await resolveNativeRuntime(f.context,f.host);
 await assert.rejects(retainNativeRuntime({...runtime,qualified:false},f.context.globalStorageUri.fsPath),/bundled pure library/);
 assert.deepEqual(await fs.readdir(f.context.globalStorageUri.fsPath),[]);
});
test('explicit machine runtime and pure source paths do not search the repository or configuration', async t => {
  const f = await fixture(t), other = path.join(f.root, 'pure-source'); await fs.mkdir(other);
  for (const name of REQUIRED_STDLIB) { await fs.mkdir(path.dirname(path.join(other, name)), { recursive: true }); await fs.writeFile(path.join(other, name), 'Synthetic pure source'); }
  const unavailable = { extensionUri: { fsPath: path.join(f.root, 'other-extension') }, globalStorageUri: f.context.globalStorageUri };
  const value = await resolveNativeRuntime(unavailable, f.host, { runtimeDirectory: f.runtime, stdlibSource: other });
  assert.equal(value.runtimeRoot, f.runtime); assert.equal(value.stdlib, other); assert.equal(value.providerConfig, undefined);
  await assert.rejects(resolveNativeRuntime(unavailable, f.host), { code: 'ENOENT' });
});
test('unsupported host or remote execution is rejected before filesystem access', async () => {
  let reads = 0; const io = new Proxy({}, { get() { reads++; throw Error('Forbidden filesystem access'); } });
  for (const host of [{ platform: 'linux', arch: 'x64' }, { platform: 'win32', arch: 'arm64' }, { platform: 'win32', arch: 'x64', remoteName: 'ssh-remote' }])
    await assert.rejects(resolveNativeRuntime({}, host, {}, { io }), /local Windows x64/);
  assert.equal(reads, 0);
});
test('binary tampering and missing pure-source inventory are rejected', async t => {
  const f = await fixture(t); await fs.writeFile(path.join(f.runtime, 'xmind.exe'), 'Changed synthetic bytes');
  await assert.rejects(verifyNativeRuntime(f.runtime), /verification failed/);
  delete f.manifest.files['stdlib/os.py']; assert.throws(() => validateManifest(f.manifest), /incomplete/);
});
test('manifest forbids unknown executables, native Python extensions, traversal and private paths', async t => {
  const f = await fixture(t);
  for (const name of ['python.exe', 'stdlib/_sqlite3.pyd', 'stdlib/os.pyc', '../escape', 'stdlib/.config/private.py', 'stdlib/.GIT/private.py', 'stdlib/site-packages/private.py', 'modules/extra.dll']) {
    assert.throws(() => validateManifest({ ...f.manifest, files: { ...f.manifest.files, [name]: 'f'.repeat(64) } }), /inventory/);
  }
  assert.throws(() => validateManifest({ ...f.manifest, files: { ...f.manifest.files, 'XMIND.EXE': 'f'.repeat(64) } }), /inventory/);
  assert.throws(() => validateManifest({ ...f.manifest, bridgeEnabled: true }), /incompatible/);
});
test('unexpected files are rejected even when every expected hash is unchanged', async t => {
  const f = await fixture(t); await fs.writeFile(path.join(f.runtime, 'python.exe'), 'Synthetic extra file'); await assert.rejects(verifyNativeRuntime(f.runtime), /inventory changed/);
});
test('relative, controlled or malformed configuration paths are rejected without reading keys', async t => {
  const f = await fixture(t);
  for (const providerConfigPath of ['.config/providers.yaml', path.join(f.root, 'config\nkey'), path.join(f.root, 'bad\ud800')])
    await assert.rejects(resolveNativeRuntime(f.context, f.host, { providerConfigPath }), /absolute local path/);
});
