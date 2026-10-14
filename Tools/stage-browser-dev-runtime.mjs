// Build a local-only browser runtime from the current native xMind executable
// and a verified, paired xlang3 runtime. This is a development snapshot, not a
// release package and carries no CI or acceptance claim.
import {cp,copyFile,lstat,mkdir,readFile,realpath,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {dirname,join,relative,resolve,isAbsolute,sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {verifyNativeRuntime} from '../extensions/vscode/native-runtime.js';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const values=new Map();
for(let i=2;i<process.argv.length;i+=2){
  const key=process.argv[i];
  if(!['--runtime','--built-server','--out'].includes(key)||!process.argv[i+1]||values.has(key))
    throw new Error('Use --runtime DIR --built-server FILE --out DIR.');
  values.set(key,process.argv[i+1]);
}
const source=resolve(values.get('--runtime')||join(root,'extensions','vscode','native-runtime'));
const server=resolve(values.get('--built-server')||join(root,'build','native','contract-bin','Release','xmind.exe'));
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
if(sha(await readFile(pairedDll))!==verified.manifest.files['xlang3_runtime.dll'])
  throw new Error('The current xMind binary is not paired with the verified xlang3 runtime DLL.');
try{await lstat(output);throw new Error('Development runtime output already exists; choose a fresh output directory.');}
catch(error){if(error.code!=='ENOENT')throw error;}
await mkdir(dirname(output),{recursive:true});
await cp(source,output,{recursive:true,errorOnExist:true,force:false});
await copyFile(server,join(output,'xmind.exe'));
manifest.nativeRevision=execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
manifest.files['xmind.exe']=sha(await readFile(join(output,'xmind.exe')));
const provenance=Buffer.from(JSON.stringify({
  kind:'local-development-browser-runtime',
  xmindRevision:manifest.nativeRevision,
  sdkRevision:manifest.sdkRevision,
  baseRuntimeManifestSha256:sha(manifestBytes),
  nativeExecutableSha256:manifest.files['xmind.exe'],
  xlangRuntimeDllSha256:manifest.files['xlang3_runtime.dll'],
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
