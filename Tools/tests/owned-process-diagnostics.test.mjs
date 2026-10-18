// Actual Windows processes; no provider, xlang3 rebuild or user profile access.
import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdir,mkdtemp,readFile,realpath} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {observeOwnedProcess,withOwnedProcessDiagnostics} from '../owned-process-diagnostics.mjs';

const windows=process.platform==='win32';
let root;
if(windows){
  let output=tmpdir();
  if(process.env.GITHUB_ACTIONS==='true'&&process.env.RUNNER_ENVIRONMENT==='github-hosted'){
    output=fileURLToPath(new URL('../../build/ci-evidence/process-diagnostic-contract/',import.meta.url));
    await mkdir(output,{recursive:true});
  }
  root=await realpath(output);
}
const shell=windows?join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe'):undefined;
function run(executable,args,options={}){
  const child=spawn(executable,args,{windowsHide:true,stdio:['ignore','pipe','pipe'],timeout:15000,...options});
  let stdout='',stderr='';
  child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);
  const done=new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',(code,signal)=>resolve({code,signal,stdout,stderr}));});
  return {child,done};
}
async function records(file){return (await readFile(file,'utf8')).trim().split(/\r?\n/).map(line=>JSON.parse(line));}
async function monitorShell(){
  for(const candidate of [join(process.env.ProgramFiles||'C:/Program Files','PowerShell/7/pwsh.exe'),shell]){
    try{return await realpath(candidate);}catch(error){if(error.code!=='ENOENT')throw error;}
  }
  throw new Error('PowerShell executable missing');
}
test('read-only diagnostic captures its actual PowerShell descendant and excludes an unrelated process and private arguments',{skip:!windows,timeout:30000},async()=>{
  const directory=await mkdtemp(join(root,'xmind-process-observer-')),marker=join(directory,'effect.txt');
  const secret='synthetic-private-argument-not-diagnostic-data';
  const command="[IO.File]::WriteAllText('"+marker.replaceAll("'","''")+"','once'); Start-Sleep -Seconds 8";
  const unrelated=run(shell,['-NoLogo','-NoProfile','-NonInteractive','-Command','Start-Sleep -Seconds 8']);
  const script="const {spawn}=require('node:child_process'); const p=spawn(process.argv[1],['-NoLogo','-NoProfile','-NonInteractive','-Command',process.argv[2]],{windowsHide:true,stdio:'ignore',timeout:12000,env:{...process.env,OBSERVER_SYNTHETIC_PRIVATE:process.argv[3]}});process.stdout.write(JSON.stringify({pid:p.pid})+'\\n');p.on('close',(code,signal)=>process.exitCode=signal?1:(code??1));";
  const launchedAfter=new Date().toISOString(),fixture=run(process.execPath,['-e',script,shell,command,secret]);
  let diagnostic;
  try{
    diagnostic=await observeOwnedProcess(fixture.child,process.execPath,{enabled:true,evidenceRoot:directory,shellPath:await monitorShell(),label:'actual-owner',launchedAfter,intervalMs:200,durationSeconds:20});
    const result=await fixture.done;assert.equal(result.code,0,result.stderr);const ownedPid=JSON.parse(result.stdout.trim()).pid;
    const stopped=await diagnostic.stop();assert.equal(stopped.exitCode,0,JSON.stringify(stopped));
    const rows=await records(diagnostic.output),samples=rows.filter(row=>row.kind==='process');
    console.log('Actual owner diagnostics retained at '+diagnostic.output);
    assert.ok(samples.length>0,'Capture at least one actual live owned PowerShell process');
    assert.ok(samples.every(row=>row.pid===ownedPid&&row.pid!==unrelated.child.pid));
    assert.ok(samples.every(row=>Array.isArray(row.threads)&&row.threads.length>0&&row.threads.length<=128&&row.modules.length<=64));
    assert.equal(rows[0].commandLinesIncluded,false);assert.equal(rows[0].environmentIncluded,false);
    const raw=await readFile(diagnostic.output,'utf8');assert.ok(!raw.includes(secret));assert.ok(!raw.includes(command));assert.ok(!raw.includes('OBSERVER_SYNTHETIC_PRIVATE'));
    assert.equal(await readFile(marker,'utf8'),'once');assert.equal(rows.at(-1).kind,'end');
    // A retiring or reused root identity must stop collection too. The actual
    // root's zero exit and exact descendant samples remain checked above.
    assert.ok(['observer_stopped','owner_exited','owner_identity_changed','owner_identity_mismatch'].includes(rows.at(-1).reason));
  }finally{await fixture.done;await unrelated.done;await diagnostic?.stop();}
});
test('identity mismatch stops observation without terminating the actual root',{skip:!windows,timeout:15000},async()=>{
  const directory=await mkdtemp(join(root,'xmind-process-observer-'));
  const launchedAfter=new Date().toISOString(),fixture=run(process.execPath,['-e',"setTimeout(()=>process.stdout.write('root completed normally'),4000)"]);
  const diagnostic=await observeOwnedProcess(fixture.child,shell,{enabled:true,evidenceRoot:directory,shellPath:await monitorShell(),label:'mismatched-owner',launchedAfter,intervalMs:200,durationSeconds:10});
  try{
    const result=await fixture.done;assert.equal(result.code,0);assert.equal(result.stdout,'root completed normally');
    assert.equal((await diagnostic.stop()).exitCode,0);
    const rows=await records(diagnostic.output);assert.equal(rows.at(-1).reason,'owner_identity_mismatch');assert.equal(rows.filter(row=>row.kind==='process').length,0);
    console.log('Identity-rejection diagnostics retained at '+diagnostic.output);
  }finally{await fixture.done;await diagnostic.stop();}
});
test('optional diagnostics preserve the actual fast nonzero execution error',{skip:!windows,timeout:10000},async()=>{
  const directory=await mkdtemp(join(root,'xmind-process-observer-'));
  const shellPath=await monitorShell();
  const operation=promisify(execFile)(process.execPath,['-e',"process.stderr.write('fixture-native-exit-7');process.exitCode=7"],{windowsHide:true,timeout:5000});
  await assert.rejects(withOwnedProcessDiagnostics(operation,process.execPath,{enabled:true,evidenceRoot:directory,shellPath,label:'fast-failure',intervalMs:200,durationSeconds:5}),error=>{
    assert.equal(error.code,7);assert.equal(error.stderr,'fixture-native-exit-7');return true;
  });
});
