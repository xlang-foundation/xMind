// Local Streamable HTTP MCP peer for native resource/prompt transport testing.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';

const executable=process.argv[2];assert.ok(executable,'Native HTTP client executable is required');
const methods=[];let resourcePages=0,subscriptionRequests=0;
const server=createServer(async(request,response)=>{
  try{
    assert.equal(request.method,'POST');assert.equal(request.url,'/mcp');
    let body='';for await(const chunk of request)body+=chunk;
    const message=JSON.parse(body);assert.equal(message.jsonrpc,'2.0');
    if(!Object.hasOwn(message,'id')){
      assert.equal(message.method,'notifications/initialized');response.writeHead(202);response.end();return;
    }
    methods.push(message.method);
    let result;
    if(message.method==='server/discover'){
      assert.equal(message.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
      result={supportedVersions:['2026-07-28'],capabilities:{resources:{subscribe:true,listChanged:true},prompts:{listChanged:true},tools:{listChanged:true}},_meta:{'io.modelcontextprotocol/serverInfo':{name:'fixture-http-peer',version:'1'}}};
    }else if(message.method==='resources/list'){
      resourcePages++;
      if(resourcePages===1){assert.equal(message.params.cursor,undefined);result={resultType:'complete',resources:[{uri:'fixture://http/readme',name:'http.readme',mimeType:'text/plain'}],nextCursor:'http-resource-page-2'};}
      else{assert.equal(resourcePages,2);assert.equal(message.params.cursor,'http-resource-page-2');result={resultType:'complete',resources:[]};}
    }else if(message.method==='resources/templates/list'){
      assert.equal(message.params.cursor,undefined);result={resultType:'complete',resourceTemplates:[{name:'http.template',uriTemplate:'https://fixture/{id}',description:'HTTP template metadata'}]};
    }else if(message.method==='resources/templates/list'){
      assert.equal(message.params.cursor,undefined);result={resultType:'complete',resourceTemplates:[{name:'http.template',uriTemplate:'https://fixture/{id}',description:'HTTP template metadata'}]};
    }else if(message.method==='resources/read'){
      assert.equal(message.params.uri,'fixture://http/readme');result={resultType:'complete',contents:[{uri:message.params.uri,mimeType:'text/plain',text:'HTTP resource body'}]};
    }else if(message.method==='prompts/list'){
      assert.equal(message.params.cursor,undefined);result={resultType:'complete',prompts:[{name:'fixture.http.prompt',arguments:[{name:'topic',required:true}]}]};
    }else if(message.method==='prompts/get'){
      assert.equal(message.params.name,'fixture.http.prompt');assert.deepEqual(message.params.arguments,{topic:'AgentFlow'});result={resultType:'complete',messages:[{role:'user',content:{type:'text',text:'HTTP prompt for AgentFlow'}}]};
    }else if(message.method==='subscriptions/listen'){
      subscriptionRequests++;
      assert.equal(request.headers['mcp-method'],'subscriptions/listen');
      response.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache'});
      const id=message.id,meta={'io.modelcontextprotocol/subscriptionId':id};
      const event=value=>`data: ${JSON.stringify(value)}\r\n\r\n`;
      if(subscriptionRequests===1){
        assert.deepEqual(message.params.notifications,{resourceSubscriptions:['fixture://http/readme'],toolsListChanged:true});
        response.write(event({jsonrpc:'2.0',method:'notifications/subscriptions/acknowledged',params:{_meta:meta,notifications:{resourceSubscriptions:['fixture://http/readme'],toolsListChanged:true}}}));
        response.write(event({jsonrpc:'2.0',method:'notifications/resources/updated',params:{_meta:meta,uri:'fixture://http/readme'}}));
        response.write(event({jsonrpc:'2.0',method:'notifications/tools/list_changed',params:{_meta:meta}}));
      }else{
        assert.equal(subscriptionRequests,2);assert.deepEqual(message.params.notifications,{resourceSubscriptions:['fixture://http/readme']});
        response.write(event({jsonrpc:'2.0',method:'notifications/subscriptions/acknowledged',params:{_meta:meta,notifications:{resourceSubscriptions:['fixture://http/readme']}}}));
        response.write(event({jsonrpc:'2.0',method:'notifications/prompts/list_changed',params:{_meta:meta}}));
      }
      response.end(event({jsonrpc:'2.0',id,result:{resultType:'complete',_meta:meta}}));return;
    }else throw new Error(`Unexpected MCP method ${message.method}`);
    response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({jsonrpc:'2.0',id:message.id,result}));
  }catch(error){response.writeHead(500,{'Content-Type':'text/plain'});response.end('fixture assertion failed');server.emit('fixtureError',error);}
});
let fixtureError;
server.on('fixtureError',error=>{fixtureError=error;});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const endpoint=`http://127.0.0.1:${server.address().port}/mcp`;
const child=spawn(executable,[endpoint],{stdio:['ignore','pipe','pipe']});let stdout='',stderr='';
child.stdout.setEncoding('utf8').on('data',chunk=>stdout+=chunk);child.stderr.setEncoding('utf8').on('data',chunk=>stderr+=chunk);
const exit=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',(code,signal)=>resolve({code,signal}));});
await new Promise(resolve=>server.close(resolve));
if(fixtureError)throw fixtureError;
assert.deepEqual(exit,{code:0,signal:null},`Native HTTP client failed: ${stderr.slice(0,2000)}`);
assert.deepEqual(methods,['server/discover','resources/list','resources/list','resources/templates/list','resources/read','prompts/list','prompts/get','subscriptions/listen','server/discover','subscriptions/listen']);
process.stdout.write(stdout);
