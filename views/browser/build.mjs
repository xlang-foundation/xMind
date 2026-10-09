import {mkdir,copyFile,writeFile} from 'node:fs/promises';import {resolve,join} from 'node:path';import {fileURLToPath} from 'node:url';import {createRequire} from 'node:module';
const root=fileURLToPath(new URL('../../',import.meta.url)),target=resolve(process.argv[2]||join(root,'.agentflow/browser-assets')),require=createRequire(import.meta.url);
const {browserHtml}=require('./page.cjs');await mkdir(target,{recursive:true});
await writeFile(join(target,'index.html'),browserHtml());
for(const [source,name] of [["extensions/vscode/patch-review.js","patch-review.js"],["extensions/vscode/media/chat.js","chat.js"],["extensions/vscode/media/chat.css","chat.css"],["extensions/vscode/client.js","client.js"],["extensions/vscode/node_modules/marked/lib/marked.umd.js","marked.js"],["extensions/vscode/node_modules/dompurify/dist/purify.min.js","purify.js"],["views/browser/browser.js","browser.js"],["views/browser/browser.css","browser.css"]])await copyFile(join(root,source),join(target,name));
console.log('Browser view assets built at '+target);
const notices=join(target,'licenses');await mkdir(notices,{recursive:true});for(const [source,name] of [['marked/LICENSE','marked.txt'],['dompurify/LICENSE','dompurify.txt'],['dompurify/LICENSE-MPL','dompurify-MPL.txt']])await copyFile(join(root,'extensions/vscode/node_modules',source),join(notices,name));
if(process.argv[3]){const adapter=resolve(process.argv[3]);await mkdir(resolve(adapter,'..'),{recursive:true});await copyFile(join(root,'views/browser/server.mjs'),adapter);console.log('Browser access adapter bundled at '+adapter);}
