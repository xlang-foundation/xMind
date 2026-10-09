// Independent synthetic DeepSeek inference wire. The native runner,
// SQLite history, encrypted credential and two workspace reads are real;
// no live inference is used.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,relative,isAbsolute} from 'node:path';
const [executable,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
const root=await mkdtemp(join(tmpdir(),'xmind-deepseek-')),workspace=join(root,'workspace');
const key='deepseek-native-fixture-not-a-live-key';
const reasoning='Synthetic 中 reasoning \0 exact';
const args='{"path":"README.md","n":1.00000000000000000001}';
const usage={prompt_tokens:19,completion_tokens:11,total_tokens:30,prompt_cache_hit_tokens:7,prompt_cache_miss_tokens:12,prompt_tokens_details:{cached_tokens:7},completion_tokens_details:{reasoning_tokens:4}};
const routes=[],engine=[];let failure,passed=false;
function chunk(delta,finish=null,metrics=null){return {id:'synthetic-response',object:'chat.completion.chunk',model:'deepseek-flash',choices:[{index:0,delta,finish_reason:finish}],usage:metrics};}
function stream(response,deltas,finish,metrics=usage){
  response.writeHead(200,{'Content-Type':'text/event-stream'});
  for(const delta of deltas)response.write('data: '+JSON.stringify(chunk(delta))+'\n\n');
  response.write('data: '+JSON.stringify(chunk({},finish,metrics))+'\n\n');
  response.end('data: [DONE]\n\n');
}
function tool(id,name,arguments_json){return {index:0,id,type:'function',function:{name,arguments:arguments_json}};}
function gateway(body){
  assert.equal(body.messages[0].role,'system');assert.equal(body.messages[0].content,'Native fixture instructions');
  assert.equal(body.messages[1].content,'Gateway fixture');assert.equal(body.max_tokens,64);
}
function assistant(body,index,expectedReason,expectedContent){
  const saved=body.messages.filter(item=>item.role==='assistant')[index];
  assert.ok(saved);assert.equal(saved.reasoning_content,expectedReason);assert.equal(saved.content,expectedContent);return saved;
}
const server=createServer((request,response)=>{
  let source='';request.on('data',data=>{source+=data;assert.ok(source.length<8*1024*1024);});
  request.on('end',()=>{
    try{
      routes.push(request.url);assert.equal(request.method,'POST');assert.equal(request.headers.authorization,'Bearer '+key);assert.ok(!source.includes(key));
      const body=JSON.parse(source);assert.equal(body.model,'deepseek-flash');assert.equal(body.stream,true);assert.ok(!Object.hasOwn(body,'n'));assert.ok(!Object.hasOwn(body,'max_completion_tokens'));assert.equal(body.stream_options.include_usage,true);
      if(request.url==='/unauthorized'){response.writeHead(401,{'Content-Type':'application/json'});response.end('{"error":{"message":"synthetic private error must not be published"}}');return;}
      if(request.url==='/redirect'){response.writeHead(307,{Location:'/must-not-arrive'});response.end();return;}
      if(request.url==='/engine'){
        engine.push(body);assert.equal(body.max_tokens,128);assert.ok(!Object.hasOwn(body,'reasoning_effort'));assert.ok(!Object.hasOwn(body,'thinking'));
        assert.deepEqual(body.tools.map(item=>item.function.name).sort(),['read_repository_instructions','read_file','list_files','search_files','list_skills','load_skill'].sort());
        const prior=body.messages.filter(item=>item.role==='assistant');
        if(engine.length===1){assert.equal(prior.length,0);assert.equal(body.messages.at(-1).content,'Read both fixture files');stream(response,[{reasoning_content:'Engine reasoning A'},{tool_calls:[tool('engine-a','read_file','{"path":"A.txt"}')]}],'tool_calls');return;}
        const first=assistant(body,0,'Engine reasoning A','');assert.equal(first.tool_calls[0].id,'engine-a');assert.equal(first.tool_calls[0].function.arguments,'{"path":"A.txt"}');
        const outputs=body.messages.filter(item=>item.role==='tool');assert.equal(JSON.parse(outputs[0].content).content,'Actual fixture file A\n');
        if(engine.length===2){assert.equal(prior.length,1);assert.equal(outputs.length,1);stream(response,[{reasoning_content:'Engine reasoning B'},{tool_calls:[tool('engine-b','read_file','{"path":"B.txt"}')]}],'tool_calls');return;}
        const second=assistant(body,1,'Engine reasoning B','');assert.equal(second.tool_calls[0].id,'engine-b');assert.equal(second.tool_calls[0].function.arguments,'{"path":"B.txt"}');assert.equal(JSON.parse(outputs[1].content).content,'Actual fixture file B\n');
        if(engine.length===3){assert.equal(prior.length,2);assert.equal(outputs.length,2);stream(response,[{reasoning_content:'Engine reasoning join'},{content:'Both actual native files read'}],'stop');return;}
        assert.equal(engine.length,4);assert.equal(prior.length,3);assistant(body,2,'Engine reasoning join','Both actual native files read');assert.equal(body.messages.at(-1).role,'user');assert.equal(body.messages.at(-1).content,'Continue the prior native session');stream(response,[{reasoning_content:'Engine reasoning reopen'},{content:'Reopened native session completed'}],'stop');return;
      }
      gateway(body);
      if(request.url==='/none'){assert.equal(body.reasoning_effort,'none');assert.ok(!Object.hasOwn(body,'tools'));stream(response,[{reasoning_content:null},{content:'Non-thinking completed'}],'stop');return;}
      assert.ok(!Object.hasOwn(body,'reasoning_effort'));assert.ok(!Object.hasOwn(body,'thinking'));assert.equal(body.tools[0].function.name,'read_file');
      if(request.url==='/incomplete'){response.writeHead(200,{'Content-Type':'text/event-stream'});response.end('data: '+JSON.stringify(chunk({reasoning_content:'Synthetic partial'}))+'\n\n');return;}
      if(request.url==='/resource'){stream(response,[{reasoning_content:'Interrupted reasoning'}],'insufficient_system_resource');return;}
      if(request.url==='/bad-usage'){stream(response,[{content:'Unacknowledged output'}],'stop',{...usage,prompt_cache_hit_tokens:-1});return;}
      if(request.url==='/missing-usage'){stream(response,[{content:'Unacknowledged output'}],'stop',null);return;}
      if(request.url==='/missing-reasoning'){stream(response,[{tool_calls:[tool('missing-reasoning-call','read_file',args)]}],'tool_calls');return;}
      if(request.url==='/continuation'||request.url==='/next-turn'){
        const original=assistant(body,0,reasoning,'');assert.equal(original.tool_calls[0].id,'gateway-call');assert.equal(original.tool_calls[0].function.arguments,args);assert.equal(body.messages[3].tool_call_id,'gateway-call');assert.equal(body.messages[3].content,'Synthetic gateway tool result');
        if(request.url==='/next-turn'){assistant(body,1,'Final gateway reasoning','Gateway completed');assert.equal(body.messages.at(-1).content,'Second gateway turn');stream(response,[{reasoning_content:'Next turn reasoning'},{content:'Next turn completed'}],'stop');}
        else stream(response,[{reasoning_content:'Final gateway reasoning'},{content:'Gateway completed'}],'stop');return;
      }
      assert.ok(request.url==='/tools'||request.url==='/unknown');
      stream(response,[{reasoning_content:reasoning.slice(0,10)},{reasoning_content:reasoning.slice(10)},{tool_calls:[tool('gateway-call',request.url==='/unknown'?'unoffered_tool':'read_file',args.slice(0,20))]},{tool_calls:[{index:0,function:{arguments:args.slice(20)}}]}],'tool_calls');
    }catch(error){failure=error;if(!response.headersSent)response.writeHead(400);response.end();}
  });
});
try{
  await mkdir(workspace);await writeFile(join(workspace,'A.txt'),'Actual fixture file A\n');await writeFile(join(workspace,'B.txt'),'Actual fixture file B\n');
  await new Promise(resolveReady=>server.listen(0,'127.0.0.1',resolveReady));
  // Explicit restricted process environment: no inherited provider secret vars.
  const env={};for(const name of ['SystemRoot','WINDIR','PATH','TEMP','TMP','ComSpec','PATHEXT','USERPROFILE','LOCALAPPDATA'])if(process.env[name])env[name]=process.env[name];
  let result;
  try{result=await execute(executable,[join(root,'state.sqlite'),modules,stdlib,workspace,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:45000,maxBuffer:1024*1024,env});}
  catch(error){await writeFile(join(root,'native-stdout.log'),error.stdout??'',{flag:'wx',mode:0o600});await writeFile(join(root,'native-stderr.log'),error.stderr??'',{flag:'wx',mode:0o600});throw new Error('Native DeepSeek fixture failed; retained owned root: '+root);}
  if(failure)throw failure;
  assert.deepEqual(routes,['/tools','/continuation','/next-turn','/none','/unknown','/bad-usage','/missing-usage','/missing-reasoning','/resource','/incomplete','/unauthorized','/redirect','/engine','/engine','/engine','/engine']);assert.equal(engine.length,4);passed=true;process.stdout.write(result.stdout);
}catch(error){
  // Synthetic raw peer errors stay only in the retained owned fixture.
  await writeFile(join(root,'private-peer-error.log'),String(failure?.stack??error.stack).slice(0,65536),{flag:'wx',mode:0o600});process.stderr.write('DeepSeek fixture failed; retained owned root: '+root+'\n');process.exitCode=1;
}finally{
  server.closeAllConnections();await new Promise(resolveClosed=>server.close(resolveClosed));
  if(passed){const canonical=resolve(root),base=resolve(tmpdir()),part=relative(base,canonical);assert.ok(part&&!part.startsWith('..')&&!isAbsolute(part)&&canonical.startsWith(join(base,'xmind-deepseek-')));await rm(canonical,{recursive:true,force:true});}
}
