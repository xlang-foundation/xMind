'use strict';
// Synthetic file packaging only. Dummy native bytes are copied and hashed;
// neither Native nor a Python interpreter is executed.
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const os=require('node:os');
const path=require('node:path');
const crypto=require('node:crypto');
const {pathToFileURL}=require('node:url');
const {REQUIRED_NATIVE,REQUIRED_STDLIB,verifyNativeRuntime,MANIFEST_NAME}=require('../native-runtime');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const packager=import(pathToFileURL(path.join(__dirname,'../scripts/package-native-runtime.mjs')).href);
async function fixture(t){
  // Windows runners can expose an 8.3 alias in TEMP. Positive fixtures use the
  // canonical parent; intentional alias-rejection cases remain unchanged.
  const temp=await fs.realpath(os.tmpdir()),root=await fs.mkdtemp(path.join(temp,'xmind-native-package-files-'));
  t.after(async()=>{const target=path.resolve(root);assert.equal(path.dirname(target).toLowerCase(),temp.toLowerCase());assert.match(path.basename(target),/^xmind-native-package-files-/);await fs.rm(target,{recursive:true});});
  const bundle=path.join(root,'accepted-bundle'),stdlibSource=path.join(root,'pure-source'),out=path.join(root,'packaged-runtime');
  await fs.mkdir(bundle);await fs.mkdir(stdlibSource);
  const files={};
  for(const name of [...REQUIRED_NATIVE,'licenses/SYNTHETIC-XLANG-LICENSE']){const value=Buffer.from('Synthetic accepted native bytes: '+name);await fs.mkdir(path.dirname(path.join(bundle,name)),{recursive:true});await fs.writeFile(path.join(bundle,name),value);files[name]=hash(value);}
  for(const name of [...REQUIRED_STDLIB,'synthetic_extra.py']){await fs.mkdir(path.dirname(path.join(stdlibSource,name)),{recursive:true});await fs.writeFile(path.join(stdlibSource,name),Buffer.from('# Synthetic source '+name+'\r\n'));}
  const stdlibLicense=path.join(root,'SYNTHETIC-PYTHON-LICENSE');await fs.writeFile(stdlibLicense,'Synthetic license text');
  const manifest={schemaVersion:1,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:'a'.repeat(40),sdkRevision:'b'.repeat(40),files};
  const bundleManifest=path.join(root,'accepted-manifest.json');
  const options={explicitStage:true,bundle,bundleManifest,stdlibSource,stdlibLicense,out};
  const save=async()=>{const raw=Buffer.from(JSON.stringify(manifest));await fs.writeFile(bundleManifest,raw);options.bundleManifestSha256=hash(raw);};await save();
  return {root,manifest,save,options};
}
test('staging preserves accepted native bytes and pure source while excluding interpreters and bytecode',async t=>{
  const f=await fixture(t);
  for(const name of ['python.exe','synthetic.pyc','synthetic.pyd'])await fs.writeFile(path.join(f.options.stdlibSource,name),'Synthetic excluded bytes');
  for(const directory of ['__pycache__','site-packages','.config']){await fs.mkdir(path.join(f.options.stdlibSource,directory));await fs.writeFile(path.join(f.options.stdlibSource,directory,'hidden.py'),'Synthetic excluded bytes');}
  const result=await(await packager).stageNativeRuntime(f.options);
  assert.equal(result.nativeFiles,5);assert.equal(result.pureSourceFiles,5);assert.equal(result.files,12);assert.equal(result.nativeExecuted,false);assert.equal(result.configAccessed,false);assert.equal(result.installed,false);
  const verified=await verifyNativeRuntime(f.options.out);assert.equal(verified.manifest.sourceManifestSha256,f.options.bundleManifestSha256);
  for(const name of REQUIRED_NATIVE)assert.deepEqual(await fs.readFile(path.join(f.options.out,name)),await fs.readFile(path.join(f.options.bundle,name)));
  assert.deepEqual(await fs.readFile(path.join(f.options.out,'stdlib/synthetic_extra.py')),Buffer.from('# Synthetic source synthetic_extra.py\r\n'));
  for(const name of ['stdlib/python.exe','stdlib/synthetic.pyc','stdlib/synthetic.pyd','stdlib/.config/hidden.py'])await assert.rejects(fs.stat(path.join(f.options.out,name)),{code:'ENOENT'});
});

