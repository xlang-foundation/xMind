'use strict';
const workspaceSkillRouteTest=require('node:test'),workspaceSkillRouteAssert=require('node:assert/strict');
workspaceSkillRouteTest('session skill controls expose only exact GET and POST routes',async()=>{
 const {allowedApiRoute}=await import('../server.mjs');for(const method of ['GET','POST','PUT','PATCH','DELETE','HEAD'])workspaceSkillRouteAssert.equal(allowedApiRoute('/v1/sessions/session/skills',method),['GET','POST'].includes(method));for(const route of ['/v1/sessions/session/skills/load','/v1/sessions/session/skills/','/v1/sessions/session/skills/../credentials'])workspaceSkillRouteAssert.equal(allowedApiRoute(route,'GET'),false);
});
workspaceSkillRouteTest('workspace skill catalogue is an exact read-only browser route',async()=>{
 const {allowedApiRoute}=await import('../server.mjs');
 for(const route of ['/v1/workspace','/v1/workspace/skills']){workspaceSkillRouteAssert.equal(allowedApiRoute(route,'GET'),true);for(const method of ['POST','PUT','DELETE'])workspaceSkillRouteAssert.equal(allowedApiRoute(route,method),false);}
 for(const route of ['/v1/workspace/skills/load','/v1/workspace/skills/../credentials','/v1/workspace/skills/'])workspaceSkillRouteAssert.equal(allowedApiRoute(route,'GET'),false);
});
workspaceSkillRouteTest('browser view cannot invoke the native provider YAML file reader',async()=>{
 const {allowedApiRoute}=await import('../server.mjs');
 for(const method of ['GET','POST','PUT','PATCH','DELETE','HEAD'])workspaceSkillRouteAssert.equal(allowedApiRoute('/v1/provider/configuration/import',method),false);
});
// Explicit synthetic native peer/assets: only thin view routing and durable
// access forwarding are tested. No native SQLite, model or agent execution.
const test=require('node:test'),assert=require('node:assert/strict');
const {createServer}=require('node:http'),{mkdtemp,writeFile,rm}=require('node:fs/promises'),{tmpdir}=require('node:os'),{join}=require('node:path');
test('context and graph-resume view allowlist restricts methods and exposes no checkpoint/state mutation route',async()=>{
 const {allowedApiRoute}=await import('../server.mjs');
 for(const path of ['/v1/sessions/session/context','/v1/sessions/session/context/requests/request'])for(const method of ['GET','POST','PUT','PATCH','DELETE','HEAD'])assert.equal(allowedApiRoute(path,method),method==='GET');
 for(const path of ['/v1/sessions/session/context/compact','/v1/graph-runs/root/resume'])for(const method of ['GET','POST','PUT','PATCH','DELETE','HEAD'])assert.equal(allowedApiRoute(path,method),method==='POST');
 for(const path of ['/v1/sessions/session/context/state','/v1/sessions/session/context/checkpoint','/v1/sessions/session/context/requests/foreign%2Frequest','/v1/sessions/session/context/compact/extra','/v1/graph-runs/root/resume/state'])for(const method of ['GET','POST'])assert.equal(allowedApiRoute(path,method),false);
});
test('planning view allowlist exposes only scoped observation, human input and resume methods',async()=>{
 const {allowedApiRoute}=await import('../server.mjs');
 for(const path of ['/v1/agent/planning','/v1/runs/owner/plan']){assert.equal(allowedApiRoute(path,'GET'),true);for(const method of ['POST','PUT','PATCH','DELETE','HEAD'])assert.equal(allowedApiRoute(path,method),false);}
 for(const path of ['/v1/runs/owner/plan/human/question','/v1/runs/owner/plan/resume']){assert.equal(allowedApiRoute(path,'POST'),true);for(const method of ['GET','PUT','PATCH','DELETE','HEAD'])assert.equal(allowedApiRoute(path,method),false);}
 for(const path of ['/v1/runs/owner/plan/spawn','/v1/runs/owner/plan/state','/v1/runs/owner/plan/spec','/v1/runs/owner/plan/revise','/v1/runs/owner/plan/human/question/extra','/v1/runs/owner/plan/human/foreign%2Fid','/v1/runs/owner/plan/human/..','/v1/plans/plan/human/question'])for(const method of ['GET','POST'])assert.equal(allowedApiRoute(path,method),false);
});
test('view forwards only allowed planning requests at the configured native origin and keeps durable cookie authentication',async()=>{
 const {createBrowserServer}=await import('../server.mjs'),directory=await mkdtemp(join(tmpdir(),'xmind-planning-view-')),observed=[];
 for(const name of ['patch-review.js','index.html','browser.js','browser.css','chat.js','chat.css','client.js','marked.js','purify.js'])await writeFile(join(directory,name),'/* Synthetic access adapter asset */');
 const master='synthetic-planning-view-master-'.padEnd(64,'x'),credential='a'.repeat(64)+'.'+'b'.repeat(64);let expectedOrigin,view;
 const peer=createServer(async(request,response)=>{
  let input='';for await(const chunk of request)input+=chunk;observed.push({path:request.url,method:request.method,authorization:request.headers.authorization,viewOrigin:request.headers['x-xmind-view-origin'],input});
  const data=request.url==='/v1/view-sessions'?{credential,expires_unix_ms:9999999999999,max_age_seconds:28800}:request.url==='/v1/view-sessions/current'?{connected:true}:{synthetic_adapter_receipt:true};
  const accepted=request.url==='/v1/view-sessions'?request.headers.authorization==='Bearer '+master:request.headers.authorization==='View '+credential&&request.headers['x-xmind-view-origin']===expectedOrigin;
  response.writeHead(accepted?200:401,{'Content-Type':'application/json'});response.end(JSON.stringify(accepted?data:{detail:'Synthetic access rejected'}));
 });
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
 try{
  view=await createBrowserServer({backend:'http://127.0.0.1:'+peer.address().port,assetRoot:directory});expectedOrigin=await view.listen();const sameOrigin={Origin:expectedOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'};
  const enrollment=await fetch(expectedOrigin+'/ui/session',{method:'POST',headers:{...sameOrigin,Authorization:'Bearer '+master},body:'{}'});assert.equal(enrollment.status,200);const cookie=enrollment.headers.get('set-cookie').split(';')[0];
  const raw=' {"number":18446744073709551617,"decimal":1.00000000000000000001} ';const body=JSON.stringify({input_json:raw,expected_revision:2,expected_state_sequence:7});
  for(const path of ['/v1/agent/planning','/v1/runs/owner/plan'])assert.equal((await fetch(expectedOrigin+path,{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,200);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan/human/question',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body})).status,200);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan/resume',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:'{"expected_revision":2,"expected_state_sequence":8}'})).status,200);
  for(const path of ['/v1/sessions/session/context?model_id=model%3Avariant%2Fv1','/v1/sessions/session/context/requests/request?model_id=model%3Avariant%2Fv1'])assert.equal((await fetch(expectedOrigin+path,{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,200);
  const compact=JSON.stringify({id:'request',model_id:'model:variant/v1',expected_head_revision:4});assert.equal((await fetch(expectedOrigin+'/v1/sessions/session/context/compact',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:compact})).status,200);
  assert.equal((await fetch(expectedOrigin+'/v1/graph-runs/root/resume',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:'{"expected_checkpoint_revision":7}'})).status,200);
  assert.equal(observed.find(value=>value.path==='/v1/sessions/session/context/compact').input,compact);assert.equal(observed.find(value=>value.path.startsWith('/v1/sessions/session/context?')).path,'/v1/sessions/session/context?model_id=model%3Avariant%2Fv1');
  for(const path of ['/v1/mcp/authorization/servers','/v1/mcp/authorization/attempts/login-1'])assert.equal((await fetch(expectedOrigin+path,{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,200);
  const mcpStart=JSON.stringify({server_id:'tools.peer',expected_config_revision:3,expected_credential_revision:0,request_id:'login-1'});
  assert.equal((await fetch(expectedOrigin+'/v1/mcp/authorization/attempts',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:mcpStart})).status,200);
  assert.equal((await fetch(expectedOrigin+'/v1/mcp/authorization/attempts/login-1/cancel',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:'{}'})).status,200);
  const forwardedMcp=observed.find(value=>value.path==='/v1/mcp/authorization/attempts');assert.equal(forwardedMcp.input,mcpStart);assert.equal(forwardedMcp.authorization,'View '+credential);assert.equal(forwardedMcp.viewOrigin,expectedOrigin);
  const input=observed.find(value=>value.path.endsWith('/human/question'));assert.equal(input.input,body);assert.equal(JSON.parse(input.input).input_json,raw);assert.equal(input.method,'POST');assert.equal(input.authorization,'View '+credential);assert.equal(input.viewOrigin,expectedOrigin);assert.equal(Object.hasOwn(JSON.parse(input.input),'actor'),false);
  const count=observed.length;
  for(const path of ['/v1/mcp/authorization/servers?after=0','/v1/mcp/authorization/attempts/login-1?after=0'])assert.equal((await fetch(expectedOrigin+path,{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,400,path);
  // fetch normalizes an empty query away; raw HTTP retains the actual target.
  const emptyQueryStatus=await new Promise((resolve,reject)=>{const request=require('node:http').request(expectedOrigin,{path:'/v1/mcp/authorization/servers?',headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}},response=>{response.resume();response.on('end',()=>resolve(response.statusCode));});request.on('error',reject);request.end();});assert.equal(emptyQueryStatus,400);
  for(const [path,method] of [['/v1/agent/planning','POST'],['/v1/runs/owner/plan','POST'],['/v1/runs/owner/plan/resume','GET'],['/v1/runs/owner/plan/human/question','GET'],['/v1/runs/owner/plan/spawn','POST'],['/v1/runs/owner/plan/state','POST'],['/v1/runs/owner/plan/human/foreign%2Fid','POST']])assert.equal((await fetch(expectedOrigin+path,{method,headers:{...sameOrigin,Cookie:cookie},...(method==='POST'?{body:'{}'}:{})})).status,404);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan?after=0',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,400);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan/human/question?after=0',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body})).status,400);
  for(const path of ['/v1/sessions/session/context?model_id=model&model_id=other','/v1/sessions/session/context?after=0','/v1/sessions/session/context?model_id=bad%20model'])assert.equal((await fetch(expectedOrigin+path,{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,400);
  for(const path of ['/v1/sessions/session/context/compact?model_id=model','/v1/graph-runs/root/resume?after=0'])assert.equal((await fetch(expectedOrigin+path,{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:'{}'})).status,400);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan/human/question',{method:'POST',headers:{'Content-Type':'application/json',Cookie:cookie,'Sec-Fetch-Site':'same-origin'},body})).status,403);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan',{headers:{Cookie:cookie,Origin:'http://127.0.0.1:1','Sec-Fetch-Site':'cross-site'}})).status,403);
  assert.equal((await fetch(expectedOrigin+'/v1/runs/owner/plan')).status,401);assert.equal(observed.length,count,'Rejected methods, identities, query or origins must never reach native');
  assert.equal((await fetch(expectedOrigin+'/ui/session',{method:'POST',headers:{...sameOrigin,Cookie:cookie},body:'{}'})).status,200,'A view refresh reuses durable access without master/provider re-entry');
  assert.equal(observed.filter(value=>value.authorization==='Bearer '+master).length,1);assert.ok(observed.every(value=>!value.input.includes(master)));
 }finally{if(view)await view.close();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));await rm(directory,{recursive:true,force:true});}
});
