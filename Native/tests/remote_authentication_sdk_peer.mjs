import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {once} from 'node:events';
import {promisify} from 'node:util';
import {mkdir,readFile} from 'node:fs/promises';
import {join} from 'node:path';
export const remoteCredential='synthetic-remote-sdk-credential-not-live';
export function remoteNegotiationResponse(request,body){
 assert.equal(request.method,'POST');assert.equal(request.headers['content-type'],'application/json');
 assert.deepEqual(JSON.parse(body),{supported_versions:[1],required_capabilities:{events:1,receipts:1,runs:1,sessions:1},expected_server_id:null});
 if(request.headers.authorization!=='Bearer '+remoteCredential)return {status:401,kind:'refused',value:{error:'synthetic_unauthorized'}};
 return {status:200,kind:'accepted',value:{server_id:'remote-sdk-server',protocol_version:1,grant_id:'private-remote-sdk-grant',grant_revision:1,expires_at:Math.floor(Date.now()/1000)+120,authorized_scopes:[{id:'remote-sdk-workspace',actions:['sessions.read','runs.submit']}],capabilities:{sessions:1,runs:1,events:1,receipts:1}}};
}
export async function runRemoteAuthenticationSdk(executable,modules,stdlib,origin,parent,{positive=false,cliExecutable}={}){
 if(positive)assert.ok(process.platform==='win32'&&process.env.GITHUB_ACTIONS==='true'&&process.env.RUNNER_ENVIRONMENT==='github-hosted'&&process.env.RUNNER_OS==='Windows'&&/^\d+$/.test(process.env.GITHUB_RUN_ID||''),'Positive native TLS fixture requires isolated hosted runner; never impersonate locally');
 const folder=join(parent,(cliExecutable?'remote-cli-':'remote-sdk-')+(positive?'positive':'untrusted'));await mkdir(folder);const child=spawn(executable,[folder,modules,stdlib,origin],{windowsHide:true,stdio:['pipe','pipe','pipe']});const exited=once(child,'exit');let stdout='',stderr='',ready=false,resolveReady,rejectReady;const readyPromise=new Promise((resolve,reject)=>{resolveReady=resolve;rejectReady=reject;});
 child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',text=>{stdout+=text;if(!ready&&stdout.includes('\n')){ready=true;try{resolveReady(JSON.parse(stdout.slice(0,stdout.indexOf('\n'))));}catch(error){rejectReady(error);}}});child.stderr.on('data',text=>stderr+=text);child.once('error',rejectReady);child.once('exit',()=>{if(!ready)rejectReady(Error('Remote SDK host exited before readiness'));});const timer=setTimeout(()=>{rejectReady(Error('Remote SDK fixture deadline'));child.kill();},20000);
 try{
  const {endpoint}=await readyPromise;assert.match(endpoint,/^lrpc:\d+$/);
  if(cliExecutable){
   const cli=(args,input='')=>new Promise((resolve,reject)=>{assert.ok(args.every(value=>!String(value).includes(remoteCredential)));const cliProcess=spawn(cliExecutable,['ipc','--endpoint',endpoint,'connections',...args],{windowsHide:true,env:{...process.env,XMIND_AUTH_TOKEN:'synthetic-remote-sdk-owner-not-live'},stdio:['pipe','pipe','pipe']});let stdout='',stderr='';const timeout=setTimeout(()=>cliProcess.kill(),12000);cliProcess.stdout.on('data',value=>stdout+=value);cliProcess.stderr.on('data',value=>stderr+=value);cliProcess.stdin.on('error',()=>{});cliProcess.once('error',reject);cliProcess.once('close',(code,signal)=>{clearTimeout(timeout);try{assert.equal(signal,null);assert.ok(!stdout.includes(remoteCredential)&&!stderr.includes(remoteCredential));resolve({code,stdout,stderr});}catch(error){reject(error);}});cliProcess.stdin.end(input);});
   const read=async args=>{const result=await cli(args);assert.equal(result.code,0,result.stderr);return JSON.parse(result.stdout);};const invoke=input=>cli(['authenticate','remote-sdk','1','0','5000'],input);
   assert.equal((await read(['capabilities'])).remote_authentication_status,1);assert.equal((await read(['authentication','remote-sdk','1'])).state,'absent');
   for(const input of ['',remoteCredential+'\nsecond-line',remoteCredential+'\x1asecond-part',remoteCredential.repeat(400)]){const result=await invoke(input);assert.notEqual(result.code,0);assert.match(result.stderr,/Connection authentication failed/);}
   const extra=await cli(['authenticate','remote-sdk','1','0','5000','unexpected-argument'],remoteCredential);assert.notEqual(extra.code,0);
   if(positive){const denied=await invoke('wrong-synthetic-remote-credential');assert.notEqual(denied.code,0);const accepted=await invoke(remoteCredential+'\r\n');assert.equal(accepted.code,0,accepted.stderr);const receipt=JSON.parse(accepted.stdout);assert.equal(receipt.server_id,'remote-sdk-server');assert.equal(receipt.credential_revision,1);assert.equal(receipt.command_routing,false);assert.notEqual((await invoke(remoteCredential)).code,0);}
   else assert.notEqual((await invoke(remoteCredential)).code,0);
   const status=await read(['authentication','remote-sdk','1']);assert.equal(status.state,positive?'stored':'absent');assert.equal(status.credential_revision,positive?1:0);assert.equal(status.command_routing,false);assert.notEqual((await cli(['authentication','remote-sdk','2'])).code,0);assert.equal((await read(['list'])).active_id,'local');
  }else{const client=await promisify(execFile)(executable,['client',endpoint,positive?'positive':'refusal'],{windowsHide:true,timeout:12000});assert.match(client.stdout,positive?/separate-sdk-auth-positive/:/separate-sdk-auth-refused/);if(!positive)assert.match(client.stdout,/sdk-transport-refusal:yes/);}
  child.stdin.end(positive?'verify-positive\n':'verify-refusal\n');const [code,signal]=await exited;assert.equal(signal,null);assert.equal(code,0,stderr);assert.match(stdout,/remote-sdk-database-reopened/);
  for(const suffix of ['','-wal']){let bytes;try{bytes=await readFile(join(folder,'remote-sdk.sqlite'+suffix));}catch(error){if(error.code==='ENOENT')continue;throw error;}assert.ok(!bytes.includes(Buffer.from(remoteCredential)));assert.ok(!bytes.includes(Buffer.from('private-remote-sdk-grant')));}
 }finally{clearTimeout(timer);if(child.exitCode===null&&child.signalCode===null){child.kill();await exited;}}
}
