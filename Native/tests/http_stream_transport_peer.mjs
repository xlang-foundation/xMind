// Synthetic HTTP/TLS protocol peer, not a model service or product execution.
import assert from 'node:assert/strict';
import {createServer as httpServer} from 'node:http';
import {createServer as httpsServer} from 'node:https';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const execute=promisify(execFile);
const [executable,openssl]=process.argv.slice(2);
const folder=await mkdtemp(join(tmpdir(),'xmind-transport-'));
let redirected=0,requests=0,plain,tls;const credentialRequests=new Map(),jsonPostRequests=new Map(),mcpRequests=new Map();
const jsonPostBody=String.raw`{"input":"native 🌍","decimal":1.00000000000000000001,"pa\u0074h":"raw"}`;
const jsonPostReply='{"input_tokens":19,"opaque":"synthetic 🌍"}';
const wire='data: '+JSON.stringify({choices:[{index:0,delta:{content:'transport fixture'},finish_reason:'stop'}]})+'\n\ndata: [DONE]\n\n';
function handler(request,response) {
  requests++;
  if(request.url.startsWith('/mcp/')){
    mcpRequests.set(request.url,(mcpRequests.get(request.url)??0)+1);
    assert.equal(request.method,'POST');assert.equal(request.headers.accept,'application/json, text/event-stream');assert.equal(request.headers['content-type'],'application/json');
    assert.equal(request.headers.authorization,'Bearer transport-test-token-not-a-real-key');assert.equal(request.headers['x-extra'],undefined);assert.equal(request.headers['anthropic-version'],undefined);
    const chunks=[];request.on('data',bytes=>chunks.push(bytes));request.on('end',()=>{
      const raw=Buffer.concat(chunks).toString('utf8'),body=JSON.parse(raw),legacy=request.url==='/mcp/legacy',notice=request.url==='/mcp/notification'||request.url==='/mcp/bad-ack';
      assert.equal(body.jsonrpc,'2.0');assert.equal(request.headers['mcp-protocol-version'],legacy?'2025-11-25':'2026-07-28');
      assert.equal(request.headers['mcp-session-id'],legacy?'fixture-session':undefined);
      if(legacy){assert.equal(body.method,'tools/list');assert.equal(request.headers['mcp-method'],undefined);assert.equal(request.headers['mcp-name'],undefined);}
      else {
        assert.equal(body.params._meta['io.modelcontextprotocol/protocolVersion'],'2026-07-28');assert.equal(request.headers['mcp-method'],body.method);
        if(notice){assert.equal(body.id,undefined);assert.equal(body.method,'notifications/progress');assert.equal(request.headers['mcp-name'],undefined);}
        else {
          assert.equal(body.method,'tools/call');assert.equal(body.params.name,'雪');assert.equal(request.headers['mcp-name'],'=?base64?6Zuq?=');
          assert.equal(request.headers['mcp-param-count'],'9007199254740991');assert.equal(request.headers['mcp-param-region'],'=?base64?bGluZTEKbGluZTI=?=');
          assert.ok(raw.includes('"precise":1.00000000000000000001'),'MCP POST must not round-trip raw argument decimals through floating point');
        }
      }
      if(notice){response.writeHead(202);response.end(request.url==='/mcp/bad-ack'?'invalid body':'');return;}
      if(request.url==='/mcp/redirect'){response.writeHead(302,{Location:`http://127.0.0.1:${plain.address().port}/redirect-target`});response.end();return;}
      if(request.url==='/mcp/auth'){response.writeHead(401,{'WWW-Authenticate':'Bearer resource_metadata="http://127.0.0.1/resource"'});response.end();return;}
      if(request.url==='/mcp/error'){response.writeHead(400,{'Content-Type':'application/json'});response.end('{"jsonrpc":"2.0","id":"http-fixture","error":{"code":-32020,"message":"fixture mismatch"}}');return;}
      if(request.url==='/mcp/delay')return;
      const reply='{"jsonrpc":"2.0","id":"http-fixture","result":{"resultType":"complete","content":[{"type":"text","text":"actual 雪 bytes"}]}}';
      const sse=request.url==='/mcp/sse';response.writeHead(200,{'Content-Type':request.url==='/mcp/wrong-media'?'text/plain':sse?'text/event-stream; charset=utf-8':'application/json; charset=utf-8',...(legacy?{'Mcp-Session-Id':'fixture-next-session'}:{})});
      const bytes=Buffer.from(sse?'event: message\ndata: '+reply+'\n\n':reply),split=bytes.indexOf(Buffer.from('雪'))+1;
      response.write(bytes.subarray(0,split));setTimeout(()=>response.end(bytes.subarray(split)),10);
    });return;
  }
  if(request.url.startsWith('/post-json/')){
    jsonPostRequests.set(request.url,(jsonPostRequests.get(request.url)??0)+1);
    assert.equal(request.method,'POST');assert.equal(request.headers.accept,'application/json');assert.equal(request.headers['content-type'],'application/json');assert.equal(request.headers['x-extra'],undefined);
    const mode=request.url.endsWith('/google-key')?'x-goog-api-key':request.url.endsWith('/api-key')||request.url.endsWith('/claude-protocol')?'x-api-key':'authorization';
    for(const header of ['authorization','x-api-key','x-goog-api-key'])assert.equal(request.headers[header],header===mode?(mode==='authorization'?'Bearer ':'')+'transport-test-token-not-a-real-key':undefined,'JSON POST must select exactly one credential placement');
    assert.equal(request.headers['anthropic-version'],request.url.endsWith('/claude-protocol')?'2023-06-01':undefined);
    const chunks=[];request.on('data',bytes=>chunks.push(bytes));request.on('end',()=>{
      assert.equal(Buffer.concat(chunks).toString('utf8'),jsonPostBody,'JSON POST must preserve raw request lexemes and UTF-8 bytes');
      if(request.url.endsWith('/redirect')){response.writeHead(302,{Location:`http://127.0.0.1:${plain.address().port}/redirect-target`});response.end();return;}
      if(request.url.endsWith('/diagnostic')){response.writeHead(400,{'Content-Type':'application/json'});response.end('{"error":{"type":"invalid_request_error","code":"context_length_exceeded","param":"input","message":"private-json-post"}}');return;}
      if(request.url.endsWith('/delay'))return;
      response.writeHead(200,{'Content-Type':request.url.endsWith('/wrong-media')?'text/event-stream':'application/json; charset=utf-8'});
      if(request.url.endsWith('/stall')){response.flushHeaders();return;}
      if(request.url.endsWith('/large')){response.end('{"opaque":"'+'x'.repeat(1024*1024+257)+'"}');return;}
      if(request.url.endsWith('/oversized')){response.end('{"opaque":"'+'x'.repeat(128)+'"}');return;}
      // Split in the middle of a multi-byte code point to check byte-preserving
      // collection independently of transport's read chunk boundaries.
      const bytes=Buffer.from(jsonPostReply),split=bytes.indexOf(Buffer.from('🌍'))+2;response.write(bytes.subarray(0,split));setTimeout(()=>response.end(bytes.subarray(split)),10);
    });return;
  }
  assert.equal(request.headers['anthropic-version'],undefined,'Generic transport must not add Claude protocol headers');
  if(request.url==='/redirect-target') {redirected++;response.writeHead(500);response.end();return;}
  if(request.url.startsWith('/auth/')||request.url.startsWith('/json-auth/')){
    credentialRequests.set(request.url,(credentialRequests.get(request.url)??0)+1);
    const google=request.url.includes('/google-key'),selected=google?'x-goog-api-key':'x-api-key';assert.equal(request.headers[selected],'transport-test-token-not-a-real-key');assert.equal(request.headers[google?'x-api-key':'x-goog-api-key'],undefined);assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-extra'],undefined);
    if(request.url.startsWith('/json-auth/')){assert.equal(request.method,'GET');assert.equal(request.headers.accept,'application/json');response.writeHead(200,{'Content-Type':'application/json'});response.end('{"object":"list","data":[]}');return;}
    assert.equal(request.method,'POST');let body='';request.on('data',bytes=>body+=bytes);request.on('end',()=>{assert.deepEqual(JSON.parse(body),{fixture:'transport'});if(request.url.endsWith('/redirect')){response.writeHead(302,{Location:`http://127.0.0.1:${plain.address().port}/redirect-target`});response.end();return;}response.writeHead(200,{'Content-Type':'text/event-stream'});response.end(wire);});return;
  }
  if(request.url.startsWith('/json')){
    assert.equal(request.method,'GET');assert.equal(request.headers.accept,'application/json');assert.equal(request.headers.authorization,'Bearer transport-test-token-not-a-real-key');
    if(request.url==='/json-redirect'){response.writeHead(302,{Location:`http://127.0.0.1:${plain.address().port}/redirect-target`});response.end();return;}
    if(request.url==='/json-delay')return;
    response.writeHead(200,{'Content-Type':request.url==='/json-wrong-media'?'text/plain':'application/json; charset=utf-8'});
    response.end(request.url==='/json-oversized'?'x'.repeat(1024*1024+1):'{"object":"list","data":[]}');return;
  }
  assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer transport-test-token-not-a-real-key');
  let body='';request.on('data',data=>{body+=data;});
  request.on('end',()=>{
    assert.deepEqual(JSON.parse(body),{fixture:'transport'});
    if(request.url==='/error') {response.writeHead(429,{'Content-Type':'application/json'});response.end(JSON.stringify({error:'do-not-log'}));return;}
    if(request.url.startsWith('/diagnostic')) {
      response.writeHead(400,{'Content-Type':'application/json'});
      if(request.url==='/diagnostic-stall'){response.flushHeaders();return;}
      response.end(request.url==='/diagnostic-large'?'x'.repeat(32769):request.url==='/diagnostic-malformed'?'{':JSON.stringify({error:{type:'invalid_request_error',code:'unsupported_parameter',param:request.url==='/diagnostic-private'?'private-fixture-key':'n',message:'do-not-log private-fixture-key'}}));return;
    }
    if(request.url==='/redirect') {response.writeHead(302,{Location:`http://127.0.0.1:${plain.address().port}/redirect-target`});response.end();return;}
    if(request.url==='/wrong-media') {response.writeHead(200,{'Content-Type':'application/json'});response.end('{}');return;}
    if(request.url==='/delay') return;
    response.writeHead(200,{'Content-Type':'Text/Event-Stream ; charset=utf-8'});response.flushHeaders();
    if(request.url==='/stall') return;
    let cursor=0;
    const timer=setInterval(()=>{
      if(cursor<wire.length) {response.write(wire.slice(cursor,cursor+7));cursor+=7;}
      else {clearInterval(timer);response.end();}
    },2);
    response.on('close',()=>clearInterval(timer));
  });
}
try {
  await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(folder,'key.pem'),'-out',join(folder,'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost,IP:127.0.0.1'],{windowsHide:true});
  plain=httpServer(handler);tls=httpsServer({key:await readFile(join(folder,'key.pem')),cert:await readFile(join(folder,'cert.pem'))},handler);
  tls.on('tlsClientError',()=>{});
  await Promise.all([new Promise(resolve=>plain.listen(0,'127.0.0.1',resolve)),new Promise(resolve=>tls.listen(0,'127.0.0.1',resolve))]);
  const result=await execute(executable,[`http://127.0.0.1:${plain.address().port}`,`https://127.0.0.1:${tls.address().port}`],{timeout:20000,windowsHide:true});
  assert.equal(redirected,0,'Credentials must not be forwarded by a followed redirect');
  assert.deepEqual(Object.fromEntries(mcpRequests),{'/mcp/json':1,'/mcp/sse':1,'/mcp/error':1,'/mcp/auth':1,'/mcp/legacy':1,'/mcp/notification':1,'/mcp/bad-ack':1,'/mcp/redirect':1,'/mcp/wrong-media':1,'/mcp/delay':2,'/mcp/after-failure':1},'Native MCP POST requests must reach sockets exactly once; invalid metadata/pre-cancel/TLS must not reach a peer');
  assert.deepEqual(Object.fromEntries(credentialRequests),{'/auth/api-key':1,'/json-auth/api-key':1,'/auth/api-key/redirect':1,'/auth/google-key':1,'/json-auth/google-key':1,'/auth/google-key/redirect':1},'Only selected-header requests reach the wire; missing/injected credentials are rejected before sending');
  assert.deepEqual(Object.fromEntries(jsonPostRequests),{'/post-json/bearer':1,'/post-json/api-key':1,'/post-json/google-key':1,'/post-json/claude-protocol':1,'/post-json/boundary':2,'/post-json/large':1,'/post-json/oversized':1,'/post-json/wrong-media':1,'/post-json/redirect':1,'/post-json/diagnostic':1,'/post-json/delay':2,'/post-json/stall':1,'/post-json/after-failure':1},'Exact native JSON POST requests reach the wire once; invalid requests, TLS failure and pre-cancellation never dispatch');
  assert.ok(requests>=9,'Protocol cases must reach real native sockets');
  process.stdout.write(result.stdout);
} finally {
  if(plain) {plain.closeAllConnections();await new Promise(resolve=>plain.close(resolve));}
  if(tls) {tls.closeAllConnections();await new Promise(resolve=>tls.close(resolve));}
  await rm(folder,{recursive:true,force:true});
}
