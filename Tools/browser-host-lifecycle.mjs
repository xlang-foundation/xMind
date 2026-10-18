import {lstat,readFile,realpath,unlink} from 'node:fs/promises';
import {isAbsolute,resolve} from 'node:path';

async function removeIfPresent(path){
  try{await unlink(path);}catch(error){if(error.code!=='ENOENT')throw error;}
}

export async function findExistingBrowserHost(state,requestedBackend,requestedAssets){
  const activePath=state+'/active.json';
  let active;
  try{
    const info=await lstat(activePath);
    if(!info.isFile()||info.isSymbolicLink()||info.nlink!==1||info.size>16384)throw new Error('Invalid existing browser host record.');
    active=JSON.parse(await readFile(activePath,'utf8'));
  }catch(error){if(error.code==='ENOENT')return undefined;throw error;}
  if(!active||typeof active.url!=='string'||!/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}\/ui\/$/.test(active.url)||typeof active.backend!=='string')throw new Error('Invalid existing browser host record.');
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),1200);
  let live=false;
  try{
    const response=await fetch(active.url,{redirect:'error',signal:controller.signal});
    const contentType=response.headers.get('content-type')||'';
    live=response.status===200&&contentType.toLowerCase().includes('text/html');
    await response.body?.cancel();
  }catch(error){
    // A refused loopback connection means this endpoint is gone. A timeout,
    // reset or observation error does not justify replacing a live owner.
    if(error?.cause?.code==='ECONNREFUSED')return undefined;
    throw new Error('The existing browser host could not be observed; it was preserved. Retry with the same --state.',{cause:error});
  }finally{clearTimeout(timer);}
  if(!live)throw new Error('The existing browser host could not be observed; it was preserved. Retry with the same --state.');
  if(requestedBackend&&new URL(requestedBackend).origin!==active.backend)throw new Error('A browser host already uses this state directory with a different backend; stop it or select another --state.');
  if(requestedAssets!==undefined){
    if(typeof active.assets!=='string'||!isAbsolute(active.assets))throw new Error('The existing browser host has no verifiable asset directory; it was preserved. Select another --state.');
    let requested,owned;
    try{requested=await realpath(resolve(requestedAssets));owned=await realpath(active.assets);}catch(error){throw new Error('The existing browser assets could not be verified; the host was preserved. Check --assets or select another --state.',{cause:error});}
    if(requested!==owned)throw new Error('A browser host already uses this state directory with different assets; use its --assets directory or select another --state.');
    active={...active,assets:owned};
  }
  return active;
}

function stopChild(child){
  if(!child||child.exitCode!==null||child.signalCode!==null)return Promise.resolve();
  return new Promise(resolve=>{
    let settled=false;
    const finish=()=>{if(settled)return;settled=true;clearTimeout(timer);child.removeListener('close',finish);resolve();};
    const timer=setTimeout(finish,3000);
    child.once('close',finish);
    try{child.kill();}catch{finish();}
  });
}

export function createBrowserHostLifecycle({view,nativeChild,state,tokenFile,ownedToken,viewPid=process.pid,url}){
  let shutdownPromise;
  const shutdown=()=>{
    if(shutdownPromise)return shutdownPromise;
    shutdownPromise=(async()=>{
      const failures=[];
      try{await view?.close();}catch(error){failures.push(error);}
      try{await stopChild(nativeChild);}catch(error){failures.push(error);}
      if(state&&url){
        try{
          const activePath=state+'/active.json';
          const active=JSON.parse(await readFile(activePath,'utf8'));
          if(active.viewPid===viewPid&&active.url===url)await removeIfPresent(activePath);
        }catch(error){if(error.code!=='ENOENT')failures.push(error);}
      }
      if(tokenFile&&typeof ownedToken==='string'){
        try{if((await readFile(tokenFile,'utf8')).trim()===ownedToken)await removeIfPresent(tokenFile);}
        catch(error){if(error.code!=='ENOENT')failures.push(error);}
      }
      if(failures.length)throw new AggregateError(failures,'Browser host shutdown was incomplete');
    })();
    return shutdownPromise;
  };
  return {shutdown};
}
