// Read-only hosted test diagnostics, separate from the native product runtime.
import {spawn} from 'node:child_process';
import {mkdir,realpath,lstat,writeFile} from 'node:fs/promises';
import {resolve,join,basename} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomUUID} from 'node:crypto';

export async function withOwnedProcessDiagnostics(operation,executable,options){
  // Handle a fast native rejection while optional diagnostics initialize.
  operation.catch(()=>{});
  let observer;
  try{observer=await observeOwnedProcess(operation.child,executable,options);}
  catch(error){process.stderr.write('Owned process diagnostics unavailable: '+error.message+'\n');}
  try{return await operation;}
  finally{try{await observer?.stop();}catch(error){process.stderr.write('Owned process diagnostics incomplete: '+error.message+'\n');}}
}

export async function observeOwnedProcess(child,executable,{
  enabled=process.platform==='win32'&&process.env.GITHUB_ACTIONS==='true'&&process.env.RUNNER_ENVIRONMENT==='github-hosted',
  evidenceRoot=fileURLToPath(new URL('../build/ci-evidence/process-observations/',import.meta.url)),
  shellPath,launchedAfter=new Date(Date.now()-1000).toISOString(),label='native-contract',
  intervalMs=2000,durationSeconds=65,
  captureStacks=process.env.GITHUB_ACTIONS==='true'&&process.env.RUNNER_ENVIRONMENT==='github-hosted',stackCaptureAgeMs=15000,captureOwner=false
}={}){
  if(!enabled)return {stop:async()=>({enabled:false})};
  if(process.platform!=='win32'||!Number.isSafeInteger(child?.pid)||child.pid<1||!/^[a-z][a-z0-9-]{0,63}$/.test(label))throw new Error('Invalid owned process diagnostic binding');
  if(!Number.isSafeInteger(intervalMs)||intervalMs<200||intervalMs>5000||!Number.isSafeInteger(durationSeconds)||durationSeconds<1||durationSeconds>70)throw new Error('Invalid diagnostic observation budget');
  if(typeof captureOwner!=='boolean'||typeof captureStacks!=='boolean'||!Number.isSafeInteger(stackCaptureAgeMs)||stackCaptureAgeMs<1000||stackCaptureAgeMs>25000)throw new Error('Invalid stack capture budget');
  const image=await realpath(executable);
  if(!shellPath){
    for(const candidate of [join(process.env.ProgramFiles||'C:/Program Files','PowerShell/7/pwsh.exe'),join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe')]){
      try{shellPath=await realpath(candidate);break;}catch(error){if(error.code!=='ENOENT')throw error;}
    }
  }
  if(!shellPath||!['pwsh.exe','powershell.exe'].includes(basename(shellPath).toLowerCase()))throw new Error('An explicit installed PowerShell executable is required');
  const directory=join(resolve(evidenceRoot),label+'-'+randomUUID());
  await mkdir(directory,{recursive:true});
  if((await lstat(directory)).isSymbolicLink())throw new Error('Diagnostic output cannot be an alias');
  const stopFile=join(directory,'stop'),output=join(directory,'observations.jsonl');
  const args=['-NoLogo','-NoProfile','-NonInteractive','-File',fileURLToPath(new URL('./owned-process-diagnostics.ps1',import.meta.url)),
    '-OwnerProcessId',String(child.pid),'-OwnerExecutable',image,'-OwnerLaunchedAfterUtc',launchedAfter,
    '-OutputFile',output,'-StopFile',stopFile,'-IntervalMs',String(intervalMs),'-DurationSeconds',String(durationSeconds)];
  if(captureStacks)args.push('-CaptureStacks','-StackCaptureAgeMs',String(stackCaptureAgeMs));
  if(captureOwner)args.push('-CaptureOwner');
  const monitor=spawn(shellPath,args,{windowsHide:true,stdio:'ignore',timeout:80000});
  monitor.unref();
  let stopPromise;
  const ended=new Promise(resolve=>{
    monitor.once('error',error=>resolve({error:error.message}));
    monitor.once('close',(exitCode,signal)=>resolve({exitCode,signal}));
  });
  async function stop(){
    if(!stopPromise)stopPromise=(async()=>{
      try{await writeFile(stopFile,'observation finished\n',{flag:'wx'});}catch(error){if(error.code!=='EEXIST')throw error;}
      let timer;
      const result=await Promise.race([ended,new Promise(resolve=>{timer=setTimeout(()=>resolve({observationPending:true,monitorPid:monitor.pid}),2000);})]);
      clearTimeout(timer);
      if(result.exitCode!==0)process.stderr.write('Owned process diagnostics incomplete: '+JSON.stringify(result)+'\n');
      return {...result,enabled:true,directory,output};
    })();
    return stopPromise;
  }
  // The observer never sends a signal to the test process or its descendants.
  child.once('close',()=>{void stop().catch(error=>process.stderr.write('Diagnostic stop failed: '+error.message+'\n'));});
  if(child.exitCode!==null||child.signalCode!==null)void stop();
  return {stop,output,directory,monitorPid:monitor.pid};
}
