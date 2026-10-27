// The local browser view imports new and updated trusted provider entries into
// existing workspace profiles without exposing keys or making provider calls.
import assert from 'node:assert/strict';
import cp from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createHash,randomBytes} from 'node:crypto';
import {stageNativeRuntime} from '../../extensions/vscode/scripts/package-native-runtime.mjs';
import {findPythonStdlibLicense} from './python-stdlib-license.mjs';
import {createBrowserServer} from '../../views/browser/server.mjs';

const [program,dll,modules,stdlib]=process.argv.slice(2);
assert.ok(stdlib);
const parent=await fs.realpath(os.tmpdir());
const base=await fs.mkdtemp(path.join(parent,'xmind-local-view-provider-'));
const initial=path.join(base,'initial workspace'),existing=path.join(base,'existing workspace');
const added=path.join(base,'added workspace'),profiles=path.join(base,'profiles');
const bundle=path.join(base,'bundle'),runtime=path.join(base,'runtime'),assets=path.join(base,'assets');
const yaml=path.join(base,'providers.yaml'),mcpConfig=path.join(base,'mcp.json');
const readyRoot=path.join(parent,'xmind-local-view-provider-ready-'+randomBytes(8).toString('hex'));
const ready=path.join(readyRoot,'ready.json');
const pids=new Map();let browser,viewProcess,viewError='',passed=false;
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
async function rememberOwner(exe,pid){const observed=await command(exe,['admin','inspect-owner-process',String(pid)]);pids.set(pid,observed.process_birth);}
async function profilePid(exe,root,workspace,languageServers,providerConfig){const args=['--profile-root',root,'--workspace',workspace];if(providerConfig)args.push('--config',providerConfig);if(languageServers)args.push('--language-server-config',languageServers);args.push('--mcp-config',mcpConfig,'profile-info');const value=await command(exe,args,{cwd:workspace});await rememberOwner(exe,value.process_id);return value;}
async function waitReady(){const deadline=Date.now()+60000;while(Date.now()<deadline){if(viewProcess.exitCode!==null)throw Error('Native view exited before readiness');try{return JSON.parse(await fs.readFile(ready,'utf8'));}catch(error){if(!['ENOENT','EBUSY'].includes(error.code)&&!(error instanceof SyntaxError))throw error;await new Promise(resolve=>setTimeout(resolve,50));}}throw Error('Native view readiness timed out');}
async function stop(pid,birth){
 const observe=()=>command(path.join(runtime,'xmind.exe'),['admin','observe-owner-exit',String(pid),birth,'10000']);
 const before=await command(path.join(runtime,'xmind.exe'),['admin','observe-owner-exit',String(pid),birth,'0']);
 if(!before.exited){assert.equal(before.identity_matches,true,'Only the captured fixture process may be signalled');try{process.kill(pid);}catch(error){if(!(await observe()).exited)throw error;}}
 // A disappeared/reused PID proves the captured original exited; identity is
 // necessarily unavailable in that case. Native defines this explicitly.
 assert.equal((await observe()).exited,true);pids.delete(pid);
}

