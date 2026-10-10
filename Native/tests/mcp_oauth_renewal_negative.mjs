import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,readFile,rm,realpath} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {createServer} from 'node:https';
import {runRenewalAcceptance} from './mcp_oauth_renewal_peer.mjs';
const [executable,modules,stdlib,openssl,cli]=process.argv.slice(2),execute=promisify(execFile),parent=await realpath(tmpdir()),directory=await mkdtemp(join(parent,'xmind-renewal-tls-'));let peer,tlsObservations=0,httpRequests=0;
try{
 await execute(openssl,['req','-x509','-newkey','rsa:2048','-nodes','-keyout',join(directory,'key.pem'),'-out',join(directory,'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1'],{windowsHide:true});
 peer=createServer({key:await readFile(join(directory,'key.pem')),cert:await readFile(join(directory,'cert.pem'))},(_request,response)=>{httpRequests++;response.writeHead(500);response.end();});peer.on('tlsClientError',()=>tlsObservations++);peer.on('secureConnection',()=>tlsObservations++);
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));await runRenewalAcceptance(executable,modules,stdlib,'https://127.0.0.1:'+peer.address().port,{negative:true,cli});
 assert.ok(tlsObservations>=9,'Each prepared renewal must reach the real TLS peer');assert.equal(httpRequests,0,'Untrusted TLS must prevent all credential/metadata/token HTTP dispatch');
}finally{
 if(peer){peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));}
 assert.equal(dirname(await realpath(directory)),parent);await rm(directory,{recursive:true,force:true});
}
