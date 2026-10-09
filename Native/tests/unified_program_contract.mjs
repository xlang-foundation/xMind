// Real native unified entry points and embedded-xlang3 SQLite. No model fixture
// or live inference is used here; agent/effect coverage is in the paired tests.
import assert from 'node:assert/strict';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {randomBytes} from 'node:crypto';
const [program,modules,stdlib]=process.argv.slice(2);
assert.ok(program&&modules&&stdlib);
const owned=await mkdtemp(join(tmpdir(),'xmind-unified-'));
const workspace=join(owned,'workspace 雪'),database=join(owned,'state 雪.sqlite'),catalog=join(owned,'graphs 雪.json');
const token=randomBytes(32).toString('hex');
const env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;delete env.XMIND_SETUP_SECRET;
let child,closed,port;
function invoke(args,options={}){
 return spawnSync(program,args,{env,windowsHide:true,encoding:'utf8',timeout:15000,...options});
}
function consoleCommand(...args){
 const result=invoke(['--port',String(port),...args]);assert.equal(result.status,0,result.stderr);
 return JSON.parse(result.stdout);
}
async function start(){
 child=spawn(program,['serve','--db',database,'--modules',modules,'--stdlib',stdlib,'--workspace',workspace,'--port','0','--graphs-config',catalog],{env,windowsHide:true});
 closed=new Promise((yes,no)=>{child.once('error',no);child.once('close',(code,signal)=>yes({code,signal}));});
 let output='',errors='';child.stderr.on('data',b=>errors+=b);
 port=await new Promise((yes,no)=>{
  const timer=setTimeout(()=>no(Error('Unified backend readiness deadline: '+errors)),15000);
  child.once('error',error=>{clearTimeout(timer);no(error);});
  child.once('exit',code=>{clearTimeout(timer);no(Error('Unified backend exited '+code+': '+errors));});
  child.stdout.on('data',b=>{output+=b;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);if(match){clearTimeout(timer);yes(Number(match[1]));}});
 });
}
async function stop(){if(child){if(child.exitCode===null)child.kill();await closed;child=undefined;}}
try{
 const help=invoke(['--help']);assert.equal(help.status,0);assert.match(help.stdout,/xmind serve/);assert.match(help.stdout,/discovers or starts its persistent local workspace profile/);
 for(const args of [['--port'],['--port','0'],['--port','65536'],['--port','12x'],['worker']])assert.equal(invoke(args).status,2,'Invalid/unimplemented mode cannot start a backend');
 const schemaRequest=JSON.stringify({jsonrpc:'2.0',id:1,method:'schema/validate',params:{schema_json:'{"type":"object","properties":{"value":{"type":"string"}},"required":["value"],"additionalProperties":false}',instance_json:'{"value":"雪"}'}})+'\n';
 const schema=invoke(['schema-worker'],{input:schemaRequest});assert.equal(schema.status,0,schema.stderr);assert.deepEqual(JSON.parse(schema.stdout),{jsonrpc:'2.0',id:1,result:{schema_valid:true,instance_valid:true}});
 assert.equal(invoke(['schema-worker','unexpected'],{input:schemaRequest}).status,2);
 const badRequest=JSON.parse(schemaRequest);badRequest.params.instance_json='{"value":1}';
 const badSchema=invoke(['schema-worker'],{input:JSON.stringify(badRequest)+'\n'});
 assert.equal(badSchema.status,0,badSchema.stderr);assert.equal(JSON.parse(badSchema.stdout).error.data.kind,'arguments_invalid');
 await mkdir(workspace);await writeFile(join(workspace,'sample.txt'),'Actual unified workspace bytes\n');
 await writeFile(catalog,JSON.stringify({graphs:[{id:'unified.read',spec:{nodes:[{id:'read',type:'tool',tool:'read_file',arguments:{path:'sample.txt'}}]}}]}));
 const imported=invoke(['admin','--db',database,'--modules',modules,'--stdlib',stdlib,'import-graphs',catalog]);
 assert.equal(imported.status,0,imported.stderr);assert.equal(JSON.parse(imported.stdout).graphs[0].id,'unified.read');
 await start();assert.equal(consoleCommand('health').status,'ok');
 const unauthorized=await fetch('http://127.0.0.1:'+port+'/v1/health');assert.equal(unauthorized.status,401);
 const wrong=invoke(['--port',String(port),'health'],{env:{...env,XMIND_AUTH_TOKEN:'w'.repeat(64)}});assert.equal(wrong.status,1);assert.ok(!wrong.stderr.includes(token));
 const identity=invoke(['admin','inspect-owner-process',String(child.pid)]);assert.equal(identity.status,0,identity.stderr);assert.equal(JSON.parse(identity.stdout).process_id,child.pid);
 const before=consoleCommand('sessions');assert.deepEqual(before,[]);
 const detached=invoke(['--port',String(port)],{input:'/exit\n'});assert.equal(detached.status,0,detached.stderr);assert.equal(child.exitCode,null);assert.deepEqual(consoleCommand('sessions'),[]);
 const composed=invoke(['--port',String(port),'chat'],{input:'/compose unified.read\nRequest 雪\n\n/exit\n/send\n/history\n/exit\n'});
 assert.equal(composed.status,0,composed.stderr);
 const records=composed.stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line));
 const roots=records.filter(row=>row.type==='run');assert.equal(roots.length,1);const root=roots[0].run;
 const graph=consoleCommand('graph',root.id);assert.equal(graph.run.state,'completed');assert.equal(graph.checkpoint.nodes[0].output.content,'Actual unified workspace bytes\n');
 const history=consoleCommand('history',root.session_id);assert.equal(history.find(message=>message.role==='user').data.content,'Request 雪\n\n/exit\n');
 const sessions=consoleCommand('sessions');assert.equal(sessions.length,1);
 const runs=consoleCommand('runs',root.session_id);assert.equal(runs.length,1);
 await stop();await start();assert.deepEqual(consoleCommand('sessions'),sessions);assert.deepEqual(consoleCommand('history',root.session_id),history);assert.deepEqual(consoleCommand('runs',root.session_id),runs);
 console.log('Unified native program passed actual serve/console/admin/schema modes, Unicode arguments, authentication, client detach without cancellation, composed tool graph and exact xlang3 SQLite restart history. No model inference, automatic bootstrap, IPC worker or installed-package acceptance is claimed.');
}finally{
 await stop();assert.equal(dirname(resolve(owned)),resolve(tmpdir()));assert.ok(basename(owned).startsWith('xmind-unified-'));await rm(owned,{recursive:true,force:true});
}
