'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const path=require('node:path');
const {LanguageServerEditor}=require('../language-server-editor');

class Position{constructor(line,character){this.line=line;this.character=character;}}
class Range{constructor(start,end){this.start=start;this.end=end;}}
class Diagnostic{constructor(range,message,severity){this.range=range;this.message=message;this.severity=severity;}}
function fixture(){
 const events=new Map(),items=new Map(),queries=[];let answer=async()=>({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:'diagnostics',result:{kind:'full',items:[{range:{start:{line:0,character:1},end:{line:0,character:3}},severity:1,message:'syntax issue',source:'rust-analyzer',code:'E001'}]},diagnostics:[]});
 const subscribe=name=>callback=>{const list=events.get(name)||[];list.push(callback);events.set(name,list);return {dispose(){const index=list.indexOf(callback);if(index>=0)list.splice(index,1);}};};
 const document={uri:{scheme:'file',fsPath:path.join('C:\\Project','src','lib.rs'),toString:()=> 'file:///C:/Project/src/lib.rs'},version:1,getText:()=> 'fn broken(\n'};
 const vscode={workspace:{isTrusted:true,textDocuments:[document],onDidOpenTextDocument:subscribe('open'),onDidChangeTextDocument:subscribe('change'),onDidSaveTextDocument:subscribe('save'),onDidCloseTextDocument:subscribe('close')},env:{remoteName:undefined},languages:{createDiagnosticCollection(){return {set:(uri,value)=>items.set(uri.toString(),value),delete:uri=>items.delete(uri.toString()),clear:()=>items.clear(),dispose:()=>items.clear()};}},Position,Range,Diagnostic,DiagnosticSeverity:{Error:0,Warning:1,Information:2,Hint:3}};
 const client={baseUrl:'http://127.0.0.1:8765',async languageServerProfiles(){return [{id:'rust',language_id:'rust',revision:1,extensions:['.rs']}]},async languageServerQuery(query,binding){queries.push({query,binding});return answer();}};
 const backend={epoch:4,active:{origin:client.baseUrl,canonical:'C:\\Project'},async prepare(){return {origin:client.baseUrl,fields:{expected_workspace_id:'workspace-1',expected_workspace_authority_id:'a'.repeat(32)}};},assert(ticket){assert.equal(ticket.origin,client.baseUrl);}};
 return {editor:new LanguageServerEditor(vscode,backend),vscode,client,backend,document,events,items,queries,setAnswer:value=>{answer=value;},emit:(name,value)=>{for(const callback of [...(events.get(name)||[])])callback(value);}};
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
