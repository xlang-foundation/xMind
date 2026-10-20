import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {openSync,closeSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [cli]=process.argv.slice(2);assert.ok(cli);
const folder=await mkdtemp(join(tmpdir(),'xmind-cli-output-'));
let requests=0,peerError;const token='synthetic-output-contract-token';
const peer=createServer((request,response)=>{try{assert.equal(request.method,'GET');assert.equal(request.url,'/v1/sessions');assert.equal(request.headers.authorization,'Bearer '+token);++requests;response.writeHead(200,{'content-type':'application/json'});response.end('[]');}catch(error){peerError=error;response.writeHead(500);response.end();}});
async function run(output='pipe'){
 const child=spawn(cli,[String(peer.address().port),'sessions'],{env:{...process.env,XMIND_AUTH_TOKEN:token},stdio:['ignore',output,'pipe'],windowsHide:true});
 let stdout='',stderr='';child.stdout?.on('data',bytes=>stdout+=bytes);child.stderr.on('data',bytes=>stderr+=bytes);
 // Observe this exact process to close; no retry or timeout-triggered restart.
 const result=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',(code,signal)=>resolve({code,signal}));});
 if(peerError)throw peerError;return {...result,stdout,stderr};
}
try{
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
 const valid=await run();assert.equal(valid.code,0,valid.stderr);assert.equal(valid.signal,null);assert.deepEqual(JSON.parse(valid.stdout),[]);
 const target=join(folder,'read-only-output');await writeFile(target,'preserved fixture bytes');const fd=openSync(target,'r');let failed;
 try{failed=await run(fd);}finally{closeSync(fd);}
 assert.equal(failed.signal,null);assert.equal(failed.code,1,'CLI must report failure when its inherited stdout handle denies writing');assert.match(failed.stderr,/CLI JSON output is unavailable/);assert.equal(requests,2,'Each explicit read-only command must issue exactly one request');
 console.log('CLI JSON output verified: valid pipe delivers parseable JSON; read-only inherited stdout rejects success with no command retry.');
}finally{peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));await rm(folder,{recursive:true,force:true});}