test('unified staging binds one product executable and rejects legacy launcher injection',async t=>{
 const f=await fixture(t);const result=await(await packager).stageNativeRuntime(f.options);
 assert.equal(result.nativeFiles,5);assert.equal(result.pureSourceFiles,5);assert.equal(result.files,12);
 const verified=await verifyNativeRuntime(f.options.out);assert.equal(verified.manifest.schemaVersion,1);
 assert.equal(verified.manifest.files['xmind.exe'],f.manifest.files['xmind.exe']);
 for(const name of ['xmind_server.exe','xmind_cli.exe','xmind_admin.exe','xmind_schema_worker.exe'])await assert.rejects(fs.stat(path.join(f.options.out,name)),{code:'ENOENT'});
 const mixed=await fixture(t);const bytes=Buffer.from('Synthetic unwanted legacy launcher');await fs.writeFile(path.join(mixed.options.bundle,'xmind_server.exe'),bytes);mixed.manifest.files['xmind_server.exe']=hash(bytes);await mixed.save();
 await assert.rejects((await packager).stageNativeRuntime(mixed.options),/unsupported/);await assert.rejects(fs.stat(mixed.options.out),{code:'ENOENT'});
});
test('unbound or changed native source manifests never publish output',async t=>{
  const f=await fixture(t),stage=(await packager).stageNativeRuntime;
  await assert.rejects(stage({...f.options,explicitStage:false}),/Explicit/);
  await assert.rejects(stage({...f.options,bundleManifestSha256:'f'.repeat(64)}),/manifest changed/);
  await fs.writeFile(path.join(f.options.bundle,'xmind.exe'),'Tampered synthetic native bytes');
  await assert.rejects(stage(f.options),/bundle file changed/);await assert.rejects(fs.stat(f.options.out),{code:'ENOENT'});
});
test('source inventory cannot include a Python executable or missing native file',async t=>{
  const f=await fixture(t),stage=(await packager).stageNativeRuntime;
  f.manifest.files['python.exe']='c'.repeat(64);await f.save();await assert.rejects(stage(f.options),/unsupported file/);
  delete f.manifest.files['python.exe'];delete f.manifest.files['xmind.exe'];await f.save();await assert.rejects(stage(f.options),/incomplete/);
  await assert.rejects(fs.stat(f.options.out),{code:'ENOENT'});
});
test('missing pure source, license output collision and reused output fail before publication',async t=>{
  const f=await fixture(t),stage=(await packager).stageNativeRuntime;
  await fs.rename(path.join(f.options.stdlibSource,'os.py'),path.join(f.root,'held-os.py'));
  await assert.rejects(stage(f.options),/source is incomplete/);
  await fs.rename(path.join(f.root,'held-os.py'),path.join(f.options.stdlibSource,'os.py'));
  const name='licenses/Python-STDLIB-LICENSE';await fs.writeFile(path.join(f.options.bundle,name),'Synthetic colliding license');f.manifest.files[name]=hash(Buffer.from('Synthetic colliding license'));await f.save();
  await assert.rejects(stage(f.options),/collides/);delete f.manifest.files[name];await f.save();
  await fs.mkdir(f.options.out);await assert.rejects(stage(f.options),/fresh directory/);
});
test('outputs cannot be nested in source trees and private paths cannot be license inputs',async t=>{
  const f=await fixture(t),stage=(await packager).stageNativeRuntime;
  for(const out of [f.options.bundle,path.join(f.options.bundle,'nested'),path.join(f.options.stdlibSource,'nested')])await assert.rejects(stage({...f.options,out}),/inside an input tree/);
  await assert.rejects(stage({...f.options,stdlibLicense:path.join(f.root,'.config/providers.yaml')}),/not a standard-library license/);
  await assert.rejects(fs.stat(f.options.out),{code:'ENOENT'});
});
test('symbolic source aliases cannot enter a native runtime package',async t=>{
  const f=await fixture(t),stage=(await packager).stageNativeRuntime,alias=path.join(f.root,'alias-bundle');
  await fs.symlink(f.options.bundle,alias,'junction');await assert.rejects(stage({...f.options,bundle:alias}),/regular canonical/);
  await assert.rejects(fs.stat(f.options.out),{code:'ENOENT'});
});
