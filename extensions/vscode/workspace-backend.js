'use strict';
// Thin extension-host ownership only. Execution, permissions and persistence
// remain in Native; provider YAML contents are never read here.
const fs=require('node:fs/promises');
const path=require('node:path');
const crypto=require('node:crypto');
const net=require('node:net');
const {spawn,execFile}=require('node:child_process');
const {promisify}=require('node:util');
const {retainNativeRuntime}=require('./native-runtime');

function canonicalPath(value){
  if(typeof value!=='string'||!value||value.length>32760||/[\x00-\x1f]/.test(value))throw new Error('Invalid local workspace path.');
  const ordinary=value.startsWith('\\\\?\\UNC\\')?'\\\\'+value.slice(8):value.startsWith('\\\\?\\')?value.slice(4):value;
  if(!/^[A-Za-z]:[\\/]/.test(ordinary)&&!/^\\\\[^\\/]+\\[^\\/]+(?:\\|$)/.test(ordinary))throw new Error('Local xMind requires an absolute Windows workspace.');
  return path.win32.normalize(ordinary);
}
// Canonical authority comparisons retain casing: NTFS can have two distinct
// case-sensitive siblings. Storage exclusion is deliberately conservative.
const keyPath=value=>canonicalPath(value).replace(/[\\]+$/,'');
const contained=(parent,child)=>{const root=keyPath(parent).toLowerCase(),candidate=keyPath(child).toLowerCase();return root===candidate||candidate.startsWith(root+'\\');};
function workspaceMetadata(value){
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==4||
    !['configured','root','workspace_id','authority_id'].every(key=>Object.hasOwn(value,key))||typeof value.configured!=='boolean')throw new Error('Update the native backend to verify its workspace.');
  if(!value.configured){if(value.root!==null||value.workspace_id!==null||value.authority_id!==null)throw new Error('Invalid backend workspace metadata.');return Object.freeze({...value});}
  canonicalPath(value.root);
  if(typeof value.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(value.workspace_id)||typeof value.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(value.authority_id))throw new Error('Invalid backend workspace identity.');
  return Object.freeze({...value});
}
function machineSetting(vscode,name){
  const inspected=vscode.workspace.getConfiguration('agentflow').inspect?.(name);
  return inspected?.globalValue??inspected?.defaultValue;
}
function loopbackPort(){return new Promise((resolve,reject)=>{
  const server=net.createServer();server.once('error',reject);server.listen(0,'127.0.0.1',()=>{
    const port=server.address().port;server.close(error=>error?reject(error):resolve(port));
  });
});}
function nativeOwnerState(value){
  const hex=v=>typeof v==='string'&&/^[a-f0-9]{32}$/.test(v);
  if(!value||!hex(value.generation)||!Number.isSafeInteger(value.revision)||value.revision<1||typeof value.quiesced!=='boolean'||typeof value.retirement_requested!=='boolean'||typeof value.replacement_prepared!=='boolean'||typeof value.retirement_supported!=='boolean'||!Number.isSafeInteger(value.process_id)||value.process_id<1||value.process_id>0xffffffff||typeof value.process_birth!=='string'||!/^[1-9][0-9]{0,19}$/.test(value.process_birth)||!((value.quiesced&&hex(value.receipt_id))||(!value.quiesced&&value.receipt_id==='')))throw new Error('Invalid native backend owner observation.');
  return value;
}
const sameReceipt=(left,right)=>left&&right&&left.generation===right.generation&&left.revision===right.revision&&left.receipt_id===right.receipt_id;
async function observeNativeExit(runtime,state){
  const {stdout}=await promisify(execFile)(path.win32.join(runtime.runtimeRoot,'xmind_admin.exe'),['observe-owner-exit',String(state.process_id),state.process_birth,'10000'],{windowsHide:true,timeout:15000,maxBuffer:4096});
  const result=JSON.parse(stdout);
  if(result.process_id!==state.process_id||result.process_birth!==state.process_birth||typeof result.exited!=='boolean'||typeof result.identity_matches!=='boolean')throw new Error('Invalid native process-exit observation.');
  return result.exited;
}
async function callNativeAdmin(runtime,args,token,environment){
  const env={...environment,XMIND_AUTH_TOKEN:token};for(const key of Object.keys(env))if(key.startsWith('XMIND_UI_')||key==='XMIND_API_KEY')delete env[key];
  const {stdout}=await promisify(execFile)(path.win32.join(runtime.runtimeRoot,'xmind_admin.exe'),args,{env,windowsHide:true,timeout:30000,maxBuffer:16384});return JSON.parse(stdout);
}
function legacyProcess(value){
  if(!value||!Number.isSafeInteger(value.process_id)||value.process_id<1||value.process_id>0xffffffff||typeof value.process_birth!=='string'||!/^[1-9][0-9]{0,19}$/.test(value.process_birth)||typeof value.server_sha256!=='string'||!/^[a-f0-9]{64}$/.test(value.server_sha256))throw new Error('Invalid native legacy process observation.');return value;
}

