'use strict';
// Trusted extension-host access adapter. This module only verifies local files;
// it never starts a process, accesses user configuration contents or opens SQLite.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');

const MANIFEST_NAME = 'native-runtime-manifest.json';
const REQUIRED_NATIVE = Object.freeze([
  'xmind.exe', 'xlang3_runtime.dll', 'xlang3.exe',
  'modules/xlang_json.x3pkg.dll', 'modules/xlang_sqlite3.x3pkg.dll'
]);
const REQUIRED_STDLIB = Object.freeze(['os.py', 'json/__init__.py', 'encodings/__init__.py', 'importlib/__init__.py']);
const FORBIDDEN_COMPONENTS = new Set(['.git', '.config', '.agentflow', '__pycache__', 'site-packages']);
function check(value, message) { if (!value) throw new Error(message); }
function safeText(value) {
  if (typeof value !== 'string' || !value || value.length > 32768) return false;
  for (const character of value) { const code = character.codePointAt(0); if (code < 32 || (code >= 0xd800 && code <= 0xdfff)) return false; }
  return true;
}
function relativeFile(value) {
  return safeText(value) && value.length <= 1024 && !/[\\:]/.test(value) && !value.startsWith('/') &&
    value.split('/').every(component => component && component !== '.' && component !== '..' &&
      !FORBIDDEN_COMPONENTS.has(component.replace(/[. ]+$/, '').toLowerCase()));
}
function absolute(value, label) {
  check(safeText(value) && path.isAbsolute(value), `${label} must be an absolute local path.`);
  return path.resolve(value);
}
function samePath(left, right) { return path.resolve(left).toLowerCase() === path.resolve(right).toLowerCase(); }
async function regular(file, directory = false, io = fs) {
  const info = await io.lstat(file);
  check((directory ? info.isDirectory() : info.isFile()) && !info.isSymbolicLink() && samePath(await io.realpath(file), file), 'The native runtime contains an aliased or unsupported file.');
  return info;
}
function runtimeFileKind(name) {
  if (REQUIRED_NATIVE.includes(name)) return 'native';
  if (name.startsWith('stdlib/') && name.endsWith('.py')) return 'stdlib';
  if (name.startsWith('licenses/') && !/\.(?:exe|dll|pyd|pyc|pyo)$/i.test(name)) return 'license';
  if (['README.txt', 'provenance.json'].includes(name)) return 'metadata';
  return undefined;
}
function validateManifest(manifest) {
  check(manifest && typeof manifest === 'object' && !Array.isArray(manifest) && manifest.schemaVersion === 1 &&
    manifest.platform === 'win32' && manifest.arch === 'x64' && manifest.bridgeEnabled === false &&
    /^[a-f0-9]{40}$/.test(manifest.nativeRevision) && /^[a-f0-9]{40}$/.test(manifest.sdkRevision) &&
    /^[a-f0-9]{64}$/.test(manifest.sourceManifestSha256), 'The native runtime manifest is incompatible.');
  const files = manifest.files, native = REQUIRED_NATIVE;
  check(files && typeof files === 'object' && !Array.isArray(files) && Object.keys(files).length >= native.length + REQUIRED_STDLIB.length &&
    Object.keys(files).length <= 20000, 'The native runtime file inventory is invalid.');
  const names = new Set();
  for (const [name, digest] of Object.entries(files)) {
    check(relativeFile(name) && runtimeFileKind(name) && /^[a-f0-9]{64}$/.test(digest) && !names.has(name.toLowerCase()), 'The native runtime file inventory is invalid.');
    names.add(name.toLowerCase());
  }
  check(native.every(name => Object.hasOwn(files, name)) && REQUIRED_STDLIB.every(name => Object.hasOwn(files, 'stdlib/' + name)), 'The native runtime or pure standard-library source is incomplete.');
  check(Object.keys(files).some(name => name.startsWith('licenses/')), 'The native runtime license notices are missing.');
  return manifest;
}
async function enumerate(root, io = fs, prefix = '') {
  const result = [];
  for (const item of await io.readdir(path.join(root, prefix), { withFileTypes: true })) {
    const name = prefix ? prefix + '/' + item.name : item.name;
    check(relativeFile(name) && !item.isSymbolicLink(), 'The native runtime contains an unsupported path.');
    if (item.isDirectory()) { await regular(path.join(root, ...name.split('/')), true, io); result.push(...await enumerate(root, io, name)); }
    else { check(item.isFile(), 'The native runtime contains an unsupported file.'); result.push(name); }
    check(result.length <= 20001, 'The native runtime file inventory exceeds its limit.');
  }
  return result.sort();
}
async function verifyNativeRuntime(runtimeRoot, { io = fs } = {}) {
  runtimeRoot = absolute(runtimeRoot, 'Native runtime directory');
  await regular(runtimeRoot, true, io);
  const manifestPath = path.join(runtimeRoot, MANIFEST_NAME), info = await regular(manifestPath, false, io);
  check(info.size <= 4 * 1024 * 1024, 'The native runtime manifest exceeds its limit.');
  const raw = await io.readFile(manifestPath);
  const manifest = validateManifest(JSON.parse(raw.toString('utf8').replace(/^\uFEFF/, '')));
  const expected = [MANIFEST_NAME, ...Object.keys(manifest.files)].sort();
  check(JSON.stringify(await enumerate(runtimeRoot, io)) === JSON.stringify(expected), 'The native runtime inventory changed.');
  let totalBytes = 0;
  for (const [name, digest] of Object.entries(manifest.files)) {
    const file = path.join(runtimeRoot, ...name.split('/')), stat = await regular(file, false, io);
    check(stat.size <= 128 * 1024 * 1024 && (totalBytes += stat.size) <= 256 * 1024 * 1024, 'The native runtime exceeds its size limit.');
    const bytes = await io.readFile(file);
    check(bytes.length === stat.size && crypto.createHash('sha256').update(bytes).digest('hex') === digest, 'The native runtime file verification failed.');
  }
  check(raw.equals(await io.readFile(manifestPath)), 'The native runtime manifest changed during verification.');
  return { runtimeRoot, manifest, manifestSha256: crypto.createHash('sha256').update(raw).digest('hex') };
}
async function resolveNativeRuntime(context, hostEnv, userSettings = {}, dependencies = {}) {
  check(hostEnv?.platform === 'win32' && hostEnv?.arch === 'x64' && !hostEnv?.remoteName,
    'Managed native xMind currently requires a local Windows x64 VS Code host.');
  const io = dependencies.io || fs;
  const extensionRoot = absolute(context?.extensionUri?.fsPath, 'Extension directory');
  const privateStateRoot = absolute(context?.globalStorageUri?.fsPath, 'Native private storage directory');
  const directory = userSettings.runtimeDirectory ? absolute(userSettings.runtimeDirectory, 'Native runtime directory') : path.join(extensionRoot, 'native-runtime');
  const verified = await verifyNativeRuntime(directory, { io });
  let stdlib = path.join(verified.runtimeRoot, 'stdlib');
  if (userSettings.stdlibSource) {
    stdlib = absolute(userSettings.stdlibSource, 'Pure standard-library source'); await regular(stdlib, true, io);
    for (const name of REQUIRED_STDLIB) await regular(path.join(stdlib, ...name.split('/')), false, io);
  }
  // Pass the ONE explicit user/machine-owned path only. Native owns parsing,
  // encrypted storage and import; no workspace file search or key access here.
  const providerConfig = userSettings.providerConfigPath ? absolute(userSettings.providerConfigPath, 'Provider configuration') : undefined;
  const program = 'xmind.exe';
  return { nativeProgram: path.join(verified.runtimeRoot, program), modules: path.join(verified.runtimeRoot, 'modules'), stdlib,
    providerConfig, privateStateRoot, runtimeRoot: verified.runtimeRoot, manifestSha256: verified.manifestSha256,
    serverSha256: verified.manifest.files[program],
    qualified: samePath(stdlib,path.join(verified.runtimeRoot,'stdlib')) };
}
async function retainNativeRuntime(runtime, privateRoot, { io=fs, uuid=()=>crypto.randomUUID() }={}) {
  check(runtime.qualified===true,'Managed owner upgrades require the verified package’s bundled pure library sources. Clear the machine stdlibSource override or use an external development server.');
  const source=await verifyNativeRuntime(runtime.runtimeRoot,{io});
  check(source.manifestSha256===runtime.manifestSha256,'The selected native runtime changed.');
  await regular(privateRoot,true,io);
  const parent=path.join(privateRoot,'runtime-generations');await io.mkdir(parent,{recursive:true});await regular(parent,true,io);
  let output;
  const verifyOutput=async()=>{const result=await verifyNativeRuntime(output,{io});check(result.manifestSha256===source.manifestSha256,'The retained native generation changed.');return {...runtime,runtimeRoot:output,nativeProgram:path.join(output,'xmind.exe'),modules:path.join(output,'modules'),stdlib:path.join(output,'stdlib')};};
  const entries=await io.readdir(parent,{withFileTypes:true});check(entries.length<=4096,'Retained native generation inventory exceeds its limit.');
  for(const entry of entries.sort((a,b)=>a.name.localeCompare(b.name))){
    if(!entry.name.startsWith(source.manifestSha256+'-'))continue;
    check(entry.isDirectory()&&!entry.isSymbolicLink(),'Retained native generation is aliased.');output=path.join(parent,entry.name);
    try{await regular(path.join(output,MANIFEST_NAME),false,io);}catch(error){if(error.code==='ENOENT')continue;throw error;}
    return verifyOutput();
  }
  const identifier=uuid();check(/^[a-f0-9-]{36}$/.test(identifier),'Invalid native generation publication identifier.');
  output=path.join(parent,source.manifestSha256+'-'+identifier);await io.mkdir(output,{recursive:false});await regular(output,true,io);
  // Copy only the verified inventory. No workspace/configuration/database files
  // are read, and an existing live generation is never overwritten or removed.
  for(const [name,digest]of Object.entries(source.manifest.files)){
    const data=await io.readFile(path.join(source.runtimeRoot,...name.split('/')));
    check(crypto.createHash('sha256').update(data).digest('hex')===digest,'Native source changed during retention.');
    const target=path.join(output,...name.split('/'));await io.mkdir(path.dirname(target),{recursive:true});await io.writeFile(target,data,{flag:'wx'});
  }
  const raw=await io.readFile(path.join(source.runtimeRoot,MANIFEST_NAME));check(crypto.createHash('sha256').update(raw).digest('hex')===source.manifestSha256,'Native manifest changed during retention.');
  // Native pins ancestor identities while a generation is live. Publish in a
  // fresh final directory without renaming beneath those handles. The complete
  // verified manifest is written last; an incomplete directory is never reused.
  await io.writeFile(path.join(output,MANIFEST_NAME),raw,{flag:'wx'});
  return verifyOutput();
}
function nativeProgramEntry(runtime, role) {
  check(['serve', 'admin', 'console', 'schema-worker'].includes(role), 'Unsupported native entry role.');
  return {program:path.join(runtime.runtimeRoot,'xmind.exe'),arguments:role==='console'?[]:[role]};
}
module.exports = { resolveNativeRuntime, retainNativeRuntime, verifyNativeRuntime, validateManifest, relativeFile, runtimeFileKind, nativeProgramEntry, REQUIRED_NATIVE, REQUIRED_STDLIB, MANIFEST_NAME };
