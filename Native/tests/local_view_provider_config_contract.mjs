// The local browser view applies its trusted provider file to empty workspace
// profiles without exposing key values to the browser or making provider calls.
import assert from 'node:assert/strict';
import cp from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createHash,randomBytes} from 'node:crypto';
import {stageNativeRuntime} from '../../extensions/vscode/scripts/package-native-runtime.mjs';
import {createBrowserServer} from '../../views/browser/server.mjs';

const [program,dll,modules,stdlib]=process.argv.slice(2);
assert.ok(stdlib);
const parent=await fs.realpath(os.tmpdir());
const base=await fs.mkdtemp(path.join(parent,'xmind-local-view-provider-'));
const initial=path.join(base,'initial workspace'),existing=path.join(base,'existing workspace');
const added=path.join(base,'added workspace'),profiles=path.join(base,'profiles');
const bundle=path.join(base,'bundle'),runtime=path.join(base,'runtime'),assets=path.join(base,'assets');
const yaml=path.join(base,'providers.yaml');
const readyRoot=path.join(parent,'xmind-local-view-provider-ready-'+randomBytes(8).toString('hex'));
const ready=path.join(readyRoot,'ready.json');
const pids=new Set();let browser,viewProcess,viewError='',passed=false;
const env={...process.env};
for(const key of Object.keys(env))if(key==='XMIND_AUTH_TOKEN'||key==='XMIND_API_KEY'||key.startsWith('XMIND_UI_'))delete env[key];

