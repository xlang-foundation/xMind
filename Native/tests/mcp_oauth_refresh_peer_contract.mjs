import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,resolve} from 'node:path';
import {createOAuthPeer} from './mcp_oauth_trusted_peer.mjs';
const [executable,openssl]=process.argv.slice(2),execute=promisify(execFile);
const unit=JSON.parse((await execute(executable,['unit','https://synthetic.example.test'],{windowsHide:true,timeout:10000})).stdout);
const form=new URLSearchParams(unit.form);assert.deepEqual([...form.keys()].sort(),['client_id','grant_type','refresh_token','resource','scope']);
assert.equal(form.get('client_id'),'synthetic +&é= client');assert.equal(form.get('resource'),'https://resource.example.test/mcp?tenant=a%20b&mode=read');assert.equal(form.get('refresh_token'),'synthetic refresh +&=');assert.equal(form.get('scope'),'tools.read tools.list');assert.equal(form.get('grant_type'),'refresh_token');
const absent=new URLSearchParams(unit.no_scope);assert.equal(absent.has('scope'),false);assert.deepEqual([...absent.keys()].sort(),['client_id','grant_type','refresh_token','resource']);
const parent=resolve(tmpdir()),directory=await mkdtemp(join(parent,'xmind-oauth-refresh-'));let peer;
try{
 await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(directory,'key.pem'),'-out',join(directory,'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1'],{windowsHide:true,timeout:10000});
 peer=createOAuthPeer({key:await readFile(join(directory,'key.pem')),cert:await readFile(join(directory,'cert.pem'))});let tls=0;peer.server.on('tlsClientError',()=>tls++);peer.server.on('secureConnection',()=>tls++);const origin=await peer.listen();
 const result=await execute(executable,['untrusted',origin],{windowsHide:true,timeout:10000});assert.match(result.stdout,/passed untrusted/);assert.ok(tls>0,'Native refresh must contact the real TLS peer');assert.deepEqual(peer.counts.refresh,{},'Untrusted TLS must dispatch zero refresh requests');peer.assertHealthy();
 process.stdout.write('Actual native refresh primitive passed independent form decoding, token rotation/retention and scope rejection, bound metadata, single-use cancellation/deadline and real untrusted HTTPS contact with zero HTTP dispatch. No trusted native refresh, durable grant publication, automatic refresh or UI acceptance.\n');
}finally{if(peer)await peer.close();assert.equal(dirname(directory),parent);await rm(directory,{recursive:true,force:true});}
