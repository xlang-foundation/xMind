'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {browserViewLauncher}=require('../browser-view');
function fixture(openNative=async()=> 'http://127.0.0.1:62001/ui/'){
  const opened=[],context={subscriptions:[]};
  const vscode={Uri:{parse:value=>value},env:{openExternal:async value=>{opened.push(value);return true;}}};
  return {opened,context,vscode,launcher:browserViewLauncher(vscode,context,{openNative})};
}
test('browser command opens the native URL without accessing or copying credentials',async()=>{
  let starts=0;const f=fixture(async()=>{starts++;return 'http://127.0.0.1:62001/ui/';});
  assert.equal(await f.launcher.open(),'http://127.0.0.1:62001/ui/');
  assert.equal(await f.launcher.open(),'http://127.0.0.1:62001/ui/');
  assert.equal(starts,2);assert.deepEqual(f.opened,['http://127.0.0.1:62001/ui/','http://127.0.0.1:62001/ui/']);
  f.launcher.dispose();await assert.rejects(f.launcher.open(),/closed/);
});
test('native browser URLs reject remote hosts, credentials, queries and foreign paths',async()=>{
  for(const value of ['https://remote.invalid/ui/','http://localhost:62001/ui/','http://127.0.0.1:62001/ui/?token=secret','http://user:secret@127.0.0.1:62001/ui/','http://127.0.0.1:62001/v1/health']){
    const f=fixture(async()=>value);await assert.rejects(f.launcher.open());assert.deepEqual(f.opened,[]);f.launcher.dispose();
  }
});
test('disposal during native startup prevents opening and does not retire the native host',async()=>{
  let complete;const f=fixture(()=>new Promise(resolve=>{complete=resolve;}));
  const opening=f.launcher.open();f.context.subscriptions[0].dispose();complete('http://127.0.0.1:62001/ui/');
  await assert.rejects(opening,/closed/);assert.deepEqual(f.opened,[]);
});
test('concurrent browser startup is rejected while the original command remains owned',async()=>{
  let complete;const f=fixture(()=>new Promise(resolve=>{complete=resolve;}));
  const opening=f.launcher.open();await assert.rejects(f.launcher.open(),/already opening/);
  complete('http://127.0.0.1:62001/ui/');await opening;assert.equal(f.opened.length,1);f.launcher.dispose();
});
test('failed native startup does not open a page and can be inspected with a later command',async()=>{
  let calls=0;const f=fixture(async()=>{if(++calls===1)throw new Error('Native owner is busy');return 'http://127.0.0.1:62001/ui/';});
  await assert.rejects(f.launcher.open(),/busy/);assert.deepEqual(f.opened,[]);await f.launcher.open();assert.equal(f.opened.length,1);f.launcher.dispose();
});
