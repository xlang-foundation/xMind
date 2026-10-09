'use strict';
// Synthetic extension-host filesystem/process/HTTP observations. No Native
// process, provider YAML, credential or real network is accessed by this suite.
const test=require('node:test');
const assert=require('node:assert/strict');
const {EventEmitter}=require('node:events');
const {WorkspaceBackend,canonicalPath}=require('../workspace-backend');
const {BackendClient}=require('../client');
const folder=(name,fsPath)=>({name,uri:{scheme:'file',authority:'',fsPath,toString:()=>`file:///${fsPath.replaceAll('\\','/')}`}});
function harness(options={}){
  const state=new Map(),globalState=new Map(),secrets=new Map(),launches=[],directories=[],fetches=[],picks=[];
  const settings={providerConfigPath:'D:\\Trusted\\providers.yaml',workspaceEdits:false,...options.settings};
  const defaults=require('../package.json').contributes.configuration.properties;
  const vscode={env:{remoteName:options.remote},workspace:{isTrusted:options.trusted!==false,workspaceFolders:options.folders||[folder('TestProj','D:\\CantorAI2026\\TestProj')],getConfiguration:()=>({inspect:key=>({globalValue:settings[key],defaultValue:defaults['agentflow.'+key]?.default,workspaceValue:'untrusted-project-value'})})},window:{showQuickPick:async items=>{picks.push(items);return items[options.pick??0];}}};
  const store=map=>({get:key=>map.get(key),update:async(key,value)=>map.set(key,value)});
  const context={workspaceState:store(state),globalState:store(globalState),secrets:{get:async key=>secrets.get(key),store:async(key,value)=>secrets.set(key,value)}};
  let time=0,nextPort=19000;
  const runtime={nativeProgram:'C:\\Runtime\\xmind_server.exe',modules:'C:\\Runtime\\modules',stdlib:'C:\\Runtime\\stdlib',providerConfig:settings.providerConfigPath,privateStateRoot:options.storage||'C:\\Private',manifestSha256:'a'.repeat(64)};
  const deps={platform:'win32',arch:'x64',env:{PATH:'synthetic-host-path',LOCALAPPDATA:'C:\\Local',XMIND_UI_BOOTSTRAP_TOKEN:'synthetic-stale-bootstrap'},uuid:()=>`owned-${nextPort}`,random:()=>`synthetic-auth-${nextPort}`.padEnd(64,'x'),port:async()=>++nextPort,now:()=>time,sleep:async ms=>{time+=ms;},fs:{realpath:async value=>options.alias?.(value)||value,mkdir:async value=>directories.push(value)},spawn:(program,args,config)=>{
    const child=new EventEmitter();child.pid=nextPort;child.exitCode=null;child.kills=0;child.unref=()=>{};child.kill=()=>{child.kills++;child.exitCode=0;child.emit('exit',0);};
    launches.push({program,args,config,child});return child;
  },fetch:async(url,request)=>{
    fetches.push({url,request});if(options.fetch)return options.fetch(url,request,launches);
    const port=Number(new URL(url).port),launch=launches.find(value=>value.args[value.args.indexOf('--port')+1]===String(port));
    return {ok:true,json:async()=>({configured:true,root:launch?.args[launch.args.indexOf('--workspace')+1]||'D:\\CantorAI2026\\TestProj',workspace_id:'windows-local-file-v1:1:'+port,authority_id:'b'.repeat(32)})};
  }};
  const resolver=async(ctx,host,config)=>{assert.equal(config.providerConfigPath,settings.providerConfigPath??defaults['agentflow.providerConfigPath']?.default);assert.equal(config.runtimeDirectory,settings.runtimeDirectory??defaults['agentflow.runtimeDirectory']?.default);return runtime;};
  const manager=new WorkspaceBackend(vscode,context,resolver,deps);
  return {manager,vscode,context,deps,runtime,launches,fetches,picks,directories,secrets,state,globalState,settings,resolver};
}
test('managed coding default passes approval-based file proposals and preserves explicit read-only and trust boundaries',async()=>{
 const h=harness({settings:{workspaceEdits:undefined}});await h.manager.connect();const args=h.launches[0].args;assert.equal(args.filter(v=>v==='--workspace-edits').length,1);assert.equal(args[args.indexOf('--workspace-edits')+1],'approved');
 const readonly=harness({settings:{workspaceEdits:false}});await readonly.manager.connect();assert.ok(!readonly.launches[0].args.includes('--workspace-edits'));const untrusted=harness({trusted:false,settings:{workspaceEdits:undefined}});await assert.rejects(untrusted.manager.connect(),/Trust/);assert.equal(untrusted.launches.length,0);
});

