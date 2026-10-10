'use strict';
// Synthetic native access peer: verifies only the thin HTTP adapter's wire
// behavior. It is not evidence of C++ persistence or real model execution.
const test=require('node:test'),assert=require('node:assert/strict');
const {createServer}=require('node:http'),{resolve}=require('node:path');
test('browser adapter forwards durable native view credentials across restart without retaining the master token',async()=>{
  const {createBrowserServer}=await import('../server.mjs');
  const assetRoot=resolve(process.env.XMIND_BROWSER_TEST_ASSETS||'.agentflow/browser-assets');
  const master='synthetic-master-token-'.padEnd(64,'x'),credential='a'.repeat(64)+'.'+'b'.repeat(64),observed=[];
  let active=false,boundOrigin,mode='ok',view;
  const peer=createServer(async(request,response)=>{
    let input='';for await(const chunk of request)input+=chunk;
    observed.push({path:request.url,authorization:request.headers.authorization,origin:request.headers['x-xmind-view-origin'],input});
    const reply=(status,data)=>{response.writeHead(status,{'Content-Type':'application/json'});response.end(JSON.stringify(data));};
    if(request.url==='/v1/view-sessions'){
      if(request.headers.authorization!=='Bearer '+master){reply(401,{});return;}
      assert.deepEqual(Object.keys(JSON.parse(input)),['origin']);boundOrigin=JSON.parse(input).origin;
      active=true;reply(200,{credential:mode==='invalid'?'invalid':credential,expires_unix_ms:Date.now()+28800000+(mode==='clock-ahead'?5000:mode==='clock-behind'?-5000:0),max_age_seconds:mode==='excessive-lifetime'?28801:mode==='missing-lifetime'?undefined:28800});return;
    }
    if(request.headers.authorization!=='View '+credential||request.headers['x-xmind-view-origin']!==boundOrigin||!active){reply(401,{});return;}
    if(request.url==='/v1/view-sessions/current'){reply(mode==='unavailable'?503:200,{connected:true});return;}
    if(request.url==='/v1/view-sessions/revoke'){active=false;reply(200,{connected:false});return;}
    if(request.url==='/v1/health'){reply(200,{status:'ok'});return;}
    if(['/v1/agent/delegation','/v1/runs/parent/children','/v1/runs/parent/children/leaf/history','/v1/runs/parent/tree-events?after=12'].includes(request.url)){assert.equal(request.method,'GET');reply(200,[]);return;}reply(404,{});
  });
  try{
    await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));const backend='http://127.0.0.1:'+peer.address().port;
    view=await createBrowserServer({backend,assetRoot});const origin=await view.listen();
    const headers={Origin:origin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'};
    const login=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+master},body:'{}'});assert.equal(login.status,200);assert.deepEqual(await login.json(),{connected:true});
    const cookie=login.headers.get('set-cookie').split(';')[0];assert.ok(!cookie.includes(master));assert.match(login.headers.get('set-cookie'),/HttpOnly; SameSite=Strict/);
    assert.equal((await fetch(origin+'/v1/health',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,200);
    assert.equal(observed.at(-1).authorization,'View '+credential);assert.equal(observed.at(-1).origin,origin);
    for(const path of ['/v1/agent/delegation','/v1/runs/parent/children','/v1/runs/parent/children/leaf/history','/v1/runs/parent/tree-events?after=12']){const result=await fetch(origin+path,{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(result.status,200);assert.equal(observed.at(-1).path,path);assert.equal(observed.at(-1).authorization,'View '+credential);}
    const beforeReadOnly=observed.length;assert.equal((await fetch(origin+'/v1/runs/parent/children',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'})).status,404);assert.equal((await fetch(origin+'/v1/runs/parent/tree-events?after=12&after=13',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,400);assert.equal(observed.length,beforeReadOnly,'Read-only child metadata cannot spawn children or forward an ambiguous cursor');
    const count=observed.length;
    assert.equal((await fetch(origin+'/v1/health',{headers:{Cookie:cookie+'; '+cookie,'Sec-Fetch-Site':'same-origin'}})).status,401);
    assert.equal((await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie,Origin:'https://untrusted.invalid'},body:'{}'})).status,403);assert.equal(observed.length,count);
    await view.close();view=null;view=await createBrowserServer({backend,assetRoot});assert.equal(await view.listen(Number(new URL(origin).port)),origin);
    const restored=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'});assert.equal(restored.status,200);assert.equal(restored.headers.get('set-cookie'),null);
    assert.equal(observed.at(-1).path,'/v1/view-sessions/current');assert.equal(observed.at(-1).authorization,'View '+credential);
    mode='unavailable';assert.equal((await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'})).status,502);mode='ok';
    const badLogin=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie,Authorization:'Bearer '+'z'.repeat(64)},body:'{}'});assert.equal(badLogin.status,401);assert.equal(badLogin.headers.get('set-cookie'),null);assert.ok(active);
    mode='invalid';const invalid=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+master},body:'{}'});assert.equal(invalid.status,502);assert.equal(invalid.headers.get('set-cookie'),null);mode='ok';
    for(const clockMode of ['clock-ahead','clock-behind']){mode=clockMode;const enrolled=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+master},body:'{}'});assert.equal(enrolled.status,200);assert.match(enrolled.headers.get('set-cookie'),/Max-Age=28800(?:;|$)/);}
    for(const invalidMode of ['excessive-lifetime','missing-lifetime']){mode=invalidMode;const rejected=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+master},body:'{}'});assert.equal(rejected.status,502);assert.equal((await rejected.json()).error_code,'invalid_view_session');assert.equal(rejected.headers.get('set-cookie'),null);}mode='ok';
    const disconnected=await fetch(origin+'/ui/session/disconnect',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'});assert.equal(disconnected.status,200);assert.match(disconnected.headers.get('set-cookie'),/Max-Age=0/);assert.equal(observed.at(-1).path,'/v1/view-sessions/revoke');assert.equal(observed.at(-1).authorization,'View '+credential);
    assert.equal((await fetch(origin+'/v1/health',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,401);
    assert.equal(observed.filter(item=>item.authorization==='Bearer '+master).length,6,'Master token is sent only for explicit enrollment');
    assert.ok(observed.every(item=>!item.input.includes(master)),'Master token cannot enter session JSON');
  }finally{if(view)await view.close();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));}
});
test('workspace catalogue and switch stay native and rotate the HttpOnly profile credential',async()=>{
 const {createBrowserServer}=await import('../server.mjs'),assetRoot=resolve(process.env.XMIND_BROWSER_TEST_ASSETS||'.agentflow/browser-assets');
 const master='workspace-switch-master-token'.padEnd(64,'x'),first='1'.repeat(64)+'.'+'2'.repeat(64),second='3'.repeat(64)+'.'+'4'.repeat(64),idA='windows-local-file-v1:alpha',idB='windows-local-file-v1:beta';let active=first,selected=idA,view;
 const profiles=[{workspace_id:idA,name:'Alpha',root:'D:\\Projects\\Alpha'},{workspace_id:idB,name:'Beta',root:'D:\\Projects\\Beta'}];
 const peer=createServer(async(request,response)=>{let input='';for await(const chunk of request)input+=chunk;const reply=(status,data)=>{response.writeHead(status,{'Content-Type':'application/json'});response.end(JSON.stringify(data));};
  if(request.url==='/v1/view-sessions'){if(request.headers.authorization!=='Bearer '+master){reply(401,{});return;}active=first;reply(200,{credential:first,expires_unix_ms:Date.now()+28800000,max_age_seconds:28800});return;}
  if(request.headers.authorization!=='View '+active||request.headers['x-xmind-view-origin']!==viewOrigin){reply(401,{});return;}
  if(request.url==='/v1/workspaces'&&request.method==='GET'){reply(200,{selected_workspace_id:selected,workspaces:profiles});return;}
  if(request.url==='/v1/workspaces/select'&&request.method==='POST'){const body=JSON.parse(input);assert.equal(Object.keys(body).length,1);if(body.workspace_id!==idB){reply(404,{});return;}selected=idB;active=second;reply(200,{credential:second,expires_unix_ms:Date.now()+28800000,max_age_seconds:28800,workspace:{configured:true,root:profiles[1].root,workspace_id:idB,authority_id:'b'.repeat(32)}});return;}
  if(request.url==='/v1/health'){reply(200,{status:'ok'});return;}if(request.url==='/v1/view-sessions/revoke'){reply(200,{revoked:true});return;}reply(404,{});
 });let viewOrigin;
 try{await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));view=await createBrowserServer({backend:'http://127.0.0.1:'+peer.address().port,assetRoot});viewOrigin=await view.listen();
  const base={Origin:viewOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'},login=await fetch(viewOrigin+'/ui/session',{method:'POST',headers:{...base,Authorization:'Bearer '+master},body:'{}'});assert.equal(login.status,200);let cookie=login.headers.get('set-cookie').split(';')[0];
  const list=await fetch(viewOrigin+'/ui/workspaces',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(list.status,200);assert.deepEqual(await list.json(),{selected_workspace_id:idA,workspaces:profiles});
  assert.equal((await fetch(viewOrigin+'/ui/workspaces',{headers:{Cookie:cookie,Origin:'https://untrusted.invalid','Sec-Fetch-Site':'same-origin'}})).status,403);
  const switched=await fetch(viewOrigin+'/ui/workspaces/select',{method:'POST',headers:{...base,Cookie:cookie},body:JSON.stringify({workspace_id:idB})});assert.equal(switched.status,200);const result=await switched.json();assert.deepEqual(result,{connected:true,workspace:{configured:true,root:profiles[1].root,workspace_id:idB,authority_id:'b'.repeat(32)}});assert.ok(!JSON.stringify(result).includes(second));const replacement=switched.headers.get('set-cookie');assert.match(replacement,/HttpOnly; SameSite=Strict/);cookie=replacement.split(';')[0];
  assert.equal((await fetch(viewOrigin+'/v1/health',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,200);
  assert.equal((await fetch(viewOrigin+'/v1/health',{headers:{Cookie:login.headers.get('set-cookie').split(';')[0],'Sec-Fetch-Site':'same-origin'}})).status,401);
  assert.equal((await fetch(viewOrigin+'/ui/workspaces/select',{method:'POST',headers:{...base,Cookie:cookie},body:JSON.stringify({workspace_id:'windows-local-file-v1:unknown'})})).status,404);
 }finally{if(view)await view.close();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));}
});
test('event stream routes are read-only and exclude foreign path encodings',async()=>{
 const {allowedApiRoute}=await import('../server.mjs');
 for(const route of ['/v1/runs/root/events/stream','/v1/runs/root/tree-events/stream','/v1/graph-runs/root/events/stream'])for(const method of ['GET','POST','HEAD','PUT','DELETE'])assert.equal(allowedApiRoute(route,method),method==='GET');
 for(const route of ['/v1/runs/root/events/stream/extra','/v1/graph-runs/root/tree-events/stream','/v1/runs/a%2Fb/events/stream'])assert.equal(allowedApiRoute(route,'GET'),false);
});
test('synthetic stream peer forwards bytes before completion and detaches without a cancel command',async()=>{
 const {createBrowserServer}=await import('../server.mjs'),assetRoot=resolve(process.env.XMIND_BROWSER_TEST_ASSETS||'.agentflow/browser-assets'),observed=[];
 const token='synthetic-event-stream-token-'.padEnd(64,'x'),frame='id: 8\nevent: committed\ndata: {"seq":8,"run_id":"root","kind":"fixture","data":{"text":"雪"}}\n\n';let upstream,view,detached=false;
 const peer=createServer(async(request,response)=>{observed.push({path:request.url,authorization:request.headers.authorization,cursor:request.headers['last-event-id']});if(request.url!=='/v1/runs/root/events/stream?after=7'){response.writeHead(404,{'Content-Type':'application/json'});response.end('{}');return;}assert.equal(request.headers.authorization,'Bearer '+token);assert.equal(request.headers['last-event-id'],'7');upstream=response;response.on('close',()=>detached=true);response.writeHead(200,{'Content-Type':'text/event-stream'});response.write(frame);});
 try{
  await new Promise(yes=>peer.listen(0,'127.0.0.1',yes));view=await createBrowserServer({backend:'http://127.0.0.1:'+peer.address().port,assetRoot});const origin=await view.listen(),abort=new AbortController();
  const response=await fetch(origin+'/v1/runs/root/events/stream?after=7',{headers:{Authorization:'Bearer '+token,'Last-Event-ID':'7'},signal:AbortSignal.any([abort.signal,AbortSignal.timeout(5000)])});assert.equal(response.status,200);assert.equal(response.headers.get('content-type'),'text/event-stream');
  const reader=response.body.getReader(),decoder=new TextDecoder();let bytes='';while(!bytes.includes('\n\n')){const result=await reader.read();assert.equal(result.done,false);bytes+=decoder.decode(result.value,{stream:true});}assert.equal(bytes,frame);assert.equal(upstream.writableEnded,false,'The browser must receive the frame while the native peer remains open');abort.abort();
  const until=Date.now()+2000;while(!detached&&Date.now()<until)await new Promise(yes=>setTimeout(yes,10));assert.equal(detached,true);assert.deepEqual(observed.map(value=>value.path),['/v1/runs/root/events/stream?after=7']);
 }finally{if(view)await view.close();peer.closeAllConnections();await new Promise(yes=>peer.close(yes));}
});
