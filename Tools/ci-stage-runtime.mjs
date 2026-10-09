// Isolated CI file packaging only. No native program, interpreter, provider,
// user configuration or database is executed or read by this adapter.
import {readFile,writeFile,readdir,lstat,realpath} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {resolve,join} from 'node:path';
import runtime from '../extensions/vscode/native-runtime.js';
import {stageNativeRuntime} from '../extensions/vscode/scripts/package-native-runtime.mjs';
if(process.env.GITHUB_ACTIONS!=='true')throw Error('Native CI staging requires an isolated GitHub runner.');
const root=resolve(import.meta.dirname,'..'),evidence=join(root,'build/ci-evidence'),bundle=join(root,'build/native-distribution');
const sdkRevision='ad8040ffb8aba6eeabeb09053a8e222df09a4e7a',stdlibRevision='ebf955df7a89ed0c7968f79faec1de49f61ed7cb';
const sha=b=>createHash('sha256').update(b).digest('hex');
const need=(condition,message)=>{if(!condition)throw Error(message);};
const gitRevision=directory=>execFileSync('git',['-C',directory,'rev-parse','HEAD'],{encoding:'utf8',windowsHide:true}).trim();
const sourceRevision=gitRevision(root);
need(sourceRevision===process.env.GITHUB_SHA,'The CI checkout differs from the workflow revision.');
need(gitRevision(resolve(root,'../xlang3'))===sdkRevision&&gitRevision(resolve(root,'../stdlib-source'))===stdlibRevision,'Runtime/library source pins differ.');
async function bytes(file,max=128*1024*1024){
 const info=await lstat(file);
 need(info.isFile()&&!info.isSymbolicLink()&&(await realpath(file)).toLowerCase()===resolve(file).toLowerCase()&&info.size<=max,'CI input is not a bounded canonical regular file.');
 const raw=await readFile(file);need(raw.length===info.size,'CI input changed while reading.');return raw;
}
const json=raw=>JSON.parse(raw.toString('utf8').replace(/^\uFEFF/,''));
const provenanceRaw=await bytes(join(evidence,'provenance.json'),1024*1024),provenance=json(provenanceRaw);
need(provenance.xmind===sourceRevision&&provenance.xlang3===sdkRevision&&provenance.stdlib_source===stdlibRevision&&provenance.runtime==='Native xlang3; no CPython execution or bridge','Native provenance differs from the accepted CI inputs.');
need(provenanceRaw.equals(await bytes(join(bundle,'provenance.json'),1024*1024)),'Bundle provenance differs from the tested build.');
const receipt=json(await bytes(join(evidence,'native-gate.json'),1024*1024));
const contractsRaw=await bytes(join(evidence,'contracts.json'),4*1024*1024),ctestRaw=await bytes(join(evidence,'native-ctest.log'),8*1024*1024);
const registered=json(contractsRaw).tests.map(test=>test.name).sort(),expected=receipt.expectedContracts;
need(receipt.schemaVersion===1&&receipt.sourceRevision===sourceRevision&&receipt.exitCode===0&&receipt.contractsSha256===sha(contractsRaw)&&receipt.ctestSha256===sha(ctestRaw),'A successful complete native gate receipt is required.');
need(Array.isArray(expected)&&expected.length>0&&new Set(expected).size===expected.length&&JSON.stringify([...expected].sort())===JSON.stringify(registered),'The native gate excludes or duplicates registered contracts.');
const log=ctestRaw.toString('utf8'),passed=[...log.matchAll(/Test\s+#\d+:\s+(\S+)\s+\.{2,}\s+Passed\s+/g)].map(match=>match[1]).sort();
need(JSON.stringify(passed)===JSON.stringify(registered)&&log.includes(`100% tests passed, 0 tests failed out of ${registered.length}`),'CTest did not pass every registered native contract.');
const files={};let total=0;
async function inventory(prefix=''){
 const directory=join(bundle,...prefix.split('/').filter(Boolean)),stat=await lstat(directory);
 need(stat.isDirectory()&&!stat.isSymbolicLink()&&(await realpath(directory)).toLowerCase()===resolve(directory).toLowerCase(),'Bundle directory is aliased.');
 for(const item of (await readdir(directory,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))){
  const name=prefix?prefix+'/'+item.name:item.name;
  need(runtime.relativeFile(name)&&!item.isSymbolicLink(),'Bundle path is unsupported.');
  if(item.isDirectory()){need(name==='modules'||name==='licenses'||name.startsWith('licenses/'),'Bundle directory is unsupported.');await inventory(name);}
  else {need(item.isFile()&&['native','license','metadata'].includes(runtime.runtimeFileKind(name)),'Bundle file is unsupported.');const raw=await bytes(join(bundle,...name.split('/')));total+=raw.length;need(total<=256*1024*1024,'Bundle exceeds its byte limit.');files[name]=sha(raw);}
  need(Object.keys(files).length<=128,'Bundle exceeds the reviewed source inventory limit.');
 }
}
await inventory();
need(runtime.REQUIRED_NATIVE.every(name=>Object.hasOwn(files,name)),'A required native executable or module is absent.');
const manifest={schemaVersion:1,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:sourceRevision,sdkRevision,files};
const manifestRaw=Buffer.from(JSON.stringify(manifest,null,2)+'\n'),bundleManifest=join(evidence,'accepted-source-manifest.json');
await writeFile(bundleManifest,manifestRaw,{flag:'wx'});
const staged=await stageNativeRuntime({explicitStage:true,bundle,bundleManifest,bundleManifestSha256:sha(manifestRaw),stdlibSource:resolve(root,'../stdlib-source/Lib'),stdlibLicense:resolve(root,'../stdlib-source/LICENSE'),out:join(root,'extensions/vscode/native-runtime')});
const report={sourceRevision,sdkRevision,stdlibRevision,contracts:registered.length,sourceFiles:Object.keys(files).length,sourceManifestSha256:sha(manifestRaw),...staged,scope:'File staging from the complete successful native gate; VSIX verification follows separately'};
await writeFile(join(evidence,'runtime-staging.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(report));
