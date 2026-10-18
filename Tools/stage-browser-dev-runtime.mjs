// Build a local-only browser runtime from the current native xMind executable
// and a verified, paired xlang3 runtime. This is a development snapshot, not a
// release package and carries no CI or acceptance claim.
import {cp,copyFile,lstat,mkdir,readdir,readFile,realpath,rm,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {dirname,join,relative,resolve,isAbsolute,sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {verifyNativeRuntime} from '../extensions/vscode/native-runtime.js';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const values=new Map();
for(let i=2;i<process.argv.length;i+=2){
  const key=process.argv[i];
  if(!['--runtime','--built-server','--xlang-runtime','--out'].includes(key)||!process.argv[i+1]||values.has(key))
    throw new Error('Use --runtime VERIFIED_DIR --built-server FILE [--xlang-runtime SDK_RELEASE_DIR] --out DIR.');
  values.set(key,process.argv[i+1]);
}
const source=resolve(values.get('--runtime')||join(root,'extensions','vscode','native-runtime'));
const server=resolve(values.get('--built-server')||join(root,'build','native','contract-bin','Release','xmind.exe'));
const sdkRuntime=values.has('--xlang-runtime')?resolve(values.get('--xlang-runtime')):undefined;
const output=resolve(values.get('--out')||join(root,'.agentflow','local-browser-runtime'));
const privateRoot=resolve(root,'.agentflow');
const within=(parent,child)=>{const path=relative(parent,child);return path!==''&&path!== '..'&&!path.startsWith('..'+sep)&&!isAbsolute(path);};
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const regular=async path=>{const info=await lstat(path);if(!info.isFile()||info.isSymbolicLink()||resolve(await realpath(path))!==path)throw new Error('Development runtime inputs must be canonical regular files.');return info;};
if(!within(privateRoot,output)||within(source,output)||within(output,source))throw new Error('The development runtime output must be a separate directory under this repository .agentflow folder.');
const verified=await verifyNativeRuntime(source);
await regular(server);
const manifestBytes=await readFile(join(source,'native-runtime-manifest.json'));
const manifest=JSON.parse(manifestBytes.toString('utf8'));
const pairedDll=join(dirname(server),'xlang3_runtime.dll');
await regular(pairedDll);
let sdkRevision=manifest.sdkRevision;
if(sdkRuntime){
  const resolvedSdk=resolve(await realpath(sdkRuntime));
  if(resolvedSdk!==sdkRuntime)throw new Error('The xlang3 SDK runtime directory must be canonical.');
  const sdkDll=join(sdkRuntime,'xlang3_runtime.dll'),sdkExe=join(sdkRuntime,'xlang3.exe'),sdkModules=join(sdkRuntime,'modules');
  await regular(sdkDll);await regular(sdkExe);
  const modulesInfo=await lstat(sdkModules);
  if(!modulesInfo.isDirectory()||modulesInfo.isSymbolicLink()||resolve(await realpath(sdkModules))!==sdkModules)
    throw new Error('The xlang3 SDK module directory must be canonical.');
  if(sha(await readFile(pairedDll))!==sha(await readFile(sdkDll)))
    throw new Error('The xMind executable and selected xlang3 SDK runtime DLL are not paired.');
  sdkRevision=execFileSync('git',['-C',sdkRuntime,'rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
}else if(sha(await readFile(pairedDll))!==verified.manifest.files['xlang3_runtime.dll']){
  throw new Error('The current xMind binary is not paired with the verified xlang3 runtime DLL. Pass its matching SDK Release directory with --xlang-runtime.');
}
try{await lstat(output);throw new Error('Development runtime output already exists; choose a fresh output directory.');}
catch(error){if(error.code!=='ENOENT')throw error;}
await mkdir(dirname(output),{recursive:true});
await cp(source,output,{recursive:true,errorOnExist:true,force:false});
const pruneBytecodeCaches=async directory=>{
  for(const entry of await readdir(directory,{withFileTypes:true})){
    const child=join(directory,entry.name);
    if(entry.isSymbolicLink())throw new Error('The staged runtime contains a symbolic link.');
    if(entry.isDirectory()){
      if(entry.name==='__pycache__')await rm(child,{recursive:true,force:false});
      else await pruneBytecodeCaches(child);
    }
  }
};
await pruneBytecodeCaches(output);
await copyFile(server,join(output,'xmind.exe'));
if(sdkRuntime){
  await copyFile(join(sdkRuntime,'xlang3_runtime.dll'),join(output,'xlang3_runtime.dll'));
  await copyFile(join(sdkRuntime,'xlang3.exe'),join(output,'xlang3.exe'));
  const stagedModules=join(output,'modules');
  await rm(stagedModules,{recursive:true,force:false});await mkdir(stagedModules);
  const copyRuntimeModules=async(sourceDirectory,destinationDirectory,inDistributionMetadata=false)=>{
    for(const entry of await readdir(sourceDirectory,{withFileTypes:true})){
      if(entry.name==='__pycache__')continue;
      const from=join(sourceDirectory,entry.name),to=join(destinationDirectory,entry.name),info=await lstat(from);
      if(info.isSymbolicLink())throw new Error('The xlang3 SDK module tree contains a symbolic link.');
      if(info.isDirectory()){
        await mkdir(to);
        await copyRuntimeModules(from,to,inDistributionMetadata||entry.name.endsWith('.dist-info'));
        if((await readdir(to)).length===0)await rm(to,{recursive:true,force:false});
      }else if(info.isFile()&&(entry.name.endsWith('.py')||entry.name.endsWith('.x3pkg.dll')||inDistributionMetadata)){
        await copyFile(from,to);
      }else if(!info.isFile())throw new Error('The xlang3 SDK module tree contains an unsupported entry.');
    }
  };
  await copyRuntimeModules(join(sdkRuntime,'modules'),stagedModules);
  for(const name of Object.keys(manifest.files))if(name.startsWith('modules/'))delete manifest.files[name];
  const addModuleHashes=async(directory,relativePath='')=>{
    for(const entry of await readdir(directory,{withFileTypes:true})){
      const file=join(directory,entry.name),name=relativePath?`${relativePath}/${entry.name}`:entry.name;
      if(entry.isDirectory())await addModuleHashes(file,name);
      else if(entry.isFile())manifest.files[`modules/${name}`]=sha(await readFile(file));
      else throw new Error('The staged xlang3 module inventory contains an unsupported entry.');
    }
  };
  await addModuleHashes(stagedModules);
  manifest.files['xlang3_runtime.dll']=sha(await readFile(join(output,'xlang3_runtime.dll')));
  manifest.files['xlang3.exe']=sha(await readFile(join(output,'xlang3.exe')));
  manifest.sdkRevision=sdkRevision;
}
manifest.nativeRevision=execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
manifest.files['xmind.exe']=sha(await readFile(join(output,'xmind.exe')));
const provenance=Buffer.from(JSON.stringify({
  kind:'local-development-browser-runtime',
  xmindRevision:manifest.nativeRevision,
  sdkRevision:manifest.sdkRevision,
  baseRuntimeManifestSha256:sha(manifestBytes),
  nativeExecutableSha256:manifest.files['xmind.exe'],
  xlangRuntimeDllSha256:manifest.files['xlang3_runtime.dll'],
  xlangRuntimeSource:sdkRuntime||source,
  releaseAccepted:false,
  validation:'The complete staged runtime inventory was hash-verified; no test-suite or release claim is made.'
},null,2)+'\n');
await writeFile(join(output,'provenance.json'),provenance);
manifest.files['provenance.json']=sha(provenance);
await writeFile(join(output,'native-runtime-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
const staged=await verifyNativeRuntime(output);
if(staged.manifest.files['xmind.exe']!==manifest.files['xmind.exe'])throw new Error('The staged xMind executable failed runtime verification.');
console.log(JSON.stringify({output:staged.runtimeRoot,xmindRevision:manifest.nativeRevision,sdkRevision:manifest.sdkRevision,
  runtimeFiles:Object.keys(staged.manifest.files).length,releaseAccepted:false,hashVerified:true}));
