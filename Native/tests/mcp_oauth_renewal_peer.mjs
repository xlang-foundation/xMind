// Independent driver for the actual native renewal service. The hosted caller
// supplies an owned, trusted synthetic authority; local acceptance uses TLS rejection.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {mkdtemp,readFile,rm,realpath} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createBrowserServer} from '../../views/browser/server.mjs';
const require=createRequire(import.meta.url),{BrowserController,BrowserSessionClient}=require('../../views/browser/browser.js'),execute=promisify(execFile);
export const renewalModes=['rotate','retain','scope-expanded','bad-json','http-failure','lost-reply','redirect','cancel','publish-fault'];
export async function runRenewalAcceptance(executable,modules,stdlib,origin,{negative=false,peer,cli}={}){
 const parent=await realpath(process.env.RUNNER_TEMP||tmpdir()),directory=await mkdtemp(join(parent,'xmind-renewal-'));
 const child=spawn(executable,[directory,modules,stdlib,origin],{windowsHide:true,stdio:['pipe','pipe','pipe']});
 const exited=once(child,'exit');let stdout='',stderr='',first=false,resolveReady,rejectReady;
 const ready=new Promise((resolve,reject)=>{resolveReady=resolve;rejectReady=reject;});
 child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',data=>{stdout+=data;if(!first&&stdout.includes('\n')){first=true;try{resolveReady(JSON.parse(stdout.slice(0,stdout.indexOf('\n'))));}catch(error){rejectReady(error);}}});child.stderr.on('data',data=>stderr+=data);child.once('error',rejectReady);child.once('exit',()=>{if(!first)rejectReady(Error(stderr||'Renewal native fixture did not start'));});
 const timer=setTimeout(()=>child.kill(),40000);let passed=false,browserView,browserController;
 try{
  const {port}=await ready,base='http://127.0.0.1:'+port+'/v1/mcp/authorization',owner={Authorization:'Bearer synthetic-renewal-service-owner-token'};
  const call=async(path,body,headers=owner)=>{const response=await fetch(base+path,{method:body===undefined?'GET':'POST',headers:{...headers,...(body===undefined?{}:{'Content-Type':'application/json'})},...(body===undefined?{}:{body:typeof body==='string'?body:JSON.stringify(body)}),signal:AbortSignal.timeout(5000)});const value=await response.json();const publicText=JSON.stringify(value);for(const privateValue of ['synthetic-old-access','synthetic refresh','synthetic-renewed-access','synthetic rotated','synthetic private','server_json','token_endpoint','generation'])assert.ok(!publicText.includes(privateValue),'Private renewal fields must stay on the backend');return {status:response.status,value};};
  const body=mode=>({request_id:'renew-'+mode,server_id:'renewal.'+mode,expected_config_revision:1,expected_credential_revision:1});
  assert.equal((await call('/renewals',body('rotate'),{})).status,401);
  for(const invalid of [{...body('rotate'),expected_credential_revision:0},{...body('rotate'),endpoint:origin},{...body('rotate'),request_id:'unsafe/id'}])assert.equal((await call('/renewals',invalid)).status,400);
  assert.equal((await call('/renewals?extra=1',body('rotate'))).status,400);
  assert.equal((await call('/renewals','{"server_id":"renewal.rotate","server_id":"renewal.rotate"}')).status,400);
  assert.equal((await call('/renewals',{...body('rotate'),expected_config_revision:2})).status,409);
  const poll=async(id)=>{const deadline=Date.now()+7000;do{const result=await call('/attempts/'+id);assert.equal(result.status,200);if(['connected','failed','cancelled'].includes(result.value.state))return result.value;assert.ok(Date.now()<deadline,'Renewal did not settle');await new Promise(resolve=>setTimeout(resolve,10));}while(true);};
  for(const mode of renewalModes){
   const admitted=await call('/renewals',body(mode));assert.equal(admitted.status,202);assert.equal(admitted.value.state,'discovering');
   if(!negative&&mode==='cancel'){
    const deadline=Date.now()+5000;while(!peer.counts.renewal?.cancel){assert.ok(Date.now()<deadline);await new Promise(resolve=>setTimeout(resolve,10));}
    assert.equal((await call('/attempts/renew-cancel/cancel',{})).status,202);
   }
   const final=await poll('renew-'+mode),committed=!negative&&['rotate','retain'].includes(mode);
   assert.equal(final.state,negative?'cancelled':committed?'connected':'failed');assert.equal(final.credential_revision,committed?2:1);assert.equal(final.authorization_url,null);
   if(!negative&&!committed)assert.equal(final.reason,'refresh_uncertain');
   const before=JSON.stringify(peer?.counts);const duplicate=await call('/renewals',body(mode));assert.equal(duplicate.status,202);assert.equal(duplicate.value.state,final.state);assert.equal((await call('/attempts/renew-'+mode+'/cancel',{})).value.state,final.state);assert.equal(JSON.stringify(peer?.counts),before,'Duplicate/terminal observations must not contact the authority');
   assert.equal((await call('/attempts',body(mode))).status,409,'A login cannot reuse a durable renewal identity');
   assert.equal((await call('/renewals',{...body(mode),expected_credential_revision:2})).status,409,'The same identity cannot change its old grant binding');
   if(!negative&&!committed)assert.equal((await call('/renewals',{...body(mode),request_id:'no-replay-'+mode})).status,409,'Uncertain exchange must prohibit a fresh replay');
  }
  if(negative){
   const backend='http://127.0.0.1:'+port,assets=join(directory,'view-assets');await execute(process.execPath,[fileURLToPath(new URL('../../views/browser/build.mjs',import.meta.url)),assets],{windowsHide:true});
   browserView=await createBrowserServer({backend,assetRoot:assets});const browserOrigin=await browserView.listen();
   const enrollment=await fetch(browserOrigin+'/ui/session',{method:'POST',headers:{...owner,Origin:browserOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'},body:'{}'});assert.equal(enrollment.status,200);const cookie=enrollment.headers.get('set-cookie').split(';')[0],requests=[],messages=[];
   const client=new BrowserSessionClient(browserOrigin,{fetchImpl:(url,options)=>{requests.push({path:new URL(url).pathname,method:options.method});return fetch(url,{...options,headers:{...options.headers,Cookie:cookie,'Sec-Fetch-Site':'same-origin',...(options.method==='POST'?{Origin:browserOrigin}:{})}});}});
   browserController=new BrowserController(client,message=>messages.push(message),{save:()=>{}});await browserController.command({type:'mcp-refresh'});await browserController.command({type:'mcp-renew',server:'renewal.rotate'});
   assert.equal(requests.filter(request=>request.method==='POST'&&request.path.endsWith('/renewals')).length,1);const deadline=Date.now()+5000;
   do{await browserController.command({type:'mcp-refresh'});if(messages.at(-1).attempts[0]?.state==='cancelled')break;assert.ok(Date.now()<deadline,JSON.stringify({last:messages.at(-1),requests}));await new Promise(resolve=>setTimeout(resolve,10));}while(true);
   const count=requests.length;browserController.dispose();browserController=undefined;assert.equal(requests.length,count,'Browser detach cannot cancel or replay renewal');await browserView.close();browserView=undefined;
   if(cli){const env={...process.env,XMIND_AUTH_TOKEN:'synthetic-renewal-service-owner-token'};const admitted=await execute(cli,['--port',String(port),'mcp-refresh','renewal.rotate'],{env,windowsHide:true,timeout:10000}),value=JSON.parse(admitted.stdout);assert.match(admitted.stderr,new RegExp(value.id));await poll(value.id);const observed=await execute(cli,['--port',String(port),'mcp-login-status',value.id],{env,windowsHide:true,timeout:10000});assert.equal(JSON.parse(observed.stdout).state,'cancelled');}
  }
  child.stdin.end((negative?'verify-negative':'verify-renewals')+'\n');const [code,signal]=await exited;assert.equal(signal,null);assert.equal(code,0,stderr);assert.match(stdout,/native-renewals-verified/);assert.match(stdout,/native-renewals-reopened/);
  for(const filename of ['renewal.sqlite','renewal.sqlite-wal']){let bytes;try{bytes=await readFile(join(directory,filename));}catch(error){if(error.code==='ENOENT')continue;throw error;}for(const secret of ['synthetic-old-access','synthetic refresh +&=','synthetic-renewed-access','synthetic rotated refresh'])assert.ok(!bytes.includes(Buffer.from(secret)),'Grant bytes must remain encrypted');}
  peer?.assertHealthy();passed=true;
  process.stdout.write(negative?'Actual native renewal service passed TLS-negative cancellation before dispatch, exact authenticated HTTP admission, durable duplicate observation and encrypted old-grant/receipt reopen. No trusted exchange verified.\n':'Actual native renewal service passed trusted discovery and single-use refresh dispatch, encrypted atomic rotation/retention, scope/JSON/HTTP/lost-reply/redirect rejection, cancellation after dispatch, publication rollback, durable duplicate/restart observations and no replay of uncertain exchanges. Synthetic authority; no automatic renewal or real-account acceptance.\n');
 }finally{
  browserController?.dispose();if(browserView)await browserView.close();
  clearTimeout(timer);if(child.exitCode===null&&child.signalCode===null){child.kill();await exited;}
  if(passed){assert.equal(dirname(await realpath(directory)),parent);await rm(directory,{recursive:true,force:true});}else process.stderr.write('Retained synthetic renewal fixture: '+directory+'\n');
 }
}
