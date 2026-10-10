import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {once} from 'node:events';
import {createServer} from 'node:https';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {createBrowserServer} from '../../views/browser/server.mjs';
const require=createRequire(import.meta.url),{BrowserController,BrowserSessionClient}=require('../../views/browser/browser.js');
const [executable,modules,stdlib,openssl]=process.argv.slice(2),execute=promisify(execFile);
const directory=await mkdtemp(join(tmpdir(),'xmind-oauth-service-'));let tls,child,browserView,browserController,httpRequests=0,tlsObservations=0;
try{
  await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(directory,'key.pem'),'-out',join(directory,'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1'],{windowsHide:true});
  tls=createServer({key:await readFile(join(directory,'key.pem')),cert:await readFile(join(directory,'cert.pem'))},(request,response)=>{httpRequests++;response.writeHead(500);response.end();});
  // Schannel may finish the handshake and reject the certificate before HTTP,
  // or abort during it. Observe both real TLS paths rather than assuming a
  // Node tlsClientError is emitted for every native certificate rejection.
  tls.on('tlsClientError',()=>{tlsObservations++;});tls.on('secureConnection',()=>{tlsObservations++;});
  await new Promise(resolve=>tls.listen(0,'127.0.0.1',resolve));
  child=spawn(executable,[directory,modules,stdlib,`https://127.0.0.1:${tls.address().port}`],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let stdout='',stderr='',settled=false;let resolveLine,rejectLine;
  const firstLine=new Promise((resolve,reject)=>{resolveLine=resolve;rejectLine=reject;});
  child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',text=>{stdout+=text;if(!settled&&stdout.includes('\n')){settled=true;try{resolveLine(JSON.parse(stdout.slice(0,stdout.indexOf('\n'))));}catch(error){rejectLine(error);}}});child.stderr.on('data',text=>{stderr+=text;});child.on('error',rejectLine);
  const exited=once(child,'exit');child.on('exit',()=>{if(!settled)rejectLine(new Error(stderr||'Native service exited before its listener'));});
  const timer=setTimeout(()=>child.kill(),25000);
  try{
    const {port}=await firstLine,base=`http://127.0.0.1:${port}`,owner={Authorization:'Bearer synthetic-oauth-service-owner-token'},origin=`http://127.0.0.1:${port}`;
    const call=async(path,{method='GET',body,headers=owner}={})=>{const result=await fetch(base+path,{method,headers:{...headers,...(body===undefined?{}:{'Content-Type':'application/json'})},...(body===undefined?{}:{body:typeof body==='string'?body:JSON.stringify(body)}),signal:AbortSignal.timeout(5000)});return {status:result.status,value:await result.json()};};
    const root='/v1/mcp/authorization',request={server_id:'oauth.peer',expected_config_revision:1,expected_credential_revision:0,request_id:'native-attempt'};
    assert.equal((await call(root+'/servers',{headers:{}})).status,401);assert.equal((await call(root+'/attempts',{method:'POST',body:request,headers:{}})).status,401);
    assert.equal((await call(root+'/servers',{headers:{...owner,Origin:'https://untrusted.example.test'}})).status,403);
    const servers=await call(root+'/servers');assert.equal(servers.status,200);assert.equal(servers.value.servers.find(item=>item.id==='oauth.peer').state,'needs_login');assert.equal(servers.value.servers.find(item=>item.id==='authorized-peer').state,'authorized');
    assert.ok(!JSON.stringify(servers.value).includes('synthetic-access'));assert.ok(!JSON.stringify(servers.value).includes('issuer.example'));
    const enrolled=await call('/v1/view-sessions',{method:'POST',body:{origin}});assert.equal(enrolled.status,200);const view={Authorization:'View '+enrolled.value.credential,'X-XMind-View-Origin':origin};
    assert.equal((await call(root+'/servers',{headers:view})).status,200);assert.equal((await call(root+'/servers',{headers:{Authorization:view.Authorization}})).status,401);
    assert.equal((await call(root+'/attempts',{method:'POST',body:{...request,expected_config_revision:2},headers:view})).status,409);
    assert.equal((await call(root+'/attempts',{method:'POST',body:{...request,endpoint:'https://attacker.example.test'},headers:view})).status,400);
    assert.equal((await call(root+'/attempts',{method:'POST',body:'{"server_id":"oauth.peer","server_id":"oauth.peer"}',headers:view})).status,400);
    assert.equal((await call(root+'/attempts',{method:'POST',body:{...request,expected_credential_revision:-1},headers:view})).status,400);
    assert.equal((await call(root+'/attempts',{method:'POST',body:{...request,server_id:'authorized-peer',expected_credential_revision:1,request_id:'already-connected'},headers:view})).status,409);
    const start=await call(root+'/attempts',{method:'POST',body:request,headers:view});assert.equal(start.status,202);assert.equal(start.value.state,'discovering');
    const duplicate=await call(root+'/attempts',{method:'POST',body:request,headers:view});assert.equal(duplicate.status,202);assert.equal(duplicate.value.id,start.value.id);
    assert.equal((await call(root+'/attempts',{method:'POST',body:{...request,expected_config_revision:2},headers:view})).status,409);
    const deadline=Date.now()+5000;let status;
    do{status=await call(root+'/attempts/native-attempt',{headers:view});assert.equal(status.status,200);if(status.value.state==='failed')break;assert.ok(Date.now()<deadline,'Actual TLS-negative authorization must settle');await new Promise(resolve=>setTimeout(resolve,10));}while(true);
    assert.equal(status.value.reason,'authorization_failed');assert.equal(status.value.authorization_url,null);assert.equal(status.value.credential_revision,0);
    assert.equal((await call(root+'/attempts/native-attempt?extra=1',{headers:view})).status,400);
    const cancelled=await call(root+'/attempts/native-attempt/cancel',{method:'POST',body:{},headers:view});assert.equal(cancelled.status,202);assert.equal(cancelled.value.state,'failed','Cancelling terminal failure must not invent another outcome');
    assert.equal((await call(root+'/attempts/unknown',{headers:view})).status,404);
    const cancellation=await call(root+'/attempts',{method:'POST',body:{...request,request_id:'cancel-attempt'},headers:view});assert.equal(cancellation.status,202);
    const cancel=await call(root+'/attempts/cancel-attempt/cancel',{method:'POST',body:{},headers:view});assert.equal(cancel.status,202);
    const cancellationDeadline=Date.now()+5000;let final;
    do{final=await call(root+'/attempts/cancel-attempt',{headers:view});if(['cancelled','failed'].includes(final.value.state))break;assert.ok(Date.now()<cancellationDeadline);await new Promise(resolve=>setTimeout(resolve,10));}while(true);
    // Actual production browser adapter, shared client and controller against
    // the native service. Only browser-platform cookie attachment is supplied
    // by this Node fixture; no domain responses or execution are fabricated.
    const assets=join(directory,'view-assets');await execute(process.execPath,[fileURLToPath(new URL('../../views/browser/build.mjs',import.meta.url)),assets],{windowsHide:true});
    browserView=await createBrowserServer({backend:base,assetRoot:assets});const browserOrigin=await browserView.listen();
    const browserEnrollment=await fetch(browserOrigin+'/ui/session',{method:'POST',headers:{...owner,Origin:browserOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'},body:'{}'});assert.equal(browserEnrollment.status,200);const cookie=browserEnrollment.headers.get('set-cookie').split(';')[0];
    const html=await (await fetch(browserOrigin+'/ui/')).text();assert.match(html,/id="mcp-settings"/);assert.match(html,/id="mcp-refresh"/);
    const browserRequests=[],observations=[],saved=[];
    const browserClient=new BrowserSessionClient(browserOrigin,{fetchImpl:(url,options)=>{browserRequests.push({path:new URL(url).pathname,method:options.method});return fetch(url,{...options,headers:{...options.headers,Cookie:cookie,'Sec-Fetch-Site':'same-origin',...(options.method==='POST'?{Origin:browserOrigin}:{})}});}});
    browserController=new BrowserController(browserClient,message=>observations.push(message),{save:value=>saved.push(value)});
    await browserController.command({type:'mcp-refresh'});assert.equal(observations.at(-1).available,true);assert.equal(observations.at(-1).servers.find(value=>value.id==='authorized-peer').state,'authorized');
    await browserController.command({type:'mcp-start',server:'oauth.peer'});assert.equal(browserRequests.filter(value=>value.method==='POST').length,1);
    const browserDeadline=Date.now()+5000;do{await browserController.command({type:'mcp-refresh'});if(observations.at(-1).attempts[0]?.state==='failed')break;assert.ok(Date.now()<browserDeadline);await new Promise(resolve=>setTimeout(resolve,10));}while(true);
    assert.equal(observations.at(-1).attempts[0].reason,'authorization_failed');assert.deepEqual(saved.at(-1).mcp,[]);const browserBefore=browserRequests.length;browserController.dispose();assert.equal(browserRequests.length,browserBefore,'View disposal must not dispatch cancellation');
    await browserView.close();browserView=undefined;
    const tlsDeadline=Date.now()+1000;while(!tlsObservations&&Date.now()<tlsDeadline)await new Promise(resolve=>setTimeout(resolve,10));
    assert.equal(httpRequests,0,'OS TLS rejection must prevent all requests, credentials and token exchange at the untrusted server');assert.ok(tlsObservations>=1,'The production service must reach the actual TLS peer');
    child.stdin.end('service-fixture-done\n');const [code,signal]=await exited;assert.equal(signal,null);assert.equal(code,0,stderr);assert.match(stdout,/Native OAuth service fixture finished/);
    process.stdout.write('Production native OAuth service passed actual asynchronous TLS-negative failure, request identity/CAS/terminal cancellation, existing encrypted grant status, native HTTP and origin-bound View authentication, actual browser adapter/shared client/controller setup, forbidden destinations/query/duplicate fields and zero untrusted-server HTTP dispatch. No trusted HTTPS login, positive token exchange or installed/rendered UI acceptance verified.\n');
  }finally{clearTimeout(timer);if(child.exitCode===null&&child.signalCode===null){child.kill();await exited;}}
}finally{browserController?.dispose();if(browserView)await browserView.close();if(tls){tls.closeAllConnections();await new Promise(resolve=>tls.close(resolve));}await rm(directory,{recursive:true,force:true});}
