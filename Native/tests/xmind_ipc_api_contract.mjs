import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {randomBytes} from 'node:crypto';
const [binary,modules,stdlib]=process.argv.slice(2);assert(binary&&modules&&stdlib);
const root=await mkdtemp(join(tmpdir(),'xmind-root-api-')),token=randomBytes(32).toString('hex');
const env={...process.env,XMIND_AUTH_TOKEN:token};let stdout='',stderr='',endpoint,passed=false;
const backend=spawn(binary,['serve','--db',join(root,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0'],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});
const exited=new Promise(resolve=>backend.once('exit',(code,signal)=>resolve({code,signal})));
backend.stdout.on('data',b=>{stdout+=b;endpoint=stdout.match(/xMind API listening on (lrpc:\d+)/)?.[1];});backend.stderr.on('data',b=>stderr+=b);
const run=promisify(execFile);const clients=[];
async function client(args,auth=token){const child=run(binary,['ipc','--endpoint',endpoint,...args],{env:{...env,XMIND_AUTH_TOKEN:auth},windowsHide:true,timeout:10000,maxBuffer:1024*1024});clients.push(child.child.pid);const result=await child;return JSON.parse(result.stdout);}
try{
    const deadline=Date.now()+15000;while(!endpoint||!stdout.includes('xMind Server listening on')){assert(Date.now()<deadline,'Root API startup timed out');assert.equal(backend.exitCode,null,'Backend exited during startup');await new Promise(resolve=>setTimeout(resolve,25));}
    assert.deepEqual(await client(['host']),{api_version:1,transport:'xlang3-lrpc'});
    assert.deepEqual(await client(['session','create','integration-session','Native API 雪']),{id:'integration-session',title:'Native API 雪',parent_id:''});
    const opened=await Promise.all([client(['session','open','integration-session']),client(['session','open','integration-session'])]);assert(opened.every(s=>s.title==='Native API 雪'));
    await assert.rejects(client(['session','create','integration-session','duplicate']));
    await assert.rejects(client(['session','create','unauthorized-session','must not exist'],'0'.repeat(64)));
    await assert.rejects(client(['session','open','unauthorized-session']));
    await assert.rejects(client(['run','open','absent-run']));
    assert.equal((await client(['session','open','integration-session'])).title,'Native API 雪');
    assert(clients.every(pid=>pid&&pid!==backend.pid));assert(!stdout.includes(token)&&!stderr.includes(token));
    passed=true;console.log(JSON.stringify({backendPid:backend.pid,clientPids:clients,apiVersion:1,transport:'xlang3-lrpc',actualUnifiedExecutable:true,verified:['Root registered at backend startup','Native Host/Sessions/Runs subobjects','Distinct xmind.exe clients call SDK methods directly','Exact UTF-8 persisted session survives independent clients','Duplicate create and wrong authentication rejected','No unauthorized session created','Unknown run rejected'],limitations:['No worker migration, model/tool/planning IPC or old dispatcher removal established by this gate','No live provider or rendering acceptance'],passed}));
}finally{backend.kill();await exited;if(passed)await rm(root,{recursive:true,force:true});else console.error('Retained root API fixture: '+root+'; backend output: '+stdout+'; '+stderr);}
