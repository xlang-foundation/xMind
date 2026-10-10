'use strict';
// Thin editor access adapter. Native owns profile discovery, startup, process
// identity, storage and execution. The host never receives backend credentials.
const fs=require('node:fs/promises');
const path=require('node:path');
const crypto=require('node:crypto');
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

class WorkspaceBackend {
  constructor(vscode,context,resolveRuntime,dependencies={}){
    this.vscode=vscode;this.context=context;this.resolveRuntime=resolveRuntime;
    this.deps={fs,spawn,fetch:(...args)=>fetch(...args),random:()=>crypto.randomBytes(32).toString('hex'),sleep:ms=>new Promise(resolve=>setTimeout(resolve,ms)),now:()=>Date.now(),platform:process.platform,arch:process.arch,env:process.env,...dependencies};
    this.epoch=0;this.adapters=new Map();this.active=undefined;this.disposed=false;this.connecting=undefined;
  }
  signature(){return JSON.stringify((this.vscode.workspace.workspaceFolders||[]).map(folder=>folder.uri.toString()));}
  invalidate(){this.epoch++;this.active=undefined;}
  dispose(){this.disposed=true;this.invalidate();for(const owner of this.adapters.values())this.closeAdapter(owner);this.adapters.clear();/* Only view adapters close; native backends persist. */}
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

