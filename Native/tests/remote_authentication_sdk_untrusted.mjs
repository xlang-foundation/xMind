import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createOAuthPeer} from './mcp_oauth_trusted_peer.mjs';
import {runRemoteAuthenticationSdk} from './remote_authentication_sdk_peer.mjs';
const [executable,modules,stdlib,openssl,cliExecutable]=process.argv.slice(2),execute=promisify(execFile),folder=await mkdtemp(join(tmpdir(),'xmind-remote-sdk-untrusted-'));let peer,passed=false;
try{
 await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(folder,'key.pem'),'-out',join(folder,'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1'],{windowsHide:true});
 peer=createOAuthPeer({key:await readFile(join(folder,'key.pem')),cert:await readFile(join(folder,'cert.pem'))});const origin=await peer.listen();await runRemoteAuthenticationSdk(executable,modules,stdlib,origin,folder);peer.assertHealthy();assert.deepEqual(peer.counts.remote,{});const stages=peer.tlsStages();console.log(JSON.stringify({nativeTlsObservation:stages}));assert.ok(stages.tcpConnections>0&&(stages.tlsConnections>0||stages.tlsFailures>0),'Peer must observe actual native TLS transport, not merely a preflight refusal');
 await runRemoteAuthenticationSdk(executable,modules,stdlib,origin,folder,{cliExecutable});peer.assertHealthy();assert.deepEqual(peer.counts.remote,{});assert.equal(peer.tlsStages().tcpConnections,stages.tcpConnections+1,'Only the well-formed CLI authentication should reach native TLS');console.log('Actual xmind CLI authentication/status passed redirected-stdin handling, empty/multiline/oversized/extra-argument refusals without credential echo, untrusted TLS refusal and read-only state without mutation. No positive CLI TLS claim.');
 console.log('Separate SDK client invoked registered authentication through actual native backend/WinHTTP; untrusted TLS rejected before HTTP credential delivery, zero peer requests, zero saved grants, Local retained, database reopen verified. No certificate trust modified or positive TLS authentication claimed.');
 passed=true;
}finally{if(peer){console.log(JSON.stringify({finalNativeTlsObservation:peer.tlsStages()}));await peer.close();}if(passed)await rm(folder,{recursive:true,force:true});else console.error('Retained failed remote SDK fixture: '+folder);}