function invoke(exe,args,{cwd=initial,extraEnv={}}={}){
 return new Promise((resolve,reject)=>{
  const child=cp.spawn(exe,args,{cwd,env:{...env,...extraEnv},windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';
  child.stdout.on('data',bytes=>stdout+=bytes);child.stderr.on('data',bytes=>stderr+=bytes);
  child.once('error',reject);child.once('close',(code,signal)=>resolve({code,signal,stdout,stderr}));
 });
}
async function command(exe,args,options){const result=await invoke(exe,args,options);assert.equal(result.code,0,result.stderr||result.stdout);assert.equal(result.signal,null);return JSON.parse(result.stdout);}
async function profilePid(exe,root,workspace){const value=await command(exe,['--profile-root',root,'--workspace',workspace,'profile-info'],{cwd:workspace});pids.add(value.process_id);return value;}
async function waitReady(){const deadline=Date.now()+60000;while(Date.now()<deadline){if(viewProcess.exitCode!==null)throw Error('Native view exited before readiness');try{return JSON.parse(await fs.readFile(ready,'utf8'));}catch(error){if(error.code!=='ENOENT'&&!(error instanceof SyntaxError))throw error;await new Promise(resolve=>setTimeout(resolve,50));}}throw Error('Native view readiness timed out');}
async function stop(pid){const observed=await command(path.join(runtime,'xmind.exe'),['admin','inspect-owner-process',String(pid)]);const exit=await command(path.join(runtime,'xmind.exe'),['admin','observe-owner-exit',String(pid),observed.process_birth,'10000']);assert.equal(exit.exited,true);pids.delete(pid);}

try{
 await Promise.all([initial,existing,added,bundle].map(directory=>fs.mkdir(directory,{recursive:true})));
 await fs.writeFile(yaml,'version: 1\nprofiles:\n  openai:\n    route: openai.responses\n    api_key: synthetic-openai-key-never-sent\n  claude:\n    route: anthropic.messages\n    api_key: synthetic-claude-key-never-sent\n');
 const files={};
 for(const[name,source]of [['xmind.exe',program],['xlang3_runtime.dll',dll],['xlang3.exe',path.join(path.dirname(modules),'xlang3.exe')],...['xlang_json.x3pkg.dll','xlang_sqlite3.x3pkg.dll'].map(name=>['modules/'+name,path.join(modules,name)])]){
  const bytes=await fs.readFile(source);await fs.mkdir(path.dirname(path.join(bundle,name)),{recursive:true});await fs.writeFile(path.join(bundle,name),bytes);files[name]=createHash('sha256').update(bytes).digest('hex');
 }
 const notice=Buffer.from('Synthetic local provider configuration contract package.');await fs.mkdir(path.join(bundle,'licenses'));await fs.writeFile(path.join(bundle,'licenses/NOTICE.txt'),notice);
 files['licenses/NOTICE.txt']=createHash('sha256').update(notice).digest('hex');
 const source=Buffer.from(JSON.stringify({schemaVersion:1,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:'a'.repeat(40),sdkRevision:'b'.repeat(40),files}));
 const sourceFile=path.join(base,'source-manifest.json');await fs.writeFile(sourceFile,source);
 await stageNativeRuntime({explicitStage:true,bundle,bundleManifest:sourceFile,bundleManifestSha256:createHash('sha256').update(source).digest('hex'),stdlibSource:stdlib,stdlibLicense:await fs.access(path.join(path.dirname(stdlib),'LICENSE')).then(()=>path.join(path.dirname(stdlib),'LICENSE'),()=>path.join(path.dirname(stdlib),'LICENSE.txt')),out:runtime});
 cp.execFileSync(process.execPath,[new URL('../../views/browser/build.mjs',import.meta.url).pathname.slice(1),assets],{windowsHide:true,stdio:'ignore'});
 const exe=path.join(runtime,'xmind.exe'),emptyProfile=await profilePid(exe,profiles,existing);
 const token=randomBytes(32).toString('hex');
 const viewArgs=['view','--workspace',initial,'--profile-root',profiles,'--ready-file',ready,'--config',yaml];
 viewProcess=cp.spawn(exe,viewArgs,{cwd:initial,env:{...env,XMIND_VIEW_TOKEN:token},windowsHide:true,stdio:['ignore','ignore','pipe']});
 viewProcess.stderr.on('data',bytes=>{if(viewError.length<4096)viewError+=bytes.toString('utf8').slice(0,4096-viewError.length);});
 const readyRecord=await waitReady();assert.equal(readyRecord.workspace.root,await fs.realpath(initial));assert.equal(readyRecord.started_backend,true);pids.add(readyRecord.backend_process_id);
 browser=await createBrowserServer({backend:readyRecord.origin,assetRoot:assets,localAccessToken:token});const browserOrigin=await browser.listen();
 const headers={Origin:browserOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'};
 const login=await fetch(browserOrigin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+token},body:'{}'});assert.equal(login.status,200,await login.clone().text());let cookie=login.headers.get('set-cookie').split(';')[0];
 const catalogue=await fetch(browserOrigin+'/ui/workspaces',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(catalogue.status,200,await catalogue.clone().text());const values=await catalogue.json(),existingRoot=await fs.realpath(existing),existingProfile=values.workspaces.find(value=>value.root===existingRoot);assert.ok(existingProfile);
 const selected=await fetch(browserOrigin+'/ui/workspaces/select',{method:'POST',headers:{...headers,Cookie:cookie},body:JSON.stringify({workspace_id:existingProfile.workspace_id})});assert.equal(selected.status,200,await selected.clone().text());cookie=selected.headers.get('set-cookie').split(';')[0];
 async function publicProfiles(){const response=await fetch(browserOrigin+'/v1/provider/profiles',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(response.status,200,await response.clone().text());return response.json();}
 let discovered=await publicProfiles();assert.deepEqual(discovered.profiles.map(value=>value.id).sort(),['claude','openai']);assert.ok(!JSON.stringify(discovered).includes('synthetic-openai-key-never-sent'));assert.ok(!JSON.stringify(discovered).includes('synthetic-claude-key-never-sent'));
 const add=await fetch(browserOrigin+'/ui/workspaces/add',{method:'POST',headers:{...headers,Cookie:cookie},body:JSON.stringify({root:added})});assert.equal(add.status,200,await add.clone().text());cookie=add.headers.get('set-cookie').split(';')[0];
 discovered=await publicProfiles();assert.deepEqual(discovered.profiles.map(value=>value.id).sort(),['claude','openai']);assert.ok(!JSON.stringify(discovered).includes('synthetic-openai-key-never-sent'));
 assert.ok(emptyProfile.process_id);await profilePid(exe,profiles,added);passed=true;
 console.log(JSON.stringify({actualNativeView:true,existingEmptyProfileImportsSharedYaml:true,newWorkspaceImportsSharedYaml:true,keysReturnedToBrowser:false,providerRequests:0,actualViewWorkspaceConfig:true}));
}finally{
 await browser?.close().catch(()=>{});
 if(viewProcess&&viewProcess.exitCode===null){viewProcess.kill();await new Promise(resolve=>viewProcess.once('close',resolve));}
 if(viewProcess?.pid)try{await stop(viewProcess.pid);}catch{}
 for(const pid of [...pids])try{await stop(pid);}catch(error){console.error('Owned profile cleanup: '+error.message);passed=false;}
 assert.equal(path.dirname(await fs.realpath(base)),parent);assert.ok(path.basename(base).startsWith('xmind-local-view-provider-'));
 if(passed){await fs.rm(base,{recursive:true,force:true});await fs.rm(readyRoot,{recursive:true,force:true});}
 else console.error(JSON.stringify({retainedFixture:base,retainedReadyRoot:readyRoot,viewError}));
}
