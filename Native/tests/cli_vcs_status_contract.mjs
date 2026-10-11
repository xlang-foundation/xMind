import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {randomBytes} from 'node:crypto';

const [cliExe]=process.argv.slice(2);assert.ok(cliExe,'Pass the native xMind CLI executable');
const token=randomBytes(32).toString('hex'),calls=[];
let payload=[{file:'src/file with spaces.cpp',additions:3,deletions:1,status:'modified'}];
const invoke=(port,env)=>new Promise((resolve,reject)=>{
  const child=spawn(cliExe,[String(port),'git-status'],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';
  const timer=setTimeout(()=>child.kill(),10000);child.stdout.on('data',chunk=>stdout+=chunk);child.stderr.on('data',chunk=>stderr+=chunk);
  child.once('error',error=>{clearTimeout(timer);reject(error);});child.once('close',(status,signal)=>{clearTimeout(timer);resolve({status,signal,stdout,stderr});});
});
const server=createServer((request,response)=>{
  calls.push({method:request.method,path:request.url,authorization:request.headers.authorization});
  response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify(payload));
});
try{
  await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
  const port=server.address().port,env={...process.env,XMIND_AUTH_TOKEN:token};
  const result=await invoke(port,env);
  assert.equal(result.status,0,result.stderr||'CLI failed');assert.deepEqual(JSON.parse(result.stdout),payload);
  assert.deepEqual(calls,[{method:'GET',path:'/v1/vcs/status',authorization:`Bearer ${token}`}]);
  payload=[{file:'src/main.cpp',additions:-1,deletions:0,status:'modified'}];
  const malformed=await invoke(port,env);
  assert.equal(malformed.status,1);assert.match(malformed.stderr,/Invalid native Git status entry/);
  assert.equal(calls.length,2);
  console.log('Native CLI Git status contract passed authenticated GET routing and strict response validation.');
}finally{await new Promise(resolve=>server.close(resolve));}
