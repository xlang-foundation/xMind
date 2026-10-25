import test from 'node:test';
import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,readFile,readdir} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {retainFixtureTiming} from '../../Native/tests/fixture_process_timing.mjs';
const launch=code=>promisify(execFile)(process.execPath,['-e',code],{windowsHide:true,timeout:5000,maxBuffer:65536});
test('fixture timing retains split UTF-8 stdout and stderr without arguments or environment',async()=>{
 const directory=await mkdtemp(join(tmpdir(),'xmind-fixture-timing-'));
 const pending=launch("const bytes=Buffer.from('Actual 🐈 bytes');process.stdout.write(bytes.subarray(0,8));setTimeout(()=>{process.stdout.write(bytes.subarray(8));process.stderr.write('controlled stderr')},30)");
 const result=await retainFixtureTiming(pending,{directory,phase:'success'});assert.equal(result.stdout,'Actual 🐈 bytes');
 const record=JSON.parse(await readFile(join(directory,'success-'+process.pid+'.json'),'utf8'));assert.equal(record.pid,pending.child.pid);assert.equal(record.code,0);assert.equal(record.signal,null);assert.equal(record.stdout,result.stdout);assert.equal(record.stderr,result.stderr);assert.equal(record.timing.filter(x=>x.stream==='stdout').map(x=>x.text).join(''),result.stdout);assert.equal(record.timing.filter(x=>x.stream==='stderr').map(x=>x.text).join(''),result.stderr);assert.ok(record.timing.every((x,i,a)=>x.elapsedMs>=0&&(!i||x.elapsedMs>=a[i-1].elapsedMs)));assert.equal(record.droppedChunks,0);assert.equal(record.commandLineIncluded,false);assert.equal(record.environmentIncluded,false);
});
test('failed fixture output survives rejection with the original exit code',async()=>{
 const directory=await mkdtemp(join(tmpdir(),'xmind-fixture-timing-'));
 const pending=launch("process.stdout.write('last completed case');process.stderr.write('controlled failure');process.exitCode=7");
 await assert.rejects(retainFixtureTiming(pending,{directory,phase:'failure'}),error=>error.code===7&&error.stdout==='last completed case');
 const record=JSON.parse(await readFile(join(directory,'failure-'+process.pid+'.json'),'utf8'));assert.equal(record.code,7);assert.equal(record.stdout,'last completed case');assert.equal(record.stderr,'controlled failure');assert.equal(record.killed,false);
});
test('disabled timing does not create diagnostic files',async()=>{
 const directory=await mkdtemp(join(tmpdir(),'xmind-fixture-timing-'));
 const result=await retainFixtureTiming(launch("process.stdout.write('ordinary fixture')"));assert.equal(result.stdout,'ordinary fixture');assert.deepEqual(await readdir(directory),[]);
});
test('timeout retains the killed signal without fabricating a successful exit code',async()=>{
 const directory=await mkdtemp(join(tmpdir(),'xmind-fixture-timing-'));
 const pending=promisify(execFile)(process.execPath,['-e',"process.stdout.write('waiting fixture');setInterval(()=>{},1000)"],{windowsHide:true,timeout:1000,maxBuffer:65536});
 await assert.rejects(retainFixtureTiming(pending,{directory,phase:'timeout'}),error=>error.killed===true);
 const record=JSON.parse(await readFile(join(directory,'timeout-'+process.pid+'.json'),'utf8'));assert.equal(record.code,null);assert.equal(record.killed,true);assert.equal(record.signal,'SIGTERM');
});
