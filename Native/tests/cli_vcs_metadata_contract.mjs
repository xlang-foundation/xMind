import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {randomBytes} from 'node:crypto';

const [cliExe]=process.argv.slice(2);assert.ok(cliExe,'Pass the native xMind CLI executable');
const token=randomBytes(32).toString('hex'),calls=[];
let reply={command:'git-info',body:{provider:'git',branch:{current:'feature/example',default:'main'}}};
const invoke=(port,command,env)=>new Promise((resolve,reject)=>{
 const child=spawn(cliExe,[String(port),command],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';
 const timer=setTimeout(()=>child.kill(),10000);child.stdout.on('data',chunk=>stdout+=chunk);child.stderr.on('data',chunk=>stderr+=chunk);
 child.once('error',error=>{clearTimeout(timer);reject(error);});child.once('close',(status,signal)=>{clearTimeout(timer);resolve({status,signal,stdout,stderr});});
});
const server=createServer((request,response)=>{calls.push({method:request.method,path:request.url,authorization:request.headers.authorization});response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify(reply.body));});
try{
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
 const port=server.address().port,env={...process.env,XMIND_AUTH_TOKEN:token};
 let result=await invoke(port,'git-info',env);assert.equal(result.status,0,result.stderr);assert.deepEqual(JSON.parse(result.stdout),reply.body);
 reply={command:'git-branches',body:['main','feature/example']};result=await invoke(port,'git-branches',env);assert.equal(result.status,0,result.stderr);assert.deepEqual(JSON.parse(result.stdout),reply.body);
 assert.deepEqual(calls,[{method:'GET',path:'/v1/vcs/info',authorization:`Bearer ${token}`},{method:'GET',path:'/v1/vcs/branches',authorization:`Bearer ${token}`}]);
 reply={command:'git-info',body:{provider:'git',branch:{current:{},default:null}}};result=await invoke(port,'git-info',env);assert.equal(result.status,1);assert.match(result.stderr,/Invalid native Git metadata response/);
 reply={command:'git-branches',body:['main','invalid\nbranch']};result=await invoke(port,'git-branches',env);assert.equal(result.status,1);assert.match(result.stderr,/Invalid native Git branch entry/);
 assert.equal(calls.length,4);
 console.log('Native CLI Git metadata contract passed authenticated routing and strict response validation.');
}finally{await new Promise(resolve=>server.close(resolve));}