  closeAdapter(owner){
    if(owner.child&&owner.child.exitCode==null)owner.child.kill();
    this.context.secrets.delete?.('xmind.auth:'+owner.origin).catch(()=>{});
  }
  async connect(forcePick=false){
    for(;;){
      if(this.disposed)throw new Error('Workspace connection closed.');
      const pending=this.connecting;
      if(pending){
        if(pending.epoch===this.epoch&&!forcePick)return pending.promise;
        // Complete stale startup cleanup before connecting the current root.
        // Its rejection must not consume a later folder-selection request.
        try{await pending.promise;}catch{}
        continue;
      }
      const operation={epoch:this.epoch,promise:undefined};
      operation.promise=this.open(forcePick).finally(()=>{if(this.connecting===operation)this.connecting=undefined;});
      this.connecting=operation;return operation.promise;
    }
  }
  async open(forcePick){
    const epoch=this.epoch,selection=await this.selectedFolder(forcePick);
    const current=()=>!this.disposed&&this.vscode.workspace.isTrusted&&!this.vscode.env?.remoteName&&this.epoch===epoch&&selection.signature===this.signature();
    const canonical=canonicalPath(await this.deps.fs.realpath(selection.folder.uri.fsPath));
    const roots=await Promise.all(selection.roots.map(async root=>canonicalPath(await this.deps.fs.realpath(root.fsPath))));
    const scope=crypto.createHash('sha256').update(keyPath(canonical)).digest('hex');
    const config={runtimeDirectory:machineSetting(this.vscode,'runtimeDirectory'),stdlibSource:machineSetting(this.vscode,'stdlibSource'),providerConfigPath:machineSetting(this.vscode,'providerConfigPath')};
    const runtime=await this.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},config);
    if(runtime.qualified!==true)throw new Error('Managed profiles require bundled verified pure-library sources. Clear stdlibSource or use an external development server.');
    if(!current())throw new Error('Workspace changed while preparing its backend.');
    const retained=this.adapters.get(scope);
    if(retained){
      if(roots.some(root=>contained(root,retained.privateDirectory)||contained(retained.privateDirectory,root)||contained(root,retained.profileDirectory)||contained(retained.profileDirectory,root)))throw new Error('Private native storage overlaps the opened folder set. Existing storage was preserved.');
      try{await this.observe(retained);if(!current())throw new Error('Workspace changed while reconnecting.');this.active={...retained,epoch,signature:selection.signature,roots:selection.roots};return this.active;}
      catch(error){if(!current())throw error;this.closeAdapter(retained);this.adapters.delete(scope);}
    }
    let privateRoot=canonicalPath(runtime.privateStateRoot);
    if(roots.some(root=>contained(root,privateRoot)||contained(privateRoot,root))){
      const local=this.deps.env.LOCALAPPDATA;if(!local)throw new Error('Configure private view storage outside the workspace folders.');
      privateRoot=path.win32.join(canonicalPath(local),'xMind','ViewHosts');
    }
    if(roots.some(root=>contained(root,privateRoot)||contained(privateRoot,root)))throw new Error('Private view storage overlaps the opened folder set.');
    await this.deps.fs.mkdir(privateRoot,{recursive:true});privateRoot=canonicalPath(await this.deps.fs.realpath(privateRoot));
    if(roots.some(root=>contained(root,privateRoot)||contained(privateRoot,root)))throw new Error('Private view storage resolves inside a workspace.');
    const directory=canonicalPath(await this.deps.fs.mkdtemp(path.win32.join(privateRoot,'native-view-')));
    if(!contained(privateRoot,directory)||roots.some(root=>contained(root,directory)||contained(directory,root)))throw new Error('View rendezvous escaped private storage.');
    const ready=path.win32.join(directory,'ready.json'),token=this.deps.random();
    if(!/^[a-f0-9]{64}$/.test(token))throw new Error('Invalid local view authentication.');
    const args=['view','--workspace',canonical,'--ready-file',ready];
    if(runtime.providerConfig)args.push('--config',runtime.providerConfig);
    if(machineSetting(this.vscode,'workspaceEdits')===false)args.push('--read-only');
    if(this.deps.profileRoot)args.push('--profile-root',canonicalPath(this.deps.profileRoot));
    if(!current())throw new Error('Workspace changed before starting its native view.');
    const env={...this.deps.env,XMIND_VIEW_TOKEN:token};for(const key of Object.keys(env))if(key.startsWith('XMIND_UI_')||key==='XMIND_API_KEY'||key==='XMIND_AUTH_TOKEN')delete env[key];
    const child=this.deps.spawn(runtime.nativeProgram,args,{cwd:canonical,env,windowsHide:true,stdio:'ignore'});
    let failed=false;child.once('error',()=>{failed=true;});child.once('exit',()=>{failed=true;});child.unref?.();
    let owner;
    try{
      const deadline=this.deps.now()+60000;let metadata;
      while(this.deps.now()<deadline&&!failed&&current()){
        try{const info=await this.deps.fs.lstat(ready);if(!info.isFile()||info.isSymbolicLink()||info.nlink!==1||info.size>16384)throw new Error('Invalid native view metadata file.');metadata=JSON.parse(await this.deps.fs.readFile(ready,'utf8'));break;}catch(error){if(!['ENOENT','EACCES','EPERM','EBUSY'].includes(error.code)&&!(error instanceof SyntaxError))throw error;await this.deps.sleep(100);}
      }
      if(!current())throw new Error('Workspace changed while its native view became ready.');
      if(failed||!metadata){
        let detail='The native view did not become ready. Its backend/profile was preserved; reconnect to inspect the same owner.';
        try{const errorFile=path.win32.join(directory,'error.json'),info=await this.deps.fs.lstat(errorFile);if(info.isFile()&&!info.isSymbolicLink()&&info.nlink===1&&info.size<=4096){const error=JSON.parse(await this.deps.fs.readFile(errorFile,'utf8'));if(error.process_id===child.pid&&error.error_code==='native_view_startup_failed'&&typeof error.detail==='string'&&error.detail.length>0&&error.detail.length<=1024)detail=error.detail;}}catch{}
        throw new Error(detail);
      }
      if(metadata.process_id!==child.pid||typeof metadata.process_birth!=='string'||!/^[1-9][0-9]{0,19}$/.test(metadata.process_birth)||!Number.isSafeInteger(metadata.backend_process_id)||metadata.backend_process_id<1||!/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}$/.test(metadata.origin))throw new Error('Native view process metadata differs.');
      const actual=workspaceMetadata(metadata.workspace),profileDirectory=canonicalPath(metadata.profile_directory);
      if(!actual.configured||keyPath(actual.root)!==keyPath(canonical)||roots.some(root=>contained(root,profileDirectory)||contained(profileDirectory,root)))throw new Error('Native workspace or private profile differs from the opened folder set.');
      owner={origin:metadata.origin,canonical,scope,metadata:actual,child,pid:child.pid,backendPid:metadata.backend_process_id,privateDirectory:directory,profileDirectory};
      await this.context.secrets.store('xmind.auth:'+owner.origin,token);
      await this.observe(owner);
      if(!current())throw new Error('Workspace changed while authenticating its native view.');
      this.adapters.set(scope,owner);this.active={...owner,epoch,signature:selection.signature,roots:selection.roots};return this.active;
    }catch(error){if(owner)this.closeAdapter(owner);else if(child.exitCode==null)child.kill();throw error;}
  }
  stateFields(){return this.active?.profileDirectory?{workspace_id:this.active.metadata.workspace_id,profile_directory:this.active.profileDirectory}:undefined;}
  async readWorkspace(origin,token){
    const response=await this.deps.fetch(origin+'/v1/workspace',{headers:{Authorization:'Bearer '+token},redirect:'error',signal:AbortSignal.timeout(2000)});
    if(!response.ok)throw new Error('Cannot verify the local native workspace.');return workspaceMetadata(await response.json());
  }
  async attach(origin,token){
    const epoch=this.epoch,selection=await this.selectedFolder(false);
    const canonical=canonicalPath(await this.deps.fs.realpath(selection.folder.uri.fsPath));
    const actual=await this.readWorkspace(origin,token);
    if(!actual.configured||keyPath(actual.root)!==keyPath(canonical))throw new Error('The attached backend workspace differs from the opened folder.');
    if(epoch!==this.epoch||selection.signature!==this.signature()||this.disposed||!this.vscode.workspace.isTrusted||this.vscode.env?.remoteName)throw new Error('Workspace changed while attaching.');
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
