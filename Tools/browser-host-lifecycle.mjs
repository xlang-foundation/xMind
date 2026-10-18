import {readFile,unlink} from 'node:fs/promises';

async function removeIfPresent(path){
  try{await unlink(path);}catch(error){if(error.code!=='ENOENT')throw error;}
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
