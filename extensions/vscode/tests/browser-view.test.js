'use strict';
// Access-host fixtures only; actual native sharing is checked separately.
const test=require('node:test'),assert=require('node:assert/strict');
const {browserViewLauncher}=require('../browser-view');
function fixture(){const opened=[],copied=[],views=[],context={extensionUri:{fsPath:'fixture-extension'},subscriptions:[]};
 const vscode={Uri:{parse:value=>value},env:{clipboard:{writeText:async value=>copied.push(value)},openExternal:async value=>{opened.push(value);return true;}}};
 const launcher=browserViewLauncher(vscode,context,{loadServer:async()=>({createBrowserServer:async options=>{const view={options,closed:false,listen:async()=> 'http://127.0.0.1:'+ (62000+views.length),close:async()=>{view.closed=true;}};views.push(view);return view;}})});
 return {opened,copied,views,context,vscode,launcher};
}
test('browser view privately enrolls the local adapter and opens without exposing credentials',async()=>{
 const f=fixture(),token='fixture-browser-connection-token-1234';const first=await f.launcher.open('http://localhost:8765',token);assert.equal(await f.launcher.open('http://127.0.0.1:8765',token),first);assert.equal(f.views.length,1);assert.equal(f.views[0].options.backend,'http://127.0.0.1:8765');assert.equal(f.views[0].options.localAccessToken,token);assert.deepEqual(f.copied,[]);assert.ok(f.opened.every(value=>!value.includes(token)&&new URL(value).pathname==='/ui/'&&!new URL(value).search));f.launcher.dispose();assert.equal(f.views[0].closed,true);
});
test('changing servers retires only the old view and disposal prevents reopening',async()=>{
 const f=fixture(),token='fixture-browser-connection-token-1234';await f.launcher.open('http://127.0.0.1:8765',token);await f.launcher.open('http://127.0.0.1:8766',token);assert.equal(f.views.length,2);assert.equal(f.views[0].closed,true);assert.equal(f.views[1].closed,false);f.context.subscriptions[0].dispose();assert.equal(f.views[1].closed,true);await assert.rejects(f.launcher.open('http://127.0.0.1:8766',token),/closed/);
});
test('invalid origins and tokens do not create access views or expose credentials',async()=>{
 const f=fixture();await assert.rejects(f.launcher.open('https://remote.invalid','fixture-browser-connection-token-1234'),/loopback/);await assert.rejects(f.launcher.open('http://127.0.0.1:8765','short'),/32/);assert.equal(f.views.length,0);assert.equal(f.copied.length,0);f.launcher.dispose();
});
