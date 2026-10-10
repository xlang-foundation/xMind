// Test the independent synthetic AS/MCP peer with real Node HTTPS and explicit
// CA verification. No native OAuth execution or Windows trust changes occur.
import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {createOAuthPeer,trustedRequest,access} from './mcp_oauth_trusted_peer.mjs';
import {renewalModes} from './mcp_oauth_renewal_peer.mjs';
const [openssl]=process.argv.slice(2),execute=promisify(execFile),directory=await mkdtemp(join(tmpdir(),'xmind-oauth-peer-'));let peer;
try{
 await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(directory,'key.pem'),'-out',join(directory,'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1'],{windowsHide:true});
 const ca=await readFile(join(directory,'cert.pem'));peer=createOAuthPeer({key:await readFile(join(directory,'key.pem')),cert:ca});const origin=await peer.listen();
 await assert.rejects(trustedRequest(origin+'/resource/success',undefined),'Default TLS must not trust the self-signed fixture');
 const probe=await trustedRequest(origin+'/mcp/success',ca,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({jsonrpc:'2.0',id:'fixture-probe',method:'server/discover'})});assert.equal(probe.status,401);assert.match(probe.headers['www-authenticate'],/resource_metadata=/);
 const metadata=await trustedRequest(origin+'/resource/success',ca);assert.equal(metadata.status,200);assert.equal(JSON.parse(metadata.body).resource,origin+'/mcp/success');
 const issuer=await trustedRequest(origin+'/.well-known/oauth-authorization-server/issuer',ca);assert.equal(JSON.parse(issuer.body).token_endpoint,origin+'/token');
 const verifier='v'.repeat(43),challenge=createHash('sha256').update(verifier).digest('base64url');
 for(const kind of ['success','registered','denied','bad-state','bad-issuer','bad-code']){
  const redirect=kind==='registered'?'http://127.0.0.1:43211/oauth2redirect/registered-client':'http://127.0.0.1:43210/oauth/callback';
  const url=new URL(origin+'/authorize');for(const [name,value]of Object.entries({client_id:'synthetic-public-client',response_type:'code',scope:'tools.read',code_challenge_method:'S256',code_challenge:challenge,state:'s'.repeat(43),resource:origin+'/mcp/'+kind,redirect_uri:redirect}))url.searchParams.set(name,value);
  const authorized=await trustedRequest(url,ca);assert.equal(authorized.status,302);const callback=new URL(authorized.headers.location);
  assert.equal(callback.searchParams.get('state'),kind==='bad-state'?'synthetic-wrong-state':'s'.repeat(43));assert.equal(callback.searchParams.get('iss'),origin+(kind==='bad-issuer'?'/other-issuer':'/issuer'));
  if(kind==='denied'){assert.equal(callback.searchParams.get('error'),'access_denied');assert.equal(callback.searchParams.has('code'),false);continue;}
  if(!['success','registered','bad-code'].includes(kind))continue;
  const body=new URLSearchParams({client_id:'synthetic-public-client',grant_type:'authorization_code',code:callback.searchParams.get('code'),code_verifier:verifier,resource:origin+'/mcp/'+kind,redirect_uri:redirect}).toString();
  const token=await trustedRequest(origin+'/token',ca,{method:'POST',body,headers:{'Content-Type':'application/x-www-form-urlencoded'}});assert.equal(token.status,kind==='bad-code'?400:200);if(kind!=='bad-code')assert.equal(JSON.parse(token.body).access_token,access);
 }
 for(const kind of ['success','registered'])for(const method of ['server/discover','tools/list']){const result=await trustedRequest(origin+'/mcp/'+kind,ca,{method:'POST',headers:{Authorization:'Bearer '+access,'Content-Type':'application/json','mcp-method':method},body:JSON.stringify({jsonrpc:'2.0',id:'fixture-'+kind+'-'+method,method})});assert.equal(result.status,200);assert.equal(JSON.parse(result.body).result.resultType,'complete');}
 peer.assertHealthy();assert.deepEqual(peer.counts.tokens,{success:1,registered:1,'bad-code':1});assert.deepEqual(peer.counts.authenticated,{'server/discover':2,'tools/list':2});
 const refreshModes=['rotate','retain','scope-expanded','bad-json','http-failure','lost-reply','redirect'];
 const refreshBody=new URLSearchParams({client_id:'synthetic-public-client',grant_type:'refresh_token',refresh_token:'synthetic refresh +&=',resource:origin+'/mcp/success',scope:'tools.read tools.list'}).toString();
 for(const mode of refreshModes){
  const request=()=>trustedRequest(origin+'/refresh/'+mode,ca,{method:'POST',body:refreshBody,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
  if(mode==='lost-reply'){await assert.rejects(request());continue;}
  const result=await request();assert.equal(result.status,mode==='http-failure'?400:mode==='redirect'?307:200);
  if(mode==='redirect'){assert.equal(result.headers.location,origin+'/refresh-forwarded');continue;}
  if(mode==='bad-json'){assert.equal((result.body.match(/"access_token"/g)||[]).length,2);continue;}
  const value=JSON.parse(result.body);if(mode==='http-failure'){assert.equal(value.error,'invalid_grant');continue;}
  assert.equal(value.access_token,'synthetic-refreshed-access');assert.equal(value.scope,mode==='scope-expanded'?'tools.admin':'tools.read');assert.equal(value.refresh_token,mode==='rotate'?'synthetic rotated refresh +&=':undefined);
 }
 peer.assertHealthy();assert.deepEqual(peer.counts.refresh,Object.fromEntries(refreshModes.map(mode=>[mode,1])));
 for(const mode of renewalModes.filter(value=>value!=='cancel')){
  const resource=await trustedRequest(origin+'/renew-resource/'+mode,ca);assert.equal(JSON.parse(resource.body).resource,origin+'/renew-mcp/'+mode);
  const issuer=await trustedRequest(origin+'/.well-known/oauth-authorization-server/renew-issuer/'+mode,ca);assert.equal(JSON.parse(issuer.body).token_endpoint,origin+'/renew-token/'+mode);
  const body=new URLSearchParams({client_id:'synthetic-public-client',grant_type:'refresh_token',refresh_token:'synthetic refresh +&=',resource:origin+'/renew-mcp/'+mode,scope:'tools.read tools.list'}).toString();
  const request=()=>trustedRequest(origin+'/renew-token/'+mode,ca,{method:'POST',body,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
  if(mode==='lost-reply'){await assert.rejects(request());continue;}const reply=await request();assert.equal(reply.status,mode==='http-failure'?400:mode==='redirect'?307:200);
  if(['rotate','retain','publish-fault'].includes(mode))assert.equal(JSON.parse(reply.body).access_token,'synthetic-renewed-access');
 }
 peer.assertHealthy();assert.deepEqual(peer.counts.renewal,Object.fromEntries(renewalModes.filter(value=>value!=='cancel').map(mode=>[mode,1])));
 process.stdout.write('Independent synthetic OAuth/MCP peer passed real HTTPS with explicit CA verification, default untrusted-TLS rejection, metadata/challenge, authorization faults, independently checked S256 code exchange and authenticated MCP responses. No native login or Windows trust provisioning executed.\n');
}finally{if(peer)await peer.close();await rm(directory,{recursive:true,force:true});}
