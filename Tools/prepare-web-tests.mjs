// CI-only bootstrap from current native build outputs. This is not a release
// acceptance receipt. All installation runs through xmind.exe's embedded pip.
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {stageNativeRuntime,webLibraryInputs} from '../extensions/vscode/scripts/package-native-runtime.mjs';
import runtimeApi from '../extensions/vscode/native-runtime.js';
import {findPythonStdlibLicense} from '../Native/tests/python-stdlib-license.mjs';
if(process.env.GITHUB_ACTIONS!=='true')throw new Error('Web CI bootstrap requires an isolated runner.');
const [executableArg,modulesArg,stdlibArg]=process.argv.slice(2);
const root=path.resolve(import.meta.dirname,'..'),executable=path.resolve(executableArg),modules=path.resolve(modulesArg),stdlib=path.resolve(stdlibArg);
const output=path.join(root,'build/web-test-runtime'),bundle=path.join(root,'build/web-test-source'),packages=path.join(root,'build/web-python');
const sha=value=>createHash('sha256').update(value).digest('hex'),files={};
await fs.mkdir(bundle,{recursive:false});
async function copy(source,name){const value=await fs.readFile(source);const target=path.join(bundle,...name.split('/'));await fs.mkdir(path.dirname(target),{recursive:true});await fs.writeFile(target,value,{flag:'wx'});files[name]=sha(value);}
for(const [source,name]of [[executable,'xmind.exe'],[path.join(path.dirname(executable),'xlang3_runtime.dll'),'xlang3_runtime.dll'],[path.join(path.dirname(modules),'xlang3.exe'),'xlang3.exe']])await copy(source,name);
async function nativeModules(prefix=''){
 for(const entry of await fs.readdir(path.join(modules,prefix),{withFileTypes:true})){
  if(entry.name==='__pycache__')continue;if(entry.isSymbolicLink())throw new Error('SDK module aliases are forbidden');
  const name=prefix?prefix+'/'+entry.name:entry.name;
  if(entry.isDirectory())await nativeModules(name);
  else if(['module','metadata'].includes(runtimeApi.runtimeFileKind('modules/'+name)))await copy(path.join(modules,name),'modules/'+name);
 }
}
await nativeModules();
const sdkRoot=execFileSync('git',['-C',path.dirname(modules),'rev-parse','--show-toplevel'],{encoding:'utf8',windowsHide:true}).trim();
await copy(path.join(sdkRoot,'modules/pydantic_core/PYDANTIC_CORE_LICENSE.txt'),'licenses/xlang3-pydantic-core/PYDANTIC_CORE_LICENSE.txt');
await copy(path.join(root,'LICENSE'),'licenses/xmind-LICENSE');
for(const name of ['xmind_web.py','xmind_web_routes.json'])await copy(path.join(root,'views/browser',name),'modules/'+name);
const revision=directory=>execFileSync('git',['-C',directory,'rev-parse','HEAD'],{encoding:'utf8',windowsHide:true}).trim();
const source={schemaVersion:1,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:revision(root),sdkRevision:revision(sdkRoot),files};
const raw=Buffer.from(JSON.stringify(source,null,2)+'\n'),manifest=path.join(root,'build/web-test-source.json');await fs.writeFile(manifest,raw,{flag:'wx'});
await stageNativeRuntime({explicitStage:true,bundle,bundleManifest:manifest,bundleManifestSha256:sha(raw),stdlibSource:stdlib,stdlibLicense:await findPythonStdlibLicense(stdlib),out:output});
const env={...process.env};for(const key of Object.keys(env))if(key.startsWith('XMIND_UI_')||['XMIND_API_KEY','XMIND_AUTH_TOKEN','XMIND_VIEW_TOKEN'].includes(key))delete env[key];
const installed=execFileSync(path.join(output,'xmind.exe'),['pip','--isolated','install','--only-binary=:all:','--no-compile','--no-input','--disable-pip-version-check','--find-links',path.join(output,'wheels'),'--target',packages,'--report',path.join(root,'build/web-install-report.json'),'-r',path.join(root,'views/browser/requirements-web.txt')],{env,windowsHide:true,maxBuffer:4*1024*1024});
await fs.writeFile(path.join(root,'build/ci-evidence/web-pip-install.log'),installed);
const runtimeManifest=JSON.parse(await fs.readFile(path.join(output,'native-runtime-manifest.json'),'utf8'));
for(const [name,value]of await webLibraryInputs(packages)){
 if(name.endsWith('.x3pkg.dll')&&runtimeManifest.files[name]!==sha(value))throw new Error('Web binding differs from SDK');
 const target=path.join(output,...name.split('/'));await fs.mkdir(path.dirname(target),{recursive:true});await fs.writeFile(target,value);runtimeManifest.files[name]=sha(value);
}
await fs.writeFile(path.join(output,'native-runtime-manifest.json'),JSON.stringify(runtimeManifest,null,2)+'\n');await runtimeApi.verifyNativeWebRuntime(output);
const config={runtime:output,modules:path.join(output,'modules'),stdlib:path.join(output,'stdlib'),packages,fixture:path.join(path.dirname(executable),'xmind_browser_asgi_fixture.exe')};
await fs.writeFile(path.join(root,'build/web-test-environment.json'),JSON.stringify(config,null,2)+'\n');console.log(JSON.stringify({prepared:true,scope:'Unqualified CI test bootstrap; embedded xlang3 pip, no inference',...config}));
