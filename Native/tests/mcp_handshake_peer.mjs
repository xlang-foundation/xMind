// Independent lifecycle fixture, not a model/server advertised in the product.
import assert from 'node:assert/strict';
import {createInterface} from 'node:readline';
const mode=process.argv[2],input=createInterface({input:process.stdin});
let probe,initialized=false,acknowledged=false;
const reply=(id,result)=>process.stdout.write(JSON.stringify({jsonrpc:'2.0',id,result})+'\n');
const error=(id,code,data)=>process.stdout.write(JSON.stringify({jsonrpc:'2.0',id,error:{code,message:'Labeled fixture error',...(data?{data}:{})}})+'\n');
for await(const line of input){
  const request=JSON.parse(line);assert.equal(request.jsonrpc,'2.0');
  if(request.method==='server/discover'){
    assert.equal(probe,undefined);probe=request.id;
    assert.equal(request.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    assert.deepEqual(request.params._meta['io.modelcontextprotocol/clientCapabilities'],{});
    if(mode==='legacy-timeout')continue;
    if(mode.startsWith('legacy')){error(request.id,mode==='legacy-invalid-params'?-32602:-32601);continue;}
    if(mode==='modern-unsupported'){error(request.id,-32022,{supported:['2027-01-01'],requested:'2026-07-28'});continue;}
    if(mode==='modern-capability-error'){error(request.id,-32021,{requiredCapabilities:['elicitation']});continue;}
    reply(request.id,{resultType:'complete',supportedVersions:[mode==='modern-no-version'?'2027-01-01':'2026-07-28'],capabilities:{tools:{}},_meta:{'io.modelcontextprotocol/serverInfo':{name:'labeled-lifecycle-peer',version:'fixture'}},instructions:'Untrusted fixture description'});
  }else if(request.method==='initialize'){
    assert.ok(mode.startsWith('legacy'),'Modern identified errors must never downgrade');
    assert.equal(initialized,false);initialized=true;assert.ok(probe);
    assert.equal(request.params.protocolVersion,'2025-11-25');assert.equal(request.params._meta,undefined);
    assert.deepEqual(request.params.capabilities,{});assert.equal(request.params.clientInfo.name,'xMind');
    if(mode==='legacy-timeout')error(probe,-32601); // late retired probe must not consume initialization
    if(mode==='legacy-rejected'){error(request.id,-32602);continue;}
    reply(request.id,{protocolVersion:mode==='legacy-older'?'2025-06-18':mode==='legacy-oldest'?'2024-11-05':mode==='legacy-unsupported'?'1900-01-01':'2025-11-25',capabilities:{tools:{}},serverInfo:{name:'labeled-legacy-peer',version:'fixture'}});
  }else if(request.method==='notifications/initialized'){
    assert.ok(initialized);assert.equal(acknowledged,false);acknowledged=true;assert.equal(request.id,undefined);
  }else if(request.method==='tools/list'){
    if(mode.startsWith('legacy')){assert.ok(acknowledged,'Tool discovery cannot precede the initialized notification');assert.equal(request.params._meta,undefined);reply(request.id,{tools:[]});}
    else{assert.equal(initialized,false);assert.equal(request.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');reply(request.id,{resultType:'complete',tools:[]});}
  }else throw new Error('Unexpected fixture lifecycle method');
}