try{
 await Promise.all([initial,existing,added,bundle].map(directory=>fs.mkdir(directory,{recursive:true})));
 await fs.writeFile(yaml,'version: 1\nactive_profile: openai\nprofiles:\n  openai:\n    route: openai.responses\n    api_key: synthetic-openai-key-never-sent\n');
 const files={};
 for(const[name,source]of [['xmind.exe',program],['xlang3_runtime.dll',dll],['xlang3.exe',path.join(path.dirname(modules),'xlang3.exe')],...['xlang_json.x3pkg.dll','xlang_sqlite3.x3pkg.dll'].map(name=>['modules/'+name,path.join(modules,name)])]){
  const bytes=await fs.readFile(source);await fs.mkdir(path.dirname(path.join(bundle,name)),{recursive:true});await fs.writeFile(path.join(bundle,name),bytes);files[name]=createHash('sha256').update(bytes).digest('hex');
 }
 const notice=Buffer.from('Synthetic local provider configuration contract package.');await fs.mkdir(path.join(bundle,'licenses'));await fs.writeFile(path.join(bundle,'licenses/NOTICE.txt'),notice);
 files['licenses/NOTICE.txt']=createHash('sha256').update(notice).digest('hex');
 const source=Buffer.from(JSON.stringify({schemaVersion:1,platform:'win32',arch:'x64',bridgeEnabled:false,nativeRevision:'a'.repeat(40),sdkRevision:'b'.repeat(40),files}));
 const sourceFile=path.join(base,'source-manifest.json');await fs.writeFile(sourceFile,source);
 await stageNativeRuntime({explicitStage:true,bundle,bundleManifest:sourceFile,bundleManifestSha256:createHash('sha256').update(source).digest('hex'),stdlibSource:stdlib,stdlibLicense:await findPythonStdlibLicense(stdlib),out:runtime});
 cp.execFileSync(process.execPath,[new URL('../../views/browser/build.mjs',import.meta.url).pathname.slice(1),assets],{windowsHide:true,stdio:'ignore'});
 const exe=path.join(runtime,'xmind.exe'),languageServers=path.join(base,'language-servers.json');await fs.writeFile(mcpConfig,JSON.stringify({servers:[{id:'fixture-mcp',transport:'http',endpoint:'https://fixture.example.test/mcp',enabled:false}]}));await fs.writeFile(languageServers,JSON.stringify({profiles:[{id:'fixture-lsp',executable:exe,language_id:'fixture',extensions:['.txt'],request_timeout_ms:1000}]}));const seededProfile=await profilePid(exe,profiles,existing,languageServers,yaml);
 await fs.writeFile(yaml,'version: 1\nactive_profile: openai\nprofiles:\n  openai:\n    route: openai.responses\n    api_key: synthetic-openai-key-never-sent\n  claude:\n    route: anthropic.messages\n    api_key: synthetic-claude-key-never-sent\n');
 const token=randomBytes(32).toString('hex');
 const viewArgs=['view','--workspace',initial,'--profile-root',profiles,'--ready-file',ready,'--upgrade-backend','--config',yaml,'--language-server-config',languageServers,'--mcp-config',mcpConfig];
 viewProcess=cp.spawn(exe,viewArgs,{cwd:initial,env:{...env,XMIND_VIEW_TOKEN:token},windowsHide:true,stdio:['ignore','ignore','pipe']});
 viewProcess.stderr.on('data',bytes=>{if(viewError.length<4096)viewError+=bytes.toString('utf8').slice(0,4096-viewError.length);});
 const readyRecord=await waitReady();assert.equal(readyRecord.workspace.root,await fs.realpath(initial));assert.equal(readyRecord.started_backend,true);await rememberOwner(exe,readyRecord.backend_process_id);
 const hostHeaders={Authorization:'Bearer '+token,'Content-Type':'application/json'};let providerState=await fetch(readyRecord.origin+'/v1/provider/profiles',{headers:hostHeaders});assert.equal(providerState.status,200,await providerState.clone().text());providerState=await providerState.json();assert.deepEqual(providerState.profiles.map(value=>value.id).sort(),['claude','openai'],'The initial browser workspace must import the trusted local provider YAML before the view is ready');assert.ok(!JSON.stringify(providerState).includes('synthetic-openai-key-never-sent')&&!JSON.stringify(providerState).includes('synthetic-claude-key-never-sent'),'Initial provider metadata must never disclose keys');const refusedPath=await fetch(readyRecord.origin+'/v1/provider/configuration/import',{method:'POST',headers:hostHeaders,body:JSON.stringify({path:initial,expected_revision:providerState.revision})});assert.equal(refusedPath.status,400);const untrustedImport=await fetch(readyRecord.origin+'/v1/provider/configuration/import',{method:'POST',headers:{Authorization:'View '+'0'.repeat(64)+'.'+'0'.repeat(64),'X-XMind-View-Origin':'http://127.0.0.1:12345','Content-Type':'application/json'},body:JSON.stringify({path:yaml,expected_revision:providerState.revision})});assert.equal(untrustedImport.status,403);const trustedImport=await fetch(readyRecord.origin+'/v1/provider/configuration/import',{method:'POST',headers:hostHeaders,body:JSON.stringify({path:yaml,expected_revision:providerState.revision})});assert.equal(trustedImport.status,200,await trustedImport.clone().text());
 await fs.writeFile(yaml,'version: [not valid\n');const invalidImport=await fetch(readyRecord.origin+'/v1/provider/configuration/import',{method:'POST',headers:hostHeaders,body:JSON.stringify({path:yaml,expected_revision:providerState.revision})});assert.equal(invalidImport.status,400);assert.equal((await invalidImport.json()).provider_yaml_error_code,'unsupported_yaml','Trusted YAML failures must expose only a fixed safe classification');
 await fs.writeFile(yaml,'version: 1\nactive_profile: openai\nprofiles:\n  openai:\n    route: openai.responses\n    api_key: synthetic-openai-key-never-sent\n  claude:\n    route: anthropic.messages\n    api_key: synthetic-claude-key-never-sent\n');
 browser=await createBrowserServer({backend:readyRecord.origin,assetRoot:assets,localAccessToken:token});const browserOrigin=await browser.listen();
 const headers={Origin:browserOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'};
 const login=await fetch(browserOrigin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+token},body:'{}'});assert.equal(login.status,200,await login.clone().text());let cookie=login.headers.get('set-cookie').split(';')[0];
 const catalogue=await fetch(browserOrigin+'/ui/workspaces',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(catalogue.status,200,await catalogue.clone().text());const values=await catalogue.json(),existingRoot=await fs.realpath(existing),existingProfile=values.workspaces.find(value=>value.root===existingRoot);assert.ok(existingProfile);
 const selected=await fetch(browserOrigin+'/ui/workspaces/select',{method:'POST',headers:{...headers,Cookie:cookie},body:JSON.stringify({workspace_id:existingProfile.workspace_id})});assert.equal(selected.status,200,await selected.clone().text());cookie=selected.headers.get('set-cookie').split(';')[0];
 async function publicProfiles(){const response=await fetch(browserOrigin+'/v1/provider/profiles',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(response.status,200,await response.clone().text());return response.json();}
 let discovered=await publicProfiles();assert.deepEqual(discovered.profiles.map(value=>value.id).sort(),['claude','openai']);assert.ok(!JSON.stringify(discovered).includes('synthetic-openai-key-never-sent'));assert.ok(!JSON.stringify(discovered).includes('synthetic-claude-key-never-sent'));
 assert.equal(discovered.active,'openai');
 const chosen=await fetch(browserOrigin+'/v1/provider/profiles/select',{method:'POST',headers:{...headers,Cookie:cookie},body:JSON.stringify({id:'claude',expected_revision:discovered.revision})});assert.equal(chosen.status,200,await chosen.clone().text());
 const chosenState=await publicProfiles();assert.equal(chosenState.active,'claude');
 const initialProfile=values.workspaces.find(value=>value.root===readyRecord.workspace.root);assert.ok(initialProfile);
 for(const workspace of [initialProfile,existingProfile]){
  const switched=await fetch(browserOrigin+'/ui/workspaces/select',{method:'POST',headers:{...headers,Cookie:cookie},body:JSON.stringify({workspace_id:workspace.workspace_id})});assert.equal(switched.status,200,await switched.clone().text());cookie=switched.headers.get('set-cookie').split(';')[0];
 }
 assert.deepEqual(await publicProfiles(),chosenState,'Native view reattachment with unchanged OpenAI-default YAML preserves the user-selected Claude registry');
 const add=await fetch(browserOrigin+'/ui/workspaces/create',{method:'POST',headers:{...headers,Cookie:cookie},body:JSON.stringify({root:added})});assert.equal(add.status,200,await add.clone().text());cookie=add.headers.get('set-cookie').split(';')[0];
 discovered=await publicProfiles();assert.deepEqual(discovered.profiles.map(value=>value.id).sort(),['claude','openai']);assert.ok(!JSON.stringify(discovered).includes('synthetic-openai-key-never-sent'));
 assert.ok(seededProfile.process_id);await profilePid(exe,profiles,added,languageServers);for(const workspace of [initial,existing,added]){const configured=await command(exe,['--profile-root',profiles,'--workspace',workspace,'mcp-servers'],{cwd:workspace});assert.deepEqual(configured,{servers:[{id:'fixture-mcp',revision:1,enabled:false,transport:'http'}],runtime_state:'per_run'});}
 // A preview handoff can explicitly retain user-owned settings even when the
 // trusted file contains new profiles or changes its default selection.
 await browser.close();browser=undefined;viewProcess.kill();await new Promise(resolve=>viewProcess.once('close',resolve));await fs.rm(ready,{force:true});
 await fs.writeFile(yaml,'version: 1\nactive_profile: alternate\nprofiles:\n  openai:\n    route: openai.responses\n    api_key: synthetic-openai-key-never-sent\n  claude:\n    route: anthropic.messages\n    api_key: synthetic-claude-key-never-sent\n  alternate:\n    route: openai.responses\n    api_key: synthetic-alternate-key-never-sent\n');
 const preservedArgs=viewArgs.filter(value=>value!=='--upgrade-backend');preservedArgs[preservedArgs.indexOf('--workspace')+1]=existing;preservedArgs.push('--preserve-provider-settings');
 viewProcess=cp.spawn(exe,preservedArgs,{cwd:existing,env:{...env,XMIND_VIEW_TOKEN:token},windowsHide:true,stdio:['ignore','ignore','pipe']});viewProcess.stderr.on('data',bytes=>{if(viewError.length<4096)viewError+=bytes.toString('utf8').slice(0,4096-viewError.length);});
 const preservedReady=await waitReady();assert.equal(preservedReady.started_backend,false,'Preserving settings must reuse the existing backend');assert.equal(preservedReady.backend_process_id,seededProfile.process_id);
 const preservedProfiles=await fetch(preservedReady.origin+'/v1/provider/profiles',{headers:hostHeaders});assert.equal(preservedProfiles.status,200,await preservedProfiles.clone().text());assert.deepEqual(await preservedProfiles.json(),chosenState,'Explicit preservation must skip new file entries and retain the user-selected provider registry');passed=true;
 console.log(JSON.stringify({actualNativeView:true,trustedMcpAvailableInInitialExistingAndCreatedWorkspaces:true,hostImportsOnlyExactTrustedProviderPath:true,untrustedViewCannotImportProviderFiles:true,existingNonemptyProfileImportsNewYamlEntries:true,newWorkspaceImportsSharedYaml:true,userSelectedProviderSurvivesWorkspaceReattachment:true,explicitPreserveOptionSkipsNewYamlEntries:true,keysReturnedToBrowser:false,providerRequests:0,actualViewWorkspaceConfig:true,renderedBrowser:false}));
}finally{
 await browser?.close().catch(()=>{});
 if(viewProcess&&viewProcess.exitCode===null){viewProcess.kill();await new Promise(resolve=>viewProcess.once('close',resolve));}
 for(const [pid,birth]of [...pids])try{await stop(pid,birth);}catch(error){console.error('Owned profile cleanup: '+error.message);passed=false;process.exitCode=1;}
 assert.equal(path.dirname(await fs.realpath(base)),parent);assert.ok(path.basename(base).startsWith('xmind-local-view-provider-'));
 if(passed){await fs.rm(base,{recursive:true,force:true});await fs.rm(readyRoot,{recursive:true,force:true});}
 else console.error(JSON.stringify({retainedFixture:base,retainedReadyRoot:readyRoot,viewError}));
}
