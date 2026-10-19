// Explicit synthetic LSP peer. This tests native protocol behavior, not a real
// TypeScript/Python language service or model inference.
import assert from 'node:assert/strict';
const mode=process.argv[2];assert.ok(['push','unversioned','pull','pull-error','silent'].includes(mode));
let buffer=Buffer.alloc(0),clientPush=false;
const send=message=>{const body=Buffer.from(JSON.stringify({jsonrpc:'2.0',...message}));process.stdout.write('Content-Length: '+body.length+'\r\n\r\n');process.stdout.write(body);};
const diagnostic=(version,uri)=>({range:{start:{line:0,character:6},end:{line:0,character:14}},severity:1,code:2322,message:'Explicit fixture type mismatch',data:{version,clientPush},source:'synthetic-language-peer'});
function publish(uri,version,items){uri=uri.replace(/^file:\/\/\/([A-Za-z]):/,(_,drive)=>'file:///'+drive.toLowerCase()+'%3A');send({method:'textDocument/publishDiagnostics',params:{uri,...(mode==='unversioned'?{}:{version}),diagnostics:items}});}
function accept(message){
 if(message.method==='initialize'){
  clientPush=message.params.capabilities?.textDocument?.publishDiagnostics?.versionSupport===true;
  return send({id:message.id,result:{capabilities:{textDocumentSync:1,...(mode.startsWith('pull')?{diagnosticProvider:{interFileDependencies:false,workspaceDiagnostics:false}}:{})}}});
 }
 if(message.method==='initialized')return;
 if(message.method==='textDocument/didOpen'||message.method==='textDocument/didChange'){
  const doc=message.params.textDocument,uri=doc.uri,version=doc.version;
  if(mode==='silent')return;
  // The stale version must not satisfy a query for the new buffer. A fresh
  // empty publication is an actual clean result, not an absent response.
  if(mode==='push')publish(uri,version-1,[diagnostic(version-1,uri)]);
  if(mode==='pull-error')return publish(uri,version,[]);
  if(!mode.startsWith('pull'))setTimeout(()=>publish(uri,version,version===1?[diagnostic(version,uri)]:[]),30);
  return;
 }
 if(message.method==='textDocument/diagnostic'){
  if(mode==='pull-error')return send({id:message.id,error:{code:-32603,message:'Explicit fixture pull failure'}});
  if(mode==='pull')return send({id:message.id,result:{kind:'full',items:[diagnostic(1,message.params.textDocument.uri)]}});
  return send({id:message.id,error:{code:-32601,message:'Explicit push-only fixture has no pull diagnostic method'}});
 }
 if(message.method==='shutdown')return send({id:message.id,result:null});
 if(message.method==='exit')return process.exit(0);
 if(message.id!==undefined)send({id:message.id,error:{code:-32601,message:'Explicit fixture method is unsupported'}});
}
process.stdin.on('data',chunk=>{buffer=Buffer.concat([buffer,chunk]);for(;;){const at=buffer.indexOf('\r\n\r\n');if(at<0)return;const match=/Content-Length: (\d+)/i.exec(buffer.subarray(0,at).toString());assert.ok(match);const size=Number(match[1]);if(buffer.length<at+4+size)return;const message=JSON.parse(buffer.subarray(at+4,at+4+size));buffer=buffer.subarray(at+4+size);accept(message);}});
