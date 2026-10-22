'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {mkdtemp,readFile,writeFile,mkdir,rm}=require('node:fs/promises');
const {tmpdir}=require('node:os');
const {join,resolve}=require('node:path');

test('saved browser configuration refuses replacement paths and invalid upgrade requests before launch',async()=>{
  const script=resolve(__dirname,'../../../Tools/start-browser.mjs');
  for(const [args,message] of [
    [['--config-mode','saved','--language-server-config','replacement.yaml'],/Saved configuration mode cannot replace profile configuration/],
    [['--config-mode','invalid'],/--config-mode requires saved or workspace/],
    [['--upgrade-backend','invalid'],/--upgrade-backend requires true or false/],
    [['--backend','http://127.0.0.1:1','--upgrade-backend','true'],/Backend attachment cannot upgrade a native profile/]
  ]){
    const child=spawn(process.execPath,[script,...args],{windowsHide:true});
    let stderr='';child.stderr.setEncoding('utf8');child.stderr.on('data',chunk=>stderr+=chunk);
    const code=await new Promise((resolveClose,reject)=>{child.once('error',reject);child.once('close',resolveClose);});
    assert.equal(code,1);assert.match(stderr,message);
  }
});

test('browser launcher serves current workspace actions from freshly built assets',async t=>{
  const root=resolve(__dirname,'../../..');
  const fixture=await mkdtemp(join(tmpdir(),'xmind-browser-launcher-'));
  const assets=join(fixture,'assets');
  const state=join(fixture,'state');
  const child=spawn(process.execPath,[join(root,'Tools/start-browser.mjs'),
    '--backend','http://127.0.0.1:1','--state',state,
    '--assets',assets,'--port','0'],{cwd:root,windowsHide:true});
  t.after(async()=>{
    if(child.exitCode===null&&!child.killed)child.kill();
    await new Promise(resolveClose=>{
      if(child.exitCode!==null||child.signalCode!==null)return resolveClose();
      child.once('close',resolveClose);
    });
    await rm(fixture,{recursive:true,force:true});
  });

  let stdout='',stderr='';
  child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');
  child.stdout.on('data',chunk=>stdout+=chunk);child.stderr.on('data',chunk=>stderr+=chunk);
  const origin=await new Promise((resolveOrigin,reject)=>{
    const deadline=setTimeout(()=>reject(new Error('Browser launcher did not become ready: '+stderr)),15000);
    child.once('error',error=>{clearTimeout(deadline);reject(error);});
    child.once('close',code=>{clearTimeout(deadline);reject(new Error(`Browser launcher exited ${code}: ${stderr}`));});
    child.stdout.on('data',()=>{
      const match=/xMind Browser ready at (http:\/\/127\.0\.0\.1:\d+)\/ui\//.exec(stdout);
      if(match){clearTimeout(deadline);resolveOrigin(match[1]);}
    });
  });
  const response=await fetch(origin+'/ui/');
  const html=await response.text();
  assert.equal(response.status,200,html);
  assert.match(html,/id="add-workspace"[^>]*>New workspace/);
  assert.match(html,/id="add-workspace-folder"[^>]*>Add folder/);
  assert.match(html,/primary folder of a new local workspace/);
  const activeBefore=await readFile(join(state,'active.json'));
  const stale='/* Explicit stale UI asset fixture; no provider/native execution. */\n';
  await writeFile(join(assets,'client.js'),stale);
  await fetch(origin+'/ui/');
  assert.equal(await (await fetch(origin+'/ui/client.js')).text(),stale);
  const reuse=spawn(process.execPath,[join(root,'Tools/start-browser.mjs'),
    '--backend','http://127.0.0.1:1','--state',state,
    '--assets',assets,'--port','0'],{cwd:root,windowsHide:true});
  let reusedOutput='';reuse.stdout.setEncoding('utf8');reuse.stdout.on('data',chunk=>reusedOutput+=chunk);
  const reused=await new Promise((resolveClose,reject)=>{
    reuse.once('error',reject);
    reuse.once('close',(code,signal)=>resolveClose({code,signal}));
  });
  assert.deepEqual(reused,{code:0,signal:null});
  assert.match(reusedOutput,new RegExp('already running at '+origin.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')+'/ui/'));
  await fetch(origin+'/ui/');
  assert.equal(await (await fetch(origin+'/ui/client.js')).text(),await readFile(join(root,'extensions/vscode/client.js'),'utf8'),'Reusing the live host must refresh its generated assets');
  assert.deepEqual(await readFile(join(state,'active.json')),activeBefore,'Refreshing UI assets must keep the same host and backend metadata');
  assert.equal(child.exitCode,null);
  await writeFile(join(assets,'client.js'),stale);
  const conflict=spawn(process.execPath,[join(root,'Tools/start-browser.mjs'),
    '--backend','http://127.0.0.1:2','--state',state,
    '--assets',assets,'--port','0'],{cwd:root,windowsHide:true});
  let conflictError='';conflict.stderr.setEncoding('utf8');conflict.stderr.on('data',chunk=>conflictError+=chunk);
  const conflicted=await new Promise((resolveClose,reject)=>{
    conflict.once('error',reject);
    conflict.once('close',(code,signal)=>resolveClose({code,signal}));
  });
  assert.equal(conflicted.code,1);
  assert.match(conflictError,/different backend/);
  assert.equal(await readFile(join(assets,'client.js'),'utf8'),stale,'A backend conflict must not write existing assets');
  const otherAssets=join(fixture,'other-assets');await mkdir(otherAssets);
  const assetConflict=spawn(process.execPath,[join(root,'Tools/start-browser.mjs'),
    '--backend','http://127.0.0.1:1','--state',state,
    '--assets',otherAssets,'--port','0'],{cwd:root,windowsHide:true});
  let assetConflictError='';assetConflict.stderr.setEncoding('utf8');assetConflict.stderr.on('data',chunk=>assetConflictError+=chunk);
  const assetConflicted=await new Promise((resolveClose,reject)=>{assetConflict.once('error',reject);assetConflict.once('close',(code,signal)=>resolveClose({code,signal}));});
  assert.equal(assetConflicted.code,1);assert.match(assetConflictError,/different assets/);
  assert.equal(await readFile(join(assets,'client.js'),'utf8'),stale);await assert.rejects(readFile(join(otherAssets,'client.js')));
  assert.deepEqual(await readFile(join(state,'active.json')),activeBefore);
  if(process.platform==='win32'){
    // Node's child_process.kill(SIGTERM) maps to unconditional process
    // termination on Windows; the lifecycle's owned-child behavior is covered
    // directly by launcher-lifecycle.test.cjs.
    const closed=new Promise(resolveClose=>child.once('close',resolveClose));
    child.kill();await closed;
  }else{
    const closed=new Promise(resolveClose=>child.once('close',(code,signal)=>resolveClose({code,signal})));
    child.kill('SIGTERM');
    assert.deepEqual(await closed,{code:0,signal:null});
    await assert.rejects(readFile(join(state,'active.json')));
  }
});