test('single opened folder starts isolated Native with host cwd/environment and one explicit config path',async()=>{
  const h=harness();const active=await h.manager.connect(),launch=h.launches[0];
  assert.equal(h.picks.length,0);assert.equal(active.metadata.root,'D:\\CantorAI2026\\TestProj');
  assert.equal(launch.config.cwd,active.metadata.root);assert.equal(launch.config.env.PATH,'synthetic-host-path');
  assert.equal(launch.config.env.XMIND_UI_BOOTSTRAP_TOKEN,undefined);assert.ok(launch.config.env.XMIND_AUTH_TOKEN);
  assert.equal(launch.config.detached,true);assert.equal(launch.config.windowsHide,true);assert.equal(launch.config.stdio,'ignore');
  assert.equal(launch.args.filter(value=>value==='--provider-config').length,1);assert.ok(launch.args.includes('D:\\Trusted\\providers.yaml'));
  assert.ok(!launch.args.includes(launch.config.env.XMIND_AUTH_TOKEN));assert.ok(!launch.args.includes('--workspace-edits'));
  const db=launch.args[launch.args.indexOf('--db')+1];assert.ok(db.startsWith('C:\\Private\\workspace-backends\\'));
  assert.equal(h.fetches[0].request.redirect,'error');assert.equal(h.globalState.get('xmind.nativeWorkspaceOwners').length,1);
  const status=await h.manager.status();assert.deepEqual(Object.keys(status).sort(),['authority_id','configured','origin','root','workspace_id']);assert.equal(status.root,active.metadata.root);assert.ok(!JSON.stringify(status).includes(launch.config.env.XMIND_AUTH_TOKEN));
});
test('folder changes and host disposal preserve owners, and returning/reloading reconnects authenticated generation',async()=>{
  const h=harness();const first=await h.manager.connect();h.vscode.workspace.workspaceFolders=[folder('Other','D:\\Other')];h.manager.invalidate();
  const second=await h.manager.connect();assert.notEqual(first.origin,second.origin);assert.notEqual(h.launches[0].args[1],h.launches[1].args[1]);
  h.vscode.workspace.workspaceFolders=[folder('TestProj','D:\\CantorAI2026\\TestProj')];h.manager.invalidate();assert.equal((await h.manager.connect()).origin,first.origin);
  h.manager.dispose();assert.equal(h.launches.reduce((sum,value)=>sum+value.child.kills,0),0);
  const reloaded=new WorkspaceBackend(h.vscode,h.context,h.resolver,h.deps);assert.equal((await reloaded.connect()).origin,first.origin);assert.equal(h.launches.length,2);
});
test('wrong native root fails startup and cleans only that unpublished child',async()=>{
  const h=harness({fetch:async()=>({ok:true,json:async()=>({configured:true,root:'D:\\Wrong',workspace_id:'windows-local-file-v1:1:2',authority_id:'b'.repeat(32)})})});
  await assert.rejects(h.manager.connect(),/differs/);assert.equal(h.launches[0].child.kills,1);assert.equal(h.secrets.size,0);
});
test('case-sensitive canonical siblings keep independent backend authority',async()=>{
 const h=harness({folders:[folder('Upper','D:\\Owned\\Root')]});const first=await h.manager.connect();
 h.vscode.workspace.workspaceFolders=[folder('Lower','D:\\Owned\\root')];h.manager.invalidate();const second=await h.manager.connect();
 assert.notEqual(first.scope,second.scope);assert.notEqual(first.origin,second.origin);assert.equal(second.metadata.root,'D:\\Owned\\root');assert.equal(h.launches.length,2);
});
test('no trusted provider path starts unconfigured and never imports a project file',async()=>{
 const h=harness({settings:{providerConfigPath:undefined}});await h.manager.connect();assert.ok(!h.launches[0].args.includes('--provider-config'));assert.ok(!h.launches[0].args.some(value=>value===undefined));
});
test('machine launch settings stay pending on the same authenticated owner without replacing saved state',async()=>{
 const h=harness();const first=await h.manager.connect(),records=JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners'));h.settings.workspaceEdits=true;h.settings.providerConfigPath='D:\\Changed\\providers.yaml';h.manager.invalidate();const second=await h.manager.connect();assert.equal(first.origin,second.origin);assert.equal(second.backendChangePending,true);assert.equal(second.privateDirectory,first.privateDirectory);assert.equal(h.launches.length,1);assert.ok(!h.launches[0].args.includes('--workspace-edits'));assert.equal(h.launches[0].child.kills,0);assert.equal(JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),records);h.settings.workspaceEdits=false;h.settings.providerConfigPath='D:\\Trusted\\providers.yaml';h.manager.invalidate();assert.equal((await h.manager.connect()).backendChangePending,false);
});
test('ready owner remains alive if folder changes during authenticated readiness',async()=>{
  let h;h=harness({fetch:async()=>{h.vscode.workspace.workspaceFolders=[folder('Other','D:\\Other')];h.manager.invalidate();return {ok:true,json:async()=>({configured:true,root:'D:\\CantorAI2026\\TestProj',workspace_id:'windows-local-file-v1:1:2',authority_id:'b'.repeat(32)})};}});
  await assert.rejects(h.manager.connect(),/changed/);assert.equal(h.launches[0].child.kills,0);assert.equal(h.globalState.get('xmind.nativeWorkspaceOwners').length,1);
});
test('multi-root explicitly selects one active root; remote, untrusted and non-file fail before launch',async()=>{
  const h=harness({folders:[folder('First','D:\\First'),folder('Second','D:\\Second')],pick:1});const active=await h.manager.connect();
  assert.equal(h.picks.length,1);assert.equal(active.roots.length,2);assert.equal(active.canonical,'D:\\Second');
  for(const options of [{remote:'ssh-remote'},{trusted:false},{folders:[{name:'virtual',uri:{scheme:'vscode-remote'}}]}]){const bad=harness(options);await assert.rejects(bad.manager.connect());assert.equal(bad.launches.length,0);}
});
test('private storage inside a folder uses external fallback and rejects a directory alias escaping private root',async()=>{
  const h=harness({storage:'D:\\CantorAI2026\\TestProj\\storage'});await h.manager.connect();assert.ok(h.launches[0].args[1].startsWith('C:\\Local\\xMind\\NativeWorkspaces\\'));
  const bad=harness({alias:value=>value.includes('workspace-backends')?'D:\\CantorAI2026\\TestProj\\aliased':value});await assert.rejects(bad.manager.connect(),/escaped/);assert.equal(bad.launches.length,0);
  assert.equal(canonicalPath('\\\\?\\UNC\\server\\share\\folder'),'\\\\server\\share\\folder');
});
test('broader folder set cannot expose private storage or silently replace the retained profile database',async()=>{
 const h=harness({storage:'D:\\Private'});const first=await h.manager.connect();
 h.vscode.workspace.workspaceFolders.push(folder('Broad root','D:\\'));h.manager.invalidate();await assert.rejects(h.manager.connect(),/no replacement database/);
 assert.equal(h.manager.active,undefined);assert.equal(h.launches.length,1);assert.equal(h.launches[0].child.kills,0);h.vscode.workspace.workspaceFolders.pop();h.manager.invalidate();assert.equal((await h.manager.connect()).origin,first.origin);
});

