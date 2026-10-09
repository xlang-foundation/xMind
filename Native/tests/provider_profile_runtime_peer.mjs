// Independent synthetic model peer; the native runtime performs all execution,
// persistence and key resolution. This does not establish live account support.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile,spawn} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,rm,mkdir,symlink,unlink} from 'node:fs/promises';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-profile-runtime-'));
const [binary,modules,stdlib]=process.argv.slice(2);
let failure;const observed=[];const workspaceTurns=new Map();
let openaiDiscoveries=0,claudePages=0,pageLimitPages=0,child,stalledDiscovery,discoveryReleased=false,aliasRetargeted=false;
const event=value=>`event: ${value.type}\ndata: ${JSON.stringify(value)}\n\n`;
async function startup(database,check){
  const token='synthetic-profile-startup-owner-access-token';
  const backend=spawn(join(dirname(binary),'xmind_server.exe'),['--db',join(root,database),'--modules',modules,'--stdlib',stdlib,'--port','0'],{windowsHide:true,env:{...process.env,XMIND_AUTH_TOKEN:token}});
  let stdout='',stderr='';backend.stderr.on('data',data=>stderr+=data);
  try{
    const port=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Native profile startup readiness timed out')),10000);
      backend.once('error',error=>{clearTimeout(timer);reject(error);});backend.once('exit',code=>{clearTimeout(timer);reject(new Error(`Native profile startup exited ${code}: ${stderr}`));});
      backend.stdout.on('data',data=>{stdout+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(stdout);if(match){clearTimeout(timer);resolve(Number(match[1]));}});
    });
    const api=async(path,body)=>{const response=await fetch(`http://127.0.0.1:${port}${path}`,{headers:{Authorization:`Bearer ${token}`,...(body?{'Content-Type':'application/json'}:{})},method:body?'POST':'GET',...(body?{body:JSON.stringify(body)}:{}),signal:AbortSignal.timeout(10000)});assert.equal(response.status,200);const result=await response.json();assert.ok(!JSON.stringify(result).includes('runtime-openai-fixture-key'));assert.ok(!JSON.stringify(result).includes('sk-invalid-legacy-model'));return result;};
    await check(api);
  }finally{if(backend.exitCode===null){const closed=new Promise(resolve=>backend.once('exit',resolve));backend.kill();await closed;}}
}
const server=createServer((request,response)=>{
  let raw='';request.on('data',data=>{raw+=data;if(raw.length>65536)request.destroy();});
  request.on('end',()=>{try{
    if(request.method==='GET'){
      assert.equal(raw,'');const url=new URL(request.url,'http://127.0.0.1');
      if(url.pathname==='/openai-models'){
        openaiDiscoveries++;assert.equal(request.headers['x-api-key'],undefined);
        if(request.headers.authorization==='Bearer runtime-delayed-catalogue-key'){
          stalledDiscovery=response;child.stdin.write('fixture-discovery-started\n');return;
        }
        assert.equal(request.headers.authorization,'Bearer runtime-openai-fixture-key');
        response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({object:'list',data:[{object:'model',id:'fixture-openai-updated'},{object:'model',id:'fixture-openai'},{object:'model',id:'fixture-openai'}]}));return;
      }
      assert.equal(url.pathname,'/claude-models');assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['anthropic-version'],'2023-06-01');assert.equal(url.searchParams.get('limit'),'1000');
      const key=request.headers['x-api-key'];
      if(key==='runtime-redirect-catalogue-key'){response.writeHead(307,{Location:'/must-not-follow'});response.end();return;}
      response.writeHead(200,{'Content-Type':'application/json'});
      if(key==='runtime-malformed-catalogue-key'){response.end('{"data":[],"has_more":false,"has_more":true,"last_id":null}');return;}
      if(key==='runtime-reflected-catalogue-key'){response.end(JSON.stringify({data:[{type:'model',id:key}],has_more:false,last_id:key}));return;}
      if(key==='runtime-prefixed-reflection-key'){const id='model-'+key;response.end(JSON.stringify({data:[{type:'model',id}],has_more:false,last_id:id}));return;}
      if(key==='runtime-loop-catalogue-key'){response.end(JSON.stringify({data:[{type:'model',id:'fixture-claude'}],has_more:true,last_id:'fixture-claude'}));return;}
      if(key==='runtime-entry-limit-key'){response.end(JSON.stringify({data:Array.from({length:4097},(_,i)=>({type:'model',id:'fixture-entry-'+i})),has_more:false,last_id:null}));return;}
      if(key==='runtime-page-limit-key'){const id='fixture-page-'+(++pageLimitPages);response.end(JSON.stringify({data:[{type:'model',id}],has_more:true,last_id:id}));return;}
      assert.equal(key,'runtime-claude-fixture-key');claudePages++;
      const next=url.searchParams.get('after_id');assert.ok(next===null||next==='fixture-claude/next');
      if(next!==null)assert.ok(request.url.includes('after_id=fixture-claude%2Fnext'));
      response.end(JSON.stringify(next===null?{data:[{type:'model',id:'fixture-claude/next'}],has_more:true,last_id:'fixture-claude/next'}:{data:[{type:'model',id:'fixture-claude'}],has_more:false,last_id:'fixture-claude'}));return;
    }
    assert.equal(request.method,'POST');assert.ok(!raw.includes('runtime-openai-fixture-key')&&!raw.includes('runtime-claude-fixture-key'));
    const body=JSON.parse(raw);assert.equal(body.stream,true);assert.ok(!raw.includes('provider_context')&&!raw.includes('profile_revision'),'Backend provenance must not become model conversation data');observed.push({route:request.url,model:body.model});
    if(request.url==='/workspace-messages'){
      assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],'runtime-claude-fixture-key');assert.equal(request.headers['anthropic-version'],'2023-06-01');assert.equal(body.model,'fixture-claude');assert.equal(body.max_tokens,4096);
      assert.deepEqual(body.tools.map(tool=>tool.name).sort(),['inspect_plan','list_files','list_skills','load_skill','plan_tasks','read_file','read_repository_instructions','revise_plan','search_files']);assert.ok(body.tools.every(tool=>tool.input_schema.type==='object'));
      const prompt=body.messages.flatMap(message=>message.role==='user'?message.content:[]).find(block=>block.type==='text'&&/^Workspace binding fixture:(left|right)$/.test(block.text));assert.ok(prompt,'Only designated native workspace roots may reach the peer');const name=prompt.text.split(':').at(-1),turn=(workspaceTurns.get(name)||0)+1;workspaceTurns.set(name,turn);assert.ok(turn<=2,'Workspace guard/reopen cannot replay native inference');
      const id='toolu-workspace-'+name,expected='Actual workspace '+name.toUpperCase()+' file\n';
      if(turn===1)assert.equal(body.messages.filter(message=>message.role==='assistant').length,0);
      else {const results=body.messages.flatMap(message=>message.role==='user'?message.content:[]).filter(block=>block.type==='tool_result');assert.equal(results.length,1);assert.equal(results[0].tool_use_id,id);assert.deepEqual(JSON.parse(results[0].content),{path:'README.md',content:expected});}
      response.writeHead(200,{'Content-Type':'text/event-stream'});response.write(event({type:'message_start',message:{id:'msg-workspace-'+name+'-'+turn,type:'message',role:'assistant',model:body.model,content:[],stop_reason:null,stop_sequence:null,usage:{input_tokens:3,output_tokens:0}}}));
      if(turn===1){response.write(event({type:'content_block_start',index:0,content_block:{type:'tool_use',id,name:'read_file',input:{}}}));response.write(event({type:'content_block_delta',index:0,delta:{type:'input_json_delta',partial_json:'{"path":"README.md"}'}}));}
      else {response.write(event({type:'content_block_start',index:0,content_block:{type:'text',text:''}}));response.write(event({type:'content_block_delta',index:0,delta:{type:'text_delta',text:'Verified native '+name+' workspace read'}}));}
      response.write(event({type:'content_block_stop',index:0}));response.write(event({type:'message_delta',delta:{stop_reason:turn===1?'tool_use':'end_turn',stop_sequence:null},usage:{output_tokens:4}}));response.end(event({type:'message_stop'}));return;
    }
    if(request.url==='/chat'){
      assert.equal(request.headers.authorization,'Bearer runtime-openai-fixture-key');assert.equal(request.headers['x-api-key'],undefined);
      assert.ok(['fixture-openai','fixture-openai-updated'].includes(body.model));assert.equal(body.max_completion_tokens,64);assert.ok(body.messages.some(message=>message.role==='user'));
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      const completion=`data: ${JSON.stringify({choices:[{index:0,delta:{content:'Actual OpenAI fixture reply'},finish_reason:'stop'}],usage:{prompt_tokens:3,completion_tokens:4,total_tokens:7}})}\n\ndata: [DONE]\n\n`;
      // Keep the first real native request in flight for the ownership assertion.
      if(observed.length===1)setTimeout(()=>response.end(completion),100);
      else response.end(completion);
    }else{
      assert.equal(request.url,'/messages');assert.equal(request.headers.authorization,undefined);
      assert.equal(request.headers['x-api-key'],'runtime-claude-fixture-key');assert.equal(request.headers['anthropic-version'],'2023-06-01');
      assert.equal(body.model,'fixture-claude');const defaultBudget=body.messages.some(message=>message.role==='user'&&Array.isArray(message.content)&&message.content.some(block=>block.type==='text'&&['Claude profile default output fixture','Claude profile reopened default output fixture'].includes(block.text)));assert.equal(body.max_tokens,defaultBudget?4096:64);assert.ok(body.messages.some(message=>message.role==='user'));
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      response.write(event({type:'message_start',message:{id:'msg-profile-fixture',type:'message',role:'assistant',model:body.model,content:[],stop_reason:null,stop_sequence:null,usage:{input_tokens:3,output_tokens:0}}}));
      response.write(event({type:'content_block_start',index:0,content_block:{type:'text',text:''}}));
      response.write(event({type:'content_block_delta',index:0,delta:{type:'text_delta',text:'Actual Claude fixture reply'}}));
      response.write(event({type:'content_block_stop',index:0}));
      response.write(event({type:'message_delta',delta:{stop_reason:'end_turn',stop_sequence:null},usage:{output_tokens:4}}));
      response.end(event({type:'message_stop'}));
    }
  }catch(error){failure=error;response.writeHead(500);response.end('Synthetic profile assertion failed');}});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try{
  // Existing native boundary fixtures use Windows junctions without admin or
  // symlink privileges. A failure fails the case rather than skipping it.
  await mkdir(join(root,'workspace-left'));await symlink(join(root,'workspace-left'),join(root,'workspace-startup-alias'),'junction');
  const running=execute(binary,[root,modules,stdlib,`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:35000});child=running.child;
  let output='';child.stdout.on('data',chunk=>{
    output+=chunk;
    if(!aliasRetargeted&&output.includes('fixture-workspace-alias-captured')){
      aliasRetargeted=true;
      (async()=>{await unlink(join(root,'workspace-startup-alias'));await symlink(join(root,'workspace-right'),join(root,'workspace-startup-alias'),'junction');child.stdin.write('fixture-workspace-alias-retargeted\n');})().catch(error=>{failure=error;child.stdin.write('fixture-workspace-alias-failed\n');});
    }
    if(!discoveryReleased&&output.includes('fixture-discovery-committed')){
      discoveryReleased=true;stalledDiscovery.writeHead(200,{'Content-Type':'application/json'});stalledDiscovery.end(JSON.stringify({object:'list',data:[{object:'model',id:'fixture-openai'}]}));
    }
  });
  const result=await running;
  if(failure)throw failure;
  assert.deepEqual(observed,[{route:'/chat',model:'fixture-openai'},{route:'/messages',model:'fixture-claude'},{route:'/chat',model:'fixture-openai-updated'},{route:'/messages',model:'fixture-claude'},{route:'/messages',model:'fixture-claude'},{route:'/messages',model:'fixture-claude'},{route:'/messages',model:'fixture-claude'},{route:'/messages',model:'fixture-claude'},{route:'/messages',model:'fixture-claude'},{route:'/workspace-messages',model:'fixture-claude'},{route:'/workspace-messages',model:'fixture-claude'},{route:'/workspace-messages',model:'fixture-claude'},{route:'/workspace-messages',model:'fixture-claude'}]);
  assert.deepEqual([...workspaceTurns],[['left',2],['right',2]]);assert.equal(aliasRetargeted,true);
  assert.equal(openaiDiscoveries,5);assert.equal(claudePages,2);assert.equal(pageLimitPages,8);assert.equal(discoveryReleased,true);
  await startup('startup-valid.sqlite',async api=>{const profiles=await api('/v1/provider/profiles');assert.equal(profiles.revision,1);assert.equal(profiles.active,'openai');assert.equal(profiles.profiles[0].model,'gpt-4.1');assert.equal((await api('/v1/health')).agent_execution,true);});
  await startup('startup-repair.sqlite',async api=>{const profiles=await api('/v1/provider/profiles');assert.equal(profiles.revision,1);assert.equal(profiles.profiles[0].model,'');assert.equal((await api('/v1/health')).agent_execution,false);assert.deepEqual((await api('/v1/models')).models,[]);const repaired=await api('/v1/provider/configuration',{model:'gpt-4.1',expected_revision:1});assert.equal(repaired.revision,2);assert.equal((await api('/v1/health')).agent_execution,true);});
  await startup('startup-repair.sqlite',async api=>{const profiles=await api('/v1/provider/profiles');assert.equal(profiles.revision,2);assert.equal(profiles.profiles[0].model,'gpt-4.1');});
  process.stdout.write(result.stdout);
}finally{await new Promise(resolve=>server.close(resolve));await rm(root,{recursive:true,force:true});}
