'use strict';
// Thin extension-host ownership only. Execution, permissions and persistence
// remain in Native; provider YAML contents are never read here.
const fs=require('node:fs/promises');
const path=require('node:path');
const crypto=require('node:crypto');
const net=require('node:net');
const {spawn}=require('node:child_process');

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

class WorkspaceBackend {
  constructor(vscode,context,resolveRuntime,dependencies={}){
    this.vscode=vscode;this.context=context;this.resolveRuntime=resolveRuntime;
    this.deps={fs,spawn,fetch:(...args)=>fetch(...args),port:loopbackPort,random:()=>crypto.randomBytes(32).toString('hex'),uuid:()=>crypto.randomUUID(),sleep:ms=>new Promise(resolve=>setTimeout(resolve,ms)),now:()=>Date.now(),platform:process.platform,arch:process.arch,env:process.env,...dependencies};
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
    const runtime=await this.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},config);
    if(!current())throw new Error('Workspace changed while preparing its backend.');
    const retained=this.owners.get(scope);
    // A package/settings change cannot implicitly replace a workspace's profile
    // with an empty database. Retain the authenticated native owner until Native
    // supplies an explicit state-preserving generation handoff.
    const reconnect=async prior=>{
      if(!/^http:\/\/127\.0\.0\.1:[0-9]{1,5}$/.test(prior.origin)||keyPath(prior.canonical)!==keyPath(canonical))throw new Error('Invalid retained workspace owner; existing storage was preserved.');
      if(!await this.ownerStorageSafe(prior,canonicalRoots))throw new Error('Existing backend storage is unavailable or inside a workspace folder. Narrow the opened folder set before reconnecting; no replacement database was created.');
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
    let directory=path.win32.join(privateRoot,'workspace-backends',scope,this.deps.uuid());
    await this.deps.fs.mkdir(directory,{recursive:true});
    directory=canonicalPath(await this.deps.fs.realpath(directory));
    if(!contained(privateRoot,directory)||canonicalRoots.some(root=>contained(root,directory)))throw new Error('Private backend directory escaped its verified storage root.');
    const port=await this.deps.port(),origin=`http://127.0.0.1:${port}`,token=this.deps.random();
    const args=['--db',path.win32.join(directory,'state.sqlite'),'--modules',runtime.modules,'--stdlib',runtime.stdlib,'--port',String(port),'--workspace',canonical];
    if(runtime.providerConfig!==undefined)args.push('--provider-config',runtime.providerConfig);
    if(machineSetting(this.vscode,'workspaceEdits')===true)args.push('--workspace-edits','approved');
    if(!current())throw new Error('Workspace changed before starting its backend.');
    const childEnv={...this.deps.env,XMIND_AUTH_TOKEN:token};
    for(const key of Object.keys(childEnv))if(key.startsWith('XMIND_UI_'))delete childEnv[key];
    const child=this.deps.spawn(runtime.nativeProgram,args,{cwd:canonical,env:childEnv,detached:true,windowsHide:true,stdio:'ignore'});
    let exited=false,spawnError=false;child.once('exit',()=>{exited=true;});child.once('error',()=>{spawnError=true;});child.unref?.();
    const owner={origin,canonical,scope,child,pid:child.pid,metadata:undefined,runtimeManifest:runtime.manifestSha256,launchConfiguration,privateDirectory:directory};
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
      const record={origin,canonical,scope,metadata:observed,runtimeManifest:runtime.manifestSha256,launchConfiguration,privateDirectory:directory};
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
    if(this.disposed||!this.vscode.workspace.isTrusted||this.vscode.env?.remoteName||!active||ticket.owner!==active.scope||ticket.epoch!==this.epoch||ticket.origin!==active.origin||ticket.signature!==this.signature())throw new Error('The selected workspace changed before admission.');
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
