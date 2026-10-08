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
      active=true;reply(200,{credential:mode==='invalid'?'invalid':credential,expires_unix_ms:Date.now()+28800000});return;
    }
    if(request.headers.authorization!=='View '+credential||request.headers['x-xmind-view-origin']!==boundOrigin||!active){reply(401,{});return;}
    if(request.url==='/v1/view-sessions/current'){reply(mode==='unavailable'?503:200,{connected:true});return;}
    if(request.url==='/v1/view-sessions/revoke'){active=false;reply(200,{connected:false});return;}
    if(request.url==='/v1/health'){reply(200,{status:'ok'});return;}reply(404,{});
  });
  try{
    await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));const backend='http://127.0.0.1:'+peer.address().port;
    view=await createBrowserServer({backend,assetRoot});const origin=await view.listen();
    const headers={Origin:origin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'};
    const login=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+master},body:'{}'});assert.equal(login.status,200);assert.deepEqual(await login.json(),{connected:true});
    const cookie=login.headers.get('set-cookie').split(';')[0];assert.ok(!cookie.includes(master));assert.match(login.headers.get('set-cookie'),/HttpOnly; SameSite=Strict/);
    assert.equal((await fetch(origin+'/v1/health',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,200);
    assert.equal(observed.at(-1).authorization,'View '+credential);assert.equal(observed.at(-1).origin,origin);
    const count=observed.length;
    assert.equal((await fetch(origin+'/v1/health',{headers:{Cookie:cookie+'; '+cookie,'Sec-Fetch-Site':'same-origin'}})).status,401);
    assert.equal((await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie,Origin:'https://untrusted.invalid'},body:'{}'})).status,403);assert.equal(observed.length,count);
    await view.close();view=null;view=await createBrowserServer({backend,assetRoot});assert.equal(await view.listen(Number(new URL(origin).port)),origin);
    const restored=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'});assert.equal(restored.status,200);assert.equal(restored.headers.get('set-cookie'),null);
    assert.equal(observed.at(-1).path,'/v1/view-sessions/current');assert.equal(observed.at(-1).authorization,'View '+credential);
    mode='unavailable';assert.equal((await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'})).status,502);mode='ok';
    const badLogin=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Cookie:cookie,Authorization:'Bearer '+'z'.repeat(64)},body:'{}'});assert.equal(badLogin.status,401);assert.equal(badLogin.headers.get('set-cookie'),null);assert.ok(active);
    mode='invalid';const invalid=await fetch(origin+'/ui/session',{method:'POST',headers:{...headers,Authorization:'Bearer '+master},body:'{}'});assert.equal(invalid.status,502);assert.equal(invalid.headers.get('set-cookie'),null);mode='ok';
    const disconnected=await fetch(origin+'/ui/session/disconnect',{method:'POST',headers:{...headers,Cookie:cookie},body:'{}'});assert.equal(disconnected.status,200);assert.match(disconnected.headers.get('set-cookie'),/Max-Age=0/);assert.equal(observed.at(-1).path,'/v1/view-sessions/revoke');assert.equal(observed.at(-1).authorization,'View '+credential);
    assert.equal((await fetch(origin+'/v1/health',{headers:{Cookie:cookie,'Sec-Fetch-Site':'same-origin'}})).status,401);
    assert.equal(observed.filter(item=>item.authorization==='Bearer '+master).length,2,'Master token is sent only for explicit enrollment');
    assert.ok(observed.every(item=>!item.input.includes(master)),'Master token cannot enter session JSON');
  }finally{if(view)await view.close();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));}
});
