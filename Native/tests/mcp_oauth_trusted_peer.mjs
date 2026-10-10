// Independent synthetic OAuth/MCP HTTPS peer. The hosted driver below runs the
// real native service; this fixture is never a product server or preview.
import assert from 'node:assert/strict';
import {createServer,request as httpsRequest} from 'node:https';
import {randomBytes,createHash,randomUUID,X509Certificate} from 'node:crypto';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {once} from 'node:events';
import {mkdtemp,readFile,writeFile,rm,realpath} from 'node:fs/promises';
import {join,resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createServer as createTcpServer} from 'node:net';
export const access='synthetic-positive-access-not-live',refresh='synthetic positive refresh not live';
export function createOAuthPeer(options,{registeredPort=43211}={}){
 const counts={probe:{},metadata:{},authorize:{},tokens:{},authenticated:{},refresh:{}};const codes=new Map();let origin,error;
 const kinds=new Set(['success','registered','denied','bad-state','bad-issuer','bad-code','cancelled','occupied']);
 const count=(group,kind)=>counts[group][kind]=(counts[group][kind]||0)+1;
 const peer=createServer(options,async(request,response)=>{
  try{
   const url=new URL(request.url,origin),reply=(status,value,headers={})=>{response.writeHead(status,{'Content-Type':'application/json',...headers});response.end(JSON.stringify(value));};
   assert.equal(request.headers.cookie,undefined);let body='';for await(const bytes of request){body+=bytes;if(body.length>65536)throw Error('Synthetic request bound exceeded');}
   if(url.pathname.startsWith('/mcp/')){
    const kind=url.pathname.slice(5);assert.ok(kinds.has(kind));assert.equal(request.method,'POST');
    const rpc=JSON.parse(body);assert.equal(rpc.jsonrpc,'2.0');
    if(request.headers.authorization===undefined){count('probe',kind);reply(401,{error:'synthetic_authorization_required'},{'WWW-Authenticate':`Bearer resource_metadata="${origin}/resource/${kind}", scope="tools.read"`});return;}
    assert.ok(['success','registered'].includes(kind));assert.equal(request.headers.authorization,'Bearer '+access);count('authenticated',rpc.method);
    assert.ok(['server/discover','tools/list'].includes(rpc.method));assert.equal(request.headers['mcp-method'],rpc.method);
    reply(200,{jsonrpc:'2.0',id:rpc.id,result:rpc.method==='server/discover'?{resultType:'complete',supportedVersions:['2026-07-28'],capabilities:{tools:{}}}:{resultType:'complete',tools:[]}});return;
   }
   assert.equal(request.headers.authorization,undefined,'Native owner/access/refresh credentials must not leak to discovery or token endpoints');
   if(url.pathname.startsWith('/refresh/')){
    const mode=url.pathname.slice(9);assert.ok(['rotate','retain','scope-expanded','bad-json','http-failure','lost-reply','redirect'].includes(mode));assert.equal(request.method,'POST');assert.equal(request.headers['content-type'],'application/x-www-form-urlencoded');
    const params=new URLSearchParams(body),keys=[...params.keys()];assert.equal(new Set(keys).size,keys.length);assert.deepEqual(keys.sort(),['client_id','grant_type','refresh_token','resource','scope']);
    assert.equal(params.get('client_id'),'synthetic-public-client');assert.equal(params.get('grant_type'),'refresh_token');assert.equal(params.get('refresh_token'),'synthetic refresh +&=');assert.equal(params.get('resource'),origin+'/mcp/success');assert.equal(params.get('scope'),'tools.read tools.list');count('refresh',mode);
    if(mode==='http-failure'){reply(400,{error:'invalid_grant',error_description:'synthetic private refresh diagnostic'});return;}
    if(mode==='lost-reply'){response.destroy();return;}
    if(mode==='redirect'){response.writeHead(307,{Location:origin+'/refresh-forwarded'});response.end();return;}
    if(mode==='bad-json'){response.writeHead(200,{'Content-Type':'application/json'});response.end('{"access_token":"synthetic-refreshed-access","access_token":"duplicate","token_type":"Bearer"}');return;}
    reply(200,{access_token:'synthetic-refreshed-access',token_type:'Bearer',expires_in:600,scope:mode==='scope-expanded'?'tools.admin':'tools.read',...(mode==='rotate'?{refresh_token:'synthetic rotated refresh +&='}:{})});return;
   }
   if(url.pathname==='/refresh-forwarded'){count('refresh','forwarded');throw Error('Refresh credentials must not follow a redirect');}
   if(url.pathname.startsWith('/resource/')){
    const kind=url.pathname.slice(10);assert.ok(kinds.has(kind));assert.equal(request.method,'GET');count('metadata',kind);reply(200,{resource:origin+'/mcp/'+kind,authorization_servers:[origin+'/issuer'],scopes_supported:['tools.read'],bearer_methods_supported:['header']});return;
   }
   if(url.pathname==='/.well-known/oauth-authorization-server/issuer'){
    assert.equal(request.method,'GET');reply(200,{issuer:origin+'/issuer',authorization_endpoint:origin+'/authorize',token_endpoint:origin+'/token',response_types_supported:['code'],grant_types_supported:['authorization_code'],code_challenge_methods_supported:['S256'],token_endpoint_auth_methods_supported:['none'],authorization_response_iss_parameter_supported:true,scopes_supported:['tools.read']});return;
   }
   if(url.pathname==='/authorize'){
    assert.equal(request.method,'GET');const names=[...url.searchParams.keys()];assert.equal(new Set(names).size,names.length);assert.deepEqual(names.sort(),['client_id','code_challenge','code_challenge_method','redirect_uri','resource','response_type','scope','state']);
    const p=Object.fromEntries(url.searchParams);assert.equal(p.client_id,'synthetic-public-client');assert.equal(p.response_type,'code');assert.equal(p.scope,'tools.read');assert.equal(p.code_challenge_method,'S256');assert.match(p.code_challenge,/^[A-Za-z0-9_-]{43}$/);assert.match(p.state,/^[A-Za-z0-9_-]{43}$/);
    const resource=new URL(p.resource),kind=resource.pathname.slice(5),callback=new URL(p.redirect_uri);assert.equal(resource.origin,origin);assert.ok(kinds.has(kind));assert.notEqual(kind,'occupied','An occupied callback port must not publish an authorization URL');assert.equal(callback.protocol,'http:');assert.equal(callback.hostname,'127.0.0.1');assert.ok(Number(callback.port)>0);assert.equal(callback.pathname,kind==='registered'?'/oauth2redirect/registered-client':'/oauth/callback');if(kind==='registered')assert.equal(Number(callback.port),registeredPort);assert.equal(callback.search,'');assert.equal(callback.hash,'');count('authorize',kind);
    callback.searchParams.set('state',kind==='bad-state'?'synthetic-wrong-state':p.state);callback.searchParams.set('iss',origin+(kind==='bad-issuer'?'/other-issuer':'/issuer'));
    if(kind==='denied')callback.searchParams.set('error','access_denied');else{const code=randomBytes(24).toString('base64url');codes.set(code,{kind,resource:p.resource,redirect:p.redirect_uri,challenge:p.code_challenge});callback.searchParams.set('code',code);}
    response.writeHead(302,{Location:callback.href,'Cache-Control':'no-store'});response.end();return;
   }
   if(url.pathname==='/token'){
    assert.equal(request.method,'POST');assert.equal(request.headers['content-type'],'application/x-www-form-urlencoded');const params=new URLSearchParams(body),keys=[...params.keys()];assert.equal(new Set(keys).size,keys.length);assert.deepEqual(keys.sort(),['client_id','code','code_verifier','grant_type','redirect_uri','resource']);const p=Object.fromEntries(params),entry=codes.get(p.code);assert.ok(entry,'Only an issued one-use code can be exchanged');codes.delete(p.code);count('tokens',entry.kind);
    assert.equal(p.grant_type,'authorization_code');assert.equal(p.client_id,'synthetic-public-client');assert.equal(p.resource,entry.resource);assert.equal(p.redirect_uri,entry.redirect);assert.match(p.code_verifier,/^[A-Za-z0-9_-]{43,128}$/);assert.equal(createHash('sha256').update(p.code_verifier).digest('base64url'),entry.challenge);
    if(entry.kind==='bad-code'){reply(400,{error:'invalid_grant',error_description:'synthetic private token diagnostic'});return;}
    assert.ok(['success','registered'].includes(entry.kind));reply(200,{token_type:'Bearer',access_token:access,refresh_token:refresh,expires_in:600,scope:'tools.read'});return;
   }
   reply(404,{error:'synthetic_fixture_route_missing'});
  }catch(failure){error=failure;response.writeHead(500);response.end('Synthetic protocol fixture rejected the request');}
 });peer.on('tlsClientError',()=>{});
 return {server:peer,counts,codes,assertHealthy(){if(error)throw error;},async listen(){await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));origin='https://127.0.0.1:'+peer.address().port;return origin;},async close(){peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));}};
}
export function trustedRequest(url,ca,{method='GET',body,headers={}}={}){
 return new Promise((resolve,reject)=>{const request=httpsRequest(url,{method,ca,headers,rejectUnauthorized:true},response=>{let text='';response.setEncoding('utf8');response.on('data',data=>{text+=data;if(text.length>65536){request.destroy();reject(Error('Fixture response bound exceeded'));}});response.on('end',()=>resolve({status:response.statusCode,headers:response.headers,body:text}));});request.setTimeout(10000,()=>request.destroy(Error('Fixture HTTPS deadline exceeded')));request.on('error',reject);request.end(body);});
}
async function hosted(){
 if(process.platform!=='win32'||process.env.GITHUB_ACTIONS!=='true'||process.env.RUNNER_ENVIRONMENT!=='github-hosted'||process.env.RUNNER_OS!=='Windows'||!/^\d+$/.test(process.env.GITHUB_RUN_ID||''))throw Error('Trusted OAuth acceptance requires an isolated GitHub-hosted Windows runner. Never impersonate its environment locally.');
 const [executable,modules,stdlib,openssl,certificateHelper,refreshExecutable]=process.argv.slice(2),execute=promisify(execFile),tempRoot=await realpath(process.env.RUNNER_TEMP),directory=await mkdtemp(join(tempRoot,'xmind-oauth-trust-'));
 assert.equal(dirname(await realpath(directory)),tempRoot);let peer,child,exited,timer,installed=false,thumbprint,registeredReservation,occupiedReservation;
 const caFile=join(directory,'ca.cer'),provision=action=>execute('pwsh.exe',['-NoProfile','-NonInteractive','-File',certificateHelper,'-Action',action,'-CertificateFile',caFile,'-Thumbprint',thumbprint],{windowsHide:true,timeout:20000});
 try{
  await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(directory,'ca-key.pem'),'-out',join(directory,'ca.pem'),'-days','1','-subj','/CN=xMind isolated OAuth CI '+randomUUID(),'-addext','basicConstraints=critical,CA:TRUE','-addext','keyUsage=critical,keyCertSign,cRLSign'],{windowsHide:true});
  await execute(openssl,['x509','-in',join(directory,'ca.pem'),'-outform','DER','-out',caFile],{windowsHide:true});
  thumbprint=new X509Certificate(await readFile(caFile)).fingerprint.replaceAll(':','');
  await execute(openssl,['req','-newkey','rsa:2048','-nodes','-keyout',join(directory,'server-key.pem'),'-out',join(directory,'server.csr'),'-subj','/CN=localhost'],{windowsHide:true});
  await writeFile(join(directory,'leaf.cnf'),'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=IP:127.0.0.1,DNS:localhost\n');
  await execute(openssl,['x509','-req','-in',join(directory,'server.csr'),'-CA',join(directory,'ca.pem'),'-CAkey',join(directory,'ca-key.pem'),'-CAcreateserial','-out',join(directory,'server.pem'),'-days','1','-sha256','-extfile',join(directory,'leaf.cnf')],{windowsHide:true});
  const installedOutput=await provision('Install');installed=true;const installedResult=JSON.parse(installedOutput.stdout);assert.equal(installedResult.thumbprint,thumbprint);assert.equal(installedResult.store,'LocalMachine/Root');
  registeredReservation=createTcpServer(socket=>socket.destroy());occupiedReservation=createTcpServer(socket=>socket.destroy());
  await new Promise(resolve=>registeredReservation.listen(0,'127.0.0.1',resolve));await new Promise(resolve=>occupiedReservation.listen(0,'127.0.0.1',resolve));
  const registeredPort=registeredReservation.address().port,occupiedPort=occupiedReservation.address().port;assert.notEqual(registeredPort,occupiedPort);await new Promise(resolve=>registeredReservation.close(resolve));registeredReservation=undefined;
  const ca=await readFile(join(directory,'ca.pem'));peer=createOAuthPeer({key:await readFile(join(directory,'server-key.pem')),cert:await readFile(join(directory,'server.pem'))},{registeredPort});const origin=await peer.listen();
  child=spawn(executable,[directory,modules,stdlib,origin,String(registeredPort),String(occupiedPort)],{windowsHide:true,stdio:['pipe','pipe','pipe']});exited=once(child,'exit');let stdout='',stderr='',resolveReady,rejectReady;const ready=new Promise((resolve,reject)=>{resolveReady=resolve;rejectReady=reject;});let first=false;
  child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',text=>{stdout+=text;if(!first&&stdout.includes('\n')){first=true;try{resolveReady(JSON.parse(stdout.slice(0,stdout.indexOf('\n'))));}catch(error){rejectReady(error);}}});child.stderr.on('data',text=>stderr+=text);child.once('error',rejectReady);child.once('exit',()=>{if(!first)rejectReady(Error(stderr||'Native trusted fixture exited before readiness'));});timer=setTimeout(()=>child.kill(),50000);
  const {port}=await ready,base='http://127.0.0.1:'+port,owner={Authorization:'Bearer synthetic-trusted-oauth-owner-token'};
  const call=async(path,body)=>{const response=await fetch(base+'/v1/mcp/authorization'+path,{method:body===undefined?'GET':'POST',headers:{...owner,...(body===undefined?{}:{'Content-Type':'application/json'})},...(body===undefined?{}:{body:JSON.stringify(body)}),signal:AbortSignal.timeout(10000)});const value=await response.json();assert.ok(!JSON.stringify(value).includes(access));assert.ok(!JSON.stringify(value).includes(refresh));return {status:response.status,value};};
  const poll=async(id,predicate)=>{const deadline=Date.now()+10000;do{const result=await call('/attempts/'+id);assert.equal(result.status,200);if(predicate(result.value))return result.value;assert.ok(Date.now()<deadline,'Native authorization phase did not settle');await new Promise(resolve=>setTimeout(resolve,20));}while(true);};
  const terminal=value=>['connected','denied','failed','cancelled','expired'].includes(value.state);
  for(const kind of ['success','registered','denied','bad-state','bad-issuer','bad-code','cancelled','occupied']){
   const id='trusted-'+kind,body={server_id:'oauth.'+kind,expected_config_revision:1,expected_credential_revision:0,request_id:id};assert.equal((await call('/attempts',body)).status,202);
   const waiting=await poll(id,value=>value.state==='awaiting_callback'||terminal(value));
   if(kind==='occupied'){assert.equal(waiting.state,'failed');assert.equal(waiting.reason,'authorization_failed');assert.equal(waiting.authorization_url,null);assert.equal(waiting.credential_revision,0);continue;}
   assert.equal(waiting.state,'awaiting_callback');assert.equal(new URL(waiting.authorization_url).origin,origin);
   if(kind==='cancelled'){assert.equal((await call('/attempts/'+id+'/cancel',{})).status,202);assert.equal((await poll(id,terminal)).state,'cancelled');continue;}
   const authorized=await trustedRequest(waiting.authorization_url,ca);assert.equal(authorized.status,302);const callback=await fetch(authorized.headers.location,{redirect:'error',signal:AbortSignal.timeout(10000)});assert.equal(callback.status,['denied','bad-state','bad-issuer'].includes(kind)?400:200);const page=await callback.text();assert.ok(!page.includes(new URL(authorized.headers.location).search));
   const positive=['success','registered'].includes(kind),final=await poll(id,terminal);assert.equal(final.state,positive?'connected':kind==='denied'?'denied':'failed');assert.equal(final.authorization_url,null);assert.equal(final.credential_revision,positive?1:0);
   if(kind==='success'){const duplicate=await call('/attempts',body);assert.equal(duplicate.status,202);assert.equal(duplicate.value.state,'connected');const cancelled=await call('/attempts/'+id+'/cancel',{});assert.equal(cancelled.value.state,'connected');}
  }
  const servers=await call('/servers');for(const kind of ['success','registered'])assert.equal(servers.value.servers.find(server=>server.id==='oauth.'+kind).state,'authorized');assert.ok(servers.value.servers.filter(server=>!['oauth.success','oauth.registered'].includes(server.id)).every(server=>server.state==='needs_login'));
  peer.assertHealthy();assert.deepEqual(peer.counts.tokens,{success:1,registered:1,'bad-code':1});assert.deepEqual(peer.counts.probe,Object.fromEntries(['success','registered','denied','bad-state','bad-issuer','bad-code','cancelled','occupied'].map(kind=>[kind,1])));assert.equal(peer.counts.authorize.occupied,undefined);
  child.stdin.end('verify-success\n');const [code,signal]=await exited;assert.equal(signal,null);assert.equal(code,0,stderr);assert.match(stdout,/native-grant-verified/);assert.match(stdout,/native-grant-reopened/);peer.assertHealthy();assert.deepEqual(peer.counts.authenticated,{'server/discover':4,'tools/list':4});
  for(const filename of ['state.sqlite','state.sqlite-wal']){let bytes;try{bytes=await readFile(join(directory,filename));}catch(error){if(error.code==='ENOENT')continue;throw error;}assert.ok(!bytes.includes(Buffer.from(access)));assert.ok(!bytes.includes(Buffer.from(refresh)));}
  for(const mode of ['rotate','retain','scope-expanded','bad-json','http-failure','lost-reply','redirect']){const result=await execute(refreshExecutable,[mode,origin],{windowsHide:true,timeout:10000});assert.match(result.stdout,new RegExp('passed '+mode));peer.assertHealthy();}
  assert.deepEqual(peer.counts.refresh,Object.fromEntries(['rotate','retain','scope-expanded','bad-json','http-failure','lost-reply','redirect'].map(mode=>[mode,1])));
  process.stdout.write('Separate actual native single-use refresh protocol passed trusted HTTPS rotation/retention, restricted scope, duplicate JSON, HTTP failure, lost reply and redirect rejection with exactly one request per owner and zero forwarded requests. Synthetic refresh inputs; no durable refresh publication, automatic refresh or client refresh support claimed.\n');
  process.stdout.write('Actual native OAuth service passed trusted HTTPS discovery, default and registered exact-path/port callbacks, occupied-port rejection without fallback, independent S256/code/resource/client binding, two positive code exchanges, encrypted complete grant publication/reopen, bearer MCP discovery before/after reopen, denial/state/issuer/code rejection and cancellation without extra token requests. Synthetic authority and tokens only; no real provider account or rendered/browser-launch acceptance.\n');
 }finally{
  clearTimeout(timer);if(child&&child.exitCode===null&&child.signalCode===null){child.kill();await exited;}if(peer)await peer.close();
  for(const reservation of [registeredReservation,occupiedReservation])if(reservation?.listening)await new Promise(resolve=>reservation.close(resolve));
  if(installed){const removed=JSON.parse((await provision('Remove')).stdout);assert.equal(removed.thumbprint,thumbprint);assert.equal(removed.store,'LocalMachine/Root');process.stdout.write('Owned isolated-runner OAuth machine-root certificate removed and absence confirmed.\n');}
  assert.equal(dirname(await realpath(directory)),tempRoot);await rm(directory,{recursive:true,force:true});
 }
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url))await hosted();
