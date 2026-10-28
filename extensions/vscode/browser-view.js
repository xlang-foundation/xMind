'use strict';
const {backendOrigin}=require('./client');

// The extension opens the native host; it never serves browser HTTP.
function browserViewLauncher(vscode,context,{openNative}={}) {
  let opening,disposed=false;
  async function open(){
    if(disposed)throw new Error('Browser view launcher has closed');
    if(opening)throw new Error('A browser view is already opening');
    if(typeof openNative!=='function')throw new Error('Native browser startup is unavailable');
    opening=(async()=>{
      const value=await openNative(),url=new URL(value),origin=backendOrigin(url.origin);
      if(value!==origin+'/ui/'||url.hostname!=='127.0.0.1')throw new Error('Invalid native browser URL');
      if(disposed)throw new Error('Browser view launcher has closed');
      if(!await vscode.env.openExternal(vscode.Uri.parse(value)))throw new Error('The browser could not open the xMind Browser view.');
      return value;
    })();
    try{return await opening;}finally{opening=undefined;}
  }
  function dispose(){disposed=true;/* The native backend owns its lifetime. */}
  context.subscriptions.push({dispose});
  return {open,dispose};
}
module.exports={browserViewLauncher};
