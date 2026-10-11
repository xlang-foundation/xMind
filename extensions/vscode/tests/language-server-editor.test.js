'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const path=require('node:path');
const {LanguageServerEditor}=require('../language-server-editor');

class Position{constructor(line,character){this.line=line;this.character=character;}}
class Range{constructor(start,end){this.start=start;this.end=end;}}
class Diagnostic{constructor(range,message,severity){this.range=range;this.message=message;this.severity=severity;}}
class Location{constructor(uri,range){this.uri=uri;this.range=range;}}
class MarkdownString{constructor(){this.value='';this.isTrusted=true;this.supportHtml=true;}appendMarkdown(value){this.value+=value;}appendText(value){this.value+=value;}appendCodeblock(value,language){this.value+='```'+language+'\n'+value+'\n```';}}
class Hover{constructor(contents,range){this.contents=contents;this.range=range;}}
function fixture(){
 const events=new Map(),items=new Map(),queries=[],providers={definition:[],reference:[],hover:[]};let answer=async query=>({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:query.method,result:query.method==='diagnostics'?{kind:'full',items:[{range:{start:{line:0,character:1},end:{line:0,character:3}},severity:1,message:'syntax issue',source:'rust-analyzer',code:'E001'}]}:null,diagnostics:[]});
 const subscribe=name=>callback=>{const list=events.get(name)||[];list.push(callback);events.set(name,list);return {dispose(){const index=list.indexOf(callback);if(index>=0)list.splice(index,1);}};};
 const document={uri:{scheme:'file',fsPath:path.join('C:\\Project','src','lib.rs'),toString:()=> 'file:///C:/Project/src/lib.rs'},version:1,getText:()=> 'fn broken(\n'};
 const vscode={workspace:{isTrusted:true,textDocuments:[document],onDidOpenTextDocument:subscribe('open'),onDidChangeTextDocument:subscribe('change'),onDidSaveTextDocument:subscribe('save'),onDidCloseTextDocument:subscribe('close')},env:{remoteName:undefined},Uri:{parse(value){const url=new URL(value);return {scheme:url.protocol.slice(0,-1),fsPath:require('node:url').fileURLToPath(url),toString:()=>value};}},languages:{createDiagnosticCollection(){return {set:(uri,value)=>items.set(uri.toString(),value),delete:uri=>items.delete(uri.toString()),clear:()=>items.clear(),dispose:()=>items.clear()};},registerDefinitionProvider(language,provider){providers.definition.push({language,provider});return {dispose(){}};},registerReferenceProvider(language,provider){providers.reference.push({language,provider});return {dispose(){}};},registerHoverProvider(language,provider){providers.hover.push({language,provider});return {dispose(){}};}},Position,Range,Location,MarkdownString,Hover,Diagnostic,DiagnosticSeverity:{Error:0,Warning:1,Information:2,Hint:3}};
 const client={baseUrl:'http://127.0.0.1:8765',async languageServerProfiles(){return [{id:'rust',language_id:'rust',revision:1,extensions:['.rs']}]},async languageServerQuery(query,binding){queries.push({query,binding});return answer(query);}};
 const backend={epoch:4,active:{origin:client.baseUrl,canonical:'C:\\Project'},async prepare(){return {origin:client.baseUrl,fields:{expected_workspace_id:'workspace-1',expected_workspace_authority_id:'a'.repeat(32)}};},assert(ticket){assert.equal(ticket.origin,client.baseUrl);}};
 return {editor:new LanguageServerEditor(vscode,backend),vscode,client,backend,document,events,items,queries,providers,setAnswer:value=>{answer=value;},emit:(name,value)=>{for(const callback of [...(events.get(name)||[])])callback(value);}};
}
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

test('VS Code publishes workspace-bound native diagnostics from the current unsaved editor buffer',async()=>{
 const f=fixture();await f.editor.connect(f.client);f.emit('save',f.document);await delay(20);
 const sent=f.queries[0];assert.equal(sent.query.path,'src/lib.rs');assert.equal(sent.query.documentText,'fn broken(\n');assert.deepEqual(sent.binding,{expected_workspace_id:'workspace-1',expected_workspace_authority_id:'a'.repeat(32)});
 const diagnostics=f.items.get(f.document.uri.toString());assert.equal(diagnostics.length,1);assert.equal(diagnostics[0].message,'syntax issue');assert.equal(diagnostics[0].severity,0);assert.equal(diagnostics[0].code,'E001');f.editor.dispose();
});

test('VS Code skips files outside the selected root and clears on backend disconnect',async()=>{
 const f=fixture();await f.editor.connect(f.client);f.document.uri.fsPath='C:\\Outside\\secret.rs';f.emit('save',f.document);await delay(10);assert.equal(f.queries.length,0);f.editor.disconnect();assert.equal(f.items.size,0);
});

test('a stale diagnostic response cannot republish after workspace authority changes',async()=>{
 const f=fixture();let resolve;f.setAnswer(()=>new Promise(done=>{resolve=done;}));await f.editor.connect(f.client);f.emit('save',f.document);await delay(5);f.backend.epoch++;
 resolve({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:'diagnostics',result:{kind:'full',items:[]},diagnostics:[]});await delay(10);assert.equal(f.items.size,0);f.editor.dispose();
});

test('VS Code navigation and hover use the native language server with the current buffer',async()=>{
 const f=fixture();await f.editor.connect(f.client);
 f.setAnswer(async query=>query.method==='definition'?{server_id:'rust',language_id:'rust',path:'src/lib.rs',method:query.method,result:[
   {uri:'file:///C:/Project/src/definition.rs',range:{start:{line:4,character:2},end:{line:4,character:8}}},
   {uri:'file:///C:/Outside/private.rs',range:{start:{line:0,character:0},end:{line:0,character:1}}}],diagnostics:[]}:
  {server_id:'rust',language_id:'rust',path:'src/lib.rs',method:query.method,result:{contents:{kind:'markdown',value:'## Safe hover\n`symbol`'},range:{start:{line:0,character:3},end:{line:0,character:9}}},diagnostics:[]});
 const definition=f.providers.definition[0].provider,hover=f.providers.hover[0].provider;assert.equal(f.providers.reference.length,1);
 const target=await definition.provideDefinition(f.document,new Position(2,7),{isCancellationRequested:false});assert.equal(target.length,1);assert.equal(target[0].uri.fsPath,path.win32.join('C:\\Project','src','definition.rs'));assert.deepEqual(target[0].range.start,new Position(4,2));
 const tip=await hover.provideHover(f.document,new Position(2,7),{isCancellationRequested:false});assert.equal(tip.contents.isTrusted,false);assert.equal(tip.contents.supportHtml,false);assert.match(tip.contents.value,/Safe hover/);
 assert.deepEqual(f.queries.map(value=>[value.query.method,value.query.line,value.query.character,value.query.documentText]),[['definition',2,7,'fn broken(\n'],['hover',2,7,'fn broken(\n']]);f.editor.dispose();
});

test('stale or cancelled language-server navigation does not return locations',async()=>{
 const f=fixture();let resolve;f.setAnswer(()=>new Promise(done=>{resolve=done;}));await f.editor.connect(f.client);const pending=f.providers.reference[0].provider.provideReferences(f.document,new Position(1,1),{}, {isCancellationRequested:false});await delay(2);f.backend.epoch++;
 resolve({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:'references',result:[{uri:'file:///C:/Project/src/definition.rs',range:{start:{line:1,character:1},end:{line:1,character:2}}}],diagnostics:[]});assert.equal(await pending,undefined);f.editor.dispose();
});
