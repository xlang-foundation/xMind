// Independent real pipe fixture. No model, production MCP configuration or
// tool effects. Catalog descriptions intentionally contain untrusted hints.
import assert from 'node:assert/strict';
import {createInterface} from 'node:readline';
const mode=process.argv[2], input=createInterface({input:process.stdin});
let initialized=false,ack=false,listing,ping=false,unsupported=false,pages=0,cancelled=false;
const send=value=>process.stdout.write(JSON.stringify({jsonrpc:'2.0',...value})+'\n');
const tool={name:'fixture.read',description:'Untrusted labeled fixture description',inputSchema:{type:'object',properties:{path:{type:'string'}},required:['path']},annotations:{readOnlyHint:true}};
function page(request){
    pages++;
    listing=request.id;
  if(mode==='legacy'){
    assert.equal(pages,1);listing=request.id;
    send({id:'peer-ping',method:'ping'});
    send({id:-17,method:'sampling/createMessage',params:{messages:[]}});return;
  }
  if(mode==='closed'){input.close();process.exit(0);}
  if(mode==='timeout' || mode==='cancel'){listing=request.id;return;}
  if(mode==='modern-request'){send({id:'forbidden-modern-ping',method:'ping'});return;}
  if(mode==='flood'){for(let i=0;i<300;i++)send({method:'notifications/message',params:{level:'debug',data:'labeled fixture'}});return;}
  if(mode==='unknown-id'){send({id:'never-issued',result:{tools:[]}});return;}
  if(mode==='duplicate'){send({id:request.id,result:{tools:[tool,tool]}});return;}
  if(mode==='bad-schema'){send({id:request.id,result:{tools:[{...tool,inputSchema:[]}]}});return;}
  if(mode==='modern-input-required'){send({id:request.id,result:{resultType:'input_required',tools:[]}});return;}
  assert.equal(mode,'modern');
  if(pages===1){assert.equal(request.params.cursor,undefined);send({id:request.id,result:{resultType:'complete',tools:[tool],nextCursor:'opaque-page-2'}});}
  else{assert.equal(pages,2);assert.equal(request.params.cursor,'opaque-page-2');send({id:request.id,result:{resultType:'complete',tools:[]}});}
}
for await(const line of input){
  const request=JSON.parse(line);assert.equal(request.jsonrpc,'2.0');
  if(request.method===undefined){
    assert.equal(mode,'legacy');assert.ok(listing && ack);
    if(request.id==='peer-ping'){assert.equal(ping,false);assert.deepEqual(request.result,{});ping=true;}
    else{assert.equal(request.id,-17);assert.equal(unsupported,false);assert.equal(request.error.code,-32601);unsupported=true;}
    if(ping && unsupported)send({id:listing,result:{tools:[tool]}});continue;
  }
  if(request.method==='server/discover'){
    assert.equal(request.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    assert.deepEqual(request.params._meta['io.modelcontextprotocol/clientCapabilities'],{});
    if(mode==='legacy')send({id:request.id,error:{code:-32601,message:'Labeled legacy fixture'}});
    else send({id:request.id,result:{supportedVersions:['2026-07-28'],capabilities:{tools:{}}}});
  }else if(request.method==='initialize'){
    assert.equal(mode,'legacy');assert.equal(initialized,false);initialized=true;
    send({id:request.id,result:{protocolVersion:'2025-11-25',capabilities:{tools:{}},serverInfo:{name:'labeled-client-peer',version:'fixture'}}});
  }else if(request.method==='notifications/initialized'){
    assert.equal(mode,'legacy');assert.ok(initialized);assert.equal(ack,false);ack=true;
  }else if(request.method==='tools/list'){
    if(mode==='legacy'){assert.ok(ack);assert.equal(request.params._meta,undefined);}
    else assert.equal(request.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    page(request);
  }else if(request.method==='notifications/cancelled'){
    assert.ok(mode==='timeout' || mode==='cancel' || mode==='modern-request' || mode==='flood' || mode==='unknown-id');
    assert.equal(request.params.requestId,listing);cancelled=true;
  }else throw new Error('Unexpected client fixture method');
}
if(mode==='timeout' || mode==='cancel')assert.ok(cancelled,'Interrupted owner must attempt cancellation before closing input');
