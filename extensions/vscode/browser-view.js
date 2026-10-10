'use strict';
const {join}=require('node:path');
const {pathToFileURL}=require('node:url');
const {backendOrigin,validateToken}=require('./client');

// Access adapter only: native execution and provider credentials stay on the
// selected server. The connection token is passed in memory, never to the page.
function browserViewLauncher(vscode,context,{loadServer=path=>import(pathToFileURL(path).href),assetRoot,serverPath}={}) {
  let active,opening,disposed=false;
  async function open(origin,token){
    if(disposed)throw new Error('Browser view launcher has closed');
    origin=backendOrigin(origin);token=validateToken(token);
    if(opening)throw new Error('A browser view is already opening');
    opening=(async()=>{
      if(!active||active.origin!==origin){
        const directory=context.extensionUri.fsPath;
        const {createBrowserServer}=await loadServer(serverPath||join(directory,'browser-runtime','server.mjs'));
        if(disposed)throw new Error('Browser view launcher has closed');
        const view=await createBrowserServer({backend:origin,assetRoot:assetRoot||join(directory,'browser-runtime','assets'),localAccessToken:token});
        let url;try{url=await view.listen();}catch(error){await view.close().catch(()=>{});throw error;}
        if(disposed){await view.close();throw new Error('Browser view launcher has closed');}
        const previous=active;active={origin,view,url:url+'/ui/'};if(previous)await previous.view.close();
      }
      // No credential is put in the URL, browser storage, clipboard or page code.
      token=undefined;
      if(disposed)throw new Error('Browser view launcher has closed');
      if(!await vscode.env.openExternal(vscode.Uri.parse(active.url)))throw new Error('The browser could not open the xMind Browser view.');
      return active.url;
    })();
    try{return await opening;}finally{opening=undefined;token=undefined;}
  }
  function dispose(){disposed=true;const current=active;active=undefined;current?.view.close().catch(()=>{});}
  context.subscriptions.push({dispose});
  return {open,dispose};
}
module.exports={browserViewLauncher};
