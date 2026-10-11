'use strict';
const path=require('node:path');

function workspaceRelativePath(root,file){
  if(typeof root!=='string'||!root||typeof file!=='string'||!file)return;
  const relative=path.relative(root,file);
  if(!relative||path.isAbsolute(relative)||relative==='..'||relative.startsWith(`..${path.sep}`))return;
  return relative.split(path.sep).join('/');
}
function lspPosition(vscode,value){
    if(!value||!Number.isSafeInteger(value.line)||value.line<0||!Number.isSafeInteger(value.character)||value.character<0)return;
  return new vscode.Position(value.line,value.character);
}
class LanguageServerEditor {
  constructor(vscode,workspaceBackend){
    this.vscode=vscode;this.workspaceBackend=workspaceBackend;this.client=undefined;this.epoch=-1;this.profiles=[];this.disposables=[];this.timers=new Map();this.sequences=new Map();this.collection=vscode.languages.createDiagnosticCollection('xMind');
  }
  active(client=this.client,epoch=this.epoch){return client&&this.client===client&&this.epoch===epoch&&this.workspaceBackend.epoch===epoch&&this.workspaceBackend.active?.origin===client.baseUrl&&this.vscode.workspace.isTrusted&&this.vscode.env?.remoteName===undefined;}
  clear(){for(const timer of this.timers.values())clearTimeout(timer);this.timers.clear();this.sequences.clear();this.collection.clear();}
  disconnect(){for(const item of this.disposables.splice(0))item.dispose();this.clear();this.profiles=[];this.client=undefined;this.epoch=-1;}
  async connect(client){
    this.disconnect();const owner=this.workspaceBackend.active;if(!owner||owner.origin!==client?.baseUrl||!this.vscode.workspace.isTrusted||this.vscode.env?.remoteName)return;
    const epoch=this.workspaceBackend.epoch;this.client=client;this.epoch=epoch;
    try{this.profiles=await client.languageServerProfiles();}catch(error){this.disconnect();if(error.status!==404)throw error;return;}
    if(!this.active(client,epoch))return;
    const schedule=(document,delay)=>{if(document?.uri?.scheme!=='file'||!this.supports(document))return;const key=document.uri.toString();const sequence=(this.sequences.get(key)||0)+1;this.sequences.set(key,sequence);clearTimeout(this.timers.get(key));this.timers.set(key,setTimeout(()=>{this.timers.delete(key);this.refresh(document,key,sequence).catch(()=>{});},delay));};
    this.disposables.push(this.vscode.workspace.onDidOpenTextDocument(document=>schedule(document,250)));
    this.disposables.push(this.vscode.workspace.onDidChangeTextDocument(event=>schedule(event.document,650)));
    this.disposables.push(this.vscode.workspace.onDidSaveTextDocument(document=>schedule(document,0)));
    this.disposables.push(this.vscode.workspace.onDidCloseTextDocument(document=>{const key=document.uri.toString();clearTimeout(this.timers.get(key));this.timers.delete(key);this.sequences.delete(key);this.collection.delete(document.uri);}));
    for(const document of this.vscode.workspace.textDocuments||[])schedule(document,100);
  }
  supports(document){
    const extension=path.extname(document.uri.fsPath).toLowerCase();let count=0;
    for(const profile of this.profiles)for(const value of profile.extensions)if(value.toLowerCase()===extension)count++;
    return count===1;
  }
  async refresh(document,key=document.uri.toString(),sequence=this.sequences.get(key)){
    const client=this.client,epoch=this.epoch;if(!this.active(client,epoch)||!this.supports(document)||!(this.vscode.workspace.textDocuments||[]).includes(document))return;
    const owner=this.workspaceBackend.active,relative=workspaceRelativePath(owner.canonical,document.uri.fsPath);if(!relative)return;
    const version=document.version,text=document.getText();const ticket=await this.workspaceBackend.prepare();this.workspaceBackend.assert(ticket);
    if(!this.active(client,epoch)||this.sequences.get(key)!==sequence||document.version!==version||ticket.origin!==client.baseUrl)return;
    try{
      const response=await client.languageServerQuery({path:relative,method:'diagnostics',documentText:text},ticket.fields);
      this.workspaceBackend.assert(ticket);
      if(!this.active(client,epoch)||this.sequences.get(key)!==sequence||document.version!==version||!(this.vscode.workspace.textDocuments||[]).includes(document))return;
      const values=Array.isArray(response.result?.items)?response.result.items:response.diagnostics;
      const mapped=[];for(const item of values){const start=lspPosition(this.vscode,item.range?.start),end=lspPosition(this.vscode,item.range?.end);if(!start||!end||typeof item.message!=='string'||item.message.length>65536)continue;
        const severity=Number.isInteger(item.severity)&&item.severity>=1&&item.severity<=4?item.severity-1:this.vscode.DiagnosticSeverity.Warning;
        const diagnostic=new this.vscode.Diagnostic(new this.vscode.Range(start,end),item.message,severity);diagnostic.source=typeof item.source==='string'&&item.source.length<=256?item.source:response.language_id;
        if((typeof item.code==='string'&&item.code.length<=256)||(Number.isSafeInteger(item.code)&&item.code>=0))diagnostic.code=item.code;mapped.push(diagnostic);
      }
      this.collection.set(document.uri,mapped);
    }catch(error){
      if(!this.active(client,epoch)||this.sequences.get(key)!==sequence)return;
      if(error.status===403||error.status===404)this.collection.delete(document.uri);
      else if(error.status===409)this.collection.clear();
    }
  }
  dispose(){this.disconnect();this.collection.dispose();}
}
module.exports={LanguageServerEditor,workspaceRelativePath};
