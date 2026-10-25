import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [hostExe,cliExe,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
const folder=await fs.mkdtemp(path.join(os.tmpdir(),'xmind-snapshot-cli-'));
const host=spawn(hostExe,['cli-host',folder,modules,stdlib],{windowsHide:true,stdio:['pipe','pipe','pipe']});let stdout='',stderr='';host.stdout.on('data',b=>stdout+=b);host.stderr.on('data',b=>stderr+=b);
const terminal=new Promise((resolve,reject)=>{host.once('error',reject);host.once('exit',(code,signal)=>resolve({code,signal}));});
const commands=[];
try{
 const end=Date.now()+10000;while(!stdout.includes('"endpoint"')&&host.exitCode===null&&Date.now()<end)await new Promise(resolve=>setTimeout(resolve,10));assert(stdout.includes('"endpoint"'),'Snapshot CLI host did not publish its endpoint');const {endpoint}=JSON.parse(stdout.split(/\r?\n/).find(s=>s.includes('"endpoint"')));
 const env={...process.env,XMIND_AUTH_TOKEN:'a'.repeat(64)},base=['remote-snapshot','1','scope-a','session-a','run-a','sdk-consumer'];
 const call=async(operation,tail=[],input=base)=>{const args=['ipc','--endpoint',endpoint,'connections','scope-run-snapshot-'+operation,...input,...tail];let result;try{result=await execute(cliExe,args,{env,windowsHide:true,timeout:10000,maxBuffer:8*1024*1024});commands.push({operation,tail,code:0});return JSON.parse(result.stdout);}catch(error){commands.push({operation,tail,code:error.code,stderr:String(error.stderr)});throw error;}};
 const refuses=async(operation,tail,input=base)=>assert.rejects(call(operation,tail,input),e=>e.code===1&&Boolean(e.stderr)&&!String(e.stderr).includes('Usage:'));
 const opened=await call('open',['1']);assert.equal(opened.checkpoint_revision,1);assert.equal(opened.accepted.sequence,0);const generation=opened.binding.backend_generation;
 await refuses('open',['2']);await refuses('open',['true']);await refuses('open',['1'],['local','1',...base.slice(2)]);
 await refuses('read',['1','null','0','foreign'],base.slice(0,-1).concat('stale-never-created'));
 await refuses('observe',['snapshot-a','foreign'],base.slice(0,-1).concat('stale-never-created'));
 await refuses('reset',['snapshot-a','1','0'.repeat(64),'foreign'],base.slice(0,-1).concat('stale-never-created'));
 await refuses('read',['1','true','0',generation]);await refuses('read',['1',JSON.stringify('snapshot-a'),'true',generation]);
 const saved=await call('read',['1',JSON.stringify('snapshot-a'),'0',generation]);assert.equal(saved.complete,true);assert.equal(saved.records.length,2);assert.equal(saved.records[1].payload.text,'Observed snapshot text');assert.equal(saved.cursor.backend_generation,generation);assert.equal(saved.checkpoint_revision,1);assert.equal(saved.revision,1);assert.equal(saved.digest.length,64);assert.equal(saved.binding.credential_revision,1);assert.equal(Object.hasOwn(saved,'origin'),false);assert.equal(Object.hasOwn(saved,'grant'),false);
 assert.deepEqual(await call('observe',['snapshot-a',generation]),saved);
 await refuses('reset',['snapshot-a','1','0'.repeat(64),generation]);await refuses('reset',['snapshot-a','true',saved.digest,generation]);
 const reset=await call('reset',['snapshot-a','1',saved.digest,generation]);assert.equal(reset.checkpoint.revision,2);assert.equal(reset.checkpoint.accepted.sequence,20);assert.equal(reset.snapshot_reset.digest,saved.digest);assert.deepEqual(await call('reset',['snapshot-a','1',saved.digest,generation]),reset);
 await refuses('read',['1',JSON.stringify('snapshot-a'),'1',generation]);assert.deepEqual(await call('observe',['snapshot-a',generation]),saved);assert.equal((await call('open',['2'])).accepted.sequence,20);
 host.stdin.end('verify\n');assert.equal((await terminal).code,0);assert.match(stdout,/snapshot-sdk-reopened/);await fs.writeFile(path.join(folder,'receipt.json'),JSON.stringify({passed:true,commands:commands.length,hostPid:host.pid,separateCliProcesses:true,standardSdkMethods:true,generationCheckedBeforeConsumerCreation:true,explicitResumeAndReset:true,databaseReopened:true,networkFetch:false,modelCalls:0,goalComplete:false},null,2));console.log('Separate native CLI snapshot open/read/observe/reset, generation refusals before consumer creation, digest acknowledgement, explicit resume and reopen passed; no network/model work.');
}catch(error){console.error('Retained snapshot CLI fixture: '+folder);throw error;}finally{if(!host.stdin.destroyed&&!host.stdin.writableEnded)host.stdin.end();await fs.writeFile(path.join(folder,'host-stdout.log'),stdout);await fs.writeFile(path.join(folder,'host-stderr.log'),stderr);await fs.writeFile(path.join(folder,'commands.json'),JSON.stringify(commands,null,2));}
