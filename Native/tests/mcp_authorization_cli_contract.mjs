// Actual compiled C++ console and real HTTP sockets; domain DTOs below are
// explicitly synthetic. No real OAuth grant/login or browser launch is claimed.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [program]=process.argv.slice(2),execute=promisify(execFile);
const token='synthetic-mcp-cli-owner-token-not-live',privateValue='synthetic-private-token-never-print';
const server=()=>({id:'tools.peer',config_revision:3,credential_revision:0,enabled:true,configured:true,state:'needs_login',expires_unix_ms:null});
const attempt=(id='request-1',fields={})=>({id,server_id:'tools.peer',state:'discovering',config_revision:3,credential_revision:0,authorization_url:null,expires_unix_ms:9999999999999,reason:null,cancellation_requested:false,...fields});
const calls=[];let responseOverride,current=attempt(),status=200,watchReads=0,grantRevision=0,loseRenewalReply=false;
const peer=createServer(async(request,response)=>{
 let input='';for await(const chunk of request)input+=chunk;assert.equal(request.headers.authorization,'Bearer '+token);const call={path:request.url,method:request.method,body:input?JSON.parse(input):undefined};calls.push(call);
 let result=call.path==='/v1/health'?{agent_execution:false}:call.path.endsWith('/servers')?{servers:[{...server(),credential_revision:grantRevision,state:grantRevision?'authorized':'needs_login'}]}:current;
 if(call.path==='/v1/mcp/authorization/attempts'&&request.method==='POST'){assert.deepEqual(Object.keys(call.body).sort(),['expected_config_revision','expected_credential_revision','request_id','server_id']);assert.equal(call.body.expected_config_revision,3);assert.equal(call.body.expected_credential_revision,0);assert.equal(call.body.server_id,'tools.peer');assert.match(call.body.request_id,/^[0-9a-f]{64}$/);current=attempt(call.body.request_id);result=current;}
 if(call.path==='/v1/mcp/authorization/renewals'&&request.method==='POST'){assert.deepEqual(Object.keys(call.body).sort(),['expected_config_revision','expected_credential_revision','request_id','server_id']);assert.equal(call.body.expected_config_revision,3);assert.equal(call.body.expected_credential_revision,grantRevision);assert.equal(call.body.server_id,'tools.peer');assert.match(call.body.request_id,/^[0-9a-f]{64}$/);current=attempt(call.body.request_id,{credential_revision:grantRevision,state:'exchanging'});result=current;if(loseRenewalReply){response.destroy();return;}}
 if(call.path.endsWith('/cancel')){assert.deepEqual(call.body,{});result=current=attempt(current.id,{state:'cancelled',reason:'cancelled',cancellation_requested:true});}
 if(responseOverride!==undefined)result=typeof responseOverride==='function'?responseOverride(call):responseOverride;
 response.writeHead(status===200&&call.method==='POST'?202:status,{'Content-Type':'application/json'});response.end(typeof result==='string'?result:JSON.stringify(result));
});
await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));const port=String(peer.address().port),env={...process.env,XMIND_AUTH_TOKEN:token};
async function run(args,{failed=false}={}){
 let result;try{result=await execute(program,['--port',port,...args],{env,windowsHide:true,timeout:10000,maxBuffer:131072});result.code=0;}catch(error){result=error;}
 assert.equal(typeof result.code,'number',result.message);assert.equal(result.code,failed?1:0,result.stderr);assert.ok(!result.stdout.includes(privateValue));assert.ok(!result.stderr.includes(privateValue));return result;
}
// Interactive tests explicitly write the console's stdin.
async function chat(input){
 const {spawn}=await import('node:child_process');const child=spawn(program,['--port',port,'chat'],{env,windowsHide:true,stdio:['pipe','pipe','pipe']});let stdout='',stderr='';child.stdout.on('data',data=>stdout+=data);child.stderr.on('data',data=>stderr+=data);const timer=setTimeout(()=>child.kill(),10000);try{child.stdin.end(input);const [code,signal]=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>resolve([code,signal]));});assert.equal(signal,null);assert.equal(code,0,stderr);return {stdout,stderr};}finally{clearTimeout(timer);}
}
try{
 assert.equal(JSON.parse((await run(['mcp-auth'])).stdout).servers[0].id,'tools.peer');
 const admitted=await run(['mcp-login','tools.peer']),started=JSON.parse(admitted.stdout);assert.equal(started.state,'discovering');assert.match(admitted.stderr,new RegExp(started.id));assert.equal(calls.filter(c=>c.method==='POST').length,1);
 assert.equal(JSON.parse((await run(['mcp-login-status',started.id])).stdout).id,started.id);
 assert.equal(JSON.parse((await run(['mcp-login-cancel',started.id])).stdout).state,'cancelled');const cancelledCount=calls.length;
 assert.equal(JSON.parse((await run(['mcp-login-cancel',started.id])).stdout).state,'cancelled');assert.equal(calls.length,cancelledCount+1,'Terminal cancellation must only inspect, with zero POST');
 const before=calls.length;for(const args of [['mcp-login','https://attacker.example.test'],['mcp-login-status','foreign/id'],['mcp-login-cancel','../request'],['mcp-login-open','request?secret'],['mcp-login-watch','foreign/request']])await run(args,{failed:true});assert.equal(calls.length,before,'Invalid argument identities never reach HTTP');
 current=attempt('request-1');
 for(const changed of [ {...attempt(),access_token:privateValue},{...attempt(),id:'foreign'},{...attempt(),state:'connected',credential_revision:0},{...attempt(),state:'awaiting_callback',authorization_url:'javascript:'+privateValue},{...attempt(),state:'awaiting_callback',authorization_url:'https://name:'+privateValue+'@issuer.example.test/authorize'},{...attempt(),state:'awaiting_callback',authorization_url:'http://issuer.example.test/authorize'},{...attempt(),state:'failed',reason:privateValue},'{"id":"request-1","id":"request-1","private":"'+privateValue+'"}']){responseOverride=changed;await run(['mcp-login-status','request-1'],{failed:true});}
 responseOverride={servers:[{...server(),refresh_token:privateValue}]};await run(['mcp-auth'],{failed:true});responseOverride={servers:[server(),server()]};await run(['mcp-auth'],{failed:true});
 responseOverride={detail:privateValue,access_token:privateValue};status=401;await run(['mcp-login-status','request-1'],{failed:true});status=200;responseOverride=undefined;
 current=attempt('request-1',{state:'connected',credential_revision:1});const terminalOpen=calls.length;await run(['mcp-login-open','request-1'],{failed:true});assert.equal(calls.length,terminalOpen+1,'Terminal attempts cannot open a browser or dispatch effects');
 responseOverride=call=>{if(call.path.endsWith('/servers'))return {servers:[server()]};return ++watchReads===1?attempt():attempt('request-1',{state:'connected',credential_revision:1});};
 const watched=await run(['mcp-login-watch','request-1']);assert.deepEqual(watched.stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line).state),['discovering','connected']);assert.equal(watchReads,2);responseOverride=undefined;
 const priorPosts=calls.filter(c=>c.method==='POST').length,interactive=await chat('/mcp\n/mcp-login tools.peer\n/exit\n');const records=interactive.stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line));assert.deepEqual(records.map(value=>value.type),['mcp_authorization_servers','mcp_authorization_attempt']);assert.equal(calls.filter(c=>c.method==='POST').length,priorPosts+1);assert.ok(!calls.slice(-3).some(call=>call.path.endsWith('/cancel')),'Chat exit must leave pending login owned by the backend');
 const beforeEmpty=calls.filter(c=>c.method==='POST').length;await run(['mcp-refresh','tools.peer'],{failed:true});assert.equal(calls.filter(c=>c.method==='POST').length,beforeEmpty,'An absent grant must not admit renewal');
 grantRevision=7;const renewal=await run(['mcp-refresh','tools.peer']),renewed=JSON.parse(renewal.stdout);assert.equal(renewed.state,'exchanging');assert.equal(renewed.credential_revision,7);assert.match(renewal.stderr,new RegExp('MCP renewal request '+renewed.id));
 for(const reason of ['refresh_uncertain','refresh_recovery_required']){current=attempt(renewed.id,{credential_revision:7,state:'failed',reason});assert.equal(JSON.parse((await run(['mcp-login-status',renewed.id])).stdout).reason,reason);}
 loseRenewalReply=true;const beforeLost=calls.filter(c=>c.path.endsWith('/renewals')).length,lost=await run(['mcp-refresh','tools.peer'],{failed:true});assert.equal(calls.filter(c=>c.path.endsWith('/renewals')).length,beforeLost+1,'Lost replies must never cause another exchange admission');assert.match(lost.stderr,new RegExp(current.id));loseRenewalReply=false;
 const beforeObserve=calls.filter(c=>c.method==='POST').length;assert.equal(JSON.parse((await run(['mcp-login-status',current.id])).stdout).state,'exchanging');assert.equal(calls.filter(c=>c.method==='POST').length,beforeObserve);
 const beforeChatRenew=calls.filter(c=>c.path.endsWith('/renewals')).length;await chat('/mcp-refresh tools.peer\n/exit\n');assert.equal(calls.filter(c=>c.path.endsWith('/renewals')).length,beforeChatRenew+1);
 process.stdout.write('Native unified console MCP commands passed exact authenticated CAS admission, generated/published request identity, status/cancellation/terminal non-dispatch, interactive detach, bounded observation and private/duplicate/foreign/unsafe-link rejection against an independent synthetic HTTP peer. No real OAuth login or browser launch verified.\n');
}finally{peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));}
