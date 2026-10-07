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
let redirected=0,requests=0,plain,tls;
const wire='data: '+JSON.stringify({choices:[{index:0,delta:{content:'transport fixture'},finish_reason:'stop'}]})+'\n\ndata: [DONE]\n\n';
function handler(request,response) {
  requests++;
  if(request.url==='/redirect-target') {redirected++;response.writeHead(500);response.end();return;}
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
  assert.ok(requests>=9,'Protocol cases must reach real native sockets');
  process.stdout.write(result.stdout);
} finally {
  if(plain) {plain.closeAllConnections();await new Promise(resolve=>plain.close(resolve));}
  if(tls) {tls.closeAllConnections();await new Promise(resolve=>tls.close(resolve));}
  await rm(folder,{recursive:true,force:true});
}
