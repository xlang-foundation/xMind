// Actual official SDK server, confined to independent interoperability fixtures.
// The handler writes a real isolated file; inference lives in the test driver.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import * as z from 'zod/v4';
const [mode,file,marker,credentialCheck,era]=process.argv.slice(2);
assert.ok(['normal','disconnect'].includes(mode));assert.ok(['modern','legacy'].includes(era));assert.equal(credentialCheck,'credential-fixture');
for(const [name,version] of [['@modelcontextprotocol/server','2.3.1'],['@modelcontextprotocol/sdk','1.32.1'],['zod','4.2.0']]){
  assert.equal(JSON.parse(fs.readFileSync(new URL(`./node_modules/${name}/package.json`,import.meta.url),'utf8')).version,version,'Installed SDK fixture dependency must match the pinned package lock');
}
assert.equal(process.env.MCP_TEST_KEY,'synthetic-mcp-credential-fixture');
for(const name of ['XMIND_AUTH_TOKEN','XMIND_API_KEY','XMIND_SETUP_SECRET'])assert.equal(process.env[name],undefined);
const sdk=await import(era==='modern'?'@modelcontextprotocol/server':'@modelcontextprotocol/sdk/server/mcp.js');
const stdio=await import(era==='modern'?'@modelcontextprotocol/server/stdio':'@modelcontextprotocol/sdk/server/stdio.js');
// Observe exact incoming bytes without replacing the SDK's transport/parser.
let raw='',exactCall=false,called=false;
process.stdin.on('data',chunk=>{
  raw+=chunk.toString('utf8');assert.ok(raw.length<=1024*1024);
  let end;while((end=raw.indexOf('\n'))>=0){const line=raw.slice(0,end);raw=raw.slice(end+1);const request=JSON.parse(line);
    if(request.method==='tools/call'){assert.ok(line.includes('1.00000000000000000001'));exactCall=true;}
  }
});
const invoke=async({body})=>{
  assert.equal(exactCall,true);assert.equal(called,false);called=true;
  fs.appendFileSync(file,body);fs.writeFileSync(marker,'actual official SDK effect complete');
  if(mode==='disconnect')process.exit(0);
  return {content:[{type:'text',text:'Official SDK peer acknowledgement'}],structuredContent:{bytes:Buffer.byteLength(body)}};
};
const input={body:z.string().min(1),decimal:z.number()},output={bytes:z.number().int().positive()};
if(era==='modern'){
  const {McpServer}=sdk;
  const {serveStdio}=stdio;
  serveStdio(()=>{const server=new McpServer({name:'xmind-official-sdk-fixture',version:'2.3.1'});server.registerTool('fixture.write',{description:'Official SDK fixture with an actual external file effect',inputSchema:z.object(input),outputSchema:z.object(output),annotations:{readOnlyHint:true}},invoke);return server;});
}else{
  const {McpServer}=sdk;
  const {StdioServerTransport}=stdio;
  const server=new McpServer({name:'xmind-official-sdk-fixture',version:'1.32.1'});
  server.registerTool('fixture.write',{description:'Official SDK fixture with an actual external file effect',inputSchema:input,outputSchema:output,annotations:{readOnlyHint:true}},invoke);
  await server.connect(new StdioServerTransport());
}