class WorkspaceBackend {
  constructor(vscode,context,resolveRuntime,dependencies={}){
    this.vscode=vscode;this.context=context;this.resolveRuntime=resolveRuntime;
    this.deps={fs,spawn,retainRuntime:retainNativeRuntime,observeExit:observeNativeExit,admin:callNativeAdmin,fetch:(...args)=>fetch(...args),port:loopbackPort,random:()=>crypto.randomBytes(32).toString('hex'),uuid:()=>crypto.randomUUID(),sleep:ms=>new Promise(resolve=>setTimeout(resolve,ms)),now:()=>Date.now(),platform:process.platform,arch:process.arch,env:process.env,...dependencies};
    this.deps.confirmLegacyStop=dependencies.confirmLegacyStop??(async owner=>await vscode.window.showWarningMessage(`Stop the legacy xMind backend and migrate the saved profile for ${owner.canonical}? Native checks require idle execution. File changes will still require approval.`,{modal:true},'Stop and migrate')==='Stop and migrate');
    this.epoch=0;this.owners=new Map();this.active=undefined;this.disposed=false;this.connecting=undefined;
  }
  signature(){return JSON.stringify((this.vscode.workspace.workspaceFolders||[]).map(folder=>folder.uri.toString()));}
  invalidate(){this.epoch++;this.active=undefined;}
  dispose(){this.disposed=true;this.invalidate();/* Native owners deliberately outlive this host/view. */}
  async selectedFolder(forcePick=false){
    if(!this.vscode.workspace.isTrusted)throw new Error('Trust the workspace before starting its local xMind backend.');
    if(this.deps.platform!=='win32'||this.deps.arch!=='x64'||this.vscode.env?.remoteName)throw new Error('Managed local xMind currently requires a local Windows x64 extension host.');
    const folders=this.vscode.workspace.workspaceFolders||[];
    if(!folders.length)throw new Error('Open a folder in VS Code before using local xMind.');
    if(folders.length>32||folders.some(folder=>folder.uri.scheme!=='file'))throw new Error('Managed local xMind requires file workspace folders on this extension host.');
    let selected=folders.length===1?folders[0]:undefined;
    const saved=this.context.workspaceState.get('xmind.activeWorkspaceRoot');
    if(!selected&&!forcePick)selected=folders.find(folder=>folder.uri.toString()===saved);
    if(!selected){
      const item=await this.vscode.window.showQuickPick(folders.map(folder=>({label:folder.name,description:folder.uri.fsPath,folder})),{title:'xMind: Choose active workspace root',placeHolder:'Native execution currently uses one root from this workspace folder set.'});
      selected=item?.folder;
      if(!selected)throw new Error('Choose an active workspace root to use local xMind.');
    }
    if(!folders.includes(selected))throw new Error('The selected workspace folder changed.');
    await this.context.workspaceState.update('xmind.activeWorkspaceRoot',selected.uri.toString());
    return {folder:selected,roots:folders.map(folder=>({name:folder.name,uri:folder.uri.toString(),fsPath:folder.uri.fsPath})),signature:this.signature()};
  }
  async connect(forcePick=false){
    if(this.disposed)throw new Error('Workspace connection closed.');
    if(this.connecting)return this.connecting;
    this.connecting=this.open(forcePick).finally(()=>{this.connecting=undefined;});return this.connecting;
  }
  async open(forcePick){
    const epoch=this.epoch,selection=await this.selectedFolder(forcePick);
    const current=()=>!this.disposed&&this.epoch===epoch&&selection.signature===this.signature();
    const canonical=canonicalPath(await this.deps.fs.realpath(selection.folder.uri.fsPath));
    const canonicalRoots=await Promise.all(selection.roots.map(async root=>canonicalPath(await this.deps.fs.realpath(root.fsPath))));
    const scope=crypto.createHash('sha256').update(keyPath(canonical)).digest('hex');
    const config={runtimeDirectory:machineSetting(this.vscode,'runtimeDirectory'),stdlibSource:machineSetting(this.vscode,'stdlibSource'),providerConfigPath:machineSetting(this.vscode,'providerConfigPath')};
    const launchConfiguration=JSON.stringify({...config,workspaceEdits:machineSetting(this.vscode,'workspaceEdits')===true});
    let runtime=await this.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},config);
    if(!current())throw new Error('Workspace changed while preparing its backend.');
    const retained=this.owners.get(scope);
    // A package/settings change cannot implicitly replace a workspace's profile
    // with an empty database. Retain the authenticated native owner until Native
    // supplies an explicit state-preserving generation handoff.
    const reconnect=async prior=>{
      if(!/^http:\/\/127\.0\.0\.1:[0-9]{1,5}$/.test(prior.origin)||keyPath(prior.canonical)!==keyPath(canonical))throw new Error('Invalid retained workspace owner; existing storage was preserved.');
      if(!await this.ownerStorageSafe(prior,canonicalRoots))throw new Error('Existing backend storage is unavailable or inside a workspace folder. Narrow the opened folder set before reconnecting; no replacement database was created.');
      if(prior.upgrade)return this.completeUpgrade(prior,selection,current);
      const owner={...prior,metadata:workspaceMetadata(prior.metadata)};
      try{await this.observe(owner);}catch{throw new Error('Cannot verify the existing workspace backend. Restore its connection before continuing; no replacement database was created.');}
      if(!current())throw new Error('Workspace changed while reconnecting.');
      this.owners.set(scope,owner);
      this.active={...owner,epoch,signature:selection.signature,roots:selection.roots,backendChangePending:owner.runtimeManifest!==runtime.manifestSha256||owner.launchConfiguration!==launchConfiguration};
      return this.active;
    };
    if(retained)return reconnect(retained);
    // Reconnect only to the latest generation this extension authenticated.
    // An unavailable owner is an unknown outcome, never proof of safe migration.
    const stored=this.context.globalState?.get('xmind.nativeWorkspaceOwners');
    const saved=stored===undefined?[]:stored;
    if(!Array.isArray(saved))throw new Error('Saved workspace ownership is invalid; existing storage was preserved.');
    if(Array.isArray(saved)){
      const prior=saved.findLast(value=>value?.scope===scope);
      if(prior)return reconnect(prior);
    }
    if(saved.length>=128)throw new Error('Local workspace owner limit reached. Reconnect an existing workspace; no existing profile was removed.');
    let privateRoot=canonicalPath(runtime.privateStateRoot);
    if(canonicalRoots.some(root=>contained(root,privateRoot))){
      const fallback=this.deps.env.LOCALAPPDATA;
      if(!fallback)throw new Error('Configure private xMind storage outside the workspace folders.');
      privateRoot=path.win32.join(canonicalPath(fallback),'xMind','NativeWorkspaces');
    }
    if(canonicalRoots.some(root=>contained(root,privateRoot)))throw new Error('Private backend storage must be outside the workspace folders.');
    await this.deps.fs.mkdir(privateRoot,{recursive:true});privateRoot=canonicalPath(await this.deps.fs.realpath(privateRoot));
    if(canonicalRoots.some(root=>contained(root,privateRoot)))throw new Error('Private backend storage resolves inside a workspace folder.');
    const generationParent=path.win32.join(privateRoot,'runtime-generations');
    if(canonicalRoots.some(root=>contained(root,generationParent)||contained(generationParent,root)))throw new Error('Native generation storage overlaps a workspace folder.');
    runtime=await this.deps.retainRuntime(runtime,privateRoot);
    if(canonicalRoots.some(root=>contained(root,runtime.runtimeRoot)))throw new Error('Native generation storage is inside a workspace folder.');
    let directory=path.win32.join(privateRoot,'workspace-backends',scope,this.deps.uuid());
    await this.deps.fs.mkdir(directory,{recursive:true});
    directory=canonicalPath(await this.deps.fs.realpath(directory));
    if(!contained(privateRoot,directory)||canonicalRoots.some(root=>contained(root,directory)))throw new Error('Private backend directory escaped its verified storage root.');
    const port=await this.deps.port(),origin=`http://127.0.0.1:${port}`,token=this.deps.random();
    const args=['--db',path.win32.join(directory,'state.sqlite'),'--modules',runtime.modules,'--stdlib',runtime.stdlib,'--port',String(port),'--workspace',canonical,'--runtime-manifest-sha256',runtime.manifestSha256];
    if(runtime.providerConfig!==undefined)args.push('--provider-config',runtime.providerConfig);
    if(machineSetting(this.vscode,'workspaceEdits')===true)args.push('--workspace-edits','approved');
    if(!current())throw new Error('Workspace changed before starting its backend.');
    const childEnv={...this.deps.env,XMIND_AUTH_TOKEN:token};
    for(const key of Object.keys(childEnv))if(key.startsWith('XMIND_UI_'))delete childEnv[key];
    const child=this.deps.spawn(runtime.nativeProgram,args,{cwd:canonical,env:childEnv,detached:true,windowsHide:true,stdio:'ignore'});
    let exited=false,spawnError=false;child.once('exit',()=>{exited=true;});child.once('error',()=>{spawnError=true;});child.unref?.();
    const owner={origin,canonical,scope,child,pid:child.pid,metadata:undefined,runtimeManifest:runtime.manifestSha256,runtimeRoot:runtime.runtimeRoot,launchConfiguration,privateDirectory:directory};
    const alive=()=>!exited&&!spawnError&&Number.isSafeInteger(owner.pid)&&owner.pid>0&&child.exitCode==null;
    try{
      const deadline=this.deps.now()+15000;let observed;
      while(this.deps.now()<deadline&&alive()){
        try{observed=await this.readWorkspace(origin,token);break;}catch{await this.deps.sleep(100);}
      }
      if(!observed||!alive())throw new Error('The local native backend did not become ready.');
      if(!observed.configured||keyPath(observed.root)!==keyPath(canonical))throw new Error('The native backend workspace differs from the opened folder.');
      owner.metadata=observed;
      await this.context.secrets.store(`xmind.auth:${origin}`,token);
      this.owners.set(scope,owner);
      const record={origin,canonical,scope,metadata:observed,runtimeManifest:runtime.manifestSha256,runtimeRoot:runtime.runtimeRoot,launchConfiguration,privateDirectory:directory};
      if(this.context.globalState){const records=saved.filter(value=>value?.origin!==origin);await this.context.globalState.update('xmind.nativeWorkspaceOwners',[...records,record]);}
      // Folder changes never terminate a ready Native owner or its work.
      if(!current())throw new Error('Workspace changed while its backend became ready.');
      this.active={...owner,epoch,signature:selection.signature,roots:selection.roots};return this.active;
    }catch(error){
      // Only a just-spawned owner that never passed admission readiness is
      // eligible for startup cleanup. Existing/ready owners are never killed.
      if(!owner.metadata&&alive())child.kill();
      throw error;
    }
  }
  async readWorkspace(origin,token){
    const response=await this.deps.fetch(origin+'/v1/workspace',{headers:{Authorization:`Bearer ${token}`},redirect:'error',signal:AbortSignal.timeout(2000)});
    if(!response.ok)throw new Error('Cannot verify the local native workspace.');
    return workspaceMetadata(await response.json());
  }
  async ownerRequest(origin,token,route,body){
    const response=await this.deps.fetch(origin+route,{method:body===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body),redirect:'error',signal:AbortSignal.timeout(10000)});
    if(!response.ok){const error=new Error(response.status===404?'This legacy backend requires an explicit operator migration; it cannot issue a native retirement receipt.':'Native backend upgrade command was rejected. Existing storage was preserved.');error.status=response.status;throw error;}
    const result=await response.json();if(Buffer.byteLength(JSON.stringify(result))>8*1024*1024)throw new Error('Native upgrade observation exceeds its limit.');return result;
  }
  async saveOwner(owner){
    const records=this.context.globalState?.get('xmind.nativeWorkspaceOwners');if(!Array.isArray(records))throw new Error('Saved workspace ownership is invalid.');
    const {child,...record}=owner;delete record.epoch;delete record.signature;delete record.roots;delete record.backendChangePending;
    await this.context.globalState.update('xmind.nativeWorkspaceOwners',[...records.filter(v=>v?.scope!==owner.scope),JSON.parse(JSON.stringify(record))]);this.owners.set(owner.scope,owner);
  }
  async snapshotOwner(owner,token,paths){
    if(!paths){const sessions=await this.ownerRequest(owner.origin,token,'/v1/sessions');if(!Array.isArray(sessions)||sessions.length>1024||sessions.some(s=>typeof s.id!=='string'||!/^[A-Za-z0-9_-]{1,256}$/.test(s.id)))throw new Error('Cannot capture saved native sessions.');paths=['/v1/sessions','/v1/provider/profiles','/v1/models',...sessions.flatMap(s=>['/history','/runs','/skills'].map(part=>'/v1/sessions/'+s.id+part))];}
    const result={};for(const route of paths){if(!/^\/v1\/(sessions(\/[A-Za-z0-9_-]{1,256}\/(history|runs|skills))?|provider\/profiles|models)$/.test(route))throw new Error('Invalid saved upgrade observation route.');const record=await this.ownerRequest(owner.origin,token,route);if(route.endsWith('/skills'))delete record.authority_id;result[route]=crypto.createHash('sha256').update(JSON.stringify(record)).digest('hex');}return result;
  }
  async upgrade(){
    if(this.upgrading)throw new Error('A native backend upgrade is already in progress.');
    const selected=await this.selectedFolder(false),selectedPath=canonicalPath(await this.deps.fs.realpath(selected.folder.uri.fsPath));
    const selectedScope=crypto.createHash('sha256').update(keyPath(selectedPath)).digest('hex');
    const previous=this.owners.get(selectedScope)??this.context.globalState?.get('xmind.nativeWorkspaceOwners')?.findLast?.(v=>v?.scope===selectedScope);
    const owner=await this.connect();if(owner.upgrade)return owner;
    if(previous?.upgrade)return owner; // connect completed the already-dispatched handoff.
    const selection=await this.selectedFolder(false),epoch=this.epoch,current=()=>!this.disposed&&this.vscode.workspace.isTrusted&&epoch===this.epoch&&selection.signature===this.signature();
    if(owner.privateDirectory===undefined)throw new Error('External backends are upgraded by their operator.');
    const token=await this.context.secrets.get(`xmind.auth:${owner.origin}`);await this.observe(owner);
    const config={runtimeDirectory:machineSetting(this.vscode,'runtimeDirectory'),stdlibSource:machineSetting(this.vscode,'stdlibSource'),providerConfigPath:machineSetting(this.vscode,'providerConfigPath')};
    let runtime=await this.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},config);
    const roots=await Promise.all(selection.roots.map(async r=>canonicalPath(await this.deps.fs.realpath(r.fsPath))));
    const privateRoot=path.win32.dirname(path.win32.dirname(path.win32.dirname(owner.privateDirectory)));
    const generationParent=path.win32.join(privateRoot,'runtime-generations');
    if(roots.some(root=>contained(root,generationParent)||contained(generationParent,root)))throw new Error('Native generation storage overlaps a workspace folder.');
    runtime=await this.deps.retainRuntime(runtime,privateRoot);
    if(roots.some(r=>contained(r,runtime.runtimeRoot))||!await this.ownerStorageSafe(owner,roots)||!current())throw new Error('Workspace or private storage changed before upgrade.');
    let state;
    try{state=nativeOwnerState(await this.ownerRequest(owner.origin,token,'/v1/backend/owner'));}catch(error){if(error.status!==404)throw error;return this.upgradeLegacy(owner,selection,current,runtime,token,config);}
    if(!state.retirement_supported||state.retirement_requested||state.replacement_prepared)throw new Error('This backend cannot begin a new native upgrade.');
    const fields={expected_workspace_id:owner.metadata.workspace_id,expected_workspace_authority_id:owner.metadata.authority_id};
    this.upgrading=true;
    try{
      if(!state.quiesced)state=nativeOwnerState(await this.ownerRequest(owner.origin,token,'/v1/backend/owner/quiesce',{...fields,expected_generation:state.generation,expected_revision:state.revision}));
      const source={generation:state.generation,revision:state.revision,receipt_id:state.receipt_id};
      const port=await this.deps.port(),origin=`http://127.0.0.1:${port}`;
      const pending={phase:'quiesced',source,state,target:{runtimeRoot:runtime.runtimeRoot,manifestSha256:runtime.manifestSha256},origin,approvedEdits:machineSetting(this.vscode,'workspaceEdits')===true,launchConfiguration:JSON.stringify({...config,workspaceEdits:machineSetting(this.vscode,'workspaceEdits')===true})};
      owner.upgrade=pending;await this.context.secrets.store(`xmind.auth:${origin}`,token);await this.saveOwner(owner);
      pending.records=await this.snapshotOwner(owner,token);if(!current())throw new Error('Workspace changed while the native backend was quiesced.');
      pending.phase='retiring';await this.saveOwner(owner);
      // Dispatch once. A lost response is resolved by actual process exit and
      // Native's persisted target/source checks, never by replaying retirement.
      try{const retired=nativeOwnerState(await this.ownerRequest(owner.origin,token,'/v1/backend/owner/retire',{...fields,expected_generation:source.generation,expected_revision:source.revision,receipt_id:source.receipt_id,target_runtime_root:runtime.runtimeRoot,target_manifest_sha256:runtime.manifestSha256,approved_edits:pending.approvedEdits}));if(!sameReceipt(retired.bootstrap_receipt,source))throw new Error('Native retirement source changed.');}catch(error){if(error.status||!['TypeError','TimeoutError','AbortError'].includes(error.name))throw error;}
      return await this.completeUpgrade(owner,selection,current);
    }finally{this.upgrading=false;}
  }
  async upgradeLegacy(owner,selection,current,runtime,token,config){
    this.upgrading=true;
    try{
      const discovered=legacyProcess(await this.deps.admin(runtime,['inspect-legacy-listener',new URL(owner.origin).port],token,this.deps.env));
      canonicalPath(discovered.image_path);
      const prior=await this.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},{runtimeDirectory:path.win32.dirname(discovered.image_path)});
      if(prior.manifestSha256!==owner.runtimeManifest||prior.serverSha256!==discovered.server_sha256||keyPath(prior.nativeProgram)!==keyPath(discovered.image_path)||!current())throw new Error('Legacy source differs from the saved verified package. No process was stopped.');
      const checked=legacyProcess(await this.deps.admin(runtime,['inspect-legacy-owner',new URL(owner.origin).port,prior.nativeProgram,prior.serverSha256,path.win32.join(owner.privateDirectory,'state.sqlite'),owner.canonical,owner.metadata.workspace_id,owner.metadata.authority_id],token,this.deps.env));
      if(checked.process_id!==discovered.process_id||checked.process_birth!==discovered.process_birth||checked.server_sha256!==discovered.server_sha256||checked.authenticated!==true||checked.database_command_line_verified!==true||checked.process_signalled!==false||checked.migration_ticket_created!==false)throw new Error('Native legacy preflight changed. No process was stopped.');
      if(!await this.deps.confirmLegacyStop(owner))throw new Error('Legacy backend migration cancelled; its process and saved profile were preserved.');
      if(!current())throw new Error('Workspace changed before legacy migration.');
      await this.observe(owner);
      const ticketId=this.deps.uuid().replaceAll('-','');if(!/^[a-f0-9]{32}$/.test(ticketId))throw new Error('Invalid legacy migration request identity.');
      const origin=`http://127.0.0.1:${await this.deps.port()}`,approvedEdits=machineSetting(this.vscode,'workspaceEdits')===true;
      const pending={kind:'legacy',phase:'stopping',state:checked,ticketId,target:{runtimeRoot:runtime.runtimeRoot,manifestSha256:runtime.manifestSha256},origin,approvedEdits,records:{},launchConfiguration:JSON.stringify({...config,workspaceEdits:approvedEdits})};
      owner.upgrade=pending;await this.context.secrets.store(`xmind.auth:${origin}`,token);await this.saveOwner(owner);
      if(!current())throw new Error('Workspace changed before native legacy stop dispatch.');
      const args=['--db',path.win32.join(owner.privateDirectory,'state.sqlite'),'--modules',runtime.modules,'--stdlib',runtime.stdlib,'stop-and-prepare-legacy-owner',runtime.runtimeRoot,runtime.manifestSha256,owner.canonical,new URL(owner.origin).port,prior.nativeProgram,prior.serverSha256,owner.metadata.workspace_id,owner.metadata.authority_id,String(checked.process_id),checked.process_birth,ticketId,approvedEdits?'approved':'read-only','confirmed-stop'];
      // Dispatch once. Reload/lost stdout recovery reads the exact native ticket;
      // a missing ticket or live source never authorizes another stop dispatch.
      try{const prepared=await this.deps.admin(runtime,args,token,this.deps.env);if(prepared.legacy_ticket_id!==ticketId||prepared.source_terminated!==true||prepared.quiescence_receipt!==false||prepared.admission_closed!==true||prepared.process_id!==checked.process_id||prepared.process_birth!==checked.process_birth)throw new Error('Native legacy stop outcome is unverified.');pending.phase='prepared';await this.saveOwner(owner);}
      catch(error){if(!await this.deps.observeExit(runtime,checked))throw error;}
      return this.completeUpgrade(owner,selection,current);
    }finally{this.upgrading=false;}
  }
  async completeUpgrade(owner,selection,current){
    const pending=owner.upgrade;
    if(!pending||!['stopping','retiring','prepared','activating'].includes(pending.phase)||typeof pending.approvedEdits!=='boolean'||!/^http:\/\/127\.0\.0\.1:[0-9]{1,5}$/.test(pending.origin)||!pending.records||Object.keys(pending.records).length>3075)throw new Error('Upgrade needs operator recovery before replacement startup. Existing storage was preserved.');
    const legacy=pending.kind==='legacy',source=pending.source;
    if(legacy){legacyProcess(pending.state);if(typeof pending.ticketId!=='string'||!/^[a-f0-9]{32}$/.test(pending.ticketId))throw new Error('Invalid saved legacy operator ticket.');}
    else{nativeOwnerState(pending.state);if(source.generation!==pending.state.generation||source.revision!==pending.state.revision||source.receipt_id!==pending.state.receipt_id)throw new Error('Saved native upgrade receipt changed.');}
    const config={runtimeDirectory:pending.target.runtimeRoot};const runtime=await this.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},config);
    if(runtime.manifestSha256!==pending.target.manifestSha256||!runtime.qualified||!current())throw new Error('Pending native target or workspace changed.');
    if(!await this.deps.observeExit(runtime,pending.state))throw new Error('The retiring native process is still running. Upgrade remains pending; no replacement was started.');
    const token=await this.context.secrets.get(`xmind.auth:${owner.origin}`);
    if(typeof token!=='string'||!/^[\x21-\x7e]{32,256}$/.test(token)||await this.context.secrets.get(`xmind.auth:${pending.origin}`)!==token)throw new Error('Pending native upgrade authentication is unavailable.');
    if(legacy&&pending.phase==='stopping'){
      const recovered=await this.deps.admin(runtime,['--db',path.win32.join(owner.privateDirectory,'state.sqlite'),'--modules',runtime.modules,'--stdlib',runtime.stdlib,'inspect-legacy-ticket',runtime.runtimeRoot,runtime.manifestSha256,owner.canonical,String(pending.state.process_id),pending.state.process_birth,pending.state.server_sha256,pending.ticketId,'read-only-inspection',pending.approvedEdits?'approved':'read-only'],token,this.deps.env);
      if(recovered.legacy_ticket_id!==pending.ticketId||recovered.admission_closed!==true||recovered.quiescence_receipt!==false||recovered.process_signalled!==false)throw new Error('Native legacy preparation outcome remains unverified. No stop command was repeated.');
      pending.phase='prepared';await this.saveOwner(owner);
    }
    let actual;
    try{actual=await this.readWorkspace(pending.origin,token);}catch{}
    if(!actual){
      if(pending.phase==='activating')throw new Error('Activation outcome is unavailable. Restore the accepted owner; no source receipt was replayed.');
      const args=['--db',path.win32.join(owner.privateDirectory,'state.sqlite'),'--modules',runtime.modules,'--stdlib',runtime.stdlib,'--workspace',owner.canonical,'--port',new URL(pending.origin).port,'--runtime-manifest-sha256',runtime.manifestSha256,...(legacy?['--legacy-owner-ticket',pending.ticketId]:['--owner-receipt',`${source.generation}:${source.revision}:${source.receipt_id}`])];if(pending.approvedEdits)args.push('--workspace-edits','approved');
      if(!current())throw new Error('Workspace changed before replacement startup.');
      const env={...this.deps.env,XMIND_AUTH_TOKEN:token};for(const key of Object.keys(env))if(key.startsWith('XMIND_UI_')||key==='XMIND_API_KEY')delete env[key];
      const child=this.deps.spawn(runtime.nativeProgram,args,{cwd:owner.canonical,env,detached:true,windowsHide:true,stdio:'ignore'});child.unref?.();let failed=false;child.once('error',()=>{failed=true;});child.once('exit',()=>{failed=true;});
      const deadline=this.deps.now()+15000;while(this.deps.now()<deadline&&!failed){try{actual=await this.readWorkspace(pending.origin,token);break;}catch{await this.deps.sleep(100);}}
      if(!actual)throw new Error('Qualified native replacement did not become ready. Admission remains closed; saved storage was preserved.');
    }
    if(!actual.configured||actual.workspace_id!==owner.metadata.workspace_id||actual.authority_id===owner.metadata.authority_id||keyPath(actual.root)!==keyPath(owner.canonical)||!current())throw new Error('Prepared native workspace differs from the saved owner.');
    const prepared=nativeOwnerState(await this.ownerRequest(pending.origin,token,'/v1/backend/owner'));
    if(!prepared.replacement_prepared&&!prepared.quiesced&&pending.phase==='activating'){
      if(prepared.generation!==pending.prepared?.generation||prepared.revision!==pending.prepared.revision+1)throw new Error('Accepted native activation identity changed.');
    }else{
      if(!prepared.quiesced||!prepared.replacement_prepared||(legacy?(prepared.legacy_ticket_id!==pending.ticketId||prepared.bootstrap_receipt!==null):!sameReceipt(prepared.bootstrap_receipt,source)))throw new Error('Native replacement did not retain its exact source receipt or legacy ticket.');
      if(!legacy){const observed=await this.snapshotOwner({origin:pending.origin},token,Object.keys(pending.records));if(JSON.stringify(observed)!==JSON.stringify(pending.records))throw new Error('Saved native records changed during upgrade. Admission remains closed.');}
      pending.phase='activating';pending.prepared=prepared;await this.saveOwner(owner);if(!current())throw new Error('Workspace changed before native activation.');
      await this.ownerRequest(pending.origin,token,'/v1/backend/owner/activate',{expected_generation:prepared.generation,expected_revision:prepared.revision,receipt_id:prepared.receipt_id,expected_workspace_id:actual.workspace_id,expected_workspace_authority_id:actual.authority_id});
    }
    const accepted=nativeOwnerState(await this.ownerRequest(pending.origin,token,'/v1/backend/owner'));if(accepted.quiesced||accepted.generation!==prepared.generation)throw new Error('Native activation remains unverified.');
    const health=await this.ownerRequest(pending.origin,token,'/v1/health');if(health.file_edit_proposals!==pending.approvedEdits)throw new Error('Native edit policy differs from the accepted target.');
    for(const key of ['agentflow.session','xmind.model','xmind.observedRun','xmind.workflow']){const prior=this.context.workspaceState.get(key);if(prior?.url===owner.origin)await this.context.workspaceState.update(key,{...prior,url:pending.origin});}
    const replacement={origin:pending.origin,canonical:owner.canonical,scope:owner.scope,metadata:actual,privateDirectory:owner.privateDirectory,runtimeManifest:runtime.manifestSha256,runtimeRoot:runtime.runtimeRoot,launchConfiguration:pending.launchConfiguration};
    await this.saveOwner(replacement);this.active={...replacement,epoch:this.epoch,signature:selection.signature,roots:selection.roots};return this.active;
  }
  async cancelUpgrade(){
    if(this.upgrading)throw new Error('Wait for the in-flight native upgrade command.');
    const selection=await this.selectedFolder(false),canonical=canonicalPath(await this.deps.fs.realpath(selection.folder.uri.fsPath));
    const scope=crypto.createHash('sha256').update(keyPath(canonical)).digest('hex');
    const records=this.context.globalState?.get('xmind.nativeWorkspaceOwners');const owner=Array.isArray(records)?records.findLast(v=>v?.scope===scope):undefined;
    if(!owner?.upgrade)throw new Error('This workspace has no pending native upgrade.');
    if(owner.upgrade.kind==='legacy')throw new Error('Legacy stop dispatch cannot be rolled back by a view. Complete native ticket recovery or use operator recovery; no old owner was restarted.');
    await this.observe(owner);const token=await this.context.secrets.get(`xmind.auth:${owner.origin}`),state=nativeOwnerState(await this.ownerRequest(owner.origin,token,'/v1/backend/owner'));
    if(state.retirement_requested||state.replacement_prepared||!sameReceipt(state,owner.upgrade.source))throw new Error('Native retirement was consumed or its receipt changed; cancellation cannot restore the old owner.');
    const resumed=nativeOwnerState(await this.ownerRequest(owner.origin,token,'/v1/backend/owner/resume',{expected_generation:state.generation,expected_revision:state.revision,receipt_id:state.receipt_id,expected_workspace_id:owner.metadata.workspace_id,expected_workspace_authority_id:owner.metadata.authority_id}));
    if(resumed.quiesced)throw new Error('Native owner resume remains unverified.');delete owner.upgrade;await this.saveOwner(owner);this.active=undefined;return this.connect();
  }
  async ownerStorageSafe(owner,canonicalRoots){
    try{const actual=canonicalPath(await this.deps.fs.realpath(canonicalPath(owner.privateDirectory)));return keyPath(actual)===keyPath(owner.privateDirectory)&&!canonicalRoots.some(root=>contained(root,actual));}catch{return false;}
  }
  async attach(origin,token){
    const epoch=this.epoch,selection=await this.selectedFolder(false);
    const canonical=canonicalPath(await this.deps.fs.realpath(selection.folder.uri.fsPath));
    const actual=await this.readWorkspace(origin,token);
    if(!actual.configured||keyPath(actual.root)!==keyPath(canonical))throw new Error('The attached backend workspace differs from the opened folder.');
    if(epoch!==this.epoch||selection.signature!==this.signature()||this.disposed)throw new Error('Workspace changed while attaching.');
    const scope=crypto.createHash('sha256').update(keyPath(canonical)).digest('hex');
    this.active={origin,canonical,scope,metadata:actual,epoch,signature:selection.signature,roots:selection.roots};return this.active;
  }
  async observe(owner){
    const token=await this.context.secrets.get(`xmind.auth:${owner.origin}`);
    if(typeof token!=='string'||!/^[\x21-\x7e]{32,256}$/.test(token))throw new Error('Local workspace authentication is unavailable.');
    const actual=await this.readWorkspace(owner.origin,token);
    if(!actual.configured||actual.workspace_id!==owner.metadata.workspace_id||actual.authority_id!==owner.metadata.authority_id||keyPath(actual.root)!==keyPath(owner.canonical))throw new Error('The local native workspace owner changed.');
    return actual;
  }
  assert(ticket){
    const active=this.active;
    if(this.upgrading||this.disposed||!this.vscode.workspace.isTrusted||this.vscode.env?.remoteName||!active||ticket.owner!==active.scope||ticket.epoch!==this.epoch||ticket.origin!==active.origin||ticket.signature!==this.signature())throw new Error('The selected workspace changed before admission.');
  }
  async prepare(){
    const active=this.active;if(!active)throw new Error('Connect the opened workspace before submitting work.');
    const ticket={owner:active.scope,epoch:this.epoch,origin:active.origin,signature:active.signature};
    this.assert(ticket);const actual=await this.observe(active);this.assert(ticket);
    return {...ticket,fields:{expected_workspace_id:actual.workspace_id,expected_workspace_authority_id:actual.authority_id}};
  }
  async status(){
    const active=this.active;
    if(!active)return {origin:null,configured:false,root:null,workspace_id:null,authority_id:null};
    const ticket={owner:active.scope,epoch:this.epoch,origin:active.origin,signature:active.signature};
    this.assert(ticket);const actual=await this.observe(active);this.assert(ticket);return {origin:active.origin,...actual};
  }
}
module.exports={WorkspaceBackend,workspaceMetadata,canonicalPath,machineSetting};
