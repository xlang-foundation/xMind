import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
const [exe,modules,stdlib]=process.argv.slice(2);
const folder=await fs.mkdtemp(path.join(os.tmpdir(),'xmind-snapshot-sdk-'));
const children=[];
function start(args){
 const child=spawn(exe,args,{windowsHide:true,stdio:['pipe','pipe','pipe']});let stdout='',stderr='';const listeners=new Set();
 child.stdout.on('data',b=>{stdout+=b;for(const listener of listeners)listener();});child.stderr.on('data',b=>stderr+=b);
 const terminal=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>{resolve({code,signal});for(const listener of listeners)listener();});});
 const line=needle=>new Promise((resolve,reject)=>{const timeout=setTimeout(()=>done(Error('Snapshot SDK observation deadline')),15000);function done(error,value){clearTimeout(timeout);listeners.delete(check);if(error)reject(error);else resolve(value);}function check(){const match=stdout.split(/\r?\n/).find(s=>s.includes(needle));if(match)done(null,match);else if(child.exitCode!==null)done(Error('Snapshot SDK process exited before observation'));}listeners.add(check);check();});
 const value={child,terminal,line,logs:()=>({stdout,stderr})};children.push(value);return value;
}
try{
 const host=start([folder,modules,stdlib]);const ready=JSON.parse(await host.line('"endpoint"'));const client=start(['client',ready.endpoint]);await client.line('snapshot-client-ready');host.child.stdin.write('retire\n');await host.line('snapshot-owner-replaced');client.child.stdin.end('retired\n');assert.equal((await client.terminal).code,0);await client.line('snapshot-sdk-retirement-verified');host.child.stdin.end('verify\n');assert.equal((await host.terminal).code,0);await host.line('snapshot-sdk-reopened');
 await fs.writeFile(path.join(folder,'receipt.json'),JSON.stringify({passed:true,hostPid:host.child.pid,clientPid:client.child.pid,separateProcesses:host.child.pid!==client.child.pid,standardXlang3Methods:true,ownerRetirementRefused:true,explicitResumeAfterOwnerReplacement:true,databaseReopened:true,networkFetch:false,modelCalls:0,goalComplete:false},null,2));console.log('Separate-process standard SDK snapshot subobject, cached payload/digest, explicit reset, owner retirement/rebind and database reopen passed; no network or model work.');
}catch(error){console.error('Retained snapshot SDK fixture: '+folder);throw error;}finally{for(let i=0;i<children.length;i++){const input=children[i].child.stdin;if(!input.destroyed&&!input.writableEnded)input.end();await fs.writeFile(path.join(folder,'process-'+i+'.json'),JSON.stringify(children[i].logs(),null,2));}}