test('package changes and host reload retain the original authenticated database and native admission identity',async()=>{
 const h=harness();const first=await h.manager.connect(),directoryCount=h.directories.length,records=JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),secrets=[...h.secrets];h.runtime.manifestSha256='c'.repeat(64);h.manager.invalidate();
 const changed=await h.manager.connect();assert.equal(changed.origin,first.origin);assert.equal(changed.privateDirectory,first.privateDirectory);assert.equal(changed.runtimeManifest,first.runtimeManifest);assert.equal(changed.backendChangePending,true);
 h.manager.dispose();const reloaded=new WorkspaceBackend(h.vscode,h.context,h.resolver,h.deps),restored=await reloaded.connect();assert.equal(restored.origin,first.origin);assert.equal(restored.backendChangePending,true);assert.equal(h.launches.length,1);assert.equal(h.directories.length,directoryCount);assert.equal(h.launches[0].child.kills,0);assert.deepEqual([...h.secrets],secrets);assert.equal(JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),records);
 const writes=[],client=new BackendClient(restored.origin,()=>h.secrets.get('xmind.auth:'+restored.origin),async(url,options)=>{writes.push({url,body:JSON.parse(options.body)});return {ok:true,json:async()=>({id:'synthetic-admitted'})};}).bindWorkspace(reloaded);await client.run('saved-conversation','synthetic requested task');assert.equal(writes.length,1);assert.equal(new URL(writes[0].url).origin,first.origin);assert.equal(writes[0].body.expected_workspace_authority_id,first.metadata.authority_id);assert.equal(writes[0].body.expected_workspace_id,first.metadata.workspace_id);
});

