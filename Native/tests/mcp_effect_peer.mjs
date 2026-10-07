// Independent real MCP subprocess fixture. The tool actually appends to an
// isolated fixture file. Its readOnlyHint is deliberately false information.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createInterface} from 'node:readline';
const [mode,file,marker]=process.argv.slice(2),input=createInterface({input:process.stdin});
let initialized=false,ack=false,called=false,callId,cancelled=false;
const send=value=>process.stdout.write(JSON.stringify({jsonrpc:'2.0',...value})+'\n');
const tool={name:'fixture.write',description:'Labeled external file effect despite untrusted read-only hint',inputSchema:{type:'object',properties:{body:{type:'string',minLength:1},decimal:{type:'number'}},required:['body','decimal'],additionalProperties:false},outputSchema:{type:'object',properties:{bytes:{type:'integer',minimum:1}},required:['bytes'],additionalProperties:false},annotations:{readOnlyHint:true}};
for await(const line of input){
  const request=JSON.parse(line);assert.equal(request.jsonrpc,'2.0');
  if(request.method==='server/discover'){
    if(mode==='legacy')send({id:request.id,error:{code:-32601,message:'Labeled legacy peer'}});
    else send({id:request.id,result:{supportedVersions:['2026-07-28'],capabilities:{tools:{}}}});
  }else if(request.method==='initialize'){
    assert.equal(mode,'legacy');initialized=true;send({id:request.id,result:{protocolVersion:'2025-11-25',capabilities:{tools:{}},serverInfo:{name:'labeled-effect-peer',version:'fixture'}}});
  }else if(request.method==='notifications/initialized'){assert.ok(initialized);ack=true;}
  else if(request.method==='tools/list'){
    if(mode==='legacy')assert.ok(ack);
    if(mode==='bad-catalog-schema')send({id:request.id,result:{tools:[{...tool,inputSchema:{type:'object',properties:{body:{minLength:-1}}}}]}});
    else if(mode==='duplicate-page')send({id:request.id,result:{tools:[tool],...(request.params.cursor?{}:{nextCursor:'second-page'})}});
    else if(mode==='cursor-cycle')send({id:request.id,result:{tools:[],nextCursor:'again'}});
    else send({id:request.id,result:{tools:[tool]}});
  }else if(request.method==='tools/call'){
    assert.equal(called,false,'Native operation must dispatch only once');called=true;callId=request.id;
    assert.equal(request.params.name,'fixture.write');
    assert.ok(line.includes('1.00000000000000000001'),'Exact approved decimal token must reach the actual peer');
    if(mode==='legacy')assert.ok(ack);else assert.equal(request.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    fs.appendFileSync(file,request.params.arguments.body);fs.writeFileSync(marker,'actual effect complete');
    if(mode==='disconnect')process.exit(0);
    if(mode==='timeout' || mode==='cancel')continue;
    if(mode==='rpc-error'){send({id:request.id,error:{code:-32603,message:'Labeled error after actual effect'}});continue;}
    send({id:request.id,result:{resultType:'complete',content:[{type:'text',text:'Labeled peer acknowledgement'}],structuredContent:{bytes:mode==='bad-output'?'invalid':Buffer.byteLength(request.params.arguments.body)},...(mode==='tool-error'?{isError:true}:{})}});
  }else if(request.method==='notifications/cancelled'){assert.equal(request.params.requestId,callId);cancelled=true;}
  else throw new Error('Unexpected fixture effect method');
}
if(called && (mode==='timeout' || mode==='cancel'))assert.ok(cancelled,'Owner must attempt cancellation after interrupted dispatched effect');
