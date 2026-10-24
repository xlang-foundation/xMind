'use strict';
const {isLocalHostConnection}=require('./native-runtime');
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
function lspRange(vscode,value){const start=lspPosition(vscode,value?.start),end=lspPosition(vscode,value?.end);return start&&end?new vscode.Range(start,end):undefined;}
class LanguageServerEditor {
  constructor(vscode,workspaceBackend){
    this.vscode=vscode;this.workspaceBackend=workspaceBackend;this.client=undefined;this.epoch=-1;this.profiles=[];this.folders=[];this.membershipRefreshedAt=0;this.disposables=[];this.timers=new Map();this.sequences=new Map();this.collection=vscode.languages.createDiagnosticCollection('xMind');
  }
  active(client=this.client,epoch=this.epoch){return client&&this.client===client&&this.epoch===epoch&&this.workspaceBackend.epoch===epoch&&this.workspaceBackend.active?.origin===client.baseUrl&&this.vscode.workspace.isTrusted&&isLocalHostConnection(this.vscode.env?.remoteName);}
  clear(){for(const timer of this.timers.values())clearTimeout(timer);this.timers.clear();this.sequences.clear();this.collection.clear();}
  disconnect(){for(const item of this.disposables.splice(0))item.dispose();this.clear();this.profiles=[];this.folders=[];this.membershipRefreshedAt=0;this.client=undefined;this.epoch=-1;}
  async connect(client){
    this.disconnect();const owner=this.workspaceBackend.active;if(!owner||owner.origin!==client?.baseUrl||!this.vscode.workspace.isTrusted||!isLocalHostConnection(this.vscode.env?.remoteName))return;
    const epoch=this.workspaceBackend.epoch;this.client=client;this.epoch=epoch;
    let profiles;try{profiles=await client.languageServerProfiles();}catch(error){if(!this.active(client,epoch))return;this.disconnect();if(error.status!==404)throw error;return;}
    let folders=[{folder_id:owner.metadata?.workspace_id,root:owner.canonical,ordinal:0}];
    if(typeof client.workspaceMembership==='function')try{folders=(await client.workspaceMembership()).folders;}catch(error){if(error.status!==404){if(!this.active(client,epoch))return;this.disconnect();throw error;}}
    if(!this.active(client,epoch))return;this.profiles=profiles;this.folders=folders;
    const schedule=(document,delay)=>{if(document?.uri?.scheme!=='file'||!this.supports(document))return;const key=document.uri.toString();const sequence=(this.sequences.get(key)||0)+1;this.sequences.set(key,sequence);clearTimeout(this.timers.get(key));this.timers.set(key,setTimeout(()=>{this.timers.delete(key);this.refresh(document,key,sequence).catch(()=>{});},delay));};
    this.disposables.push(this.vscode.workspace.onDidOpenTextDocument(document=>schedule(document,250)));
    this.disposables.push(this.vscode.workspace.onDidChangeTextDocument(event=>schedule(event.document,650)));
    this.disposables.push(this.vscode.workspace.onDidSaveTextDocument(document=>schedule(document,0)));
    this.disposables.push(this.vscode.workspace.onDidCloseTextDocument(document=>{const key=document.uri.toString();clearTimeout(this.timers.get(key));this.timers.delete(key);this.sequences.delete(key);this.collection.delete(document.uri);}));
    const languages=new Set();for(const profile of this.profiles){if(languages.has(profile.language_id))continue;languages.add(profile.language_id);
      this.disposables.push(this.vscode.languages.registerDefinitionProvider(profile.language_id,{provideDefinition:(document,position,token)=>this.navigate(document,position,'definition',token)}));
      this.disposables.push(this.vscode.languages.registerReferenceProvider(profile.language_id,{provideReferences:(document,position,_context,token)=>this.navigate(document,position,'references',token)}));
      this.disposables.push(this.vscode.languages.registerHoverProvider(profile.language_id,{provideHover:(document,position,token)=>this.hover(document,position,token)}));
      this.disposables.push(this.vscode.languages.registerDocumentFormattingEditProvider(profile.language_id,{provideDocumentFormattingEdits:(document,options,token)=>this.format(document,options,token)}));
    }
    for(const document of this.vscode.workspace.textDocuments||[])schedule(document,100);
  }
  supports(document){
    const extension=path.extname(document.uri.fsPath).toLowerCase();let count=0;
    for(const profile of this.profiles)for(const value of profile.extensions)if(value.toLowerCase()===extension)count++;
    return count===1;
  }
  folderFor(document){
    if(document?.uri?.scheme!=='file'||typeof document.uri.fsPath!=='string')return;
    const matches=[];for(const folder of this.folders){const relative=workspaceRelativePath(folder.root,document.uri.fsPath);if(relative)matches.push({folder,path:relative});}
    if(matches.length!==1)return;const match=matches[0];return {path:match.path,folderId:match.folder.ordinal===0?undefined:match.folder.folder_id};
  }
  async folderForCurrent(document,client=this.client,epoch=this.epoch){
    let target=this.folderFor(document);if(target||typeof client?.workspaceMembership!=='function'||Date.now()-this.membershipRefreshedAt<1500)return target;
    this.membershipRefreshedAt=Date.now();try{const membership=await client.workspaceMembership();if(!this.active(client,epoch))return;this.folders=membership.folders;}catch(error){if(error.status!==404)throw error;}
    return this.active(client,epoch)?this.folderFor(document):undefined;
  }
  containsRegisteredFile(file){return this.folders.some(folder=>workspaceRelativePath(folder.root,file));}
  async refresh(document,key=document.uri.toString(),sequence=this.sequences.get(key)){
    const client=this.client,epoch=this.epoch;if(!this.active(client,epoch)||!this.supports(document)||!(this.vscode.workspace.textDocuments||[]).includes(document))return;
    const target=await this.folderForCurrent(document,client,epoch);if(!target)return;const relative=target.path;
    const version=document.version,text=document.getText();const ticket=await this.workspaceBackend.prepare();this.workspaceBackend.assert(ticket);
    if(!this.active(client,epoch)||this.sequences.get(key)!==sequence||document.version!==version||ticket.origin!==client.baseUrl)return;
    try{
      const response=await client.languageServerQuery({path:relative,method:'diagnostics',documentText:text,...(target.folderId?{folderId:target.folderId}:{})},ticket.fields);
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
  async query(document,position,method,token,formattingOptions){
    const client=this.client,epoch=this.epoch;if(!this.active(client,epoch)||!this.supports(document)||!(this.vscode.workspace.textDocuments||[]).includes(document)||token?.isCancellationRequested)return;
    const target=await this.folderForCurrent(document,client,epoch);if(!target)return;const relative=target.path;
    const version=document.version,line=position?.line??0,character=position?.character??0;if(!Number.isSafeInteger(line)||line<0||!Number.isSafeInteger(character)||character<0)return;
    const ticket=await this.workspaceBackend.prepare();this.workspaceBackend.assert(ticket);
    if(!this.active(client,epoch)||document.version!==version||token?.isCancellationRequested||ticket.origin!==client.baseUrl)return;
    const response=await client.languageServerQuery({path:relative,method,line,character,documentText:document.getText(),...(target.folderId?{folderId:target.folderId}:{}),...(formattingOptions?{formattingOptions}:{})},ticket.fields);this.workspaceBackend.assert(ticket);
    if(!this.active(client,epoch)||document.version!==version||token?.isCancellationRequested||!(this.vscode.workspace.textDocuments||[]).includes(document)||response.path!==relative||response.method!==method)return;
    return response.result;
  }
  async format(document,options,token){
    if(!options||!Number.isSafeInteger(options.tabSize)||options.tabSize<1||options.tabSize>16||typeof options.insertSpaces!=='boolean')return;
    const formattingOptions={tabSize:options.tabSize,insertSpaces:options.insertSpaces};
    for(const [source,target] of [['trimTrailingWhitespace','trimTrailingWhitespace'],['insertFinalNewline','insertFinalNewline'],['trimFinalNewlines','trimFinalNewlines']])if(typeof options[source]==='boolean')formattingOptions[target]=options[source];
    const result=await this.query(document,undefined,'formatting',token,formattingOptions);
    if(result===null)return;if(!Array.isArray(result)||result.length>10000)return;
    const source=document.getText();if(Buffer.byteLength(source,'utf8')>900*1024)return;
    const checked=[];let bytes=0;
    for(const item of result){
      if(!item||typeof item!=='object'||typeof item.newText!=='string'||Buffer.byteLength(item.newText,'utf8')>900*1024)return;
      bytes+=Buffer.byteLength(item.newText,'utf8');if(bytes>900*1024)return;
      const range=lspRange(this.vscode,item.range);if(!range)return;
      for(const point of [range.start,range.end])if(point.line>=document.lineCount||point.character>document.lineAt(point.line).text.length)return;
      const start=document.offsetAt(range.start),end=document.offsetAt(range.end);
      if(!Number.isSafeInteger(start)||!Number.isSafeInteger(end)||start<0||end<start||end>source.length)return;
      checked.push({start,end,range,newText:item.newText});
    }
    checked.sort((left,right)=>left.start-right.start||left.end-right.end);
    for(let index=1;index<checked.length;index++)if(checked[index].start<checked[index-1].end||(checked[index].start===checked[index-1].start&&checked[index].end===checked[index].start))return;
    return checked.map(value=>new this.vscode.TextEdit(value.range,value.newText));
  }
  async navigate(document,position,method,token){
    const result=await this.query(document,position,method,token);if(result==null)return;
    const locations=Array.isArray(result)?result:[result];if(locations.length>10000)return;
    const mapped=[];for(const item of locations){if(!item||typeof item!=='object')continue;
      const uriText=typeof item.uri==='string'?item.uri:item.targetUri;if(typeof uriText!=='string'||uriText.length>32768)continue;
      let uri;try{uri=this.vscode.Uri.parse(uriText);}catch{continue;}if(uri.scheme!=='file'||!this.containsRegisteredFile(uri.fsPath))continue;
      const range=lspRange(this.vscode,item.range||item.targetSelectionRange||item.targetRange);if(range)mapped.push(new this.vscode.Location(uri,range));
    }return mapped.length?mapped:undefined;
  }
  async hover(document,position,token){
    const result=await this.query(document,position,'hover',token);if(!result||typeof result!=='object'||Array.isArray(result))return;
    const values=Array.isArray(result.contents)?result.contents:[result.contents];if(!values.length)return;
    const markdown=new this.vscode.MarkdownString();markdown.isTrusted=false;markdown.supportHtml=false;let used=0;
    for(const value of values){if(used>=65536)break;if(typeof value==='string'){const text=value.slice(0,65536-used);markdown.appendMarkdown(text);used+=text.length;}
      else if(value&&typeof value==='object'&&typeof value.value==='string'){
        const text=value.value.slice(0,65536-used);if(value.kind==='plaintext')markdown.appendText(text);else if(typeof value.language==='string'&&/^[A-Za-z0-9_+.-]{1,64}$/.test(value.language))markdown.appendCodeblock(text,value.language);else markdown.appendMarkdown(text);used+=text.length;
      }
    }
    if(!used)return;const range=lspRange(this.vscode,result.range);return new this.vscode.Hover(markdown,range);
  }
  dispose(){this.disconnect();this.collection.dispose();}
}
module.exports={LanguageServerEditor,workspaceRelativePath,lspPosition,lspRange};