test('unavailable credentials, HTTP or changed authority never create a competing owner or empty database',async()=>{
 for(const failure of ['credentials','unreachable','authority']){
  const h=harness();const first=await h.manager.connect(),records=JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),directories=h.directories.length;h.manager.dispose();h.runtime.manifestSha256='c'.repeat(64);
  if(failure==='credentials')h.secrets.clear();else h.deps.fetch=async()=>failure==='unreachable'?{ok:false}:{ok:true,json:async()=>({...first.metadata,authority_id:'d'.repeat(32)})};
  const reloaded=new WorkspaceBackend(h.vscode,h.context,h.resolver,h.deps);await assert.rejects(reloaded.connect(),/no replacement database/);assert.equal(reloaded.active,undefined);assert.equal(h.launches.length,1);assert.equal(h.directories.length,directories);assert.equal(h.launches[0].child.kills,0);assert.equal(JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),records);
 }
});

test('latest persisted generation failures cannot fall back to older owners or unsafe storage',async()=>{
 for(const failure of ['latest-origin','storage','metadata','registry','falsy-registry']){
  const h=harness();const first=await h.manager.connect(),records=h.globalState.get('xmind.nativeWorkspaceOwners');h.manager.dispose();
  if(failure==='registry')h.globalState.set('xmind.nativeWorkspaceOwners',{invalid:records});else if(failure==='falsy-registry')h.globalState.set('xmind.nativeWorkspaceOwners',false);else{const latest={...records[0]};if(failure==='latest-origin')latest.origin='https://untrusted.invalid';if(failure==='storage')latest.privateDirectory='D:\\CantorAI2026\\TestProj\\private';if(failure==='metadata')latest.metadata={...latest.metadata,authority_id:'invalid'};h.globalState.set('xmind.nativeWorkspaceOwners',[...records,latest]);}
  const prior=JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),directories=h.directories.length,reloaded=new WorkspaceBackend(h.vscode,h.context,h.resolver,h.deps);await assert.rejects(reloaded.connect());assert.equal(reloaded.active,undefined);assert.equal(h.launches.length,1);assert.equal(h.directories.length,directories);assert.equal(h.launches[0].child.kills,0);assert.equal(JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),prior);assert.equal(first.metadata.configured,true);
 }
});

