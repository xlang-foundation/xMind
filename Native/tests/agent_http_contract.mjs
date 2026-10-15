// Synthetic inference peer for protocol verification only. Product binaries
// execute the actual native loop, filesystem operations and xlang3 persistence.
// This test is not evidence of live inference or coding-task completion.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,spawnSync} from 'node:child_process';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {randomBytes} from 'node:crypto';
import {setTimeout as delay} from 'node:timers/promises';
import {createRequire} from 'node:module';
const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');

const [serverExe,cliExe,adminExe,modules,stdlib]=process.argv.slice(2);
assert.ok(serverExe && cliExe && adminExe && modules && stdlib,'Pass server, CLI, admin and runtime roots');
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-http-'));
const workspace=join(folder,'workspace'),token=randomBytes(32).toString('hex');
const key='synthetic-native-http-protocol-credential';
const baseEnv={...process.env,XMIND_AUTH_TOKEN:token};
delete baseEnv.XMIND_API_KEY;
let child,port,peerError,requests=0;
const requestsByPrompt=new Map();
const peer=createServer((request,response)=>{
  let source='';request.on('data',chunk=>{source+=chunk;});
  request.on('end',()=>{
    try {
      ++requests;
      assert.equal(request.headers.authorization,`Bearer ${key}`);
      assert.ok(!source.includes(key));
      const body=JSON.parse(source);
      assert.equal(body.model,'synthetic-protocol-model');assert.equal(body.stream,true);
      const toolNames=body.tools.map(tool=>tool.function.name),expectedTools=toolNames.length===1&&toolNames[0]==='read_file'?['read_file']:['read_repository_instructions','read_file','list_files','search_files','find_files','glob_files','list_skills','load_skill','run_process','wait_process','stop_process','delegate_tasks'];assert.deepEqual(toolNames,expectedTools);
      const prompt=body.messages.findLast(message=>message.role==='user').content;
      requestsByPrompt.set(prompt,(requestsByPrompt.get(prompt)??0)+1);
      if(prompt==='provider-error') {
        response.writeHead(429,{'Content-Type':'application/json'});
        response.end(JSON.stringify({error:{type:'rate_limit_error',code:'rate_limit_exceeded',param:'private-provider-error-body',message:'private-provider-error-body'}}));return;
      }
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      const send=value=>response.write(`data: ${JSON.stringify(value)}\n\n`);
      if(prompt==='hold-stream') {
        send({choices:[{index:0,delta:{content:'Synthetic pending inference stream'}}]});return;
      }
      const last=body.messages.at(-1);
      if(last.role==='tool') {
        if(prompt==='Search the large native fixture by regex'){
          const result=JSON.parse(last.content);assert.equal(result.matches.length,2);assert.deepEqual(result.matches.map(match=>[match.path,match.line,match.text]),[['large.txt',99999,'line 99999 中'],['large.txt',100000,'line 100000 中']]);assert.equal(result.scanned_files,1);assert.equal(result.skipped_entries,0);assert.equal(result.truncated,false);assert.deepEqual(result.limits,[]);
        }else if(prompt==='Find nonignored owned fixture paths'){
          const result=JSON.parse(last.content);assert.deepEqual(result.paths,['ignore-http/keep.tmp','ignore-http/main.cpp']);assert.equal(result.truncated,false);assert.equal(result.ignore_files,1);assert.ok(result.ignored_entries>0);
        }else if(prompt==='Find filtered native content'){
          const result=JSON.parse(last.content);assert.deepEqual(result.matches.map(match=>match.path).sort(),['ignore-http/keep.tmp','ignore-http/main.cpp']);assert.ok(result.matches.every(match=>match.line===1&&match.text==='IgnoreHttpNeedle'));assert.equal(result.ignore_files,1);assert.ok(result.ignored_entries>0);
        }else if(prompt==='Discover actual native source paths'){
          const result=JSON.parse(last.content);assert.deepEqual(result.paths,['glob-src/main.cpp','glob-src/nested/header.hpp']);assert.equal(result.truncated,false);assert.equal(result.skipped_entries,0);
        }else if(prompt==='Read the last two lines of the large native fixture'){
          const page=JSON.parse(last.content);
          assert.equal(page.path,'large.txt');assert.equal(page.offset,99999);assert.equal(page.lines_read,2);
          assert.equal(page.content,'line 99999 中\r\nline 100000 中\r\n');assert.equal(page.has_more,false);assert.equal(page.truncated,false);assert.deepEqual(page.truncated_lines,[]);assert.ok(!Object.hasOwn(page,'next_offset'));
        }else assert.equal(JSON.parse(last.content).content,'Actual workspace content\n');
        send({choices:[{index:0,delta:{content:'Synthetic peer checked the actual workspace read.'},finish_reason:'stop'}]});
      } else {
        if(prompt==='Search the large native fixture by regex'){
          const schema=body.tools.find(tool=>tool.function.name==='search_files').function.parameters;assert.deepEqual(schema.required,['query']);assert.equal(schema.properties.regex.type,'boolean');assert.equal(schema.properties.case_sensitive.type,'boolean');assert.equal(schema.properties.limit.maximum,1000);
          send({choices:[{index:0,delta:{tool_calls:[{index:0,id:`http-call-${requests}`,type:'function',function:{name:'search_files',arguments:JSON.stringify({query:'^line (99999|100000) 中$',regex:true,path:'large.txt',limit:10})}}]},finish_reason:'tool_calls'}]});response.end('data: [DONE]\n\n');return;
        }
        const paged=prompt==='Read the last two lines of the large native fixture',discovery=prompt==='Discover actual native source paths';
        if(paged){const schema=body.tools.find(tool=>tool.function.name==='read_file').function.parameters;assert.equal(schema.properties.offset.minimum,1);assert.equal(schema.properties.limit.maximum,2000);assert.deepEqual(schema.required,['path']);}
        const ignored=prompt==='Find nonignored owned fixture paths',search=prompt==='Find filtered native content';
        send({choices:[{index:0,delta:{tool_calls:[{index:0,id:`http-call-${requests}`,type:'function',function:{name:search?'search_files':discovery||ignored?'glob_files':'read_file',arguments:search?'{"query":"IgnoreHttpNeedle"}':ignored?'{"pattern":"!*.never","path":"ignore-http"}':discovery?'{"pattern":"glob-src/**/*.{cpp,hpp}"}':paged?'{"path":"large.txt","offset":99999,"limit":2}':'{"path":"README.md"}'}}]},finish_reason:'tool_calls'}]});
      }
      response.end('data: [DONE]\n\n');
    } catch(error) {peerError=error;response.destroy();}
  });
});
async function start(withKey=false) {
  child=spawn(serverExe,['--db',join(folder,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,
    '--port','0','--model','synthetic-protocol-model','--model-endpoint',`http://127.0.0.1:${peer.address().port}/chat`,
    '--model-tools','supported','--workspace',workspace,'--credential-id','integration-provider','--workers','1','--queue-limit','1'],
    {env:withKey?{...baseEnv,XMIND_API_KEY:key}:baseEnv,windowsHide:true});
  let stdout='',stderr='';child.stderr.on('data',data=>{stderr+=data;});
  port=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error(`Server readiness timeout: ${stderr}`)),10000);
    child.once('error',error=>{clearTimeout(timer);reject(error);});
    child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Server exited ${code}: ${stderr}`));});
    child.stdout.on('data',data=>{
      stdout+=data;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(stdout);
      if(match) {clearTimeout(timer);resolve(Number(match[1]));}
    });
  });
}
async function stop() {
  if(!child || child.exitCode!==null) return;
  const exiting=new Promise(resolve=>child.once('exit',resolve));
  // Windows kill is abrupt: recovery assertions below test that actual failure.
  child.kill();await exiting;child=null;
}
async function api(path,body,method) {
  const response=await fetch(`http://127.0.0.1:${port}${path}`,{
    method:method|| (body===undefined?'GET':'POST'),headers:{Authorization:`Bearer ${token}`,...(body===undefined?{}:{'Content-Type':'application/json'})},
    body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(10000),redirect:'error'});
  return {status:response.status,data:response.status===204?{}:await response.json()};
}
function cli(...args) {
  const result=spawnSync(cliExe,[String(port),...args],{env:baseEnv,encoding:'utf8',windowsHide:true,timeout:15000});
  assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);
}
async function until(read,predicate,label) {
  const deadline=Date.now()+10000;
  while(Date.now()<deadline) {
    if(peerError) throw peerError;
    const value=await read();if(predicate(value)) return value;await delay(25);
  }
  throw new Error(`Timed out waiting for ${label}`);
}
const terminal=(id,state)=>until(async()=> (await api(`/v1/runs/${id}`)).data,value=>value.state===state,`${id} ${state}`);
const streaming=id=>until(async()=> (await api(`/v1/runs/${id}/events`)).data,events=>events.some(event=>event.kind==='model.text'),`${id} stream`);
async function session(id) {assert.equal((await api('/v1/sessions',{id,title:id})).status,201);}
async function chatResult(expectedExit,input,...args){
 const process=spawn(cliExe,[String(port),'chat',...args],{env:baseEnv,windowsHide:true});let output='',errors='';process.stdout.on('data',bytes=>output+=bytes);process.stderr.on('data',bytes=>errors+=bytes);const timer=setTimeout(()=>process.kill(),12000);
 const result=await new Promise((yes,no)=>{process.once('error',no);process.once('close',(code,signal)=>{clearTimeout(timer);yes({code,signal});});process.stdin.end(input);});assert.equal(result.signal,null,'Interactive CLI must finish without being killed');assert.equal(result.code,expectedExit,errors);assert.ok(!output.includes(key));return output.trim()?output.trim().split('\n').map(line=>JSON.parse(line)):[];
}
function liveChat(sessionId){
 const process=spawn(cliExe,[String(port),'chat',sessionId],{env:baseEnv,windowsHide:true});let output='',errors='';
 process.stdout.on('data',bytes=>output+=bytes);process.stderr.on('data',bytes=>errors+=bytes);
 const records=()=>output.split(/\r?\n/).filter(Boolean).flatMap(line=>{try{return [JSON.parse(line)];}catch{return [];}});
 const waitFor=async(predicate,label)=>until(()=>records(),predicate,label);
 return {process,records,waitFor,errors:()=>errors,output:()=>output};
}
const chat=(input,...args)=>chatResult(0,input,...args);
try {
  await mkdir(workspace);await writeFile(join(workspace,'README.md'),'Actual workspace content\n');
  const agentSource=join(folder,'agents.yaml');await writeFile(agentSource,'agents:\n  - id: reviewer.v1\n    name: Repository Reviewer\n    description: Reviews workspace changes with source evidence.\n    model_id: synthetic-protocol-model\n    instructions: Preserve the exact persisted reviewer guidance marker.\n    tools:\n      - read_file\n');
  const imported=spawnSync(adminExe,['--db',join(folder,'state.sqlite'),'--modules',modules,'--stdlib',stdlib,'import-agents',agentSource],{encoding:'utf8',windowsHide:true,timeout:15000});assert.equal(imported.status,0,imported.stderr);assert.equal(JSON.parse(imported.stdout).agents[0].id,'reviewer.v1');
  await mkdir(join(workspace,'.config'));await writeFile(join(workspace,'.config','providers.yaml'),'private provider configuration\n');
  await mkdir(join(workspace,'.agentflow'));await writeFile(join(workspace,'.agentflow','state.sqlite'),'private AgentFlow state\n');
  await writeFile(join(workspace,'large.txt'),Array.from({length:100000},(_,i)=>`line ${i+1} 中\r\n`).join(''));
  await mkdir(join(workspace,'glob-src','nested'),{recursive:true});await writeFile(join(workspace,'glob-src','main.cpp'),'');await writeFile(join(workspace,'glob-src','nested','header.hpp'),'');
  await mkdir(join(workspace,'ignore-http','.git'),{recursive:true});await writeFile(join(workspace,'ignore-http','.git','config'),'private Git configuration\n');await mkdir(join(workspace,'ignore-http','build'));await writeFile(join(workspace,'ignore-http','.gitignore'),'build/\n*.tmp\n!keep.tmp\n');
  for(const name of ['main.cpp','keep.tmp','drop.tmp','build/skip.cpp'])await writeFile(join(workspace,'ignore-http',name),'IgnoreHttpNeedle\n');
  await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
  await start(true);
  assert.equal(cli('health').agent_execution,true);
  const agentDetail={agent:{id:'reviewer.v1',name:'Repository Reviewer',description:'Reviews workspace changes with source evidence.',revision:1,model_id:'synthetic-protocol-model',instructions:'Preserve the exact persisted reviewer guidance marker.',tools:['read_file']}};
  assert.deepEqual((await api('/v1/agents/reviewer.v1')).data,agentDetail);assert.deepEqual(cli('agent-definition','reviewer.v1'),agentDetail);assert.deepEqual((await api('/v1/agents')).data.agents,[{id:'reviewer.v1',revision:1,model_id:'synthetic-protocol-model',name:'Repository Reviewer',description:'Reviews workspace changes with source evidence.'}]);
  assert.equal((await api('/v1/agents/missing-agent')).status,404);assert.equal((await api('/v1/agents/reviewer.v1?include=all')).status,400);
  const directory=await api('/v1/workspace/files?path=.');assert.equal(directory.status,200);assert.deepEqual(directory.data.path,'.');assert.equal(directory.data.workspace_id,(await api('/v1/workspace')).data.workspace_id);assert.equal(directory.data.truncated,false);const names=directory.data.entries.map(entry=>entry.name);assert.ok(names.includes('README.md'));assert.ok(!names.includes('.config'));assert.ok(!names.includes('.agentflow'));
  const fuzzyFiles=await api('/v1/workspace/find?query=MAIN&path=ignore-http&type=file');assert.equal(fuzzyFiles.status,200);assert.deepEqual(fuzzyFiles.data.entries,[{path:'ignore-http/main.cpp',type:'file'}]);const fuzzyDirectories=await api('/v1/workspace/find?query=ignore-http&type=directory');assert.deepEqual(fuzzyDirectories.data.entries,[{path:'ignore-http',type:'directory'}]);const ignoredFind=await api('/v1/workspace/find?query=skip&path=ignore-http');assert.deepEqual(ignoredFind.data.entries,[]);assert.equal((await api('/v1/workspace/find?query=main&type=other')).status,400);assert.equal((await api('/v1/workspace/find?query=main&hidden=yes')).status,400);assert.equal((await api('/v1/workspace/find?query=main&query=again')).status,400);
  const nestedGit=await api('/v1/workspace/files?path=ignore-http');assert.equal(nestedGit.status,200);assert.ok(!nestedGit.data.entries.some(entry=>entry.name==='.git'));
  const preview=await api('/v1/workspace/file?path=README.md');assert.equal(preview.status,200);assert.deepEqual(preview.data,{path:'README.md',workspace_id:directory.data.workspace_id,content:'Actual workspace content\n'});
  const viewOrigin='http://127.0.0.1:43127',viewSession=await api('/v1/view-sessions',{origin:viewOrigin});assert.equal(viewSession.status,200,'A same-machine browser view receives its private native session');const viewHeaders={Authorization:'View '+viewSession.data.credential,'X-XMind-View-Origin':viewOrigin};
  const viewDirectory=await fetch(`http://127.0.0.1:${port}/v1/workspace/files?path=.`,{headers:viewHeaders});assert.equal(viewDirectory.status,200,'The local browser session can read its selected workspace tree');const viewDirectoryData=await viewDirectory.json();assert.equal(viewDirectoryData.workspace_id,directory.data.workspace_id);assert.ok(viewDirectoryData.entries.some(entry=>entry.name==='README.md'));
  const viewFind=await fetch(`http://127.0.0.1:${port}/v1/workspace/find?query=README`,{headers:viewHeaders});assert.equal(viewFind.status,200,'The local browser session can find files in its workspace');assert.deepEqual((await viewFind.json()).entries,[{path:'README.md',type:'file'}]);
  const viewPreview=await fetch(`http://127.0.0.1:${port}/v1/workspace/file?path=README.md`,{headers:viewHeaders});assert.equal(viewPreview.status,200,'The local browser session can preview workspace files');assert.equal((await viewPreview.json()).content,'Actual workspace content\n');
  for(const path of ['ignore-http/.git/config','.config/providers.yaml','.agentflow/state.sqlite','../outside'])assert.equal((await api('/v1/workspace/file?path='+encodeURIComponent(path))).status,403,`Browser workspace preview must reject ${path}`);
  assert.equal((await api('/v1/workspace/files?path=.&path=.')).status,400,'Duplicate browser browsing paths are rejected');
  await session('coding');const selectedAgent=await api('/v1/sessions/coding/agent',{agent_id:'reviewer.v1',agent_revision:1,expected_revision:0});assert.deepEqual(selectedAgent.data,{session_id:'coding',revision:1,editable:true,selected:{id:'reviewer.v1',revision:1,model_id:'synthetic-protocol-model',name:'Repository Reviewer',description:'Reviews workspace changes with source evidence.'}});const agentArchive=await api('/v1/sessions/coding/export');assert.deepEqual(agentArchive.data.session.agent,{id:'reviewer.v1',name:'Repository Reviewer',description:'Reviews workspace changes with source evidence.',revision:1,model_id:'synthetic-protocol-model',instructions:'Preserve the exact persisted reviewer guidance marker.',tools:['read_file']});const oldAgentArchive={format:'xmind.session',version:2,session:{title:'Legacy agent archive',agent:{id:'old.reviewer',revision:1,model_id:'',instructions:'Keep the old snapshot readable.'}},messages:[]};const importedOldAgent=await api('/v1/sessions/import',{archive:oldAgentArchive});assert.equal(importedOldAgent.status,201);assert.deepEqual((await api(`/v1/sessions/${importedOldAgent.data.id}/export`)).data.session.agent,{id:'old.reviewer',name:'old.reviewer',description:'',revision:1,model_id:'',instructions:'Keep the old snapshot readable.'});assert.equal((await api('/v1/sessions/import',{archive:{...oldAgentArchive,session:{...oldAgentArchive.session,agent:{...oldAgentArchive.session.agent,unexpected:'field'}}}})).status,400,'Archived agent snapshots reject unknown fields');
  const run=cli('run','coding','Read the actual README.md');
  assert.equal(run.state,'queued');await terminal(run.id,'completed');
  const history=cli('history','coding');
  assert.deepEqual(history.map(message=>message.role),['user','assistant','tool','assistant']);
  assert.equal(JSON.parse(history[2].data.content).content,'Actual workspace content\n');
  const events=cli('events',run.id);
  assert.ok(events.some(event=>event.kind==='tool.completed'));
  assert.ok(events.some(event=>event.kind==='run.completed'));
  const last=events.at(-1).seq;
  assert.deepEqual(cli('events',run.id,String(last)),[]);
  await until(()=>api(`/v1/runs/${run.id}/cancel`,{}),result=>result.status===409,'completed run release');
  assert.equal((await api(`/v1/runs/${run.id}/transition`,{expected:'completed',next:'running'})).status,404);
  await session('paged');const pagedRun=cli('run','paged','Read the last two lines of the large native fixture');await terminal(pagedRun.id,'completed');
  const pagedHistory=cli('history','paged');assert.deepEqual(pagedHistory.map(message=>message.role),['user','assistant','tool','assistant']);
  assert.equal(JSON.parse(pagedHistory[2].data.content).content,'line 99999 中\r\nline 100000 中\r\n');
  assert.ok(cli('events',pagedRun.id).some(event=>event.kind==='tool.completed'));
  await session('discovery');const discovered=cli('run','discovery','Discover actual native source paths');await terminal(discovered.id,'completed');const discoveryHistory=cli('history','discovery');assert.deepEqual(JSON.parse(discoveryHistory.find(message=>message.role==='tool').data.content).paths,['glob-src/main.cpp','glob-src/nested/header.hpp']);
  const ignoreHistories=new Map();for(const [id,prompt] of [['ignored','Find nonignored owned fixture paths'],['filtered','Find filtered native content']]){await session(id);const result=cli('run',id,prompt);await terminal(result.id,'completed');const saved=cli('history',id);assert.deepEqual(saved.map(message=>message.role),['user','assistant','tool','assistant']);ignoreHistories.set(id,saved);}
  await session('regex');const regexRun=cli('run','regex','Search the large native fixture by regex');await terminal(regexRun.id,'completed');const regexHistory=cli('history','regex');assert.deepEqual(regexHistory.map(message=>message.role),['user','assistant','tool','assistant']);assert.equal(JSON.parse(regexHistory[2].data.content).matches.length,2);assert.equal(requestsByPrompt.get('Search the large native fixture by regex'),2);assert.ok(cli('events',regexRun.id).some(event=>event.kind==='tool.completed'));assert.deepEqual(cli('operations',regexRun.id),[]);
  const beforeChat=cli('sessions').length;assert.deepEqual(await chat('/exit\n'),[]);assert.equal(cli('sessions').length,beforeChat,'Leaving an empty chat must not create a session');
  const beforeInvalidRequests=requests,beforeInvalidRuns=cli('runs','coding').length;
  assert.deepEqual(await chatResult(1,'Read README\n/exit\n','coding','unavailable-fixture'),[],'An unavailable initial model must fail before emitting history or admitting work');
  assert.equal(cli('runs','coding').length,beforeInvalidRuns);assert.equal(requests,beforeInvalidRequests);assert.deepEqual(cli('history','coding'),history);
  assert.equal((await chat('/exit\n','coding','synthetic-protocol-model')).find(record=>record.type==='history').history.length,history.length,'A backend-enabled initial model preserves normal session inspection');
  const commandRequests=requests,commands=await chat('/help\n/models\n/model unavailable-fixture\n/model synthetic-protocol-model\n/history\n/model\n/unknown\n/exit\n');
  assert.equal(cli('sessions').length,beforeChat,'Read-only chat commands must not create conversations');assert.equal(requests,commandRequests,'Chat commands must not invoke inference');
  assert.equal(commands.filter(record=>record.type==='model').length,2,'Unavailable model must not change selection');
  assert.equal(commands.find(record=>record.type==='models').catalogue.models[0].id,'synthetic-protocol-model');assert.deepEqual(commands.find(record=>record.type==='history').history,[]);
  assert.equal(commands.filter(record=>record.type==='model')[1].model_id,'','Reset must follow the backend default');
  const chatRecords=await chat('Read README from the CLI\nRead README again in the same conversation\n/exit\n'),chatSession=chatRecords.find(record=>record.type==='session').session_id;
  assert.equal(chatRecords.filter(record=>record.type==='session').length,1);const chatRuns=chatRecords.filter(record=>record.type==='run').map(record=>record.run);assert.equal(chatRuns.length,2);assert.ok(chatRuns.every(item=>item.session_id===chatSession));assert.equal(chatRecords.filter(record=>record.type==='turn_finished'&&record.exit_status===0).length,2);assert.equal(cli('history',chatSession).length,8);assert.ok(chatRecords.some(record=>record.kind==='tool.completed'));assert.ok(chatRecords.some(record=>record.kind==='run.completed'));
  const resumedChat=await chat('Read README after reconnect\n/exit\n',chatSession);assert.equal(resumedChat.find(record=>record.type==='session').session_id,chatSession);assert.equal(cli('history',chatSession).length,12);assert.equal(cli('runs',chatSession).length,3,'Reconnect must add only the requested new turn, not replay completed work');
  assert.equal(resumedChat.find(record=>record.type==='history').history.length,8,'Resuming emits the actual pre-turn durable conversation');
  assert.deepEqual((await chat('/exit\n',chatSession)).find(record=>record.type==='history').history,cli('history',chatSession),'Viewing history without a request must not start work');
  const attachmentRequests=requests,attachmentRuns=cli('runs','coding');
  const attached=await chat('/runs\n/watch '+run.id+'\n/runs\n/exit\n');
  const listedRuns=attached.filter(record=>record.type==='runs');assert.deepEqual(listedRuns[0],{type:'runs',session_id:'',runs:[]});assert.equal(listedRuns[1].session_id,'coding');assert.deepEqual(listedRuns[1].runs,attachmentRuns);
  assert.equal(attached.find(record=>record.type==='run_attached').run.id,run.id);assert.ok(!attached.some(record=>record.type==='run'));assert.equal(attached.find(record=>record.type==='turn_finished').exit_status,0);assert.deepEqual(attached.find(record=>record.type==='history').history,history);
  const rejectedAttachments=await chat('/watch ../invalid\n/watch absent-fixture\n/watch '+run.id+'\n/history\n/exit\n',chatSession);
  assert.ok(!rejectedAttachments.some(record=>record.type==='run_attached'||record.type==='turn_finished'));assert.ok(rejectedAttachments.every(record=>record.type!=='session'||record.session_id===chatSession));assert.deepEqual(cli('runs','coding'),attachmentRuns);assert.equal(requests,attachmentRequests,'Attaching completed runs and rejecting foreign/absent runs cannot invoke inference');
  const failureChat=await chatResult(1,'provider-error\n/exit\n');
  assert.equal(failureChat.filter(record=>record.type==='turn_finished'&&record.exit_status===1).length,1);
  assert.ok(failureChat.some(record=>record.kind==='run.failed'&&record.data.status===429));
  assert.ok(!JSON.stringify(failureChat).includes('private-provider-error-body'));
  const recovered=await chat('provider-error\nRead README after the failed turn\n/exit\n');
  assert.deepEqual(recovered.filter(record=>record.type==='turn_finished').map(record=>record.exit_status),[1,0],'A later explicit request may proceed after a failed turn; there is no automatic retry');
  assert.equal(recovered.filter(record=>record.type==='run').length,2);
  const literal=await chat('//literal fixture request\n/exit\n'),literalSession=literal.find(record=>record.type==='session').session_id;assert.equal(cli('history',literalSession)[0].data.content,'/literal fixture request','Escaped slash requests must persist literally');

  for(const id of ['held','queued','overflow']) await session(id);
  await session('active-control');
  const activeControl=liveChat('active-control');activeControl.process.stdin.write('hold-stream\n');
  const activeRunRecords=await activeControl.waitFor(records=>records.some(record=>record.type==='run'),'CLI active-run admission');
  const activeRunRecord=activeRunRecords.find(record=>record.type==='run');
  const activeRun=activeRunRecord.run;assert.equal(activeRun.session_id,'active-control');await streaming(activeRun.id);
  activeControl.process.stdin.write('Read README after the active turn\n');
  const queuedPromptRecords=await activeControl.waitFor(records=>records.some(record=>record.type==='prompt_queued'),'CLI durable prompt queue');
  const queuedPromptRecord=queuedPromptRecords.find(record=>record.type==='prompt_queued');assert.equal(queuedPromptRecord.item.content,'Read README after the active turn');assert.equal(queuedPromptRecord.item.state,'queued');
  assert.equal((await api('/v1/sessions/active-control/inbox')).data.items[0].content,'Read README after the active turn');
  activeControl.process.stdin.write('/cancel\n');
  try{await terminal(activeRun.id,'cancelled');}catch(error){throw new Error(error.message+'\n'+activeControl.errors()+'\n'+activeControl.output());}
  const cancelledRecords=await activeControl.waitFor(records=>records.some(record=>record.type==='turn_finished'),'CLI active cancellation result');
  const cancelledTurn=cancelledRecords.find(record=>record.type==='turn_finished');
  assert.equal(cancelledTurn.type,'turn_finished');assert.equal(cancelledTurn.exit_status,2);
  assert.ok(activeControl.records().some(record=>record.type==='run_cancel_result'),'The interactive CLI must send cancellation while it is observing a live turn');
  const queuedRunRecords=await activeControl.waitFor(records=>records.some(record=>record.type==='queued_run'),'CLI automatic queued-prompt admission');
  const queuedRun=queuedRunRecords.find(record=>record.type==='queued_run').run;assert.equal(queuedRun.session_id,'active-control');await terminal(queuedRun.id,'completed');
  const queuedFinishRecords=await activeControl.waitFor(records=>records.filter(record=>record.type==='turn_finished').length===2,'CLI queued turn completion');
  assert.equal(queuedFinishRecords.filter(record=>record.type==='turn_finished')[1].exit_status,0);
  const inboxAfterDispatch=(await api('/v1/sessions/active-control/inbox')).data.items;assert.equal(inboxAfterDispatch[0].state,'claimed');assert.equal(inboxAfterDispatch[0].run_id,queuedRun.id);
  const activeHistory=cli('history','active-control');assert.equal(activeHistory.filter(message=>message.role==='user'&&message.data.content==='Read README after the active turn').length,1,'The queued prompt must be saved exactly once in session history');
  activeControl.process.stdin.write('/exit\n');
  const activeControlExit=await new Promise((yes,no)=>{const timer=setTimeout(()=>activeControl.process.kill(),10000);activeControl.process.once('error',no);activeControl.process.once('close',(code,signal)=>{clearTimeout(timer);yes({code,signal});});});
  assert.equal(activeControlExit.signal,null);assert.equal(activeControlExit.code,0,activeControl.errors());assert.ok(!activeControl.output().includes(key));
  await session('active-detach');
  const activeDetach=liveChat('active-detach');activeDetach.process.stdin.write('hold-stream\n');
  const detachedRunRecords=await activeDetach.waitFor(records=>records.some(record=>record.type==='run'),'CLI detachable active-run admission');
  const detachedRunRecord=detachedRunRecords.find(record=>record.type==='run');
  const detachedRun=detachedRunRecord.run;await streaming(detachedRun.id);activeDetach.process.stdin.write('/exit\n');
  const detachedExit=await new Promise((yes,no)=>{const timer=setTimeout(()=>activeDetach.process.kill(),10000);activeDetach.process.once('error',no);activeDetach.process.once('close',(code,signal)=>{clearTimeout(timer);yes({code,signal});});});
  assert.equal(detachedExit.signal,null);assert.equal(detachedExit.code,0,activeDetach.errors());
  assert.equal((await api(`/v1/runs/${detachedRun.id}`)).data.state,'running','/exit must detach without cancelling the server-owned run');
  cli('cancel',detachedRun.id);await terminal(detachedRun.id,'cancelled');
  assert.equal((await api('/v1/runs',{id:'held-run',session_id:'held',prompt:'hold-stream'})).status,202);
  await streaming('held-run');
  assert.equal((await api('/v1/runs',{id:'duplicate-session',session_id:'held',prompt:'must not persist'})).status,409);
  assert.equal((await api('/v1/sessions/held/messages',{role:'user',data:{content:'must not interleave'}})).status,409);
  assert.equal(cli('history','held').length,1);
  assert.equal((await api('/v1/runs',{id:'queued-run',session_id:'queued',prompt:'must not execute'})).status,202);
  assert.equal((await api('/v1/runs',{id:'overflow-run',session_id:'overflow',prompt:'must not persist'})).status,503);
  assert.equal((await api('/v1/runs/overflow-run')).status,404);
  assert.deepEqual(cli('history','overflow'),[]);
  cli('cancel','queued-run');await terminal('queued-run','cancelled');
  // Queued cancellation frees bounded admission capacity before the worker exits.
  assert.equal((await api('/v1/runs',{id:'replacement-run',session_id:'overflow',prompt:'Read README again'})).status,202);
  cli('cancel','held-run');await terminal('held-run','cancelled');
  await session('durable-inbox');
  await session('cancel-inbox');const cancellableInput=await api('/v1/sessions/cancel-inbox/inbox',{id:'cancel-before-run',content:'Remove this queued prompt'});assert.equal(cancellableInput.status,202);
  assert.equal((await api('/v1/sessions/cancel-inbox/inbox/cancel-before-run',undefined,'DELETE')).status,204);
  assert.deepEqual((await api('/v1/sessions/cancel-inbox/inbox')).data.items,[],'Cancellation must remove an unclaimed inbox row');
  assert.equal((await api('/v1/sessions/cancel-inbox/inbox/cancel-before-run',undefined,'DELETE')).status,204,'Cancelling the same queued item twice is idempotent');
  assert.equal((await api('/v1/sessions/cancel-inbox/inbox',{id:'cancel-before-run',content:'Remove this queued prompt'})).status,409,'Cancelled input identity cannot be reused');
  assert.equal((await api('/v1/sessions/cancel-inbox/inbox/missing-input',undefined,'DELETE')).status,404,'Unknown input identity remains not found');
  assert.equal((await api('/v1/runs',{id:'inbox-active-run',session_id:'durable-inbox',prompt:'hold-stream'})).status,202);await streaming('inbox-active-run');
  const queuedInput=await api('/v1/sessions/durable-inbox/inbox',{id:'durable-input-1',content:'Run this after the active turn'});assert.equal(queuedInput.status,202);assert.equal(queuedInput.data.state,'queued');
  assert.deepEqual((await api('/v1/sessions/durable-inbox/inbox')).data.items.map(item=>({id:item.id,state:item.state,run_id:item.run_id})),[{id:'durable-input-1',state:'queued',run_id:undefined}]);
  assert.equal((await api('/v1/runs',{id:'inbox-too-early',session_id:'durable-inbox',prompt:queuedInput.data.content,inbox_id:queuedInput.data.id})).status,409,'A queued input cannot violate per-session single-root admission');
  assert.equal((await api('/v1/sessions/durable-inbox/inbox')).data.items[0].state,'queued','Failed admission must leave the durable item available');
  cli('cancel','inbox-active-run');await terminal('inbox-active-run','cancelled');
  const inboxRun=await api('/v1/runs',{id:'inbox-follow-up-run',session_id:'durable-inbox',prompt:queuedInput.data.content,inbox_id:queuedInput.data.id});assert.equal(inboxRun.status,202,JSON.stringify(inboxRun.data));await terminal(inboxRun.data.id,'completed');
  const claimedInput=(await api('/v1/sessions/durable-inbox/inbox')).data.items.find(item=>item.id==='durable-input-1');assert.equal(claimedInput.state,'claimed');assert.equal(claimedInput.run_id,inboxRun.data.id);
  assert.equal((await api('/v1/sessions/durable-inbox/inbox/durable-input-1',undefined,'DELETE')).status,204,'Cancelling a claimed item is idempotent and must preserve its run link');
  assert.equal((await api('/v1/sessions/durable-inbox/inbox')).data.items[0].run_id,inboxRun.data.id);
  assert.equal(cli('history','durable-inbox').filter(message=>message.role==='user'&&message.data.content==='Run this after the active turn').length,1,'Claimed input must enter conversation history exactly once');
  await session('http-interrupt');
  assert.equal((await api('/v1/runs',{id:'http-interrupt-run',session_id:'http-interrupt',prompt:'hold-stream'})).status,202);
  await streaming('http-interrupt-run');
  const viewInterrupt=await fetch(`http://127.0.0.1:${port}/v1/sessions/http-interrupt/interrupt?resume=false`,{method:'POST',headers:{...viewHeaders,'Content-Type':'application/json'},body:'{}',signal:AbortSignal.timeout(10000)});
  assert.equal(viewInterrupt.status,200,'An origin-bound local view may interrupt its active session');assert.deepEqual(await viewInterrupt.json(),{interrupted:true});
  await terminal('http-interrupt-run','cancelled');
  assert.deepEqual((await api('/v1/sessions/http-interrupt/interrupt',{})),{status:200,data:{interrupted:false}},'Idle interruption is an idempotent no-op');
  assert.equal((await api('/v1/sessions/absent-session/interrupt',{})).status,404);
  assert.equal((await api('/v1/sessions/http-interrupt/interrupt?resume=true',{})).status,400,'Unsupported resume must fail explicitly');
  assert.deepEqual(cli('session-interrupt','http-interrupt'),{interrupted:false},'CLI exposes the session interrupt contract');
  const cancelledAttachmentRequests=requestsByPrompt.get('hold-stream'),cancelledAttachmentRuns=cli('runs','held');
  const cancelledAttachment=await chatResult(2,'/watch held-run\n/exit\n');
  assert.equal(cancelledAttachment.find(record=>record.type==='run_attached').run.id,'held-run');assert.equal(cancelledAttachment.find(record=>record.type==='turn_finished').exit_status,2);assert.equal(requestsByPrompt.get('hold-stream'),cancelledAttachmentRequests,'Unrelated queued work may execute while the cancelled task is inspected');assert.ok(!cancelledAttachment.some(record=>record.type==='run'));assert.deepEqual(cli('runs','held'),cancelledAttachmentRuns,'Viewing cancellation cannot restart the run');
  await terminal('replacement-run','completed');

  await session('error');
  assert.equal((await api('/v1/runs',{id:'error-run',session_id:'error',prompt:'provider-error'})).status,202);
  await terminal('error-run','failed');
  const failed=cli('events','error-run');
  assert.ok(failed.some(event=>event.kind==='run.failed' && event.data.status===429));
  const terminalFailure=failed.find(event=>event.kind==='run.failed').data;assert.equal(terminalFailure.provider_error_type,'rate_limit_error');assert.equal(terminalFailure.provider_error_code,'rate_limit_exceeded');assert.ok(!Object.hasOwn(terminalFailure,'provider_error_param'));
  assert.ok(!JSON.stringify(failed).includes('private-provider-error-body'));
  assert.equal(cli('history','error').length,1);

  await stop();await start(); // Reuse encrypted stored credential without env key.
  assert.deepEqual(cli('history','coding'),history);
  assert.deepEqual(cli('history','paged'),pagedHistory,'Exact native range results must survive embedded xlang3/SQLite restart');
  assert.deepEqual(cli('history','discovery'),discoveryHistory,'Native discovery results must survive embedded xlang3/SQLite restart');
  for(const [id,saved] of ignoreHistories)assert.deepEqual(cli('history',id),saved,'Native ignore-filtered results must persist exactly without replay');
  assert.deepEqual(cli('history','regex'),regexHistory,'Exact native regex results must survive embedded xlang3/SQLite restart');assert.equal(requestsByPrompt.get('Search the large native fixture by regex'),2,'Restart cannot replay the recorded search');
  assert.equal(cli('history',chatSession).length,12,'Interactive CLI history must survive native restart');
  // Use the actual editor host client against the same native server; no IDE
  // rendering or VS Code SecretStorage behavior is claimed by this wire test.
  const editorClient=new BackendClient(`http://127.0.0.1:${port}`,()=>token);
  assert.equal((await editorClient.health()).agent_execution,true);
  const reused=await editorClient.run('coding','Read the persisted workspace again');
  await terminal(reused.id,'completed');
  assert.equal((await editorClient.status(reused.id)).state,'completed');
  assert.equal((await editorClient.history('coding')).length,8);
  await session('crash');
  const crashed=cli('run','crash','hold-stream');await streaming(crashed.id);
  await stop();await start();
  assert.equal(cli('status',crashed.id).state,'failed');
  assert.equal(cli('history','crash').length,1);
  assert.equal(cli('health').status,'ok');
  if(peerError) throw peerError;
  console.log('Native agent HTTP/CLI contract passed: actual read/tool loop, interactive shared-session turns/reconnect/restart without replay, durable prompt queue and automatic dispatch, bounded admission, cancellation, encrypted credential reuse, provider failure and crash recovery. Inference peer is synthetic.');
} finally {
  await stop();peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));
  await rm(folder,{recursive:true,force:true});
}
