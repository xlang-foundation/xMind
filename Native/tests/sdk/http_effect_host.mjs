// Actual official modern HTTP SDK; isolated real peer file effects, no model.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,basename} from 'node:path';
import {McpServer,createMcpHandler} from '@modelcontextprotocol/server';
import * as z from 'zod/v4';
const [executable,...nativeArgs]=process.argv.slice(2),execute=promisify(execFile);
for(const [name,version]of [['@modelcontextprotocol/server','2.3.1'],['zod','4.2.0']])assert.equal(JSON.parse(fs.readFileSync(new URL(`./node_modules/${name}/package.json`,import.meta.url))).version,version);
const root=await mkdtemp(join(tmpdir(),'xmind-official-http-effects-')),handlers=new Map(),counts=new Map(),failures=[];
const modes=['normal-json','normal-sse','deny','disconnect'];
for(const mode of modes){
  const directory=join(root,mode);fs.mkdirSync(directory);fs.writeFileSync(join(directory,'effect.txt'),'');counts.set(mode,{discover:0,list:0,call:0,resourceList:0,resourceRead:0,promptList:0,promptGet:0});
  handlers.set(mode,createMcpHandler(()=>{
    const server=new McpServer({name:'xmind-official-http-fixture',version:'2.3.1'});
    server.registerResource('fixture.readme','fixture://official/readme',{description:'Official SDK read-only resource',mimeType:'text/plain'},async(uri)=>{
      ++counts.get(mode).resourceRead;return {contents:[{uri:uri.href,mimeType:'text/plain',text:'Official HTTP resource body'}]};
    });
    server.registerPrompt('fixture.prompt',{description:'Official SDK prompt fixture',argsSchema:{topic:z.string()}},async({topic})=>{
      ++counts.get(mode).promptGet;return {messages:[{role:'user',content:{type:'text',text:`Official HTTP prompt for ${topic}`}}]};
    });
    server.registerTool('fixture.write',{description:'Official HTTP SDK fixture with actual external file effect',inputSchema:z.object({body:z.string().min(1).meta({'x-mcp-header':'Body'}),decimal:z.number()}),outputSchema:z.object({bytes:z.number().int().positive()}),annotations:{readOnlyHint:true}},async({body})=>{
      assert.equal(counts.get(mode).call,1,'Actual remote effect must dispatch once');
      fs.appendFileSync(join(directory,'effect.txt'),body);fs.writeFileSync(join(directory,'effect.marker'),'actual official HTTP SDK effect complete');
      return {content:[{type:'text',text:'Official HTTP SDK acknowledgement'}],structuredContent:{bytes:Buffer.byteLength(body)}};
    });return server;
  },{legacy:'reject',responseMode:mode==='normal-sse'?'sse':'json',keepAliveMs:0}));
}
const legacyHttpEffect=join(root,'legacy-peer-elicitation');fs.mkdirSync(legacyHttpEffect);fs.writeFileSync(join(legacyHttpEffect,'effect.txt'),'');
let legacyReply;
const host=createServer(async(request,response)=>{
  try{
    const mode=request.url.slice(1);assert.equal(request.method,'POST');
    assert.equal(request.headers.authorization,'Bearer synthetic-mcp-http-fixture');assert.equal(request.headers.accept,'application/json, text/event-stream');
    const chunks=[];for await(const bytes of request)chunks.push(bytes);const body=Buffer.concat(chunks),raw=body.toString('utf8'),rpc=JSON.parse(raw);
    if(mode==='legacy-peer-elicitation'){
      if(rpc.method==='server/discover'){
        assert.equal(request.headers['mcp-protocol-version'],'2026-07-28');assert.equal(request.headers['mcp-session-id'],undefined);response.writeHead(404);response.end();return;
      }
      assert.equal(request.headers['mcp-protocol-version'],'2025-11-25');
      if(rpc.method==='initialize'){
        assert.equal(request.headers['mcp-session-id'],undefined);assert.deepEqual(rpc.params.capabilities.elicitation,{form:{}});
        response.writeHead(200,{'Content-Type':'application/json','Mcp-Session-Id':'legacy-http-session'});
        response.end(JSON.stringify({jsonrpc:'2.0',id:rpc.id,result:{protocolVersion:'2025-11-25',capabilities:{tools:{}},serverInfo:{name:'legacy-http-peer',version:'fixture'}}}));return;
      }
      assert.equal(request.headers['mcp-session-id'],'legacy-http-session');
      if(rpc.method==='notifications/initialized'){response.writeHead(202);response.end();return;}
      if(rpc.method==='tools/list'){
        response.writeHead(200,{'Content-Type':'application/json'});
        response.end(JSON.stringify({jsonrpc:'2.0',id:rpc.id,result:{tools:[{name:'fixture.write',description:'Legacy HTTP peer effect',inputSchema:{type:'object',properties:{body:{type:'string',minLength:1},decimal:{type:'number'}},required:['body','decimal'],additionalProperties:false},outputSchema:{type:'object',properties:{bytes:{type:'integer',minimum:1}},required:['bytes'],additionalProperties:false}}]}}));return;
      }
      if(!Object.hasOwn(rpc,'method')){
        assert.equal(rpc.id,'peer-elicitation-http-1');assert.deepEqual(rpc.result,{action:'accept',content:{approved:true}});
        assert.ok(legacyReply);legacyReply();response.writeHead(202);response.end();return;
      }
      assert.equal(rpc.method,'tools/call');assert.equal(rpc.params.name,'fixture.write');assert.ok(raw.includes('1.00000000000000000001'));
      const event=value=>`data: ${JSON.stringify(value)}\r\n\r\n`;
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.write(event({jsonrpc:'2.0',id:'peer-elicitation-http-1',method:'elicitation/create',params:{mode:'form',message:'Approve the HTTP fixture effect',requestedSchema:{type:'object',properties:{approved:{type:'boolean'}},required:['approved'],additionalProperties:false}}}));
      await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Legacy HTTP peer reply timed out')),10000);legacyReply=()=>{clearTimeout(timer);legacyReply=undefined;resolve();};response.on('close',()=>{clearTimeout(timer);reject(new Error('Legacy HTTP tool stream closed before the peer reply'));});});
      fs.appendFileSync(join(legacyHttpEffect,'effect.txt'),rpc.params.arguments.body);
      response.end(event({jsonrpc:'2.0',id:rpc.id,result:{content:[{type:'text',text:'Legacy HTTP peer acknowledged'}],structuredContent:{bytes:Buffer.byteLength(rpc.params.arguments.body)}}}));return;
    }
    assert.ok(handlers.has(mode));assert.equal(request.headers['mcp-protocol-version'],'2026-07-28');assert.equal(request.headers['mcp-session-id'],undefined);
    assert.equal(request.headers['mcp-method'],rpc.method);assert.equal(rpc.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    const count=counts.get(mode);
    if(rpc.method==='server/discover')++count.discover;
    else if(rpc.method==='tools/list')++count.list;
    else if(rpc.method==='resources/list')++count.resourceList;
    else if(rpc.method==='prompts/list')++count.promptList;
    else if(rpc.method==='resources/read'){
      assert.equal(rpc.params.uri,'fixture://official/readme');
    }else if(rpc.method==='prompts/get'){
      assert.equal(rpc.params.name,'fixture.prompt');assert.deepEqual(rpc.params.arguments,{topic:'AgentFlow'});
    }else{
      assert.equal(rpc.method,'tools/call');++count.call;assert.ok(raw.includes('1.00000000000000000001'));
      assert.equal(request.headers['mcp-name'],'fixture.write');assert.equal(request.headers['mcp-param-body'],'=?base64?'+Buffer.from(rpc.params.arguments.body).toString('base64')+'?=');
    }
    const abort=new AbortController();response.on('close',()=>{if(!response.writableEnded)abort.abort();});
    const url=`http://127.0.0.1:${host.address().port}${request.url}`;
    const result=await handlers.get(mode).fetch(new Request(url,{method:'POST',headers:request.headers,body,signal:abort.signal}));
    if(mode==='disconnect' && rpc.method==='tools/call'){assert.ok(fs.existsSync(join(root,mode,'effect.marker')));response.destroy();return;}
    response.writeHead(result.status,Object.fromEntries(result.headers));if(result.body){for await(const bytes of result.body)response.write(bytes);}response.end();
  }catch(error){failures.push(error);response.destroy();}
});
try{
  await new Promise(resolve=>host.listen(0,'127.0.0.1',resolve));
  const result=await execute(executable,[...nativeArgs,`http://127.0.0.1:${host.address().port}`,root],{windowsHide:true,timeout:55000,maxBuffer:2*1024*1024});assert.deepEqual(failures,[]);
  for(const mode of modes){
    assert.equal(fs.readFileSync(join(root,mode,'effect.txt'),'utf8'),mode==='deny'?'':'actual native HTTP MCP effect 雪\n');
    assert.equal(counts.get(mode).call,mode==='deny'?0:1,'Denied/duplicate/restarted operations must not replay remote effects');assert.ok(counts.get(mode).discover>=1 && counts.get(mode).list>=1);
  }
  assert.equal(counts.get('normal-json').resourceRead,1,'One explicitly approved official HTTP resource read must reach the SDK peer');
  assert.equal(counts.get('normal-json').promptGet,1,'One explicitly approved official HTTP prompt retrieval must reach the SDK peer');
  assert.equal(counts.get('normal-json').resourceList,5,'Each fresh official HTTP registry must discover actual SDK resources');
  assert.equal(counts.get('normal-json').promptList,5,'Each fresh official HTTP registry must discover actual SDK prompts');
  for(const mode of modes.slice(1))assert.equal(counts.get(mode).resourceRead+counts.get(mode).promptGet,0,'Other HTTP SDK modes must have no unapproved read side effects');
  process.stdout.write(result.stdout);process.stdout.write('Official modern HTTP SDK independently verified approved JSON/SSE effects, denied zero dispatch and one lost-reply effect without replay. No model/UI acceptance claimed.\n');
}finally{host.closeAllConnections();await new Promise(resolve=>host.close(resolve));assert.equal(dirname(root),tmpdir());assert.ok(basename(root).startsWith('xmind-official-http-effects-'));await rm(root,{recursive:true,force:true});}
