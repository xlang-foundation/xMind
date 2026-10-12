import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {randomBytes} from 'node:crypto';

const [cliExe]=process.argv.slice(2);assert.ok(cliExe,'Pass the native xMind CLI executable');
const token=randomBytes(32).toString('hex'),calls=[];let response=null;
const server=createServer((request,res)=>{calls.push({method:request.method,path:request.url,authorization:request.headers.authorization});res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(response));});
const invoke=(port,args,env)=>new Promise((resolve,reject)=>{const child=spawn(cliExe,[String(port),...args],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';const timer=setTimeout(()=>child.kill(),10000);child.stdout.on('data',chunk=>stdout+=chunk);child.stderr.on('data',chunk=>stderr+=chunk);child.once('error',error=>{clearTimeout(timer);reject(error);});child.once('close',(status,signal)=>{clearTimeout(timer);resolve({status,signal,stdout,stderr});});});
try{
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});const port=server.address().port,env={...process.env,XMIND_AUTH_TOKEN:token};
 response=null;let result=await invoke(port,['git-base','windows-local-file-v1:2:shared'],env);assert.equal(result.status,0,result.stderr);assert.equal(JSON.parse(result.stdout),null);
 const base='a'.repeat(40);response=[{file:'folder/file with spaces.txt',patch:'diff --git a/file b/file\n',additions:2,deletions:1,status:'modified'}];result=await invoke(port,['git-diff','committed',base,'4','--folder-id','windows-local-file-v1:2:shared'],env);assert.equal(result.status,0,result.stderr);assert.deepEqual(JSON.parse(result.stdout),response);assert.deepEqual(calls,[{method:'GET',path:'/v1/vcs/base?folder_id=windows-local-file-v1:2:shared',authorization:`Bearer ${token}`},{method:'GET',path:`/v1/vcs/diff?mode=committed&base=${base}&context=4&folder_id=windows-local-file-v1:2:shared`,authorization:`Bearer ${token}`}]);
 response=[{file:'bad',patch:'',additions:-1,deletions:0,status:'modified'}];result=await invoke(port,['git-diff','working'],env);assert.equal(result.status,1);assert.match(result.stderr,/Invalid native Git diff entry/);assert.equal(calls.length,3);
 console.log('Native CLI Git diff contract passed authenticated folder routing, query construction and strict response validation.');
}finally{await new Promise(resolve=>server.close(resolve));}
