// Actual compiled CLI -> real native HttpServer/profile runtime/xlang3 SQLite.
// Provider sockets, names, keys and responses below are bounded synthetic fixtures.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createInterface} from 'node:readline';
import {mkdtemp,readdir,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,basename,resolve} from 'node:path';

const [executable,modules,stdlib,cli]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib&&cli,'Pass native fixture, xlang3 roots and actual compiled CLI');
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-provider-profile-cli-'));
const owner='synthetic-profile-cli-owner-token-not-live';
const keys={openai:'synthetic-profile-cli-openai-key-not-live',claude:'synthetic-profile-cli-claude-key-not-live',gemini:'synthetic-profile-cli-gemini-key-not-live'};
const rejectedKey='synthetic-profile-cli-rejected-key-not-live',privateProviderBody='synthetic private provider message must never reach the CLI';
const environment={...process.env,XMIND_AUTH_TOKEN:owner,MISSING_FIXTURE_KEY:''};
const model='models/fixture-gemini',firstAnswer='Synthetic selected Gemini CLI response.',secondAnswer='Synthetic Gemini CLI signed replay after reopen.',recoveredAnswer='Synthetic Gemini CLI recovered actual turn.';
const precise='{"precision":1.2345678901234567890123456789,"large":18446744073709551615}';
const firstParts='[{"text":"Synthetic hidden CLI thought.","thoughtSignature":"Y2xpLWhpZGRlbi1zaWduYXR1cmU=","thought":true},{"text":"'+firstAnswer+'","thoughtSignature":"Y2xpLWFuc3dlci1zaWduYXR1cmU=","partMetadata":'+precise+'},{"thoughtSignature":"Y2xpLXNpZ25hdHVyZS1vbmx5","thought":false}]';
const secondParts='[{"text":"'+secondAnswer+'","thoughtSignature":"Y2xpLXJlb3BlbmVkLXNpZ25hdHVyZQ=="}]';
const geminiCursor='opaque/CLI+?=&雪',claudeCursor='fixture/claude-cursor_2';
const catalogues=[],modelRequests=[],audit=[];let backend,backendPort,failure,race=false,raceChanged=false;
const routeIds=['openai.chat','anthropic.messages','gemini.generate-content'];
const listed={
 openai:{models:[{id:'fixture-openai'},{id:'fixture-openai-other'}]},
 claude:{models:[{id:'fixture-claude'},{id:claudeCursor}]},
 gemini:{models:[{id:model},{id:'models/fixture-gemini-other'}]}
};
function privateAbsent(source){
 for(const secret of [...Object.values(keys),rejectedKey,owner,privateProviderBody])assert.equal(source.includes(secret),false,'CLI/public output cannot disclose fixture credentials or private provider messages');
}
function publicMetadata(value){
 const source=JSON.stringify(value);privateAbsent(source);
 for(const field of ['api_key','credential_id','credential_scope','credential_purpose','endpoint'])assert.equal(source.includes('"'+field+'"'),false,'Public metadata cannot reveal credentials or destinations');
}
function safe(error){
 let message=String(error?.stack||error);
 for(const secret of [...Object.values(keys),rejectedKey,owner,privateProviderBody])message=message.split(secret).join('[redacted fixture credential]');
 return new Error(message);
}
async function requestBody(request){
 const chunks=[];let bytes=0;
 for await(const chunk of request){bytes+=chunk.length;assert.ok(bytes<=512*1024,'Synthetic fixture HTTP bodies must remain bounded');chunks.push(chunk);}
 return Buffer.concat(chunks).toString('utf8');
}
function json(response,value){response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify(value));}
function geminiReply(response,parts,usage,id){
 const wire=Buffer.from('data: {"candidates":[{"index":0,"content":{"role":"model","parts":'+parts+'},"finishReason":"STOP"}],"usageMetadata":'+JSON.stringify(usage)+',"responseId":"'+id+'","modelVersion":"fixture-gemini"}\n\n');
 response.writeHead(200,{'Content-Type':'text/event-stream'});
 for(let offset=0;offset<wire.length;offset+=13)response.write(wire.subarray(offset,offset+13));
 response.end();
}
const peer=createServer(async(request,response)=>{
 try{
  const source=await requestBody(request),url=new URL(request.url,'http://127.0.0.1');
  for(const secret of Object.values(keys))assert.equal((request.url+source).includes(secret),false,'Provider authentication must remain header-only');
  if(request.method==='GET'&&url.pathname==='/openai/models'){
   assert.equal(request.url,'/openai/models');assert.equal(source,'');assert.equal(request.headers.authorization,'Bearer '+keys.openai);
   assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['x-goog-api-key'],undefined);
   catalogues.push('openai');
   json(response,{object:'list',data:[{object:'model',id:'fixture-openai-other'},{object:'model',id:'fixture-openai'}]});return;
  }
  if(request.method==='GET'&&url.pathname==='/claude/models'){
   assert.equal(source,'');assert.equal(request.headers['x-api-key'],keys.claude);assert.equal(request.headers['anthropic-version'],'2023-06-01');
   assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-goog-api-key'],undefined);
   const after=url.searchParams.get('after_id');assert.ok(after===null||after===claudeCursor);
   assert.equal(request.url,'/claude/models?limit=1000'+(after===null?'':'&after_id='+encodeURIComponent(claudeCursor)));
   catalogues.push(after===null?'claude-first':'claude-next');
   json(response,after===null?{data:[{type:'model',id:claudeCursor}],has_more:true,last_id:claudeCursor}:{data:[{type:'model',id:'fixture-claude'}],has_more:false,last_id:'fixture-claude'});return;
  }
  if(request.method==='GET'&&url.pathname==='/v1beta/models'){
   if(request.headers['x-goog-api-key']===rejectedKey){
    assert.equal(request.url,'/v1beta/models?pageSize=1000');assert.equal(source,'');catalogues.push('gemini-denied');
    response.writeHead(401,{'Content-Type':'application/json'});response.end(JSON.stringify({error:{type:'authentication_error',code:'invalid_api_key',message:privateProviderBody+' '+rejectedKey,untrusted:keys.gemini}}));return;
   }
   assert.equal(source,'');assert.equal(request.headers['x-goog-api-key'],keys.gemini);
   assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['anthropic-version'],undefined);
   const token=url.searchParams.get('pageToken');assert.ok(token===null||token===geminiCursor);
   assert.equal(request.url,'/v1beta/models?pageSize=1000'+(token===null?'':'&pageToken='+encodeURIComponent(geminiCursor)));
   catalogues.push(token===null?'gemini-first':'gemini-next');
   json(response,token===null?{models:[{name:'models/fixture-gemini-other',supportedGenerationMethods:['generateContent']},{name:'models/fixture-embedding',supportedGenerationMethods:['embedContent']}],nextPageToken:geminiCursor}:{models:[{name:model,supportedGenerationMethods:['countTokens','generateContent'],displayName:'Do not use this display label as identity',baseModelId:'different-base'}]});return;
  }
  assert.equal(request.method,'POST');assert.equal(request.url,'/v1beta/models/fixture-gemini:streamGenerateContent?alt=sse');
  assert.equal(request.headers['x-goog-api-key'],keys.gemini);assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],undefined);
  assert.equal(request.headers.accept,'text/event-stream');assert.equal(request.headers['content-type'],'application/json');
  assert.ok(!source.includes('provider_context')&&!source.includes('provider_items')&&!source.includes('credential_id'));
  const body=JSON.parse(source);assert.deepEqual(body.generationConfig,{maxOutputTokens:64});assert.equal(Object.hasOwn(body,'tools'),false,'Unknown-tools Gemini profile must execute text-only without declarations');
  assert.deepEqual(body.systemInstruction,{parts:[{text:'Synthetic native CLI profile contract.'}]});
  const prompt=body.contents.at(-1).parts[0].text;modelRequests.push(prompt);
  if(prompt==='Selected Gemini CLI request'){
   assert.deepEqual(body.contents,[{role:'user',parts:[{text:prompt}]}]);
   geminiReply(response,firstParts,{promptTokenCount:7,candidatesTokenCount:3,totalTokenCount:83,cachedContentTokenCount:2,thoughtsTokenCount:5},'synthetic-cli-first');return;
  }
  assert.equal(body.contents[0].parts[0].text,'Selected Gemini CLI request');
  assert.deepEqual(body.contents[1],{role:'model',parts:JSON.parse(firstParts)});
  assert.ok(source.includes('"partMetadata":'+precise),'Signed metadata numeric tokens must survive actual SQLite reopen without rounding');
  if(prompt==='Continue signed CLI after reopen'){
   assert.equal(body.contents.length,3);geminiReply(response,secondParts,{promptTokenCount:13,candidatesTokenCount:4},'synthetic-cli-reopened');return;
  }
  assert.equal(body.contents[2].parts[0].text,'Continue signed CLI after reopen');assert.deepEqual(body.contents[3],{role:'model',parts:JSON.parse(secondParts)});
  if(prompt==='Synthetic blocked CLI turn'||prompt==='Synthetic blocked CLI recovery probe'){
   assert.equal(body.contents.length,prompt==='Synthetic blocked CLI turn'?5:6);
   response.writeHead(200,{'Content-Type':'text/event-stream'});response.end('data: {"candidates":[{"index":0,"finishReason":"SAFETY"}]}\n\n');return;
  }
  assert.equal(prompt,'Synthetic CLI recovered turn');assert.equal(body.contents.length,7);
  assert.equal(body.contents[4].parts[0].text,'Synthetic blocked CLI turn');assert.equal(body.contents[5].parts[0].text,'Synthetic blocked CLI recovery probe');
  geminiReply(response,'[{"text":"'+recoveredAnswer+'"}]',{promptTokenCount:21,candidatesTokenCount:5},'synthetic-cli-recovered');
 }catch(error){failure=error;response.destroy();}
});
async function backendFetch(path,body){
 const response=await fetch('http://127.0.0.1:'+backendPort+path,{method:body?'POST':'GET',headers:{Authorization:'Bearer '+owner,...(body?{'Content-Type':'application/json'}:{})},...(body?{body:JSON.stringify(body)}:{})});
 assert.equal(response.status,200,'Independent setup assertion must use the real native API');const value=await response.json();publicMetadata(value);return value;
}
const proxy=createServer(async(request,response)=>{
 try{
  const source=await requestBody(request);const body=source?JSON.parse(source):undefined;
  const record={method:request.method,path:request.url,body};audit.push(record);
  if(race&&request.method==='POST'&&request.url==='/v1/provider/profiles/models'&&!raceChanged){
   raceChanged=true;const selected=await backendFetch('/v1/provider/profiles/select',{id:'claude',expected_revision:6});assert.equal(selected.revision,7);assert.equal(selected.active,'claude');
  }
  const upstream=await fetch('http://127.0.0.1:'+backendPort+request.url,{method:request.method,headers:{Authorization:request.headers.authorization||'',...(source?{'Content-Type':'application/json'}:{})},...(source?{body:source}:{})});
  record.status=upstream.status;response.writeHead(upstream.status,{'Content-Type':'application/json'});response.end(await upstream.text());
 }catch(error){failure=error;if(response.headersSent)response.destroy();else{response.writeHead(500,{'Content-Type':'application/json'});response.end('{}');}}
});
function waitMessage(state,type){
 const index=state.messages.findIndex(value=>value.type===type);
 if(index>=0)return Promise.resolve(state.messages.splice(index,1)[0]);
 return new Promise((resolve,reject)=>{
  const waiter={type,resolve,reject,timer:setTimeout(()=>{state.waiters=state.waiters.filter(value=>value!==waiter);reject(new Error('Native fixture acknowledgement timed out: '+type));},9000)};
  state.waiters.push(waiter);
  if(state.closed){clearTimeout(waiter.timer);state.waiters=state.waiters.filter(value=>value!==waiter);reject(new Error('Native fixture exited before '+type+': '+state.stderr));}
 });
}
async function start(){
 const child=spawn(executable,[root,modules,stdlib,'http://127.0.0.1:'+peer.address().port],{windowsHide:true,stdio:['pipe','pipe','pipe']});
 const state={child,messages:[],waiters:[],stderr:'',closed:false};backend=state;
 state.exit=new Promise(resolve=>child.once('close',(code,signal)=>{state.closed=true;for(const waiter of state.waiters){clearTimeout(waiter.timer);waiter.reject(new Error('Native fixture exited before '+waiter.type+': '+state.stderr));}state.waiters=[];resolve({code,signal});}));
 child.once('error',error=>{failure=error;});child.stderr.on('data',chunk=>state.stderr+=chunk);
 createInterface({input:child.stdout}).on('line',line=>{
  try{
   const value=JSON.parse(line),index=state.waiters.findIndex(waiter=>waiter.type===value.type);
   if(index>=0){const waiter=state.waiters.splice(index,1)[0];clearTimeout(waiter.timer);waiter.resolve(value);}else state.messages.push(value);
  }catch(error){failure=error;}
 });
 const ready=await waitMessage(state,'fixture_ready');assert.ok(ready.port>0);backendPort=ready.port;return ready;
}
async function control(command,type='fixture_checked'){
 backend.child.stdin.write(JSON.stringify(command)+'\n');const result=await waitMessage(backend,type);if(failure)throw failure;return result;
}
async function stop(expected){
 if(!backend)return;
 const state=backend;
 if(!state.closed){
  if(expected)await control({command:'stop',...expected});else state.child.stdin.write('{"command":"abort"}\n');
  const timer=setTimeout(()=>state.child.kill(),9000);const result=await state.exit;clearTimeout(timer);
  assert.equal(result.code,0,state.stderr);privateAbsent(state.stderr);
 }
 backend=undefined;
}
async function invoke(args,extra={},status=0){
 let result;
 try{result=await execute(cli,[String(proxy.address().port),...args],{env:{...environment,...extra},windowsHide:true,timeout:12000,maxBuffer:2*1024*1024});result.code=0;}
 catch(error){if(typeof error.code!=='number')throw error;result={stdout:error.stdout,stderr:error.stderr,code:error.code};}
 privateAbsent(result.stdout+result.stderr);assert.equal(result.code,status,result.stderr);if(failure)throw failure;
 return status===0?JSON.parse(result.stdout):result;
}
async function chat(session,input,{modelOverride,exit=0}={}){
 const args=[String(proxy.address().port),'chat',session,...(modelOverride===undefined?[]:[modelOverride])];
 const running=execute(cli,args,{env:environment,windowsHide:true,timeout:16000,maxBuffer:2*1024*1024});running.child.stdin.end(input);
 let result;try{result=await running;result.code=0;}catch(error){if(typeof error.code!=='number')throw error;result={stdout:error.stdout,stderr:error.stderr,code:error.code};}
 privateAbsent(result.stdout+result.stderr);assert.equal(result.code,exit,result.stderr);if(failure)throw failure;
 return {records:result.stdout.split(/\r?\n/).filter(Boolean).map(line=>JSON.parse(line)),stderr:result.stderr};
}
async function rejectBeforeHttp(args,extra={}){
 const before=audit.length;await invoke(args,extra,1);assert.equal(audit.length,before,'Malformed CLI arguments or reserved key input must fail before native HTTP');
}
function receipt(history,index=1){
 const data=history[index].data;assert.equal(data.content,index===1?firstAnswer:secondAnswer);assert.equal(data.provider_items[0].type,'gemini_content');assert.equal(data.provider_items[0].parts_json,index===1?firstParts:secondParts);
 assert.deepEqual(data.provider_context,{profile_id:'gemini',profile_revision:3,route_id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',model_id:model});
 assert.equal(data.elapsed_ms>=0,true);assert.equal(data.first_token_ms>=0,true);assert.equal(Object.hasOwn(data,'tool_calls'),false);return data;
}
try{
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));
 const ready=await start();assert.equal(ready.reopened,false);assert.equal(ready.revision,0);
 const initial=await invoke(['provider-profiles']);publicMetadata(initial);assert.equal(initial.revision,0);assert.equal(initial.active,'');assert.deepEqual(initial.profiles,[]);assert.deepEqual(initial.routes.map(value=>value.id),routeIds);
 await invoke(['provider-profiles'],{XMIND_AUTH_TOKEN:'synthetic-unauthorized-token'},1);
 for(const args of [
  ['provider-profiles','extra'],['profile-models','gemini','gemini.generate-content'],['select-profile','gemini','0','extra'],
  ['save-profile','gemini','gemini.generate-content',model,'0','--activate','FIXTURE_GEMINI_KEY'],
  ['save-profile','gemini','gemini.generate-content',model,'0','FIXTURE_GEMINI_KEY','--unknown'],
  ['profile-models','bad id','gemini.generate-content','0'],['select-profile','gemini','bad-revision'],
  ['profile-models','gemini','gemini.generate-content','0',''],['save-profile','gemini','gemini.generate-content',model,'0',''],
  ['profile-models','gemini','gemini.generate-content','0','MISSING_FIXTURE_KEY'],
 ])await rejectBeforeHttp(args);
 for(const revision of ['-1','+1','1x','1.0','9007199254740992'])await rejectBeforeHttp(['profile-models','gemini','gemini.generate-content',revision,'FIXTURE_GEMINI_KEY'],{FIXTURE_GEMINI_KEY:keys.gemini});
 for(const variable of ['XMIND_AUTH_TOKEN','xmind_auth_token','XMIND_UI_BOOTSTRAP_TOKEN','xmind_ui_fixture','9INVALID_KEY','BAD-KEY']){
  await rejectBeforeHttp(['profile-models','gemini','gemini.generate-content','0',variable],{[variable]:keys.gemini});
  await rejectBeforeHttp(['save-profile','gemini','gemini.generate-content',model,'0',variable,'--activate'],{[variable]:keys.gemini});
 }
 await invoke(['profile-models','gemini','gemini.generate-content','0'],{},1);
 for(const [id,route,variable] of [['openai','openai.chat','FIXTURE_OPENAI_KEY'],['claude','anthropic.messages','FIXTURE_CLAUDE_KEY'],['gemini','gemini.generate-content','FIXTURE_GEMINI_KEY']]){
  const reflectedId='reflected-'+keys[id]+'-suffix',reflectedModel=(id==='gemini'?'models/':'')+'reflected-'+keys[id]+'-suffix';
  const reflectedDraft=await invoke(['profile-models',reflectedId,route,'0',variable],{[variable]:keys[id]},1);assert.match(reflectedDraft.stderr,/HTTP 400/);
  const reflectedEnrollment=await invoke(['save-profile','reflected-'+id,route,reflectedModel,'0',variable],{[variable]:keys[id]},1);assert.match(reflectedEnrollment.stderr,/HTTP 400/);
 }
 for(const [id,route,variable] of [['openai','openai.chat','FIXTURE_OPENAI_KEY'],['claude','anthropic.messages','FIXTURE_CLAUDE_KEY'],['gemini','gemini.generate-content','FIXTURE_GEMINI_KEY']]){
  const found=await invoke(['profile-models',id,route,'0',variable],{[variable]:keys[id]});assert.deepEqual(found,listed[id]);publicMetadata(found);
 }
 await control({command:'check',revision:0,credentials:0,sessions:0});assert.deepEqual(await invoke(['sessions']),[]);
 let configured=await invoke(['save-profile','openai','openai.chat','fixture-openai','0','FIXTURE_OPENAI_KEY','--activate'],{FIXTURE_OPENAI_KEY:keys.openai});publicMetadata(configured);assert.equal(configured.revision,1);assert.equal(configured.active,'openai');
 assert.deepEqual(await invoke(['provider-models']),listed.openai,'Ergonomic discovery must use the active saved OpenAI profile');
 configured=await invoke(['save-profile','claude','anthropic.messages','fixture-claude','1','FIXTURE_CLAUDE_KEY'],{FIXTURE_CLAUDE_KEY:keys.claude});assert.equal(configured.revision,2);assert.equal(configured.active,'openai');publicMetadata(configured);
 configured=await invoke(['save-profile','gemini','gemini.generate-content',model,'2','FIXTURE_GEMINI_KEY'],{FIXTURE_GEMINI_KEY:keys.gemini});assert.equal(configured.revision,3);assert.equal(configured.active,'openai');publicMetadata(configured);
 // Omitted key input must resolve the saved owned key, while each native save
 // intentionally creates a separately encrypted candidate and profile version.
 configured=await invoke(['save-profile','gemini','gemini.generate-content','models/fixture-gemini-other','3']);assert.equal(configured.revision,4);assert.equal(configured.active,'openai');assert.equal(configured.profiles.find(value=>value.id==='gemini').revision,2);publicMetadata(configured);
 configured=await invoke(['save-profile','gemini','gemini.generate-content',model,'4']);assert.equal(configured.revision,5);assert.equal(configured.active,'openai');assert.equal(configured.profiles.find(value=>value.id==='gemini').revision,3);publicMetadata(configured);
 for(const [id,route] of [['openai','openai.chat'],['claude','anthropic.messages'],['gemini','gemini.generate-content']]){
  assert.deepEqual(await invoke(['profile-models',id,route,'5']),listed[id]);
  const reflectedModel=(id==='gemini'?'models/':'')+'reflected-'+keys[id]+'-suffix';
  const rejected=await invoke(['save-profile',id,route,reflectedModel,'5'],{},1);assert.match(rejected.stderr,/HTTP 400/);
 }
 const staleDiscoveryStart=audit.length,staleCatalogueCount=catalogues.length;
 const staleDiscovery=await invoke(['profile-models','gemini','gemini.generate-content','4'],{},1);assert.match(staleDiscovery.stderr,/HTTP 409/);assert.match(staleDiscovery.stderr,/no automatic retry/i);
 assert.equal(audit.length,staleDiscoveryStart+1);assert.equal(catalogues.length,staleCatalogueCount,'Stale discovery must reject before provider access');
 const deniedStart=audit.length;
 const denied=await invoke(['profile-models','gemini-denied','gemini.generate-content','5','FIXTURE_REJECTED_KEY'],{FIXTURE_REJECTED_KEY:rejectedKey},1);
 assert.match(denied.stderr,/"provider_status"\s*:\s*401/);assert.match(denied.stderr,/"provider_error_code"\s*:\s*"invalid_api_key"/);assert.match(denied.stderr,/"provider_error_type"\s*:\s*"authentication_error"/);assert.match(denied.stderr,/Model discovery provider returned HTTP 401/);assert.match(denied.stderr,/no automatic retry/i);
 assert.equal(audit.length,deniedStart+1);assert.equal(audit.at(-1).status,502,'Native API must preserve the authoritative provider status within its 502 error DTO');
 await invoke(['save-profile','openai','openai.chat','fixture-openai-other','4'],{},1);
 await invoke(['select-profile','gemini','4'],{},1);
 await control({command:'arm_cas_failure'},'fixture_fault');await invoke(['select-profile','claude','5'],{},1);await control({command:'disarm_cas_failure'},'fixture_fault');
 assert.equal((await invoke(['provider-profiles'])).active,'openai');await control({command:'check',revision:5,credentials:5,sessions:0});assert.deepEqual(await invoke(['sessions']),[],'Setup commands cannot fabricate chat sessions');
 const session=await invoke(['create-session','Actual CLI profile acceptance with synthetic provider responses']);
 const firstAudit=audit.length;
 const selected=await chat(session.id,'/profiles\n/profile gemini bad-revision\n/profile gemini 4\n/models\n/profile gemini 5\n/models\n/provider-models\nSelected Gemini CLI request\n/history\n/exit\n',{modelOverride:'fixture-openai'});
 assert.equal(selected.records.find(value=>value.type==='provider_profile_rejected').http_status,409);
 assert.match(selected.stderr,/No selection sent/);
 assert.deepEqual(audit.slice(firstAudit).filter(value=>value.path==='/v1/provider/profiles/select').map(value=>value.body.expected_revision),[4,5],'Malformed interactive revision must not reach the native selection API');
 const cataloguesInChat=selected.records.filter(value=>value.type==='models');assert.equal(cataloguesInChat[0].selected_model,'fixture-openai');assert.equal(cataloguesInChat[1].selected_model,'');
 const binding=selected.records.find(value=>value.type==='provider_profile');assert.equal(binding.provider_profile_id,'gemini');assert.equal(binding.expected_provider_revision,6);assert.equal(binding.selected_model,'');assert.equal(binding.metadata.active,'gemini');
 assert.deepEqual(selected.records.find(value=>value.type==='provider_models').catalogue,listed.gemini);
 const firstAdmissions=audit.slice(firstAudit).filter(value=>value.path==='/v1/runs'&&value.method==='POST');assert.equal(firstAdmissions.length,1);assert.equal(firstAdmissions[0].body.provider_profile_id,'gemini');assert.equal(firstAdmissions[0].body.expected_provider_revision,6);assert.equal(Object.hasOwn(firstAdmissions[0].body,'model_id'),false,'Explicit selection must clear the old OpenAI model override before Gemini admission');
 assert.deepEqual(selected.records.filter(value=>value.type==='turn_finished').map(value=>value.exit_status),[0]);
 const saved=await invoke(['history',session.id]);assert.equal(saved.length,2);const answer=receipt(saved);assert.equal(answer.usage.prompt_tokens,7);assert.equal(answer.usage.completion_tokens,3);assert.equal(answer.usage.total_tokens,83);assert.equal(answer.usage.prompt_tokens_details.cached_tokens,2);assert.equal(answer.usage.completion_tokens_details.reasoning_tokens,5);
 assert.deepEqual(await invoke(['provider-models']),listed.gemini,'Default one-shot discovery must use the active saved Gemini profile');
 const savedRuns=await invoke(['runs',session.id]);assert.equal(savedRuns.length,1);
 race=true;const raceAudit=audit.length;
 const rejected=await chat(session.id,'/provider-models\nSynthetic stale request one\nSynthetic stale request two\n/exit\n',{exit:1});race=false;assert.equal(raceChanged,true);
 const rejectedDiscovery=rejected.records.find(value=>value.type==='provider_models_rejected');assert.equal(rejectedDiscovery.http_status,409);assert.ok(rejectedDiscovery.error.detail,'Interactive rejected discovery must retain safe native detail');
 assert.deepEqual(rejected.records.filter(value=>value.type==='run_rejected').map(value=>value.http_status),[409,409]);assert.match(rejected.stderr,/no automatic retry/i);
 const staleAdmissions=audit.slice(raceAudit).filter(value=>value.path==='/v1/runs'&&value.method==='POST');assert.equal(staleAdmissions.length,2);
 for(const record of staleAdmissions){assert.equal(record.body.provider_profile_id,'gemini');assert.equal(record.body.expected_provider_revision,6);assert.equal(record.status,409);}
 assert.deepEqual(await invoke(['history',session.id]),saved);assert.deepEqual(await invoke(['runs',session.id]),savedRuns,'Stale admission cannot append a prompt, create a run or silently rebind');
 assert.deepEqual(await invoke(['provider-models']),listed.claude,'Default discovery must use the independently saved active Claude profile');
 configured=await invoke(['select-profile','gemini','7']);assert.equal(configured.revision,8);assert.equal(configured.active,'gemini');publicMetadata(configured);
 await stop({revision:8,credentials:5,sessions:1,session_id:session.id,messages:2,runs:1});
 const reopened=await start();assert.equal(reopened.reopened,true);assert.equal(reopened.revision,8);assert.deepEqual(await invoke(['history',session.id]),saved);
 assert.deepEqual(await invoke(['provider-models']),listed.gemini,'SQLite reopen must resolve the original saved Gemini credential');
 const continuation=await chat(session.id,'/profiles\nContinue signed CLI after reopen\n/history\n/exit\n');
 assert.deepEqual(continuation.records.filter(value=>value.type==='turn_finished').map(value=>value.exit_status),[0]);
 const resumed=await invoke(['history',session.id]);assert.equal(resumed.length,4);assert.deepEqual(resumed.slice(0,2),saved);const reopenedAnswer=receipt(resumed,3);assert.equal(reopenedAnswer.usage.prompt_tokens,13);assert.equal(reopenedAnswer.usage.completion_tokens,4);assert.equal(Object.hasOwn(reopenedAnswer.usage,'total_tokens'),false);
 const blocked=await chat(session.id,'Synthetic blocked CLI turn\n/provider-models\n/profiles\n/profile gemini 8\n/exit\n',{exit:1});
 assert.deepEqual(blocked.records.filter(value=>value.type==='turn_finished').map(value=>value.exit_status),[1]);assert.equal(blocked.records.find(value=>value.type==='provider_profile').expected_provider_revision,9);
 const failedHistory=await invoke(['history',session.id]);assert.equal(failedHistory.length,5);assert.deepEqual(failedHistory.slice(0,4),resumed);assert.equal(failedHistory[4].role,'user');
 const recovered=await chat(session.id,'Synthetic blocked CLI recovery probe\n/provider-models\n/profile gemini 9\nSynthetic CLI recovered turn\n/exit\n');
 assert.deepEqual(recovered.records.filter(value=>value.type==='turn_finished').map(value=>value.exit_status),[1,0],'Only an actual successful later agent turn can recover the prior turn exit status');assert.equal(recovered.records.find(value=>value.type==='provider_profile').expected_provider_revision,10);
 await stop({revision:10,credentials:5,sessions:1,session_id:session.id,messages:8,runs:5});
 assert.deepEqual(modelRequests,['Selected Gemini CLI request','Continue signed CLI after reopen','Synthetic blocked CLI turn','Synthetic blocked CLI recovery probe','Synthetic CLI recovered turn'],'Setup, discovery and stale admission must not request inference or retry failed models');
 assert.equal(catalogues.filter(value=>value==='openai').length,3);assert.equal(catalogues.filter(value=>value==='claude-first').length,3);assert.equal(catalogues.filter(value=>value==='claude-next').length,3);assert.equal(catalogues.filter(value=>value==='gemini-first').length,7);assert.equal(catalogues.filter(value=>value==='gemini-next').length,7);
 assert.equal(catalogues.filter(value=>value==='gemini-denied').length,1,'Provider authentication failure must not be retried');
 for(const record of audit.filter(value=>value.path==='/v1/provider/profiles/models'&&!Object.hasOwn(value.body||{},'api_key')))assert.deepEqual(Object.keys(record.body).sort(),['expected_revision','id','route_id'],'Saved-key discovery must omit the provider credential');
 for(const entry of await readdir(root,{withFileTypes:true}))if(entry.isFile()&&/\.sqlite(?:-wal|-shm)?$/.test(entry.name)){
  const bytes=await readFile(join(root,entry.name));for(const secret of [...Object.values(keys),rejectedKey])assert.equal(bytes.includes(Buffer.from(secret)),false,'Actual closed xlang3 SQLite files must not contain plaintext provider keys');
 }
 if(failure)throw failure;
 process.stdout.write('Actual native CLI profile acceptance passed three native catalogue authentication formats, encoded pagination/full Gemini resources, private environment input, inactive/active CAS and encrypted SQLite reopen, committed interactive profile/model binding, signed native Gemini history/usage, stale admission without retry and failed-turn status preserved through settings then actual recovery. Provider sockets and credentials are synthetic; no live account, UI or cross-provider history conversion claimed.\n');
}catch(error){throw safe(error);}
finally{
 try{await stop();}finally{
  proxy.closeAllConnections();peer.closeAllConnections();
  if(proxy.listening)await new Promise(resolve=>proxy.close(resolve));if(peer.listening)await new Promise(resolve=>peer.close(resolve));
  const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('xmind-provider-profile-cli-'));
  await rm(target,{recursive:true,force:true});
 }
}
