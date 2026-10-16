import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';

const executable=process.argv[2];assert.ok(executable,'Native MCP fallback contract executable is required');
const cases=[
  {name:'empty-404',fallback:true,status:404,contentType:undefined,body:''},
  {name:'plain-405',fallback:true,status:405,contentType:'text/plain',body:'not an MCP endpoint'},
  {name:'unknown-json-error',fallback:true,status:400,contentType:'application/json',error:-32000},
  {name:'modern-method-not-found',fallback:false,status:404,contentType:'application/json',error:-32601},
  {name:'modern-header-error',fallback:false,status:400,contentType:'application/json',error:-32020},
];

for(const item of cases){
  let probeCount=0,initializeCount=0,notificationCount=0,fixtureError;
  const server=createServer(async(request,response)=>{
    try{
      assert.equal(request.method,'POST');assert.equal(request.url,`/${item.name}`);
      let source='';for await(const chunk of request)source+=chunk;
      const message=JSON.parse(source);assert.equal(message.jsonrpc,'2.0');
      if(message.method==='server/discover'){
        probeCount++;assert.equal(probeCount,1);assert.equal(message.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
        if(item.contentType)response.setHeader('Content-Type',item.contentType);
        response.writeHead(item.status);
        if(item.error!==undefined)response.end(JSON.stringify({jsonrpc:'2.0',id:message.id,error:{code:item.error,message:'fixture error'}}));
        else response.end(item.body);
      }else if(message.method==='initialize'){
        initializeCount++;assert.equal(item.fallback,true);assert.equal(request.headers['mcp-protocol-version'],'2025-11-25');assert.equal(message.params.protocolVersion,'2025-11-25');
        response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({jsonrpc:'2.0',id:message.id,result:{protocolVersion:'2025-11-25',capabilities:{},serverInfo:{name:'legacy-fixture',version:'1'}}}));
      }else if(message.method==='notifications/initialized'){
        notificationCount++;assert.equal(item.fallback,true);assert.equal(initializeCount,1);response.writeHead(202);response.end();
      }else throw new Error(`Unexpected method ${message.method}`);
    }catch(error){fixtureError=error;response.writeHead(500);response.end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const endpoint=`http://127.0.0.1:${server.address().port}/${item.name}`;
  const child=spawn(executable,[endpoint,item.fallback?'fallback':'reject'],{stdio:['ignore','pipe','pipe'],windowsHide:true});let stdout='',stderr='';
  child.stdout.setEncoding('utf8').on('data',chunk=>stdout+=chunk);child.stderr.setEncoding('utf8').on('data',chunk=>stderr+=chunk);
  const exit=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',(code,signal)=>resolve({code,signal}));});
  await new Promise(resolve=>server.close(resolve));
  if(fixtureError)throw fixtureError;
  assert.deepEqual(exit,{code:0,signal:null},`${item.name}: ${stderr.slice(0,1000)}`);
  assert.equal(probeCount,1);assert.equal(initializeCount,item.fallback?1:0);assert.equal(notificationCount,item.fallback?1:0);
  process.stdout.write(`${item.name}: ${stdout.trim()}\n`);
}
