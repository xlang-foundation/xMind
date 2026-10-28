// Native distribution file adapter only. No compiler, SDK, native program,
// Python interpreter, provider API, user configuration or SQLite is executed.
// Root stages an already accepted bundle with its exact manifest SHA first;
// normal VSIX prepublish then verifies the staged immutable runtime inventory.
import {readFile,writeFile,lstat,realpath,readdir,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve,join,dirname,relative,isAbsolute,sep} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import runtime from '../native-runtime.js';
import {pipBootstrap} from './pip-bootstrap.mjs';
const {REQUIRED_NATIVE,REQUIRED_STDLIB,MANIFEST_NAME,relativeFile,runtimeFileKind,verifyNativeRuntime,validateModuleInventory}=runtime;
const EXTENSION=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const need=(condition,message)=>{if(!condition)throw new Error(message);};
const same=(a,b)=>resolve(a).toLowerCase()===resolve(b).toLowerCase();
const inside=(parent,child)=>{const part=relative(parent,child);return part&&part!=='..'&&!part.startsWith('..'+sep)&&!isAbsolute(part);};
async function regular(path,directory=false){const info=await lstat(path);need((directory?info.isDirectory():info.isFile())&&!info.isSymbolicLink()&&same(await realpath(path),path),'Only regular canonical source files may be packaged.');return info;}
async function bytes(path,max=128*1024*1024){const info=await regular(path);need(info.size<=max,'A native package input exceeds its limit.');const value=await readFile(path);need(value.length===info.size,'A native package input changed while reading.');return value;}
async function absent(path){try{await lstat(path);}catch(error){if(error.code==='ENOENT')return;throw error;}throw Error('Native package output must be a fresh directory.');}
function sourceManifest(value){
  need(value&&value.schemaVersion===1&&value.platform==='win32'&&value.arch==='x64'&&value.bridgeEnabled===false&&/^[a-f0-9]{40}$/.test(value.nativeRevision)&&/^[a-f0-9]{40}$/.test(value.sdkRevision),'A verified Windows x64 native source manifest is required.');
  const native=REQUIRED_NATIVE;
  need(value.files&&typeof value.files==='object'&&!Array.isArray(value.files)&&Object.keys(value.files).length>=native.length&&Object.keys(value.files).length<=128,'The native source inventory is invalid.');
  const seen=new Set();
  for(const [name,digest]of Object.entries(value.files)){
    need(relativeFile(name)&&['native','module','license','metadata'].includes(runtimeFileKind(name))&&/^[a-f0-9]{64}$/.test(digest)&&!seen.has(name.toLowerCase()),'The native source inventory contains an unsupported file.');seen.add(name.toLowerCase());
  }
  need(native.every(name=>Object.hasOwn(value.files,name))&&Object.keys(value.files).some(name=>name.startsWith('licenses/')),'The native source files or notices are incomplete.');validateModuleInventory(value.files);return value;
}
async function pureSources(root,prefix=''){
  const found=[];
  for(const item of await readdir(join(root,prefix),{withFileTypes:true})){
    if(['__pycache__','site-packages','.git','.config','.agentflow'].includes(item.name.toLowerCase()))continue;
    const name=prefix?prefix+'/'+item.name:item.name;
    need(relativeFile(name)&&!item.isSymbolicLink(),'Pure standard-library sources contain an unsupported path.');
    if(item.isDirectory()){await regular(join(root,name),true);found.push(...await pureSources(root,name));}
    else if(item.isFile()&&name.endsWith('.py'))found.push(name);
    // Executables, native extensions, bytecode and non-source package data are
    // never copied. No CPython installation or interpreter is packaged.
    need(found.length<=20000,'The pure standard-library source inventory exceeds its limit.');
  }
  return found.sort();
}
// Package the SDK's own native binding for isolated pip --target installs.
// No CPython ABI extension or dependency-bypassing metadata is substituted.
export async function nativeDependencyWheels(inputs){
  const plugin=['modules/pydantic_core._pydantic_core.x3pkg.dll','modules/pydantic_core/_pydantic_core.x3pkg.dll'].find(name=>inputs.has(name));
  if(!plugin)return new Map();
  const metadataName=[...inputs.keys()].find(name=>/^modules\/pydantic_core-[^/]+\.dist-info\/METADATA$/.test(name));
  need(metadataName,'Native wheel distribution metadata is missing.');
  const metadata=inputs.get(metadataName),version=metadata.toString('utf8').match(/^Version: ([0-9]+(?:\.[0-9]+)+\+xlang3)\r?$/m)?.[1];
  need(version&&metadata.toString('utf8').match(/^Name: pydantic-core\r?$/m)&&metadataName===`modules/pydantic_core-${version}.dist-info/METADATA`,'Native wheel distribution identity is invalid.');
  const licenseName=[...inputs.keys()].find(name=>name.startsWith('licenses/')&&name.endsWith('/PYDANTIC_CORE_LICENSE.txt'));
  need(licenseName,'Native Pydantic license notice is required before wheel packaging.');
  const distribution=`pydantic_core-${version}.dist-info`,members=new Map();
  for(const name of [plugin,...['__init__.py','core_schema.py','_schema_gather.py'].map(name=>'modules/pydantic_core/'+name)]){
    need(inputs.has(name),'Native wheel package facade is missing.');members.set(name.slice(8),inputs.get(name));
  }
  members.set(distribution+'/METADATA',metadata);
  members.set(distribution+'/WHEEL',Buffer.from('Wheel-Version: 1.0\nGenerator: xMind xlang3 SDK packager\nRoot-Is-Purelib: false\nTag: py3-none-win_amd64\n'));
  members.set(distribution+'/licenses/PYDANTIC_CORE_LICENSE.txt',inputs.get(licenseName));
  const record=[...members].map(([name,value])=>`${name},sha256=${createHash('sha256').update(value).digest('base64url')},${value.length}`).join('\n')+`\n${distribution}/RECORD,,\n`;
  members.set(distribution+'/RECORD',Buffer.from(record));
  const {ZipFile}=await import('yazl');const archive=new ZipFile();
  for(const [name,value]of [...members].sort(([a],[b])=>a.localeCompare(b)))archive.addBuffer(value,name,{mtime:new Date('2020-01-01T00:00:00Z'),mode:0o100644});
  archive.end();const chunks=[];let length=0;
  for await(const chunk of archive.outputStream){length+=chunk.length;need(length<=128*1024*1024,'Native dependency wheel exceeds its bound.');chunks.push(chunk);}
  return new Map([[`wheels/pydantic_core-${version}-py3-none-win_amd64.whl`,Buffer.concat(chunks)]]);
}
// Libraries are installed separately by xmind pip. Packaging only freezes their
// source/metadata; generated CLI scripts and bytecode are never shipped.
export async function webLibraryInputs(directory){
  const root=resolve(directory);await regular(root,true);const files=new Map();let total=0;
  async function collect(prefix=''){
    for(const entry of await readdir(join(root,prefix),{withFileTypes:true})){
      if(entry.name==='__pycache__'||!prefix&&entry.name==='bin')continue;
      const name=prefix?prefix+'/'+entry.name:entry.name,path=join(root,...name.split('/'));
      need(relativeFile(name)&&!entry.isSymbolicLink(),'Web libraries contain an unsupported path.');
      if(entry.isDirectory()){await regular(path,true);await collect(name);continue;}
      if(/\.(?:pyc|pyo)$/.test(name))continue;
      need(entry.isFile()&&['module','metadata'].includes(runtimeFileKind('modules/'+name)),'Web libraries contain an unsupported executable or resource.');
      const value=await bytes(path,16*1024*1024);total+=value.length;need(total<=64*1024*1024&&files.size<5000,'Web library inventory exceeds limits.');files.set('modules/'+name,value);
    }
  }
  await collect();
  for(const name of ['fastapi/__init__.py','uvicorn/server.py','starlette/applications.py','anyio/__init__.py','pydantic/__init__.py','typing_extensions.py'])need(files.has('modules/'+name),'Install the pinned web requirements with xmind pip first.');
  return files;
}
export async function stageNativeRuntime(options={}){
  need(options.explicitStage===true,'Explicit native runtime staging is required.');
  const {bundle,bundleManifest,bundleManifestSha256,stdlibSource,stdlibLicense,out}=options;
  need([bundle,bundleManifest,stdlibSource,stdlibLicense,out].every(value=>typeof value==='string'&&isAbsolute(value)&&!/[\x00-\x1f]/.test(value))&&/^[a-f0-9]{64}$/.test(bundleManifestSha256),'Native staging requires explicit absolute source paths and the accepted manifest digest.');
  const sourceRoot=resolve(bundle),pureRoot=resolve(stdlibSource),output=resolve(out);await regular(sourceRoot,true);await regular(pureRoot,true);
  need(!same(output,sourceRoot)&&!same(output,pureRoot)&&!inside(sourceRoot,output)&&!inside(pureRoot,output),'Native package output cannot be inside an input tree.');await absent(output);
  const raw=await bytes(resolve(bundleManifest),1024*1024);need(sha(raw)===bundleManifestSha256,'The accepted native source manifest changed.');const source=sourceManifest(JSON.parse(raw.toString('utf8').replace(/^\uFEFF/,'')));
  const inputs=new Map();let total=0;
  for(const [name,digest]of Object.entries(source.files)){const value=await bytes(join(sourceRoot,...name.split('/')),Math.min(128*1024*1024,256*1024*1024-total));need(sha(value)===digest,'An accepted native bundle file changed.');total+=value.length;need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');inputs.set(name,value);}
  const pureNames=await pureSources(pureRoot);need(REQUIRED_STDLIB.every(name=>pureNames.includes(name)),'The pure standard-library source is incomplete.');
  for(const name of pureNames){const value=await bytes(join(pureRoot,...name.split('/')),8*1024*1024);inputs.set('stdlib/'+name,value);total+=value.length;need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');}
  const bundled=join(pureRoot,'ensurepip','_bundled');await regular(bundled,true);
  const pipWheels=(await readdir(bundled)).filter(name=>/^pip-.+\.whl$/.test(name));need(pipWheels.length===1,'Exactly one bundled pip bootstrap wheel is required.');
  const pipPath=join(bundled,pipWheels[0]),pipBytes=await bytes(pipPath,8*1024*1024),pip=await pipBootstrap(pipBytes,pipWheels[0]);
  for(const [name,value]of pip.files){need(!inputs.has(name),'Pip bootstrap collides with a source input.');inputs.set(name,value);total+=value.length;need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');}
  const licensePath=resolve(stdlibLicense);need(!licensePath.split(/[\\/]/).some(name=>['.config','.agentflow'].includes(name.toLowerCase())),'Provider/private state is not a standard-library license input.');
  need(![...inputs.keys()].some(name=>name.toLowerCase()==='licenses/python-stdlib-license'),'The standard-library license output collides with the accepted bundle.');
  const license=await bytes(licensePath,1024*1024);inputs.set('licenses/Python-STDLIB-LICENSE',license);total+=license.length;need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');
  for(const [name,value]of await nativeDependencyWheels(inputs)){need(!inputs.has(name),'Native dependency wheel collides with an accepted input.');inputs.set(name,value);total+=value.length;need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');}
  let webInputs;
  if(options.webLibrarySource){
    need(isAbsolute(options.webLibrarySource),'Web library sources require an absolute directory.');
    webInputs=await webLibraryInputs(options.webLibrarySource);
    for(const [name,value]of webInputs){
      if(name.endsWith('.x3pkg.dll'))need(inputs.has(name)&&inputs.get(name).equals(value),'Web native bindings must match the accepted xlang3 SDK exactly.');
      total+=value.length-(inputs.get(name)?.length||0);inputs.set(name,value);
    }
    for(const name of ['xmind_web.py','xmind_web_routes.json']){const value=await bytes(join(EXTENSION,'../../views/browser',name),1024*1024);inputs.set('modules/'+name,value);total+=value.length;}
    need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');
  }
  // Verify all source bytes again before publication. Source changes cannot
  // silently combine accepted DLLs with a newer SDK or newer Python source.
  need(raw.equals(await bytes(resolve(bundleManifest),1024*1024)),'The source manifest changed during staging.');
  need(pipBytes.equals(await bytes(pipPath,8*1024*1024))&&JSON.stringify((await readdir(bundled)).filter(name=>/^pip-.+\.whl$/.test(name)))===JSON.stringify(pipWheels),'The pip bootstrap wheel changed during staging.');
  for(const [name,digest]of Object.entries(source.files))need(sha(await bytes(join(sourceRoot,...name.split('/'))))===digest,'Native source bytes changed during staging.');
  need(JSON.stringify(await pureSources(pureRoot))===JSON.stringify(pureNames),'The pure source file set changed during staging.');
  if(webInputs){const frozen=await webLibraryInputs(options.webLibrarySource);need(frozen.size===webInputs.size&&[...webInputs].every(([name,value])=>frozen.get(name)?.equals(value)),'Web library sources changed during staging.');for(const name of ['xmind_web.py','xmind_web_routes.json'])need((await bytes(join(EXTENSION,'../../views/browser',name),1024*1024)).equals(inputs.get('modules/'+name)),'Web adapter sources changed during staging.');}
  for(const name of pureNames)need((await bytes(join(pureRoot,...name.split('/')),8*1024*1024)).equals(inputs.get('stdlib/'+name)),'A pure source changed during staging.');need(license.equals(await bytes(licensePath,1024*1024)),'The source license changed during staging.');
  await mkdir(output,{recursive:false});const files={};
  for(const [name,value]of inputs){const target=join(output,...name.split('/'));need(inside(output,target),'Native package output path escaped.');await mkdir(dirname(target),{recursive:true});await writeFile(target,value,{flag:'wx',mode:0o600});files[name]=sha(value);}
  const manifest={schemaVersion:source.schemaVersion,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:source.nativeRevision,sdkRevision:source.sdkRevision,sourceManifestSha256:bundleManifestSha256,stdlib:'Bundled Python3.14 pure source and pip bootstrap, including generic Windows script resources; no CPython runtime, ABI extension, execution or bytecode',pipBootstrap:pip.metadata,...(webInputs?{embeddedWeb:true}:{}),files};
  await writeFile(join(output,MANIFEST_NAME),JSON.stringify(manifest,null,2)+'\n',{flag:'wx',mode:0o600});const verified=await verifyNativeRuntime(output);
  return {output,manifestSha256:verified.manifestSha256,nativeFiles:REQUIRED_NATIVE.length,pureSourceFiles:pureNames.length,files:Object.keys(files).length,nativeExecuted:false,configAccessed:false,installed:false};
}
export async function main(args=process.argv.slice(2)){
  if(!args.length){console.log('Explicit --stage with accepted source manifest, or --verify of an already staged native runtime is required.');return;}
  if(args[0]==='--verify'||args[0]==='--verify-web'){need(args.length===1||args.length===3&&args[1]==='--runtime','Invalid native verification arguments.');const verifier=args[0]==='--verify-web'?runtime.verifyNativeWebRuntime:verifyNativeRuntime;const result=await verifier(args.length===1?join(EXTENSION,'native-runtime'):resolve(args[2]));console.log(JSON.stringify({nativeRuntimeVerified:true,files:Object.keys(result.manifest.files).length,manifestSha256:result.manifestSha256,nativeExecuted:false,installed:false}));return;}
  need(args[0]==='--stage','Invalid native packaging command.');const values={};for(let i=1;i<args.length;i+=2){const flag=args[i];need(['--bundle','--bundle-manifest','--bundle-manifest-sha','--stdlib-source','--stdlib-license','--web-library-source','--out'].includes(flag)&&!Object.hasOwn(values,flag)&&typeof args[i+1]==='string','Invalid native staging arguments.');values[flag]=args[i+1];}
  console.log(JSON.stringify(await stageNativeRuntime({explicitStage:true,bundle:values['--bundle'],bundleManifest:values['--bundle-manifest'],bundleManifestSha256:values['--bundle-manifest-sha'],stdlibSource:values['--stdlib-source'],stdlibLicense:values['--stdlib-license'],webLibrarySource:values['--web-library-source'],out:values['--out']})));
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href)main().catch(()=>{console.error('Native runtime packaging failed; verify the accepted source bundle, manifest and pure source inputs.');process.exitCode=1;});
