'use strict';
// Synthetic editor access observations; real native view/SQLite acceptance is
// covered by Native/tests/local_view_contract.mjs, not this host fixture.
const test=require('node:test'),assert=require('node:assert/strict');
const {EventEmitter}=require('node:events');
const {WorkspaceBackend}=require('../workspace-backend');
const {BackendClient}=require('../client');
const folder=(name,fsPath)=>({name,uri:{scheme:'file',fsPath,toString:()=>`file:///${fsPath.replaceAll('\\','/')}`}});
function harness(options={}){
 const state=new Map(),secrets=new Map(),launches=[],files=new Map(),directories=[],picks=[];
 const settings={providerConfigPath:'D:\\Trusted\\providers.yaml',workspaceEdits:true,...options.settings};
 const defaults=require('../package.json').contributes.configuration.properties;
 const vscode={ExtensionMode:{Development:2},env:{remoteName:options.remote},workspace:{isTrusted:options.trusted!==false,workspaceFolders:options.folders||[folder('Example','D:\\Projects\\Example')],getConfiguration:()=>({inspect:key=>({globalValue:settings[key],defaultValue:defaults['agentflow.'+key]?.default,workspaceValue:'untrusted-project-value'})})},window:{showQuickPick:async items=>{picks.push(items);return items[options.pick??0];}}};
 const context={extensionMode:options.extensionMode??2,extensionUri:{fsPath:'D:\\CantorAI2026\\xMind\\extensions\\vscode'},workspaceState:{get:key=>state.get(key),update:async(key,value)=>state.set(key,value)},secrets:{get:async key=>secrets.get(key),store:async(key,value)=>secrets.set(key,value),delete:async key=>secrets.delete(key)},globalState:{get:()=>{throw Error('No editor owner registry');},update:()=>{throw Error('No editor owner registry');}}};
 let time=0,sequence=0;
 const runtime={nativeProgram:'C:\\Runtime\\xmind.exe',qualified:true,providerConfig:settings.providerConfigPath,privateStateRoot:options.storage||'C:\\Private'};
 const configCandidate='D:\\CantorAI2026\\xMind\\.config\\providers.yaml';if(options.configFile)files.set(configCandidate,'private fixture only');
 const requests=[];
 const deps={platform:'win32',arch:'x64',env:{PATH:'fixture-path',LOCALAPPDATA:'C:\\Local',USERPROFILE:'C:\\User',XMIND_AUTH_TOKEN:'stale-master',XMIND_API_KEY:'stale-provider',XMIND_UI_BOOTSTRAP_TOKEN:'stale-preview'},random:()=> 'c'.repeat(64),directoryNonce:()=> (++sequence).toString(16).padStart(32,'0'),now:()=>time,sleep:async ms=>{time+=ms;},fs:{realpath:async value=>options.alias?.(value)||value,mkdir:async value=>directories.push(value),mkdtemp:async prefix=>prefix+(++sequence),lstat:async file=>{if(!files.has(file)){const e=Error('pending');e.code='ENOENT';throw e;}return {isFile:()=>true,isSymbolicLink:()=>false,nlink:1,size:1000};},readFile:async file=>JSON.stringify(files.get(file))},spawn:(program,args,config)=>{
  const child=new EventEmitter();child.pid=2000+sequence;child.exitCode=null;child.kills=0;child.unref=()=>{};child.kill=()=>{child.kills++;child.exitCode=1;child.emit('exit',1);};
  const root=args[args.indexOf('--workspace')+1],ready=args[args.indexOf('--ready-file')+1];
  const info={origin:'http://127.0.0.1:'+(19000+sequence),process_id:child.pid,process_birth:'12345678',backend_process_id:3000,profile_directory:'C:\\Local\\xMind\\LocalProfiles\\'+root.replaceAll(/[\\:]/g,'_'),workspace:{configured:true,root,workspace_id:'windows-local-file-v1:1:'+root.replaceAll(/[^a-zA-Z0-9]/g,'_'),authority_id:'b'.repeat(32)}};
  options.ready?.(info);files.set(ready,info);launches.push({program,args,config,child,info});return child;
 },fetch:async(url,request)=>{requests.push({url,request});const info=launches.find(v=>url.startsWith(v.info.origin+'/'))?.info;if(options.fetch)return options.fetch(url,request,info);const route=new URL(url).pathname,routes=[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true}],empty={active:'',profiles:[],routes};if(route==='/v1/provider/profiles')return {ok:true,json:async()=>({revision:0,...empty})};if(route==='/v1/provider/configuration/import')return {ok:true,json:async()=>({revision:1,active:'',profiles:[{id:'openai',route_id:'openai.responses',provider:'openai',model:'',revision:1}],routes})};return {ok:true,json:async()=>info?.workspace};}};
 const resolver=async(_context,_host,config)=>({...runtime,providerConfig:config.providerConfigPath}),manager=new WorkspaceBackend(vscode,context,resolver,deps);
 return {manager,vscode,context,runtime,deps,launches,state,secrets,picks,resolver,directories,requests,configCandidate};
}
test('host launches native view without backend secrets and imports provider YAML through its authenticated API',async()=>{
 const h=harness(),owner=await h.manager.connect(),launch=h.launches[0];assert.equal(launch.program,h.runtime.nativeProgram);assert.equal(launch.args[0],'view');assert.equal(launch.args[launch.args.indexOf('--profile-root')+1],'C:\\User\\.xMind\\p');assert.equal(launch.config.stdio,'ignore');assert.equal(launch.config.cwd,owner.canonical);assert.equal(launch.config.env.XMIND_VIEW_TOKEN,'c'.repeat(64));for(const key of ['XMIND_AUTH_TOKEN','XMIND_API_KEY','XMIND_UI_BOOTSTRAP_TOKEN'])assert.equal(launch.config.env[key],undefined);for(const key of ['--db','--modules','--stdlib','--port','--config'])assert.ok(!launch.args.includes(key));const imported=h.requests.find(value=>new URL(value.url).pathname==='/v1/provider/configuration/import');assert.ok(imported);assert.equal(imported.request.headers.Authorization,'Bearer '+'c'.repeat(64));assert.deepEqual(JSON.parse(imported.request.body),{path:'D:\\Trusted\\providers.yaml',expected_revision:0});assert.deepEqual(h.manager.stateFields(),{workspace_id:owner.metadata.workspace_id,profile_directory:owner.profileDirectory});
});
test('development extension discovers only the repository config and never searches the opened workspace',async()=>{
 const h=harness({settings:{providerConfigPath:''},configFile:true});await h.manager.connect();const imported=h.requests.find(value=>new URL(value.url).pathname==='/v1/provider/configuration/import');assert.ok(imported);assert.equal(JSON.parse(imported.request.body).path,h.configCandidate);const h2=harness({settings:{providerConfigPath:''},extensionMode:1,configFile:true});await h2.manager.connect();assert.ok(!h2.requests.some(value=>new URL(value.url).pathname==='/v1/provider/configuration/import'));
});
test('packaged Windows profile remains short and canonical despite redirected LocalAppData',async()=>{
 const h=harness();h.deps.env.LOCALAPPDATA='C:\\Users\\shaw9\\AppData\\Local\\Packages\\OpenAI.Codex_2p2nqsd0c76g0\\LocalCache\\Local';const owner=await h.manager.connect(),root=h.launches[0].args[h.launches[0].args.indexOf('--profile-root')+1];assert.equal(root,'C:\\User\\.xMind\\p');assert.ok(root.length<=84);assert.equal(owner.canonical,'D:\\Projects\\Example');
});
test('a package-redirected profile parent falls back to the short canonical user root',async()=>{
 const physical='C:\\Users\\shaw9\\AppData\\Local\\Packages\\OpenAI.Codex_2p2nqsd0c76g0\\LocalCache\\Local\\xMind',h=harness({alias:value=>value==='C:\\Users\\shaw9\\.xMind'?physical:undefined});h.deps.env.USERPROFILE='C:\\Users\\shaw9';const owner=await h.manager.connect(),root=h.launches[0].args[h.launches[0].args.indexOf('--profile-root')+1];assert.equal(root,'C:\\Users\\shaw9\\p');assert.ok(root.length<=84);assert.equal(owner.canonical,'D:\\Projects\\Example');
});
test('reload replaces only the view; profile and native backend identity remain stable',async()=>{
 const h=harness(),first=await h.manager.connect();assert.equal((await h.manager.connect()).origin,first.origin);assert.equal(h.launches.length,1);h.manager.dispose();assert.equal(h.launches[0].child.kills,1);const reload=new WorkspaceBackend(h.vscode,h.context,h.resolver,h.deps),second=await reload.connect();assert.notEqual(second.origin,first.origin);assert.equal(second.backendPid,first.backendPid);assert.equal(second.profileDirectory,first.profileDirectory);assert.equal(second.metadata.workspace_id,first.metadata.workspace_id);reload.dispose();
});
test('read-only, trust, remote and pure-source boundaries apply before launch',async()=>{
 const h=harness({settings:{workspaceEdits:false}});await h.manager.connect();assert.ok(h.launches[0].args.includes('--read-only'));for(const options of [{trusted:false},{remote:'ssh-remote'},{folders:[]},{folders:[{name:'virtual',uri:{scheme:'virtual'}}]}]){const bad=harness(options);await assert.rejects(bad.manager.connect());assert.equal(bad.launches.length,0);}const bad=harness();bad.runtime.qualified=false;await assert.rejects(bad.manager.connect(),/bundled/);assert.equal(bad.launches.length,0);
});
test('multi-root explicitly uses the opened root and host cwd',async()=>{
 const h=harness({folders:[folder('First','D:\\First'),folder('Second','D:\\Second')],pick:1}),owner=await h.manager.connect();assert.equal(owner.canonical,'D:\\Second');assert.equal(owner.roots.length,2);assert.equal(h.picks.length,1);assert.equal(h.launches[0].config.cwd,'D:\\Second');
});
test('private rendezvous fallback and aliases cannot expose storage to workspace tools',async()=>{
 const h=harness({storage:'D:\\Projects\\Example\\private'});await h.manager.connect();assert.ok(h.launches[0].args[h.launches[0].args.indexOf('--ready-file')+1].startsWith('C:\\Local\\xMind\\ViewHosts'));const bad=harness({alias:value=>value==='C:\\Private'?'D:\\Projects\\Example\\alias':value});await assert.rejects(bad.manager.connect(),/resolves/);assert.equal(bad.launches.length,0);
});
test('wrong roots, process metadata and profile overlap fail and close only the view',async()=>{
 for(const mutate of [info=>info.workspace.root='D:\\Wrong',info=>info.process_id++,info=>info.profile_directory='D:\\Projects\\Example\\private',info=>info.workspace.authority_id='invalid']){const h=harness({ready:mutate});await assert.rejects(h.manager.connect());assert.equal(h.launches[0].child.kills,1);assert.equal(h.secrets.size,0);}
});
test('expanded folder set cannot expose a retained profile or rendezvous',async()=>{
 const h=harness(),first=await h.manager.connect();h.vscode.workspace.workspaceFolders.push(folder('Private','C:\\Private'));h.manager.invalidate();await assert.rejects(h.manager.connect(),/overlaps/);assert.equal(h.launches.length,1);assert.equal(h.launches[0].child.kills,0);h.vscode.workspace.workspaceFolders.pop();h.manager.invalidate();assert.equal((await h.manager.connect()).origin,first.origin);
});
test('workspace mutation during readiness closes the adapter without claiming backend termination',async()=>{
 let h;h=harness({ready:()=>{h.vscode.workspace.workspaceFolders=[folder('Other','D:\\Other')];h.manager.invalidate();}});await assert.rejects(h.manager.connect(),/changed/);assert.equal(h.launches[0].child.kills,1);assert.equal(h.secrets.size,0);
});
test('admission carries verified workspace authority and rejects late selection mutation',async()=>{
 const h=harness(),owner=await h.manager.connect(),writes=[];const client=new BackendClient(owner.origin,()=>h.context.secrets.get('xmind.auth:'+owner.origin),async(url,request)=>{writes.push(JSON.parse(request.body));return {ok:true,json:async()=>({id:'fixture-run'})};}).bindWorkspace(h.manager);await client.run('fixture-session','requested fixture task');assert.equal(writes[0].expected_workspace_id,owner.metadata.workspace_id);assert.equal(writes[0].expected_workspace_authority_id,owner.metadata.authority_id);const ticket=await h.manager.prepare();h.manager.invalidate();assert.throws(()=>h.manager.assert(ticket),/changed/);
});
test('authority drift rejects admission before a replacement run can be created',async()=>{
 const h=harness(),owner=await h.manager.connect();h.launches[0].info.workspace={...owner.metadata,authority_id:'d'.repeat(32)};await assert.rejects(h.manager.prepare(),/owner changed/);
});
test('readiness timeout does not retry spawning or create a JavaScript database',async()=>{
 const h=harness();h.deps.fs.lstat=async()=>{const e=Error('not published');e.code='ENOENT';throw e;};await assert.rejects(h.manager.connect(),/did not become ready/);assert.equal(h.launches.length,1);assert.equal(h.launches[0].child.kills,1);assert.ok(!h.directories.some(v=>v.endsWith('.sqlite')));
});
test('external attachment still verifies the actual opened workspace',async()=>{
 const h=harness({fetch:async()=>({ok:true,json:async()=>({configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:1:2',authority_id:'b'.repeat(32)})})});await h.manager.attach('http://127.0.0.1:19999','x'.repeat(64));assert.equal(h.launches.length,0);assert.equal(h.manager.stateFields(),undefined);
});


test('private native startup errors are accepted only for the spawned adapter PID',async()=>{
 for(const foreign of [false,true]){
  const h=harness(),spawn=h.manager.deps.spawn;let pid;
  h.manager.deps.spawn=(...args)=>{const child=spawn(...args);pid=child.pid;queueMicrotask(()=>{child.exitCode=2;child.emit('exit',2);});return child;};
  h.deps.fs.lstat=async file=>{if(!file.endsWith('error.json')){const e=Error('no ready record');e.code='ENOENT';throw e;}return {isFile:()=>true,isSymbolicLink:()=>false,nlink:1,size:500};};
  h.deps.fs.readFile=async()=>JSON.stringify({error_code:'native_view_startup_failed',process_id:foreign?pid+1:pid,detail:'Native view access could not be issued'});
  await assert.rejects(h.manager.connect(),foreign?/did not become ready/:/could not be issued/);assert.equal(h.launches.length,1);assert.equal(h.secrets.size,0);
 }
});


test('a folder switch during preparation connects the latest folder after stale startup cleanup',async()=>{
 const h=harness();let release,entered;const arrived=new Promise(resolve=>entered=resolve),paused=new Promise(resolve=>release=resolve);let calls=0;
 h.manager.resolveRuntime=async()=>{if(++calls===1){entered();await paused;}return h.runtime;};
 const initial=h.manager.connect(),oldResult=assert.rejects(initial,/changed/);await arrived;
 h.vscode.workspace.workspaceFolders=[folder('New root','D:\\Latest')];h.manager.invalidate();const current=h.manager.connect();release();await oldResult;
 const owner=await current;assert.equal(owner.canonical,'D:\\Latest');assert.equal(h.launches.length,1);assert.equal(h.launches[0].config.cwd,'D:\\Latest');assert.equal(h.manager.active.origin,owner.origin);
});
test('trust revocation during preparation prevents native startup without cancelling existing work',async()=>{
 const h=harness();let release,entered;const arrived=new Promise(resolve=>entered=resolve),paused=new Promise(resolve=>release=resolve);
 h.manager.resolveRuntime=async()=>{entered();await paused;return h.runtime;};const opening=h.manager.connect(),refusal=assert.rejects(opening,/changed/);await arrived;h.vscode.workspace.isTrusted=false;release();await refusal;assert.equal(h.launches.length,0);assert.equal(h.secrets.size,0);
});

// External attachment is also an asynchronous native workspace observation.
test('attachment refuses trust or remote-host changes during workspace verification',async()=>{
 for(const change of [h=>{h.vscode.workspace.isTrusted=false;},h=>{h.vscode.env.remoteName='ssh-remote';}]){
  const h=harness();h.manager.deps.fetch=async()=>{change(h);return {ok:true,json:async()=>({configured:true,root:'D:\\Projects\\Example',workspace_id:'windows-local-file-v1:1:fixture',authority_id:'b'.repeat(32)})};};
  await assert.rejects(h.manager.attach('http://127.0.0.1:19100','c'.repeat(64)),/Workspace changed while attaching/);
  assert.equal(h.manager.active,undefined);assert.equal(h.launches.length,0);
 }
});

test('native creates the rendezvous directory ownership; the host only chooses a fresh name',async()=>{
 const h=harness();h.deps.fs.mkdtemp=async()=>{throw Error('Editor must not create native rendezvous ownership');};
 const owner=await h.manager.connect();assert.equal(h.launches.length,1);
 assert.deepEqual(h.directories,['C:\\Private','C:\\User\\.xMind']);assert.ok(owner.privateDirectory.startsWith('C:\\Private\\native-view-'));
});

test('a chosen rendezvous collision is preserved and cannot start a native adapter',async()=>{
 const h=harness();h.manager.deps.fs.lstat=async()=>({isDirectory:()=>true});
 await assert.rejects(h.manager.connect(),/already exists/);assert.equal(h.launches.length,0);assert.equal(h.secrets.size,0);
});
test('a published rendezvous directory alias is refused before authentication',async()=>{
 const h=harness({alias:value=>/\\native-view-[a-f0-9]{32}$/.test(value)?'C:\\Different':value});
 await assert.rejects(h.manager.connect(),/different directory/);assert.equal(h.launches.length,1);assert.equal(h.launches[0].child.kills,1);assert.equal(h.secrets.size,0);
});
