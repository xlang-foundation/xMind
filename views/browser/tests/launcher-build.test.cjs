'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
test('prebuilt native browser assets contain current shared renderer bytes and refresh in place',async t=>{
 const root=path.resolve(__dirname,'../../..'),temporary=await fs.mkdtemp(path.join(os.tmpdir(),'xmind-assets-')),assets=path.join(temporary,'assets');t.after(async()=>{const resolved=await fs.realpath(temporary);assert.equal(path.dirname(resolved),await fs.realpath(os.tmpdir()));assert(path.basename(resolved).startsWith('xmind-assets-'));await fs.rm(resolved,{recursive:true,force:true});});
 const {buildBrowserAssets}=await import('../build.mjs');await buildBrowserAssets(assets);
 for(const name of ['index.html','browser.js','browser.css','chat.js','chat.css','client.js','layout.js','layout.css','terminal.js','xterm.js','xterm.css','xterm-fit.js','marked.js','purify.js','patch-review.js','licenses/marked.txt','licenses/dompurify.txt','licenses/dompurify-MPL.txt','licenses/xterm.txt','licenses/xterm-addon-fit.txt'])assert((await fs.stat(path.join(assets,name))).isFile(),name);
 const html=await fs.readFile(path.join(assets,'index.html'),'utf8');assert.match(html,/<title>xMind/);for(const script of ['/ui/chat.js','/ui/client.js','/ui/layout.js'])assert(html.includes(script),script);
 await assert.rejects(fs.stat(path.join(assets,'server.mjs')),{code:'ENOENT'});
 await fs.writeFile(path.join(assets,'client.js'),'stale fixture');await buildBrowserAssets(assets);
 assert.deepEqual(await fs.readFile(path.join(assets,'client.js')),await fs.readFile(path.join(root,'extensions/vscode/client.js')));
 assert.deepEqual(await fs.readFile(path.join(assets,'layout.css')),await fs.readFile(path.join(root,'views/browser/layout.css')));
});
