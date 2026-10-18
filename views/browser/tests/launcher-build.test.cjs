'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {mkdtemp,readFile,rm}=require('node:fs/promises');
const {tmpdir}=require('node:os');
const {join,resolve}=require('node:path');

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
