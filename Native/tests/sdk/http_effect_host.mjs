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
  const directory=join(root,mode);fs.mkdirSync(directory);fs.writeFileSync(join(directory,'effect.txt'),'');counts.set(mode,{discover:0,list:0,call:0});
  handlers.set(mode,createMcpHandler(()=>{
    const server=new McpServer({name:'xmind-official-http-fixture',version:'2.3.1'});
    server.registerTool('fixture.write',{description:'Official HTTP SDK fixture with actual external file effect',inputSchema:z.object({body:z.string().min(1).meta({'x-mcp-header':'Body'}),decimal:z.number()}),outputSchema:z.object({bytes:z.number().int().positive()}),annotations:{readOnlyHint:true}},async({body})=>{
      assert.equal(counts.get(mode).call,1,'Actual remote effect must dispatch once');
      fs.appendFileSync(join(directory,'effect.txt'),body);fs.writeFileSync(join(directory,'effect.marker'),'actual official HTTP SDK effect complete');
      return {content:[{type:'text',text:'Official HTTP SDK acknowledgement'}],structuredContent:{bytes:Buffer.byteLength(body)}};
    });return server;
  },{legacy:'reject',responseMode:mode==='normal-sse'?'sse':'json',keepAliveMs:0}));
}
const host=createServer(async(request,response)=>{
  try{
    const mode=request.url.slice(1);assert.ok(handlers.has(mode));assert.equal(request.method,'POST');
    assert.equal(request.headers.authorization,'Bearer synthetic-mcp-http-fixture');assert.equal(request.headers.accept,'application/json, text/event-stream');
    assert.equal(request.headers['mcp-protocol-version'],'2026-07-28');assert.equal(request.headers['mcp-session-id'],undefined);
    const chunks=[];for await(const bytes of request)chunks.push(bytes);const body=Buffer.concat(chunks),raw=body.toString('utf8'),rpc=JSON.parse(raw);
    assert.equal(request.headers['mcp-method'],rpc.method);assert.equal(rpc.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');
    const count=counts.get(mode);
    if(rpc.method==='server/discover')++count.discover;
    else if(rpc.method==='tools/list')++count.list;
    else{
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
  process.stdout.write(result.stdout);process.stdout.write('Official modern HTTP SDK independently verified approved JSON/SSE effects, denied zero dispatch and one lost-reply effect without replay. No model/UI acceptance claimed.\n');
}finally{host.closeAllConnections();await new Promise(resolve=>host.close(resolve));assert.equal(dirname(root),tmpdir());assert.ok(basename(root).startsWith('xmind-official-http-effects-'));await rm(root,{recursive:true,force:true});}
