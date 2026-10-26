'use strict';
const path=require('node:path');
const {execFile}=require('node:child_process');
const {backendOrigin,validateToken}=require('./client');
const {resolveNativeRuntime}=require('./native-runtime');
const {machineSetting}=require('./workspace-backend');

function execute(program,args,options,input){
  return new Promise((resolve,reject)=>{
    const child=execFile(program,args,options,(error,stdout)=>{
      // Never forward child output or error objects containing private IPC data.
      if(error)reject(new Error('Native authentication storage failed; existing credentials were preserved.'));
      else resolve(stdout);
    });
    child.stdin?.on('error',()=>{});
    child.stdin?.end(input||'');
  });
}

class BackendAuth {
  constructor(vscode,context,dependencies={}){
    this.vscode=vscode;this.context=context;
    this.deps={platform:process.platform,arch:process.arch,env:process.env,resolveRuntime:resolveNativeRuntime,execute,...dependencies};
    this.native=this.deps.platform==='win32'&&this.deps.arch==='x64'&&vscode.UIKind?.Web!==undefined&&vscode.env?.uiKind===vscode.UIKind.Web;
    this.pending=new Map();this.runtime=undefined;
    this.listener=context.secrets.onDidChange?.(event=>{
      if(!event.key?.startsWith('xmind.auth:'))return;
      const origin=event.key.slice(11),pending=this.pending.get(origin);
      if(pending)Promise.all([pending,context.secrets.get(event.key)]).then(([cached,actual])=>{
        if(this.pending.get(origin)===pending&&cached!==actual)this.pending.delete(origin);
      }).catch(()=>{});
    });
  }
  dispose(){this.listener?.dispose();this.pending.clear();}
  key(origin){return 'xmind.auth:'+backendOrigin(origin);}
  trusted(){if(!this.vscode.workspace.isTrusted)throw new Error('Trust the workspace before accessing xMind authentication.');}
  async prepare(){
    if(!this.runtime)this.runtime=(async()=>{
      const runtime=await this.deps.resolveRuntime(this.context,{platform:this.deps.platform,arch:this.deps.arch,remoteName:this.vscode.env?.remoteName??null},
        {runtimeDirectory:machineSetting(this.vscode,'runtimeDirectory'),stdlibSource:machineSetting(this.vscode,'stdlibSource')});
      if(runtime.qualified!==true)throw new Error('Persistent authentication requires the bundled verified native runtime.');
      const directory=path.win32.join(runtime.privateStateRoot,'backend-auth');
      const canonical=p=>path.win32.resolve(p).toLowerCase().replace(/[\\/]+$/,'');
      const vault=canonical(directory);
      for(const folder of this.vscode.workspace.workspaceFolders||[]){
        if(folder.uri.scheme!=='file')continue;
        const root=canonical(folder.uri.fsPath);
        if(root===vault||vault.startsWith(root+'\\')||root.startsWith(vault+'\\'))throw new Error('Private authentication storage overlaps the opened workspace.');
      }
      const env={};for(const key of ['SystemRoot','SystemDrive','WINDIR','TEMP','TMP','USERPROFILE','LOCALAPPDATA','APPDATA','PATH','PATHEXT'])if(this.deps.env[key]!==undefined)env[key]=this.deps.env[key];
      const options={cwd:runtime.runtimeRoot,env,windowsHide:true,timeout:60000,maxBuffer:65536,encoding:'utf8'};
      // A user-selected older runtime must not treat an unknown bridge command
      // as a normal console command and start an unintended workspace profile.
      const help=await this.deps.execute(runtime.nativeProgram,['--help'],options);
      if(!help.includes('xmind client-auth get|put|seed|delete DIRECTORY ORIGIN'))throw new Error('Install the matching xMind native runtime to persist authentication.');
      return {runtime,directory,options};
    })().catch(error=>{this.runtime=undefined;throw error;});
    return this.runtime;
  }
  async call(action,origin,token){
    const {runtime,directory,options}=await this.prepare();this.trusted();
    let output;
    try{output=await this.deps.execute(runtime.nativeProgram,['client-auth',action,directory,origin],options,
      token===undefined?undefined:JSON.stringify({token:validateToken(token)}));}
    catch{throw new Error('Native authentication storage failed; existing credentials were preserved.');}
    let result;try{result=JSON.parse(output);}catch{throw new Error('Native authentication storage returned an invalid response.');}
    if(action==='get'){
      if(!result||Object.keys(result).length!==1||!Object.hasOwn(result,'token'))throw new Error('Native authentication storage returned an invalid response.');
      return result.token===null?undefined:validateToken(result.token);
    }
    if(!result||(action==='delete'?result.deleted:result.saved)!==true)throw new Error('Native authentication storage did not confirm the change.');
  }
  async get(origin){
    this.trusted();origin=backendOrigin(origin);
    if(!this.native)return this.context.secrets.get(this.key(origin));
    if(!this.pending.has(origin))this.pending.set(origin,this.restore(origin).catch(error=>{this.pending.delete(origin);throw error;}));
    return this.pending.get(origin);
  }
  async restore(origin){
    let token=await this.call('get',origin);
    if(token===undefined){
      const prior=await this.context.secrets.get(this.key(origin));
      if(prior!==undefined){
        // Atomic seed cannot overwrite a token rotated by another host.
        await this.call('seed',origin,prior);token=await this.call('get',origin);
      }
    }
    if(token!==undefined)await this.context.secrets.store(this.key(origin),token);
    return token;
  }
  async store(origin,token){
    this.trusted();origin=backendOrigin(origin);token=validateToken(token);
    if(this.native)await this.call('put',origin,token);
    await this.context.secrets.store(this.key(origin),token);
    if(this.native)this.pending.set(origin,Promise.resolve(token));
  }
  async delete(origin){
    this.trusted();origin=backendOrigin(origin);
    if(this.native)await this.call('delete',origin);
    await this.context.secrets.delete(this.key(origin));this.pending.delete(origin);
  }
}
module.exports={BackendAuth};
