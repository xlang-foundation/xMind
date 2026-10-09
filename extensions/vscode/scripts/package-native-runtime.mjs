// Native distribution file adapter only. No compiler, SDK, native program,
// Python interpreter, provider API, user configuration or SQLite is executed.
// Root stages an already accepted bundle with its exact manifest SHA first;
// normal VSIX prepublish then verifies the staged immutable runtime inventory.
import {readFile,writeFile,lstat,realpath,readdir,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve,join,dirname,relative,isAbsolute,sep} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import runtime from '../native-runtime.js';
const {REQUIRED_NATIVE,REQUIRED_STDLIB,MANIFEST_NAME,relativeFile,runtimeFileKind,verifyNativeRuntime}=runtime;
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
    need(relativeFile(name)&&['native','license','metadata'].includes(runtimeFileKind(name))&&/^[a-f0-9]{64}$/.test(digest)&&!seen.has(name.toLowerCase()),'The native source inventory contains an unsupported file.');seen.add(name.toLowerCase());
  }
  need(native.every(name=>Object.hasOwn(value.files,name))&&Object.keys(value.files).some(name=>name.startsWith('licenses/')),'The native source files or notices are incomplete.');return value;
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
  const licensePath=resolve(stdlibLicense);need(!licensePath.split(/[\\/]/).some(name=>['.config','.agentflow'].includes(name.toLowerCase())),'Provider/private state is not a standard-library license input.');
  need(![...inputs.keys()].some(name=>name.toLowerCase()==='licenses/python-stdlib-license'),'The standard-library license output collides with the accepted bundle.');
  const license=await bytes(licensePath,1024*1024);inputs.set('licenses/Python-STDLIB-LICENSE',license);total+=license.length;need(total<=256*1024*1024,'The complete native runtime exceeds its size limit.');
  // Verify all source bytes again before publication. Source changes cannot
  // silently combine accepted DLLs with a newer SDK or newer Python source.
  need(raw.equals(await bytes(resolve(bundleManifest),1024*1024)),'The source manifest changed during staging.');
  for(const [name,digest]of Object.entries(source.files))need(sha(await bytes(join(sourceRoot,...name.split('/'))))===digest,'Native source bytes changed during staging.');
  need(JSON.stringify(await pureSources(pureRoot))===JSON.stringify(pureNames),'The pure source file set changed during staging.');
  for(const name of pureNames)need((await bytes(join(pureRoot,...name.split('/')),8*1024*1024)).equals(inputs.get('stdlib/'+name)),'A pure source changed during staging.');need(license.equals(await bytes(licensePath,1024*1024)),'The source license changed during staging.');
  await mkdir(output,{recursive:false});const files={};
  for(const [name,value]of inputs){const target=join(output,...name.split('/'));need(inside(output,target),'Native package output path escaped.');await mkdir(dirname(target),{recursive:true});await writeFile(target,value,{flag:'wx',mode:0o600});files[name]=sha(value);}
  const manifest={schemaVersion:source.schemaVersion,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:source.nativeRevision,sdkRevision:source.sdkRevision,sourceManifestSha256:bundleManifestSha256,stdlib:'Bundled pure Python3.14 source only; no CPython execution, executable, extension or bytecode',files};
  await writeFile(join(output,MANIFEST_NAME),JSON.stringify(manifest,null,2)+'\n',{flag:'wx',mode:0o600});const verified=await verifyNativeRuntime(output);
  return {output,manifestSha256:verified.manifestSha256,nativeFiles:REQUIRED_NATIVE.length,pureSourceFiles:pureNames.length,files:Object.keys(files).length,nativeExecuted:false,configAccessed:false,installed:false};
}
export async function main(args=process.argv.slice(2)){
  if(!args.length){console.log('Explicit --stage with accepted source manifest, or --verify of an already staged native runtime is required.');return;}
  if(args[0]==='--verify'){need(args.length===1||args.length===3&&args[1]==='--runtime','Invalid native verification arguments.');const result=await verifyNativeRuntime(args.length===1?join(EXTENSION,'native-runtime'):resolve(args[2]));console.log(JSON.stringify({nativeRuntimeVerified:true,files:Object.keys(result.manifest.files).length,manifestSha256:result.manifestSha256,nativeExecuted:false,installed:false}));return;}
  need(args[0]==='--stage','Invalid native packaging command.');const values={};for(let i=1;i<args.length;i+=2){const flag=args[i];need(['--bundle','--bundle-manifest','--bundle-manifest-sha','--stdlib-source','--stdlib-license','--out'].includes(flag)&&!Object.hasOwn(values,flag)&&typeof args[i+1]==='string','Invalid native staging arguments.');values[flag]=args[i+1];}
  console.log(JSON.stringify(await stageNativeRuntime({explicitStage:true,bundle:values['--bundle'],bundleManifest:values['--bundle-manifest'],bundleManifestSha256:values['--bundle-manifest-sha'],stdlibSource:values['--stdlib-source'],stdlibLicense:values['--stdlib-license'],out:values['--out']})));
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href)main().catch(()=>{console.error('Native runtime packaging failed; verify the accepted source bundle, manifest and pure source inputs.');process.exitCode=1;});
