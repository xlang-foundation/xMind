'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {EventEmitter}=require('node:events');
const {mkdtemp,readFile,writeFile,rm}=require('node:fs/promises');
const {tmpdir}=require('node:os');
const {join}=require('node:path');
const {createServer}=require('node:http');

for(const mode of ['unavailable','timeout'])test('browser host '+mode+' observation preserves its record instead of declaring it stopped',async()=>{
  const {findExistingBrowserHost}=await import('../../../Tools/browser-host-lifecycle.mjs');
  const state=await mkdtemp(join(tmpdir(),'xmind-browser-observation-'));
  const server=createServer((request,response)=>{if(mode==='unavailable'){response.writeHead(503,{'Content-Type':'application/json'});response.end('{"detail":"Synthetic observation unavailable"}');}});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const record=JSON.stringify({url:'http://127.0.0.1:'+server.address().port+'/ui/',backend:'http://127.0.0.1:61234',assets:state,viewPid:process.pid});
  const active=join(state,'active.json');await writeFile(active,record);
  try{await assert.rejects(findExistingBrowserHost(state,'http://127.0.0.1:61234',state),/could not be observed.*preserved/i);assert.equal(await readFile(active,'utf8'),record);assert.equal(server.listening,true);}
  finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));await rm(state,{recursive:true,force:true});}
});

test('a closed browser endpoint permits a new attachment without deleting saved metadata',async()=>{
  const {findExistingBrowserHost}=await import('../../../Tools/browser-host-lifecycle.mjs');
  const state=await mkdtemp(join(tmpdir(),'xmind-browser-closed-')),server=createServer();
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const port=server.address().port;await new Promise(resolve=>server.close(resolve));
  const active=join(state,'active.json'),record=JSON.stringify({url:'http://127.0.0.1:'+port+'/ui/',backend:'http://127.0.0.1:61234',assets:state,viewPid:process.pid});await writeFile(active,record);
  try{assert.equal(await findExistingBrowserHost(state,'http://127.0.0.1:61234',state),undefined);assert.equal(await readFile(active,'utf8'),record);}finally{await rm(state,{recursive:true,force:true});}
});

test('browser launcher shutdown closes its adapter and owned native view and retires its own local credentials',async()=>{
  const {createBrowserHostLifecycle}=await import('../../../Tools/browser-host-lifecycle.mjs');
  const state=await mkdtemp(join(tmpdir(),'xmind-browser-lifecycle-'));
  const activePath=join(state,'active.json'),tokenPath=join(state,'auth.token'),token='a'.repeat(64),url='http://127.0.0.1:60405/ui/';
  await writeFile(activePath,JSON.stringify({viewPid:731,url,backend:'http://127.0.0.1:61234'}));
  await writeFile(tokenPath,token);
  let viewClosed=false,killed=false;
  const child=new EventEmitter();child.exitCode=null;child.signalCode=null;
  child.kill=()=>{killed=true;child.signalCode='SIGTERM';setImmediate(()=>child.emit('close',null,'SIGTERM'));return true;};
  const lifecycle=createBrowserHostLifecycle({view:{close:async()=>{viewClosed=true;}},nativeChild:child,state,tokenFile:tokenPath,ownedToken:token,viewPid:731,url});
  try{
    await lifecycle.shutdown();
    assert.equal(viewClosed,true);
    assert.equal(killed,true);
    await assert.rejects(readFile(activePath));
    await assert.rejects(readFile(tokenPath));
    await lifecycle.shutdown();
  }finally{await rm(state,{recursive:true,force:true});}
});

test('an attached browser adapter shutdown leaves its shared backend and other launch metadata alone',async()=>{
  const {createBrowserHostLifecycle}=await import('../../../Tools/browser-host-lifecycle.mjs');
  const state=await mkdtemp(join(tmpdir(),'xmind-browser-attached-'));
  const activePath=join(state,'active.json'),url='http://127.0.0.1:60406/ui/';
  await writeFile(activePath,JSON.stringify({viewPid:732,url:'http://127.0.0.1:60405/ui/',backend:'http://127.0.0.1:61235'}));
  let viewClosed=false;
  const lifecycle=createBrowserHostLifecycle({view:{close:async()=>{viewClosed=true;}},state,viewPid:732,url});
  try{
    await lifecycle.shutdown();
    assert.equal(viewClosed,true);
    assert.deepEqual(JSON.parse(await readFile(activePath,'utf8')),{viewPid:732,url:'http://127.0.0.1:60405/ui/',backend:'http://127.0.0.1:61235'});
  }finally{await rm(state,{recursive:true,force:true});}
});
