import {mkdir,copyFile,writeFile} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';

const root=fileURLToPath(new URL('../../',import.meta.url));
const require=createRequire(import.meta.url);

export async function buildBrowserAssets(targetPath,adapterPath){
  const target=resolve(targetPath||join(root,'.agentflow/browser-assets'));
  const {browserHtml}=require('./page.cjs');
  await mkdir(target,{recursive:true});
  await writeFile(join(target,'index.html'),browserHtml());
  await copyFile(join(root,'views/browser/layout.js'),join(target,'layout.js'));
  await copyFile(join(root,'views/browser/layout.css'),join(target,'layout.css'));
  for(const [source,name] of [["extensions/vscode/patch-review.js","patch-review.js"],["extensions/vscode/media/chat.js","chat.js"],["extensions/vscode/media/chat.css","chat.css"],["extensions/vscode/client.js","client.js"],["extensions/vscode/node_modules/marked/lib/marked.umd.js","marked.js"],["extensions/vscode/node_modules/dompurify/dist/purify.min.js","purify.js"],["extensions/vscode/node_modules/@xterm/xterm/lib/xterm.js","xterm.js"],["extensions/vscode/node_modules/@xterm/xterm/css/xterm.css","xterm.css"],["extensions/vscode/node_modules/@xterm/addon-fit/lib/addon-fit.js","xterm-fit.js"],["views/browser/terminal.js","terminal.js"],["views/browser/browser.js","browser.js"],["views/browser/browser.css","browser.css"]])
    await copyFile(join(root,source),join(target,name));
  console.log('Browser view assets built at '+target);
  const notices=join(target,'licenses');
  await mkdir(notices,{recursive:true});
  for(const [source,name] of [['marked/LICENSE','marked.txt'],['dompurify/LICENSE','dompurify.txt'],['dompurify/LICENSE-MPL','dompurify-MPL.txt'],['@xterm/xterm/LICENSE','xterm.txt'],['@xterm/addon-fit/LICENSE','xterm-addon-fit.txt']])
    await copyFile(join(root,'extensions/vscode/node_modules',source),join(notices,name));
  if(adapterPath){
    const adapter=resolve(adapterPath);
    await mkdir(resolve(adapter,'..'),{recursive:true});
    await copyFile(join(root,'views/browser/server.mjs'),adapter);
    console.log('Browser access adapter bundled at '+adapter);
  }
  return target;
}

if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href)
  await buildBrowserAssets(process.argv[2],process.argv[3]);