test('owner registry capacity never evicts saved profiles or starts an unpublished replacement',async()=>{
 const h=harness();const first=await h.manager.connect(),record=h.globalState.get('xmind.nativeWorkspaceOwners')[0];const retained=[record,...Array.from({length:127},(_,i)=>({...record,scope:'synthetic-distinct-scope-'+i,origin:'http://127.0.0.1:'+(20000+i)}))];h.globalState.set('xmind.nativeWorkspaceOwners',retained);const previous=JSON.stringify(retained),directories=h.directories.length;
 h.vscode.workspace.workspaceFolders=[folder('New root','D:\\NewRoot')];h.manager.invalidate();await assert.rejects(h.manager.connect(),/owner limit/);assert.equal(h.launches.length,1);assert.equal(h.directories.length,directories);assert.equal(JSON.stringify(h.globalState.get('xmind.nativeWorkspaceOwners')),previous);assert.equal(h.launches[0].child.kills,0);
 h.vscode.workspace.workspaceFolders=[folder('TestProj','D:\\CantorAI2026\\TestProj')];h.manager.invalidate();assert.equal((await h.manager.connect()).origin,first.origin);assert.equal(h.launches.length,1);
});
test('malformed public authority is never accepted as authenticated readiness',async()=>{
 const h=harness({fetch:async()=>({ok:true,json:async()=>({configured:true,root:'D:\\CantorAI2026\\TestProj',workspace_id:'windows-local-file-v1:1:2',authority_id:'arbitrary-text'})})});await assert.rejects(h.manager.connect(),/ready/);assert.equal(h.launches[0].child.kills,1);
});
test('each POST fresh-checks owner; run and graph get exact native pair while unrelated bodies stay unchanged',async()=>{
  const h=harness();const active=await h.manager.connect(),writes=[];
  const client=new BackendClient(active.origin,()=>h.secrets.get('xmind.auth:'+active.origin),async(url,options)=>{writes.push({url,body:JSON.parse(options.body)});return {ok:true,json:async()=>({id:'synthetic'})};}).bindWorkspace(h.manager);
  await client.run('session','prompt');await client.graphRun('session','graph',1,'prompt');await client.createSession('title');
  for(const write of writes.slice(0,2)){assert.equal(write.body.expected_workspace_id,active.metadata.workspace_id);assert.equal(write.body.expected_workspace_authority_id,active.metadata.authority_id);}
  assert.deepEqual(writes[2].body,{title:'title'});assert.equal(h.fetches.length,4);
});
test('folder change during token await blocks dispatch; changed native generation blocks without killing owner',async()=>{
  const h=harness();const active=await h.manager.connect();let release;const waiting=new Promise(resolve=>{release=resolve;});let writes=0;
  const client=new BackendClient(active.origin,()=>waiting,async()=>{writes++;return {ok:true,json:async()=>({})};}).bindWorkspace(h.manager);
  const pending=client.run('session','prompt');await new Promise(resolve=>setImmediate(resolve));h.manager.invalidate();release('synthetic-token'.padEnd(32,'x'));
  await assert.rejects(pending,/changed/);assert.equal(writes,0);
  await h.manager.connect();h.manager.deps.fetch=async()=>({ok:true,json:async()=>({...active.metadata,authority_id:'c'.repeat(32)})});
  await assert.rejects(client.run('session','prompt'),/owner changed/);assert.equal(writes,0);assert.equal(h.launches[0].child.kills,0);
});
test('revoked workspace trust prevents every mutation while preserving the backend owner',async()=>{
 const h=harness();const active=await h.manager.connect();h.vscode.workspace.isTrusted=false;await assert.rejects(h.manager.prepare(),/changed/);assert.equal(h.launches[0].child.kills,0);assert.equal(active.metadata.configured,true);
});
