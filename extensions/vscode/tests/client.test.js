'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const { BackendClient,SessionHistoryPager,parseWorkspaceCommand,EventStreamSubscription } = require('../client');
const { canRestoreFileOperation } = require('../patch-review');
const {SkillViewController,validateSessionSkills}=require('../client');
const skillWorkspace='windows-local-file-v1:synthetic-test-workspace',skillAuthority='a'.repeat(32);
const skillState=(ids=[],revision=0,editable=true)=>({session_id:'session',workspace_id:skillWorkspace,authority_id:skillAuthority,revision,ids,manual_ids:ids,editable});
const skillCatalogue={workspace_id:skillWorkspace,authority_id:skillAuthority,skills:[{id:'manual',name:'User guide',path:'.agents/skills/manual.md',model_invocable:false}]};
test('process reconciliation validates UTF-8 byte limits before sending an audit note',async()=>{
 const requests=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>({})};});
 for(const note of ['已检查工作区及输出','é'.repeat(512),'x'.repeat(1024)]){await client.reconcileProcess('operation',note);assert.equal(JSON.parse(requests.at(-1).options.body).note,note);}
 const sent=requests.length;
 for(const note of ['短注释','é'.repeat(513),'x'.repeat(1025),'Audit note with\nnewline','Audit note with\rreturn','Audit note with\0null'])assert.throws(()=>client.reconcileProcess('operation',note),/16.*1,024.*UTF-8/);
 assert.equal(requests.length,sent,'Invalid notes must not reach the backend');
});
test('workspace slash commands resolve exact catalogue IDs and preserve raw arguments',()=>{
 const catalogue={commands:[{id:'review/security'},{id:'review'}]};
 assert.deepEqual(parseWorkspaceCommand('/review/security "src/app file.cpp" --strict',catalogue),{id:'review/security',arguments:'"src/app file.cpp" --strict'});
 assert.equal(parseWorkspaceCommand('Please review this file',catalogue),undefined);
 assert.throws(()=>parseWorkspaceCommand('/review/missing now',catalogue),/not found/);
 assert.throws(()=>parseWorkspaceCommand('/review//security',catalogue),/Use \/command/);
 assert.throws(()=>parseWorkspaceCommand('/review',{}),/catalogue is invalid/);
});
test('restore eligibility accepts only successful, receipt-bound native file changes',()=>{
 const binding={workspace_id:'workspace',folder_id:'folder',workspace_revision:3,target_workspace_id:'folder'};
 const make=(id,tool,action)=>{const operationId=tool==='patch_file'?id+'-0':id,path=action==='move'?'source.txt':'target.txt',destination='moved.txt',fileId='file-identity',before='a'.repeat(64),after='b'.repeat(64),saved={...binding,path};let receipt;
  if(action==='update'){Object.assign(saved,{file_id:fileId,before_content:'before\n',after_content:'after\n',before_sha256:before,after_sha256:after});receipt=tool==='patch_file'?{operation_id:operationId,state:'succeeded',action,path,file_id:fileId,before_sha256:before,after_sha256:after}:{path,file_id:fileId,after_sha256:after};}
  else if(action==='add'){Object.assign(saved,{action,before_exists:false,after_exists:true,parent_id:'parent',before_content:'',after_content:'added\n',after_sha256:after,create_directories:[]});receipt=tool==='patch_file'?{operation_id:operationId,state:'succeeded',action,path,file_id:fileId,after_sha256:after}:{path,file_id:fileId,after_sha256:after};}
  else if(action==='delete'){Object.assign(saved,{action,before_exists:true,after_exists:false,parent_id:'parent',file_id:fileId,before_content:'before\n',before_sha256:before,after_content:''});receipt={operation_id:operationId,state:'succeeded',action,path,file_id:fileId,removed_sha256:before};}
  else{Object.assign(saved,{action,before_exists:true,after_exists:true,parent_id:'parent',file_id:fileId,before_content:'before\n',before_sha256:before,after_content:'after\n',after_sha256:after,destination_path:destination,destination_parent_id:'destination-parent',create_directories:[]});receipt={operation_id:operationId,state:'succeeded',action,path:destination,source_path:path,file_id:fileId,before_sha256:before,after_sha256:after};}
  if(tool==='patch_file'){saved.patch_id=id;saved.patch_index=0;saved.patch_file_count=1;saved.action=action;saved.before_exists=action!=='add';saved.after_exists=action!=='delete';saved.parent_id='parent';}
  return{id:operationId,workspace_id:'workspace',tool,state:'succeeded',arguments_json:JSON.stringify(saved),result_json:JSON.stringify(receipt)};
 };
 for(const operation of [make('edit','replace_file','update'),make('create','create_file','add'),...['update','add','delete','move'].map(action=>make('patch-'+action,'patch_file',action))])assert.equal(canRestoreFileOperation(operation),true,operation.id);
 for(const change of [{state:'uncertain'},{tool:'run_process'},{result_json:'{}'},{arguments_json:'{}'},{workspace_id:'foreign'}])assert.equal(canRestoreFileOperation({...make('patch-update','patch_file','update'),...change}),false);
 const mismatched=make('patch-add','patch_file','add');mismatched.result_json=JSON.stringify({...JSON.parse(mismatched.result_json),operation_id:'another-operation'});assert.equal(canRestoreFileOperation(mismatched),false);
});
test('workspace command submission carries fresh workspace, provider and graph authority to native backend',async()=>{
 const requests=[],workspace={configured:true,root:'D:\\TestProj',workspace_id:skillWorkspace,authority_id:skillAuthority},client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>workspace};});
 const result=await client.command('session-command','review/security','src/app.cpp','model-id',{provider_profile_id:'profile-a',expected_provider_revision:4},workspace,{id:'review-flow',revision:3});
 assert.deepEqual(result,workspace);assert.equal(new URL(requests[0].url).pathname,'/v1/sessions/session-command/commands');assert.equal(requests[0].options.method,'POST');
 assert.deepEqual(JSON.parse(requests[0].options.body),{command_id:'review/security',arguments:'src/app.cpp',model_id:'model-id',provider_profile_id:'profile-a',expected_provider_revision:4,graph_id:'review-flow',graph_revision:3,expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority});
});
test('opened workspace folder attachment preserves the native primary workspace identity',async()=>{
 const workspace={configured:true,root:'D:\\TestProj',workspace_id:'windows-local-file-v1:1:testproj',authority_id:'a'.repeat(32)},requests=[];
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});if(url.endsWith('/v1/workspace'))return{ok:true,json:async()=>workspace};return{ok:true,json:async()=>({workspace_id:workspace.workspace_id,revision:2,primary_folder_id:workspace.workspace_id,folders:[{folder_id:workspace.workspace_id,root:workspace.root},{folder_id:'windows-local-file-v1:2:shared',root:'D:\\\\Shared'}]})};});
 const result=await client.addWorkspaceFolder('D:\\\\Shared');assert.equal(result.revision,2);assert.equal(result.folders.length,2);assert.deepEqual(requests.map(value=>[new URL(value.url).pathname,value.options.method]),[['/v1/workspace','GET'],['/v1/workspaces/add','POST']]);assert.deepEqual(JSON.parse(requests[1].options.body),{workspace_id:workspace.workspace_id,root:'D:\\\\Shared'});
 await assert.rejects(client.addWorkspaceFolder('relative-folder'),/absolute local or UNC/);
});
test('native workspace membership is validated and enriched for editor folder selection',async()=>{
 const workspace={configured:true,root:'D:\\TestProj',workspace_id:'windows-local-file-v1:1:testproj',authority_id:'a'.repeat(32)},nativeGroup={workspace_id:workspace.workspace_id,revision:3,primary_folder_id:workspace.workspace_id,folders:[{folder_id:workspace.workspace_id,root:workspace.root},{folder_id:'windows-local-file-v1:2:shared',root:'D:\\Shared'}]},group={...nativeGroup,name:'TestProj',root:workspace.root,folders:[{...nativeGroup.folders[0],name:'TestProj',ordinal:0},{...nativeGroup.folders[1],name:'Shared',ordinal:1}]},requests=[];let response=nativeGroup;
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async url=>{requests.push(url);return{ok:true,json:async()=>url.endsWith('/v1/workspace')?workspace:response};});
 assert.deepEqual(await client.workspaceMembership(),group);assert.deepEqual(requests.map(url=>new URL(url).pathname),['/v1/workspace','/v1/workspace/membership']);
 response=group;assert.deepEqual(await client.workspaceMembership(workspace),group,'Native hosts may return the already enriched, exact membership record.');
 for(const mutate of [value=>{value.name='Other';},value=>{value.folders[1].ordinal=0;},value=>{value.unexpected=true;}]){const malformed=structuredClone(group);mutate(malformed);response=malformed;await assert.rejects(client.workspaceMembership(workspace));}
 const invalid=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async url=>({ok:true,json:async()=>url.endsWith('/v1/workspace')?workspace:{...nativeGroup,folders:[...nativeGroup.folders,{folder_id:nativeGroup.folders[0].folder_id,root:'D:\\Other'}]}}));
 await assert.rejects(invalid.workspaceMembership(),/invalid workspace folder/i);
});
test('process output client validates paginated byte artifacts without text coercion',async()=>{
 const requests=[],page={operation_id:'process-op',run_id:'run-root',state:'complete',channel:'stderr',encoding:'hex',data:'ff0080',offset:12,total_bytes:15,next_offset:15,complete:true};
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>page};});
 assert.deepEqual(await client.processOutput('process-op','stderr',12,64),page);assert.equal(new URL(requests[0].url).pathname,'/v1/operations/process-op/output');assert.equal(new URL(requests[0].url).search,'?channel=stderr&after=12&limit=64');
 for(const args of [['process-op','input',0,64],['process-op','stdout',-1,64],['process-op','stdout',0,262144]])await assert.rejects(client.processOutput(...args),/process output/i);page.next_offset=16;await assert.rejects(client.processOutput('process-op','stderr',12,64),/native process output page/i);
});
test('direct restore client binds the returned native graph run to its conversation',async()=>{
 const requests=[],run={id:'restore-run',session_id:'session',state:'queued',parent_id:'',node_id:'',graph_root:true};const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>run};});
 assert.deepEqual(await client.restoreOperation('saved-edit','session'),run);assert.equal(new URL(requests[0].url).pathname,'/v1/operations/saved-edit/restore');assert.deepEqual(JSON.parse(requests[0].options.body),{session_id:'session'});
 for(const invalid of [{...run,session_id:'other'},{...run,parent_id:'foreign-run'},{...run,graph_root:false}]){const changed=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async()=>({ok:true,json:async()=>invalid}));await assert.rejects(changed.restoreOperation('saved-edit','session'),/conversation/);}
});
test('session history page client validates bounded chronological pages and cursor shape',async()=>{
 const requests=[],page={messages:[8,9].map(seq=>({id:`msg_${seq}`,seq,role:'user',data:{content:`m${seq}`}})),has_more:true,next_before:'msg_8'};
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>page};});
 assert.deepEqual(await client.historyPage('session',2),page);assert.equal(new URL(requests[0].url).pathname,'/v1/sessions/session/messages');assert.equal(new URL(requests[0].url).search,'?limit=2');
 assert.deepEqual(await client.historyPage('session',2,'msg_8'),page);assert.equal(new URL(requests[1].url).search,'?limit=2&before=msg_8');
 await assert.rejects(client.historyPage('session',0),/page size/);await assert.rejects(client.historyPage('session',1001),/page size/);await assert.rejects(client.historyPage('session',10,'msg_bad'),/cursor/);
 page.next_before='msg_7';await assert.rejects(client.historyPage('session',2),/history page/);
});
test('session archive client validates portable v1/v2 records and uses native import/export routes',async()=>{
 const requests=[],archive={format:'xmind.session',version:2,session:{title:'Portable session',agent:{id:'reviewer',name:'Repository Reviewer',description:'Reviews changes.',revision:2,model_id:'openai/gpt-5.6',instructions:'Review carefully',tools:['read_file'],permissions:[{action:'read_file',resource:'**/.env*',effect:'deny'}]}},messages:[{role:'user',data:{content:'Inspect the project'}},{role:'assistant',data:{content:'Ready'}}]};
 archive.session.agent.permissions.push({action:'read_file',resource:'.env.example',effect:'allow'});
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>new URL(url).pathname.endsWith('/export')?archive:{id:'imported-1',title:'Portable session'}};});
 assert.deepEqual(await client.exportSession('session-1'),archive);assert.equal(new URL(requests[0].url).pathname,'/v1/sessions/session-1/export');assert.equal(requests[0].options.method,'GET');
 assert.deepEqual(await client.importSession(archive),{id:'imported-1',title:'Portable session'});assert.equal(new URL(requests[1].url).pathname,'/v1/sessions/import');assert.equal(requests[1].options.method,'POST');assert.deepEqual(JSON.parse(requests[1].options.body),{archive});
 const legacy={format:'xmind.session',version:1,session:{title:'Old archive'},messages:[]};assert.deepEqual(await client.importSession(legacy),{id:'imported-1',title:'Portable session'});const legacyAgent={...archive,session:{title:'Old v2 archive',agent:{id:'reviewer',revision:2,model_id:'openai/gpt-5.6',instructions:'Review carefully',tools:['read_file']}}};assert.deepEqual(await client.importSession(legacyAgent),{id:'imported-1',title:'Portable session'});
 const count=requests.length;for(const invalid of [{...archive,extra:true},{...archive,messages:[{role:'system',data:{content:'not an allowed imported role'}}]},{...archive,messages:[{role:'user',data:{content:'x'.repeat(262145)}}]},{...legacy,session:{title:'Old archive',agent:null}},{...archive,session:{...archive.session,agent:{...archive.session.agent,tools:Array(65).fill('read_file')}}},{...archive,session:{...archive.session,agent:{...archive.session.agent,permissions:[{action:'*',resource:'**',effect:'ask'}]}}}])await assert.rejects(client.importSession(invalid));assert.equal(requests.length,count,'Malformed archives are rejected before they reach the backend');
 const malformed=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async()=>({ok:true,json:async()=>({...archive,version:3})}));await assert.rejects(malformed.exportSession('session-1'),/archive/i);
});
test('default session history load is bounded to forty recent messages',async()=>{
 const requests=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push(url);return{ok:true,json:async()=>({messages:[],has_more:false,next_before:null})};});
 await client.historyPage('session');assert.equal(new URL(requests[0]).search,'?limit=40');
 const all=Array.from({length:41},(_,i)=>({id:`msg_${i+1}`,seq:i+1,role:'user',data:{content:`m${i+1}`}}));
 const pager=new SessionHistoryPager({historyPage:async(_session,limit,before)=>{assert.equal(limit,40);const end=before?Number(before.slice(4))-1:all.length,start=Math.max(0,end-limit);return{messages:all.slice(start,end),has_more:start>0,next_before:start>0?all[start].id:null};}});
 let page=await pager.select('session');assert.equal(page.history.length,40);assert.equal(page.history[0].seq,2);assert.equal(page.historyHasMore,true);
 page=await pager.older();assert.equal(page.history.length,41);assert.equal(page.history[0].seq,1);assert.equal(page.historyHasMore,false);
});
test('session history pager merges live snapshots and prepends older pages without gaps',async()=>{
 const all=Array.from({length:5},(_,i)=>({id:`msg_${i+1}`,seq:i+1,role:'user',data:{content:`m${i+1}`}})),calls=[];
 const client={historyPage:async(session,limit,before)=>{calls.push({session,limit,before});const end=before?Number(before.slice(4))-1:all.length,start=Math.max(0,end-limit);return{messages:all.slice(start,end),has_more:start>0,next_before:start>0?all[start].id:null};}};
 const pager=new SessionHistoryPager(client,2);let view=await pager.select('session');assert.deepEqual(view.history.map(item=>item.seq),[4,5]);assert.equal(view.historyHasMore,true);
 view=await pager.older();assert.deepEqual(view.history.map(item=>item.seq),[2,3,4,5]);assert.equal(view.historyHasMore,true);
 view=await pager.refresh();assert.deepEqual(view.history.map(item=>item.seq),[2,3,4,5]);assert.equal(view.historyHasMore,true);
 view=await pager.older();assert.deepEqual(view.history.map(item=>item.seq),[1,2,3,4,5]);assert.equal(view.historyHasMore,false);
 all.push({id:'msg_6',seq:6,role:'assistant',data:{content:'m6'}});view=await pager.refresh();assert.deepEqual(view.history.map(item=>item.seq),[1,2,3,4,5,6]);assert.equal(view.historyHasMore,false);
 assert.deepEqual(calls.map(call=>call.before),[undefined,'msg_4',undefined,'msg_2',undefined]);
});
test('session history pager ignores a page response retired by a session switch',async()=>{
 let release;const pending=new Promise(resolve=>release=resolve),client={historyPage:session=>session==='old'?pending:Promise.resolve({messages:[{id:'msg_1',seq:1,role:'user',data:{content:'new'}}],has_more:false,next_before:null})};
 const pager=new SessionHistoryPager(client,10),old=pager.select('old'),fresh=await pager.select('new');release({messages:[{id:'msg_9',seq:9,role:'user',data:{content:'old'}}],has_more:false,next_before:null});await old;assert.equal(fresh.history[0].id,'msg_1');assert.equal(pager.snapshot().history[0].id,'msg_1');
});
test('session deletion uses the exact native DELETE identity route without a body and accepts 204',async()=>{
 const requests=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,status:204,json:async()=>{throw Error('A 204 response must not be decoded as JSON');}};});assert.deepEqual(await client.deleteSession('session-1'),{});assert.equal(new URL(requests[0].url).pathname,'/v1/sessions/session-1');assert.equal(requests[0].options.method,'DELETE');assert.equal(requests[0].options.body,undefined);assert.throws(()=>client.deleteSession('../other'),/identity/i);assert.equal(requests.length,1);
});
test('clearing a deleted session retires late history responses and leaves an empty snapshot',async()=>{
 let release;const pending=new Promise(resolve=>release=resolve),pager=new SessionHistoryPager({historyPage:()=>pending});const old=pager.select('deleted');const empty=pager.clear();assert.deepEqual(empty,{history:[],historyHasMore:false,historyLoading:false});release({messages:[{id:'msg_1',seq:1,role:'user',data:{content:'deleted history'}}],has_more:false,next_before:null});await old;assert.equal(pager.session,undefined);assert.deepEqual(pager.snapshot(),empty);
});
test('session fork client binds the selected source and exact before-message boundary',async()=>{
 const requests=[],receipt={id:'forked',title:'Saved session (fork #1)',parent_id:'saved',fork:{session_id:'saved',boundary_type:'before',message_id:'msg_9'}};
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>receipt};});
 assert.deepEqual(await client.forkSession('saved','msg_9'),receipt);assert.equal(new URL(requests[0].url).pathname,'/v1/sessions/saved/fork');assert.deepEqual(JSON.parse(requests[0].options.body),{before:'msg_9'});assert.equal(requests[0].options.method,'POST');
 await assert.rejects(client.forkSession('saved','foreign'),/fork boundary/i);assert.equal(requests.length,1);
 const malformed=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async()=>({ok:true,json:async()=>({...receipt,parent_id:'other'})}));await assert.rejects(malformed.forkSession('saved','msg_9'),/fork session receipt/i);
});
test('single-message lookup is session-scoped, validated and uses the read-only browser route',async()=>{
 const requests=[],message={id:'msg_9',seq:9,role:'assistant',data:{content:'Saved answer'}},client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>message};});
 assert.deepEqual(await client.sessionMessage('saved','msg_9'),message);assert.equal(new URL(requests[0].url).pathname,'/v1/sessions/saved/messages/msg_9');assert.equal(requests[0].options.method,'GET');
 await assert.rejects(client.sessionMessage('saved','msg_0'),/message ID/i);await assert.rejects(client.sessionMessage('saved','msg_9007199254740992'),/message ID/i);assert.equal(requests.length,1);
 const malformed=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async()=>({ok:true,json:async()=>({...message,seq:8})}));await assert.rejects(malformed.sessionMessage('saved','msg_9'),/native session message/i);
});
test('language-server formatting carries bounded options and workspace authority',async()=>{
 const requests=[],binding={expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority},client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:'formatting',result:[],diagnostics:[]})};});
 await client.languageServerQuery({path:'src/lib.rs',method:'formatting',documentText:'fn main(){}',formattingOptions:{tabSize:2,insertSpaces:true}},binding);
 assert.deepEqual(JSON.parse(requests[0].options.body),{path:'src/lib.rs',method:'formatting',line:0,character:0,document_text:'fn main(){}',formatting_options:{tab_size:2,insert_spaces:true},expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority});
 await assert.rejects(client.languageServerQuery({path:'src/lib.rs',method:'formatting',formattingOptions:{tabSize:0,insertSpaces:true}},binding),/formatting/i);await assert.rejects(client.languageServerQuery({path:'src/lib.rs',method:'hover',formattingOptions:{tabSize:2,insertSpaces:true}},binding),/formatting/i);assert.equal(requests.length,1);
});
test('language-server query binds an optional registered folder ID and checks the echoed identity',async()=>{
 const folderId='windows-local-file-v1:attached',requests=[],binding={expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority},client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:'diagnostics',result:{kind:'full',items:[]},diagnostics:[],folder_id:folderId})};});
 await client.languageServerQuery({path:'src/lib.rs',method:'diagnostics',folderId},binding);
 assert.deepEqual(JSON.parse(requests[0].options.body),{path:'src/lib.rs',method:'diagnostics',line:0,character:0,folder_id:folderId,expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority});
 await assert.rejects(client.languageServerQuery({path:'src/lib.rs',method:'diagnostics',folderId:'D:\\Outside'},binding),/language-server query/i);
 const wrong=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async()=>({ok:true,json:async()=>({server_id:'rust',language_id:'rust',path:'src/lib.rs',method:'diagnostics',result:null,diagnostics:[],folder_id:'windows-local-file-v1:other'})}));
 await assert.rejects(wrong.languageServerQuery({path:'src/lib.rs',method:'diagnostics',folderId},binding),/language-server response/i);
});
test('terminal client authenticates each session-bound lifecycle request and validates byte cursors',async()=>{
 const id='e'.repeat(32),requests=[],metadata={id,session_id:'session-one',title:'Terminal',rows:24,columns:80,running:true,exit_code:null,output_sequence:7};
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{
  requests.push({url,options});const route=new URL(url).pathname;
  const value=route.endsWith('/terminals')&&options.method==='GET'?{terminals:[metadata]}:route.endsWith('/terminals')?metadata:route.endsWith('/output')?{encoding:'hex',data:'f09f',next_sequence:9,truncated:false,running:true,exit_code:null}:route.endsWith('/input')?{written:true}:route.endsWith('/resize')?{...metadata,rows:30,columns:100}:route.endsWith('/close')?{closed:true}:metadata;
  return{ok:true,json:async()=>value};
 });
 assert.deepEqual(await client.terminals('session-one'),[metadata]);
 await client.createTerminal('session-one','Terminal',24,80);const output=await client.terminalOutput(id,7);assert.equal(output.next_sequence,9);
 await client.terminalInput(id,'echo hi\r');await client.resizeTerminal(id,30,100);await client.closeTerminal(id);
 assert.deepEqual(requests.map(value=>new URL(value.url).pathname),[
  '/v1/sessions/session-one/terminals','/v1/sessions/session-one/terminals',`/v1/terminals/${id}/output`,`/v1/terminals/${id}/input`,`/v1/terminals/${id}/resize`,`/v1/terminals/${id}/close`
 ]);
 assert.ok(requests.every(value=>value.options.headers.Authorization==='Bearer synthetic-client-token-32-bytes-long'));
 assert.equal(requests[2].url.endsWith('?after=7'),true);assert.deepEqual(JSON.parse(requests[4].options.body),{rows:30,columns:100});
 await assert.rejects(client.terminalOutput(id,-1),/invalid/i);await assert.rejects(client.terminalInput(id,'\0'),/invalid/i);assert.equal(requests.length,6);
});
test('session skill client posts only exact revision, ids and workspace authority',async()=>{
 const requests=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>skillState(['manual'],1)};});
 const result=await client.replaceSessionSkills('session',['manual'],skillState());assert.equal(result.revision,1);assert.deepEqual(JSON.parse(requests[0].options.body),{ids:['manual'],expected_revision:0,expected_workspace_id:skillWorkspace,expected_workspace_authority_id:skillAuthority});assert.equal(requests[0].options.method,'POST');assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/sessions/session/skills');
 await assert.rejects(client.replaceSessionSkills('session',['manual','manual'],skillState()),/distinct/);await assert.rejects(client.replaceSessionSkills('session',[],skillState(['manual'],1,false)),/idle/);assert.equal(requests.length,1);assert.throws(()=>validateSessionSkills({...skillState(),manual_ids:['foreign']},'session'),/provenance/);
});
test('skill controller publishes acknowledged manual attachment and removal without model prompts',async()=>{
 let state=skillState();const commands=[],posted=[];const client={skills:async()=>skillCatalogue,sessionSkills:async()=>state,replaceSessionSkills:async(session,ids,previous)=>{commands.push({session,ids,previous});state=skillState(ids,previous.revision+1);return state;}};
 const controller=new SkillViewController(client,m=>posted.push(m),()=>({session:'session',generation:1,enabled:true}));await controller.read();await controller.change({session:'session',revision:0,ids:['manual']});await controller.change({session:'session',revision:1,ids:[]});assert.deepEqual(commands.map(c=>c.ids),[['manual'],[]]);assert.equal(posted.at(-1).selection.revision,2);assert.deepEqual(posted.at(-1).selection.ids,[]);assert.ok(posted.every(m=>m.type==='skills'));
 await assert.rejects(controller.change({session:'session',revision:1,ids:['manual']}),/Refresh/);await assert.rejects(controller.change({session:'session',revision:2,ids:['unknown']}),/catalogue/);assert.equal(commands.length,2);
});
test('skill reads discard responses from a previous conversation generation',async()=>{
 let release,scope={session:'session',generation:1,enabled:true};const posted=[];const controller=new SkillViewController({sessionSkills:()=>new Promise(yes=>release=yes),skills:async()=>skillCatalogue},m=>posted.push(m),()=>scope);
 const pending=controller.read();scope={session:'other',generation:2,enabled:true};controller.invalidate();release(skillState());await pending;assert.equal(controller.record,undefined);assert.ok(!posted.some(m=>m.type==='skills'));
});
test('skill acknowledgements cannot publish into a newly selected conversation',async()=>{
 let release,scope={session:'session',generation:1,enabled:true};const posted=[];const controller=new SkillViewController({sessionSkills:async()=>skillState(),skills:async()=>skillCatalogue,replaceSessionSkills:()=>new Promise(yes=>release=yes)},m=>posted.push(m),()=>scope);await controller.read();const pending=controller.change({session:'session',revision:0,ids:['manual']});scope={session:'other',generation:2,enabled:true};controller.invalidate();release(skillState(['manual'],1));await pending;assert.equal(controller.record,undefined);assert.equal(posted.filter(m=>m.type==='skills').length,1);
});
test('malformed catalogue metadata cannot enter a skill chooser, while source conflicts allow only clearing',async()=>{
 const invalid=new SkillViewController({sessionSkills:async()=>skillState(),skills:async()=>({...skillCatalogue,skills:[{...skillCatalogue.skills[0],body:'Untrusted injected guide'}]})},()=>{},()=>({session:'session',generation:1,enabled:true}));await assert.rejects(invalid.read(),/catalogue entry/);assert.equal(invalid.record,undefined);
 const posted=[];let changed=0;const conflict=Object.assign(new Error('Ambiguous current skill sources'),{status:409});const client={sessionSkills:async()=>skillState(['manual'],1),skills:async()=>{throw conflict;},replaceSessionSkills:async()=>{changed++;return skillState([],2);}};const controller=new SkillViewController(client,m=>posted.push(m),()=>({session:'session',generation:1,enabled:true}));await controller.read();assert.equal(posted.at(-1).catalogueError,conflict.message);await assert.rejects(controller.change({session:'session',revision:1,ids:['manual']}),/catalogue/);await controller.change({session:'session',revision:1,ids:[]});assert.equal(changed,1);assert.deepEqual(posted.at(-1).selection.ids,[]);
});
test('provider enrollment accepts only matching approved OpenAI wire and endpoint pairs',()=>{
  const {providerEnrollmentWire}=require('../client'),base={provider:'openai',revision:1,endpoint:'https://api.openai.com/v1/chat/completions'};
  assert.equal(providerEnrollmentWire(base),'chat-completions');assert.equal(providerEnrollmentWire({...base,wire:'responses',endpoint:'https://api.openai.com/v1/responses'}),'responses');
  for(const changed of [{wire:'responses'},{endpoint:'https://api.openai.com/v1/responses'},{wire:'unknown'},{endpoint:'https://unapproved.invalid/v1/responses',wire:'responses'},{revision:-1}])assert.throws(()=>providerEnrollmentWire({...base,...changed}),/policy/);
});
const token = 'native-client-contract-token-32-bytes';
// Synthetic native SSE frames: parser/ownership/cursor behavior only.
// No native runtime, provider or agent execution is supplied by these fixtures.
const streamRoot=(changed={})=>({id:'root',session_id:'session',state:'completed',parent_id:'',node_id:'',graph_root:false,...changed});
const streamEvent=(seq,run_id='root')=>({seq,run_id,kind:'model.text',data:{text:'Synthetic stream 雪'}});
const streamFrame=(type,data,id)=>`${id===undefined?'':'id: '+id+'\n'}event: ${type}\ndata: ${JSON.stringify(data)}\n\n`;
const streamWire=(events=[],{scope='run',after=0,run=streamRoot(),reason='terminal'}={})=>streamFrame('observation',{run,scope,after})+events.map(e=>streamFrame('committed',e,e.seq)).join('')+streamFrame('end',{reason,after:events.at(-1)?.seq??after});
function streamResponse(text,step=7){const bytes=new TextEncoder().encode(text);return new Response(new ReadableStream({start(controller){for(let i=0;i<bytes.length;i+=step)controller.enqueue(bytes.subarray(i,i+step));controller.close();}}),{headers:{'Content-Type':'text/event-stream'}});}
function subscriptionHarness(peer,callbacks={}){const {EventStreamSubscription}=require('../client'),tasks=[],errors=[],delivered=[],ended=[],client={baseUrl:'http://127.0.0.1:8765',eventStream:peer};let live=true;const subscription=new EventStreamSubscription({current:()=>live,onEvent:e=>delivered.push(e),onEnd:e=>ended.push(e),onError:e=>errors.push(e),schedule:(callback,delay)=>{const item={callback,delay};tasks.push(item);return item;},unschedule:item=>{const at=tasks.indexOf(item);if(at!==-1)tasks.splice(at,1);},...callbacks});return {subscription,client,tasks,errors,delivered,ended,binding:{client,root:'root',session:'session',scope:'run',generation:1,after:0},retire:()=>live=false};}
const subscriptionTick=()=>new Promise(resolve=>setImmediate(resolve));
test('subscription rotates using its delivered cursor without duplicating a matching watch',async()=>{
 const calls=[],h=subscriptionHarness(async(id,options)=>{calls.push({id,options});const seq=options.after+1;await options.onEvent(streamEvent(seq));return {reason:calls.length===1?'reconnect':'terminal',cursor:seq};});h.subscription.watch(h.binding);await subscriptionTick();assert.equal(calls.length,1);assert.equal(h.tasks.length,1);assert.equal(h.tasks[0].delay,0);h.subscription.watch({...h.binding,after:99});assert.equal(calls.length,1);h.tasks.shift().callback();await subscriptionTick();assert.equal(calls.length,2);assert.equal(calls[1].options.after,1);assert.deepEqual(h.delivered.map(e=>e.seq),[1,2]);assert.equal(h.ended.length,1);assert.equal(h.tasks.length,0);h.subscription.dispose();
});
test('subscription replacement aborts stale observation and cannot deliver its event or terminal outcome',async()=>{
 const calls=[],h=subscriptionHarness((id,options)=>new Promise(resolve=>calls.push({id,options,resolve})));h.subscription.watch(h.binding);await subscriptionTick();h.subscription.watch({...h.binding,root:'next',generation:2});await subscriptionTick();assert.equal(calls.length,2);assert.equal(calls[0].options.signal.aborted,true);await assert.rejects(calls[0].options.onEvent(streamEvent(1)),/retired/);calls[0].resolve({reason:'terminal',cursor:0});await subscriptionTick();assert.deepEqual(h.delivered,[]);assert.deepEqual(h.ended,[]);await calls[1].options.onEvent(streamEvent(1,'next'));calls[1].resolve({reason:'terminal',cursor:1});await subscriptionTick();assert.equal(h.delivered.length,1);assert.equal(h.delivered[0].run_id,'next');assert.equal(h.ended.length,1);h.subscription.dispose();
});
test('subscription retirement, origin change and disposal prevent delayed retries',async()=>{
 for(const retire of [h=>h.retire(),h=>h.client.baseUrl='http://127.0.0.1:8766',h=>h.subscription.dispose()]){let calls=0;const h=subscriptionHarness(async()=>{calls++;return {reason:'reconnect',cursor:0};});h.subscription.watch(h.binding);await subscriptionTick();assert.equal(h.tasks.length,1);const task=h.tasks.shift();retire(h);task.callback();await subscriptionTick();assert.equal(calls,1);assert.equal(h.errors.length,0);h.subscription.dispose();}
});
test('subscription backs off repeated stream-busy errors and never retries authentication or malformed acknowledgements',async()=>{
 let calls=0;const busy=subscriptionHarness(async()=>{calls++;const error=Error('Synthetic stream busy');error.status=503;throw error;});busy.subscription.watch(busy.binding);await subscriptionTick();for(const delay of [500,1000,2000,4000,8000,8000]){assert.equal(busy.tasks.length,1);const task=busy.tasks.shift();assert.equal(task.delay,delay);task.callback();await subscriptionTick();}assert.equal(calls,7);assert.equal(busy.tasks.length,1,'A lease may become available later; retries remain rate-limited');assert.equal(busy.errors.length,0);busy.subscription.dispose();assert.equal(busy.tasks.length,0,'Disposal cancels the pending retry');
 for(const response of [{reason:'reauthenticate',cursor:0},{reason:'interrupted',cursor:0},{reason:'terminal',cursor:7},undefined]){const h=subscriptionHarness(async()=>response);h.subscription.watch(h.binding);await subscriptionTick();assert.equal(h.tasks.length,0);assert.equal(h.errors.length,1);assert.equal(h.ended.length,0);h.subscription.dispose();}
});
test('a consumer TypeError stops the subscription without acknowledging or retrying its event',async()=>{
 const h=subscriptionHarness(async(id,options)=>{await options.onEvent(streamEvent(1));return {reason:'reconnect',cursor:1};},{onEvent(){throw new TypeError('Synthetic consumer rejected delivery');}});h.subscription.watch(h.binding);await subscriptionTick();assert.equal(h.subscription.cursor,0);assert.equal(h.tasks.length,0);assert.equal(h.errors.length,1);assert.equal(h.errors[0].eventConsumerFailure,true);h.subscription.dispose();
});
test('subscription refuses invalid bindings before opening or scheduling a connection',()=>{
 let calls=0;const h=subscriptionHarness(async()=>{calls++;});for(const changed of [{root:'invalid/root'},{session:'invalid/session'},{scope:'unknown'},{generation:-1},{after:-1}])assert.throws(()=>h.subscription.watch({...h.binding,...changed}));assert.equal(calls,0);assert.equal(h.tasks.length,0);h.subscription.dispose();h.subscription.watch(h.binding);assert.equal(calls,0);
});
test('committed stream reader handles split UTF-8 and CRLF using one authenticated read-only request',async()=>{
 const observed=[],sent=[],events=[streamEvent(1),streamEvent(2)],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return streamResponse(': keepalive\r\n\r\n'+streamWire(events).replaceAll('\n','\r\n'),1);});
 const result=await client.eventStream('root',{session_id:'session',onEvent:e=>observed.push(e)});assert.deepEqual(observed,events);assert.equal(result.reason,'terminal');assert.equal(result.cursor,2);assert.equal(result.observation.run.id,'root');assert.equal(sent.length,1);assert.equal(sent[0].url,'http://127.0.0.1:8765/v1/runs/root/events/stream?after=0');assert.equal(sent[0].options.method,'GET');assert.equal(sent[0].options.headers.Authorization,'Bearer '+token);assert.equal(sent[0].options.body,undefined);assert.equal(sent[0].options.redirect,'error');
});
const graphStreamRecord=node=>({run:streamRoot({graph_root:true}),spec:{nodes:[{id:node,type:'agent'}]}});
test('graph stream validates owned child conversation before delivering its event',async()=>{
 const sent=[],observed=[],events=[streamEvent(1,'child')],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});if(url.endsWith('/children'))return new Response(JSON.stringify([streamRoot({id:'child',parent_id:'root',node_id:'worker'})]),{headers:{'Content-Type':'application/json'}});if(url.endsWith('/root'))return new Response(JSON.stringify(graphStreamRecord('worker')));return streamResponse(streamWire(events,{scope:'graph',run:streamRoot({graph_root:true})}));});
 assert.equal((await client.eventStream('root',{scope:'graph',session_id:'session',onEvent:e=>observed.push(e)})).cursor,1);assert.deepEqual(observed,events);assert.deepEqual(sent.map(s=>new URL(s.url).pathname),['/v1/graph-runs/root/events/stream','/v1/graph-runs/root','/v1/graph-runs/root/children']);assert.ok(sent.every(s=>s.options.method==='GET'&&s.options.body===undefined));
 for(const children of [[],[streamRoot({id:'child',parent_id:'foreign',node_id:'worker'})],[streamRoot({id:'child',parent_id:'root',session_id:'foreign',node_id:'worker'})]]){const delivered=[],invalid=new BackendClient('http://localhost:8765',()=>token,async url=>url.endsWith('/children')?new Response(JSON.stringify(children)):url.endsWith('/root')?new Response(JSON.stringify(graphStreamRecord('worker'))):streamResponse(streamWire(events,{scope:'graph',run:streamRoot({graph_root:true})})));await assert.rejects(invalid.eventStream('root',{scope:'graph',session_id:'session',onEvent:e=>delivered.push(e)}),error=>/Unowned|identity/.test(error.message)&&error.eventCursor===0);assert.deepEqual(delivered,[]);}
});
test('native delegated node IDs keep the tree subscription alive through parent completion',async()=>{
 const events=[streamEvent(1,'child'),{...streamEvent(2),kind:'run.completed',data:{}}],observed=[],ended=[],errors=[];
 const client=new BackendClient('http://localhost:8765',()=>token,async url=>url.endsWith('/children')?new Response(JSON.stringify([{run:streamRoot({id:'child',parent_id:'root',node_id:'d.'+'a'.repeat(32)})}])):streamResponse(streamWire(events,{scope:'tree'})));
 const subscription=new EventStreamSubscription({current:()=>true,onEvent:event=>observed.push(event),onEnd:value=>ended.push(value),onError:error=>errors.push(error)});
 try{subscription.watch({client,root:'root',session:'session',scope:'tree',generation:0,after:0});await subscriptionTick();assert.deepEqual(errors,[]);assert.deepEqual(observed,events);assert.equal(ended.length,1);assert.equal(ended[0].reason,'terminal');assert.equal(ended[0].cursor,2);}finally{subscription.dispose();}
});

test('dotted graph node labels retain their declared parent and session ownership',async()=>{
 const observed=[],events=[streamEvent(1,'child')],client=new BackendClient('http://localhost:8765',()=>token,async url=>url.endsWith('/children')?new Response(JSON.stringify([streamRoot({id:'child',parent_id:'root',node_id:'build.docs'})])):url.endsWith('/root')?new Response(JSON.stringify(graphStreamRecord('build.docs'))):streamResponse(streamWire(events,{scope:'graph',run:streamRoot({graph_root:true})})));
 const result=await client.eventStream('root',{scope:'graph',session_id:'session',onEvent:event=>observed.push(event)});assert.deepEqual(observed,events);assert.equal(result.reason,'terminal');assert.equal(result.cursor,1);
});

test('node metadata validation preserves route identity and ownership rejection',async()=>{
 const node='d.'+'a'.repeat(32),invalid=[...['bad/node','bad\\node','bad node','bad\0node','a'.repeat(129),'d.'+'a'.repeat(63),null,7].map(node_id=>({node_id})),{id:'child.bad'},{session_id:'foreign'},{parent_id:'foreign'},{graph_root:true}];
 for(const changed of invalid){const observed=[],child=streamRoot({id:'child',parent_id:'root',node_id:node,...changed}),client=new BackendClient('http://localhost:8765',()=>token,async url=>url.endsWith('/children')?new Response(JSON.stringify([{run:child}])):streamResponse(streamWire([streamEvent(1,'child')],{scope:'tree'})));await assert.rejects(client.eventStream('root',{scope:'tree',session_id:'session',onEvent:event=>observed.push(event)}),error=>error.eventCursor===0);assert.deepEqual(observed,[]);}
});

test('stream rejects changed root/session/scope and events without an initial observation',async()=>{
 for(const source of [streamWire([streamEvent(1)],{run:streamRoot({id:'foreign'})}),streamWire([streamEvent(1)],{run:streamRoot({session_id:'foreign'})}),streamWire([streamEvent(1)],{scope:'tree'}),streamFrame('committed',streamEvent(1),1)]){const events=[],client=new BackendClient('http://localhost:8765',()=>token,async()=>streamResponse(source));await assert.rejects(client.eventStream('root',{session_id:'session',onEvent:e=>events.push(e)}),error=>/identity|observation/.test(error.message)&&error.eventCursor===0);assert.deepEqual(events,[]);}
});
test('stream cursor advances only after delivery and rejects duplicate IDs and conflicting end cursors',async()=>{
 const initial=streamFrame('observation',{run:streamRoot(),scope:'run',after:0});
 for(const [source,cursor,count]of [[initial+streamFrame('committed',streamEvent(1),'01'),0,0],[initial+streamFrame('committed',streamEvent(1),2),0,0],[streamWire([streamEvent(1),streamEvent(1)]),1,1],[initial+streamFrame('committed',streamEvent(1),1)+streamFrame('end',{reason:'terminal',after:4}),1,1]]){let delivered=0;const client=new BackendClient('http://localhost:8765',()=>token,async()=>streamResponse(source));await assert.rejects(client.eventStream('root',{session_id:'session',onEvent:()=>delivered++}),error=>/cursor/.test(error.message)&&error.eventCursor===cursor);assert.equal(delivered,count);}
});
test('stream validates admission values and an abort during token lookup cannot send a request',async()=>{
 let fetched=0,tokens=0;const client=new BackendClient('http://localhost:8765',()=>{tokens++;return token;},async()=>{fetched++;throw Error('No request permitted');});
 for(const changed of [{after:-1},{after:1.5},{after:Number.MAX_SAFE_INTEGER+1},{scope:'unknown'},{session_id:'foreign/session'}])await assert.rejects(client.eventStream('root',{session_id:'session',onEvent(){},...changed}));assert.equal(tokens,0);assert.equal(fetched,0);
 let release;const lookup=new Promise(yes=>release=yes),abort=new AbortController(),pendingClient=new BackendClient('http://localhost:8765',()=>lookup,async()=>{fetched++;throw Error('Retired connection must not fetch');}),pending=pendingClient.eventStream('root',{session_id:'session',signal:abort.signal,onEvent(){}});abort.abort();release(token);await assert.rejects(pending,{name:'AbortError'});assert.equal(fetched,0);
});
test('stream detach preserves delivered cursor and a rejected callback never acknowledges its event',async()=>{
 const abort=new AbortController(),sent=[],source=streamWire([streamEvent(1),streamEvent(2)]),client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return streamResponse(source);});let delivered=0;
 await assert.rejects(client.eventStream('root',{session_id:'session',signal:abort.signal,onEvent(){delivered++;abort.abort();}}),error=>error.name==='AbortError'&&error.eventCursor===1);assert.equal(delivered,1);assert.equal(sent.length,1);assert.equal(sent[0].options.method,'GET');
 await assert.rejects(client.eventStream('root',{session_id:'session',onEvent(){throw Error('Consumer rejected delivery');}}),error=>error.message==='Consumer rejected delivery'&&error.eventCursor===0);assert.equal(sent.length,2);
});
test('EOF, malformed JSON and unsupported frames retain the last accepted cursor',async()=>{
 const initial=streamFrame('observation',{run:streamRoot(),scope:'run',after:0}),committed=streamFrame('committed',streamEvent(1),1);
 for(const tail of ['', 'event: committed\ndata: {\n\n','event: unknown\ndata: {}\n\n','event: end\nevent: end\ndata: {}\n\n','event: committed\ndata: {']){const events=[],client=new BackendClient('http://localhost:8765',()=>token,async()=>streamResponse(initial+committed+tail));await assert.rejects(client.eventStream('root',{session_id:'session',onEvent:e=>events.push(e)}),error=>error.eventCursor===1&&(!tail.endsWith('\n\n')?error.eventTransportUnavailable===true:!error.eventTransportUnavailable));assert.equal(events.length,1);}
});

test('actual reader and subscription reconnect clean EOF at the accepted cursor without replaying commands or events',async()=>{
 const {EventStreamSubscription}=require('../client'),requests=[],events=[],errors=[],tasks=[],ended=[];
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options});const after=Number(new URL(url).searchParams.get('after'));return streamResponse(requests.length===1?streamFrame('observation',{run:streamRoot({state:'running'}),scope:'run',after})+streamFrame('committed',streamEvent(7),7):streamWire([streamEvent(8)],{after}));});
 const subscription=new EventStreamSubscription({current:()=>true,onEvent:event=>events.push(event),onEnd:value=>ended.push(value),onError:error=>errors.push(error),schedule:(callback,delay)=>{tasks.push({callback,delay});return callback;},unschedule:()=>{}});
 try{subscription.watch({client,root:'root',session:'session',scope:'run',generation:1,after:0});await subscriptionTick();assert.equal(subscription.cursor,7);assert.equal(tasks.length,1);assert.equal(tasks[0].delay,500);tasks.shift().callback();await subscriptionTick();assert.deepEqual(requests.map(value=>new URL(value.url).searchParams.get('after')),['0','7']);assert.deepEqual(events.map(event=>event.seq),[7,8]);assert.equal(ended[0].cursor,8);assert.equal(errors.length,0);assert.equal(tasks.length,0);assert.ok(requests.every(value=>value.options.method==='GET'&&new URL(value.url).pathname==='/v1/runs/root/events/stream'));
 }finally{subscription.dispose();}
});

test('clean EOF retry stops after its bounded attempts while preserving the acknowledged cursor',async()=>{
 const {EventStreamSubscription}=require('../client'),requests=[],tasks=[],errors=[];const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options});return streamResponse(streamFrame('observation',{run:streamRoot({state:'paused'}),scope:'run',after:7}));});
 const subscription=new EventStreamSubscription({current:()=>true,onEvent:()=>{throw Error('No new event permitted');},onError:error=>errors.push(error),schedule:(callback,delay)=>{tasks.push({callback,delay});return callback;},unschedule:()=>{}});
 try{subscription.watch({client,root:'root',session:'session',scope:'run',generation:1,after:7});await subscriptionTick();const delays=[];while(tasks.length){const task=tasks.shift();delays.push(task.delay);task.callback();await subscriptionTick();}assert.deepEqual(delays,[500,1000,2000]);assert.equal(requests.length,4);assert.equal(errors.length,1);assert.equal(errors[0].eventCursor,7);assert.equal(subscription.cursor,7);assert.equal(subscription.finished,true);assert.ok(requests.every(value=>value.options.method==='GET'&&value.url.endsWith('/events/stream?after=7')));
 }finally{subscription.dispose();}
});

test('truncated SSE frames reconnect at the accepted cursor, including a split UTF-8 character',async()=>{
 const {EventStreamSubscription}=require('../client'),encoder=new TextEncoder();
 const complete=encoder.encode(streamFrame('committed',streamEvent(8),8)),snow=encoder.encode('雪');
 const snowStart=Buffer.from(complete).indexOf(Buffer.from(snow));assert.ok(snowStart>0);
 for(const partial of [complete.subarray(0,12),complete.subarray(0,complete.length-1),complete.subarray(0,snowStart+1),complete.subarray(0,snowStart+2)]){
  const requests=[],events=[],errors=[],tasks=[],ended=[];
  const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{
   requests.push({url,options});const after=Number(new URL(url).searchParams.get('after'));
   if(requests.length>1)return streamResponse(streamWire([streamEvent(8)],{after}));
   return new Response(new ReadableStream({start(controller){controller.enqueue(encoder.encode(streamFrame('observation',{run:streamRoot({state:'running'}),scope:'run',after})+streamFrame('committed',streamEvent(7),7)));controller.enqueue(partial);controller.close();}}),{headers:{'Content-Type':'text/event-stream'}});
  });
  const subscription=new EventStreamSubscription({current:()=>true,onEvent:event=>events.push(event),onEnd:value=>ended.push(value),onError:error=>errors.push(error),schedule:(callback,delay)=>{tasks.push({callback,delay});return callback;},unschedule:()=>{}});
  try{
   subscription.watch({client,root:'root',session:'session',scope:'run',generation:1,after:0});await subscriptionTick();
   assert.equal(subscription.cursor,7);assert.equal(tasks.length,1,'Incomplete transport bytes must not permanently stop observation');assert.equal(tasks[0].delay,500);
   tasks.shift().callback();await subscriptionTick();assert.deepEqual(requests.map(value=>new URL(value.url).searchParams.get('after')),['0','7']);
   assert.deepEqual(events.map(event=>event.seq),[7,8]);assert.equal(ended[0].cursor,8);assert.equal(errors.length,0);assert.equal(tasks.length,0);
   assert.ok(requests.every(value=>value.options.method==='GET'&&new URL(value.url).pathname==='/v1/runs/root/events/stream'));
  }finally{subscription.dispose();}
 }
});

test('invalid UTF-8 bytes remain a protocol failure rather than a reconnectable truncation',async()=>{
 const {EventStreamSubscription}=require('../client'),events=[],errors=[],tasks=[];
 const client=new BackendClient('http://localhost:8765',()=>token,async()=>new Response(new ReadableStream({start(controller){controller.enqueue(new TextEncoder().encode(streamFrame('observation',{run:streamRoot(),scope:'run',after:0})+streamFrame('committed',streamEvent(7),7)));controller.enqueue(new Uint8Array([0xc3,0x28]));controller.close();}}),{headers:{'Content-Type':'text/event-stream'}}));
 const subscription=new EventStreamSubscription({current:()=>true,onEvent:event=>events.push(event),onError:error=>errors.push(error),schedule:(callback,delay)=>{tasks.push({callback,delay});return callback;},unschedule:()=>{}});
 try{subscription.watch({client,root:'root',session:'session',scope:'run',generation:1,after:0});await subscriptionTick();assert.deepEqual(events.map(event=>event.seq),[7]);assert.equal(subscription.cursor,7);assert.equal(subscription.finished,true);assert.equal(errors.length,1);assert.equal(errors[0].eventTransportUnavailable,undefined);assert.equal(tasks.length,0);}finally{subscription.dispose();}
});
test('resume uses the accepted scoped cursor and HTTP rejection does not trigger a fallback or command',async()=>{
 const sent=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return streamResponse(streamWire([streamEvent(8)],{scope:'tree',after:7,reason:'reconnect'}));});const result=await client.eventStream('root',{session_id:'session',scope:'tree',after:7,onEvent(){}});assert.equal(result.reason,'reconnect');assert.equal(result.cursor,8);assert.equal(sent[0].url,'http://127.0.0.1:8765/v1/runs/root/tree-events/stream?after=7');
 let rejectedCalls=0;const rejected=new BackendClient('http://localhost:8765',()=>token,async()=>{rejectedCalls++;return new Response(JSON.stringify({detail:'Stream busy'}),{status:503,headers:{'Content-Type':'application/json'}});});await assert.rejects(rejected.eventStream('root',{session_id:'session',onEvent(){throw Error('No event permitted');}}),error=>error.status===503);assert.equal(rejectedCalls,1);
});
test('workspace skill inspection uses the native read-only catalogue without inventing activation',async()=>{
 const catalogue={workspace_id:'synthetic-workspace',authority_id:'a'.repeat(32),skills:[{id:'disabled',model_invocable:false}]},sent=[];
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return {ok:true,json:async()=>catalogue};});
 assert.deepEqual(await client.skills(),catalogue);assert.equal(sent.length,1);assert.equal(sent[0].url,'http://127.0.0.1:8765/v1/workspace/skills');assert.equal(sent[0].options.method,'GET');assert.equal(sent[0].options.body,undefined);
});
test('owned-child client keeps parent scope and validates the committed tree cursor',async()=>{
 const sent=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push({url,options});return {ok:true,json:async()=>[]};});
 await client.ownedChildren('parent');await client.ownedChildHistory('parent','opaque/child');await client.treeEvents('parent',12);await client.delegation();
 assert.deepEqual(sent.map(value=>value.url),['/v1/runs/parent/children','/v1/runs/parent/children/opaque%2Fchild/history','/v1/runs/parent/tree-events?after=12','/v1/agent/delegation'].map(path=>'http://127.0.0.1:8765'+path));
 assert.ok(sent.every(value=>value.options.method==='GET'&&value.options.headers.Authorization==='Bearer '+token));for(const cursor of [-1,1.5,NaN,Number.MAX_SAFE_INTEGER+1])assert.throws(()=>client.treeEvents('parent',cursor));assert.equal(sent.length,4);
});
test('owned observation covers concurrent admission and paged events without inventing aggregate usage',async()=>{
 const {observeOwnedRun}=require('../client'),run={id:'parent',session_id:'session',parent_id:'',graph_root:false,state:'completed'},child={run:{id:'leaf',parent_id:'parent',session_id:'session',state:'completed',graph_root:false},kind:'delegated_leaf',batch_id:'batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1},calls=[];
 const history=[{role:'assistant',data:{content:'Synthetic observed leaf fixture',usage:{prompt_tokens:4,completion_tokens:2}}}];
 const client={treeEvents:async(id,after)=>{calls.push(['events',after]);return Array.from({length:after===0?256:2},(_,index)=>({seq:after+index+1,run_id:index%2?'leaf':'parent',kind:'model.usage',data:{prompt_tokens:4}}));},ownedChildren:async()=>{calls.push(['children']);return [child];},ownedChildHistory:async(parent,id)=>{calls.push(['history',parent,id]);return history;},operations:async()=>[]};
 const result=await observeOwnedRun(client,run,'session',0);assert.deepEqual(calls.slice(0,3),[['events',0],['events',256],['children']]);assert.equal(result.cursor,258);assert.equal(result.caughtUp,true);assert.deepEqual(result.histories.leaf,history);assert.equal(result.usage,undefined);assert.equal(history[0].data.usage.total_tokens,undefined);
 const bounded=await observeOwnedRun({...client,treeEvents:async(id,after)=>Array.from({length:256},(_,index)=>({seq:after+index+1,run_id:'parent',kind:'model.text',data:{text:'synthetic'}}))},run,'session',0);assert.equal(bounded.cursor,4096);assert.equal(bounded.caughtUp,false,'A full bounded page cannot be mistaken for a caught-up terminal cursor');
});
test('owned observation rejects foreign children and events before requesting private child history',async()=>{
 const {observeOwnedRun}=require('../client'),run={id:'parent',session_id:'session'},child={run:{id:'leaf',parent_id:'parent',session_id:'session'},kind:'delegated_leaf',batch_id:'batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1};let histories=0;
 const base={treeEvents:async()=>[],ownedChildren:async()=>[child],ownedChildHistory:async()=>{histories++;return [];},operations:async()=>[]};
 await assert.rejects(observeOwnedRun({...base,ownedChildren:async()=>[{...child,run:{...child.run,session_id:'foreign'}}]},run,'session',0),/identity/);
 await assert.rejects(observeOwnedRun({...base,treeEvents:async()=>[{seq:1,run_id:'foreign',kind:'model.text'}]},run,'session',0),/Unowned/);assert.equal(histories,0);
});
test('agent and graph requests preserve the observed profile binding without silently acquiring a newer revision',async()=>{
 const sent=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{sent.push(JSON.parse(options.body));return {ok:true,json:async()=>({})};});
 const binding={provider_profile_id:'openai-account',expected_provider_revision:7};await client.run('session','Task','same-model',binding);await client.graphRun('session','review',2,'Review','same-model',binding);assert.equal(sent[0].provider_profile_id,'openai-account');assert.equal(sent[0].expected_provider_revision,7);assert.equal(sent[1].expected_provider_revision,7);assert.throws(()=>client.run('session','Task','same-model',{provider_profile_id:'openai-account'}));assert.equal(sent.length,2);
 await client.graphRun('session','review',2,'Model-free',undefined,{provider_profile_id:'',expected_provider_revision:0});assert.equal(sent[2].provider_profile_id,'');
});
test('agent file attachments carry only workspace-relative names and the current native authority',async()=>{
 const workspace={configured:true,root:'D:\\TestProj',workspace_id:'windows-local-file-v1:1:testproj',authority_id:'b'.repeat(32)},requests=[];
 const membership={workspace_id:workspace.workspace_id,revision:9,primary_folder_id:workspace.workspace_id,folders:[{folder_id:workspace.workspace_id,root:workspace.root}]};
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>url.endsWith('/v1/workspace')?workspace:url.endsWith('/v1/workspace/membership')?membership:{id:'attached-run'}};});
 const files=[{path:'src/app.cpp',folder_id:workspace.workspace_id,name:'app.cpp'}];const result=await client.run('session','Inspect this file','model',{provider_profile_id:'profile-a',expected_provider_revision:5},undefined,files,membership.revision);
 assert.deepEqual(result,{id:'attached-run'});assert.deepEqual(requests.map(value=>new URL(value.url).pathname),['/v1/workspace','/v1/workspace/membership','/v1/runs']);assert.deepEqual(JSON.parse(requests[2].options.body),{session_id:'session',prompt:'Inspect this file',files,expected_workspace_id:workspace.workspace_id,expected_workspace_authority_id:workspace.authority_id,expected_workspace_revision:membership.revision,model_id:'model',provider_profile_id:'profile-a',expected_provider_revision:5});
 await assert.rejects(client.run('session','Bad attachment','model',undefined,undefined,[{path:'../outside'}]),/workspace-relative|invalid/i);
});
test('graph file attachments carry workspace identity and observed membership revision',async()=>{
 const workspace={configured:true,root:'D:\\TestProj',workspace_id:'windows-local-file-v1:1:testproj',authority_id:'c'.repeat(32)},membership={workspace_id:workspace.workspace_id,revision:12,primary_folder_id:workspace.workspace_id,folders:[{folder_id:workspace.workspace_id,root:workspace.root}]},requests=[];
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{requests.push({url,options});return{ok:true,json:async()=>url.endsWith('/v1/workspace')?workspace:url.endsWith('/v1/workspace/membership')?membership:{id:'graph-run'}};});const files=[{path:'src/app.cpp',folder_id:workspace.workspace_id,name:'app.cpp'}];
 assert.deepEqual(await client.graphRun('session','review',3,'Inspect attached source','model',{provider_profile_id:'profile-a',expected_provider_revision:8},files,membership.revision),{id:'graph-run'});assert.deepEqual(requests.map(value=>new URL(value.url).pathname),['/v1/workspace','/v1/workspace/membership','/v1/graph-runs']);assert.deepEqual(JSON.parse(requests[2].options.body),{session_id:'session',graph_id:'review',graph_revision:3,prompt:'Inspect attached source',files,expected_workspace_id:workspace.workspace_id,expected_workspace_authority_id:workspace.authority_id,expected_workspace_revision:12,model_id:'model',provider_profile_id:'profile-a',expected_provider_revision:8});
 await assert.rejects(client.graphRun('session','review',3,'Stale attachment','model',undefined,files,11),/membership changed/i);
});
const profileState=()=>({revision:1,active:'openai',profiles:[{id:'openai',route_id:'openai.responses',provider:'openai',model:'fixture-model',revision:1}],routes:[{id:'openai.responses',provider:'openai',wire:'responses',discovery:true},{id:'anthropic.messages',provider:'anthropic',wire:'anthropic-messages',discovery:true}]});
test('client accepts only an advertised GenerateContent route and carries its native wire through profile admission',async()=>{
 const {ProviderProfileController,providerEnrollmentWire}=require('../client'),state=profileState();state.active='fixture-gemini';state.profiles.push({id:state.active,route_id:'fixture.custom-gemini',provider:'gemini',model:'fixture-gemini-model',revision:1});state.routes.push({id:'fixture.custom-gemini',provider:'gemini',wire:'gemini-generate-content',discovery:true});
 const requests=[],posted=[],client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>state};}),controller=new ProviderProfileController(client,message=>posted.push(message));
 try{await controller.refresh();assert.deepEqual(controller.state,state);assert.equal(controller.wire('fixture-gemini'),'gemini-generate-content');assert.deepEqual(await controller.admission(),{provider_profile_id:'fixture-gemini',expected_provider_revision:1});assert.equal(requests.length,1);assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/provider/profiles');assert.equal(requests[0].options.headers.Authorization,'Bearer '+token);assert.equal(posted[0].routes.length,3);assert.equal(profileState().routes.some(route=>route.wire==='gemini-generate-content'),false,'Client must not add an unavailable default route');
 assert.throws(()=>providerEnrollmentWire({provider:'gemini',wire:'gemini-generate-content',revision:1,endpoint:'https://generativelanguage.googleapis.com/v1beta'}),/policy/,'Legacy OpenAI enrollment must not acquire a new provider policy');
 for(const bad of [value=>{value.routes[2].wire='gemini_generate_content';},value=>{value.routes[2].api_key='synthetic-private-key';},value=>{value.profiles[1].provider='google';}]){const changed=structuredClone(state);bad(changed);const rejected=new BackendClient('http://localhost:8765',()=>token,async()=>({ok:true,json:async()=>changed}));await assert.rejects(rejected.providerProfiles());}
 }finally{controller.dispose();}
});
test('native provider profile client preserves route, key omission, activation and revision at its authenticated origin',async()=>{
 const requests=[];const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{requests.push({url,options,body:options.body?JSON.parse(options.body):undefined});return {ok:true,json:async()=>url.endsWith('/models')?{models:[{id:'fixture-model'}]}:profileState()};});
 await client.providerProfiles();await client.discoverProfileModels('openai','openai.responses',undefined,1);await client.saveProviderProfile('claude','anthropic.messages','fixture-claude','synthetic-claude-key',1,true);await client.selectProviderProfile('openai',2);
 assert.deepEqual(requests.map(request=>request.url),['/v1/provider/profiles','/v1/provider/profiles/models','/v1/provider/profiles','/v1/provider/profiles/select'].map(path=>'http://127.0.0.1:8765'+path));
 assert.deepEqual(requests[1].body,{id:'openai',route_id:'openai.responses',expected_revision:1});assert.deepEqual(requests[2].body,{id:'claude',route_id:'anthropic.messages',model:'fixture-claude',api_key:'synthetic-claude-key',expected_revision:1,activate:true});assert.deepEqual(requests[3].body,{id:'openai',expected_revision:2});
 assert.ok(requests.every(request=>request.options.headers.Authorization==='Bearer '+token&&request.options.redirect==='error'));assert.ok(!requests.some(request=>request.url.includes('synthetic-claude-key')));
});

test('advertised Gemini Settings discovery keeps full model resources and publishes a profile only after footer selection',async()=>{
 // Host/API fixtures verify the thin adapter. The native backend owns encrypted
 // credential storage; this fixture does not claim encryption or live inference.
 const {ProviderProfileController}=require('../client'),key='synthetic-gemini-settings-key',model='models/fixture-gemini',alternate='models/fixture-gemini-next',requests=[],posted=[];
 let state=profileState();state.routes.push({id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',discovery:true});
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{
  const body=options.body?JSON.parse(options.body):undefined;requests.push({url,options,body});
  if(url.endsWith('/profiles/models'))return {ok:true,json:async()=>({models:[{id:model},{id:alternate}]})};
  if(body){assert.equal(body.route_id,'gemini.generate-content');assert.ok([model,alternate].includes(body.model));assert.equal(body.expected_revision,state.revision);state={...state,revision:state.revision+1,active:body.id,profiles:[state.profiles[0],{id:body.id,route_id:body.route_id,provider:'gemini',model:body.model,revision:state.revision+1}]};}
  return {ok:true,json:async()=>structuredClone(state)};
 }),controller=new ProviderProfileController(client,value=>posted.push(value));
 try{
  await controller.refresh();await controller.discover(key,'','gemini.generate-content');
  assert.equal(requests.length,2,'Discovery cannot save or activate a draft profile');assert.equal(controller.state.active,'openai');
  const discovery=requests[1];assert.equal(discovery.url,'http://127.0.0.1:8765/v1/provider/profiles/models');assert.match(discovery.body.id,/^gemini-[0-9a-f-]{36}$/);assert.deepEqual(discovery.body,{id:discovery.body.id,route_id:'gemini.generate-content',api_key:key,expected_revision:1});
  assert.deepEqual(posted.findLast(value=>value.type==='model-list').models,[{id:model},{id:alternate}]);assert.equal(controller.draft.key,key);assert.ok(Number.isFinite(controller.draft.expires));assert.ok(!JSON.stringify(posted).includes(key),'The renderer receives model metadata without the private draft key');
  await assert.rejects(controller.save('fixture-gemini'),/returned model/);assert.equal(requests.length,2,'A shortened or invented alias cannot replace the discovered resource');
  await controller.save(model);assert.equal(requests[2].options.method,'GET');assert.equal(requests[2].url,'http://127.0.0.1:8765/v1/provider/profiles');assert.equal(requests[2].body,undefined);const saved=requests[3];assert.deepEqual(saved.body,{id:discovery.body.id,route_id:'gemini.generate-content',model,api_key:key,expected_revision:1,activate:true});assert.equal(controller.draft.key,undefined);assert.equal(controller.draft.expires,Infinity);
  assert.equal(controller.state.profiles[1].model,model);assert.deepEqual(await controller.admission(),{provider_profile_id:discovery.body.id,expected_provider_revision:2});assert.equal(controller.wire(discovery.body.id),'gemini-generate-content');assert.deepEqual(posted.findLast(value=>value.type==='model-list'),{type:'model-list',models:[{id:model},{id:alternate}],model});assert.deepEqual(posted.findLast(value=>value.type==='provider-wire'),{type:'provider-wire',wire:'gemini-generate-content'});
  await controller.save(alternate);assert.equal(requests.length,6);assert.equal(requests[4].options.method,'GET');assert.equal(requests[4].url,'http://127.0.0.1:8765/v1/provider/profiles');assert.equal(requests[4].body,undefined);assert.deepEqual(requests[5].body,{id:discovery.body.id,route_id:'gemini.generate-content',model:alternate,expected_revision:2,activate:true});assert.equal(controller.state.profiles[1].model,alternate);assert.equal(controller.state.profiles[0].model,'fixture-model');
  assert.ok(requests.every(request=>request.options.headers.Authorization==='Bearer '+token&&request.options.redirect==='error'));assert.ok(!requests.some(request=>request.url.includes(key)));assert.ok(!JSON.stringify(posted).includes(key));assert.ok(!JSON.stringify(controller.state).includes(key));
 }finally{controller.dispose();}
});

test('Gemini Settings cannot discover through absent, disabled or foreign advertised routes',async()=>{
 const {ProviderProfileController}=require('../client'),key='synthetic-unavailable-gemini-key';
 for(const variant of ['absent','disabled','foreign']){
  const state=profileState(),requests=[],posted=[];let profile='';
  if(variant!=='absent')state.routes.push({id:'gemini.generate-content',provider:'gemini',wire:'gemini-generate-content',discovery:variant!=='disabled'});
  if(variant==='foreign'){profile='saved-gemini';state.profiles.push({id:profile,route_id:'gemini.generate-content',provider:'gemini',model:'models/fixture-gemini',revision:1});}
  const client=new BackendClient('http://localhost:8765',()=>token,async(url)=>{requests.push(url);return {ok:true,json:async()=>structuredClone(state)};}),controller=new ProviderProfileController(client,value=>posted.push(value));
  try{await assert.rejects(controller.discover(key,profile,variant==='foreign'?'openai.responses':'gemini.generate-content'),/available route/);assert.deepEqual(requests,['http://127.0.0.1:8765/v1/provider/profiles']);assert.equal(controller.draft,undefined);assert.ok(!posted.some(value=>value.type==='model-list'));assert.ok(!JSON.stringify(posted).includes(key));}
  finally{controller.dispose();}
 }
});
test('profile metadata rejects secret references, duplicate identities and inconsistent backend route families',async()=>{
 const variants=[state=>{state.credential_id='private';},state=>{state.profiles[0].api_key='private';},state=>{state.profiles.push({...state.profiles[0]});},state=>{state.routes[0].provider='anthropic';},state=>{state.active='unknown';},state=>{state.revision=0;},state=>{state.routes[0].wire='unrecognized';},state=>{state.profiles[0].model='sk-private';},state=>{state.profiles[0].revision=2;}];
 for(const modify of variants){const state=profileState();modify(state);const client=new BackendClient('http://localhost:8765',()=>token,async()=>({ok:true,json:async()=>state}));await assert.rejects(client.providerProfiles());}
 const state=profileState();state.profiles[0].model='';const client=new BackendClient('http://localhost:8765',()=>token,async()=>({ok:true,json:async()=>state}));assert.equal((await client.providerProfiles()).profiles[0].model,'','Trusted key-only migration metadata must remain repairable');
});
test('invalid profile mutations never leave the client and discovery rejects key reflection',async()=>{
 let sent=0;const client=new BackendClient('http://localhost:8765',()=>token,async()=>{sent++;return {ok:true,json:async()=>({models:[{id:'prefix-fixture-private-key'}]})};});
 await assert.rejects(client.selectProviderProfile('../profile?',1));await assert.rejects(client.selectProviderProfile('openai',Number.MAX_SAFE_INTEGER+1));await assert.rejects(client.saveProviderProfile('openai','openai.responses','fixture-model','key with space',0));await assert.rejects(client.saveProviderProfile('openai','openai.responses','fixture-private-key','fixture-private-key',0));assert.equal(sent,0);
 await assert.rejects(client.discoverProfileModels('openai','openai.responses','fixture-private-key',0));assert.equal(sent,1);
});
test('graph access adapter preserves backend identity, revision and raw human JSON',async()=>{
 const requests=[];const client=new BackendClient('http://127.0.0.1:8765',()=>token,async(url,options)=>{requests.push({url,body:options.body?JSON.parse(options.body):undefined});return {ok:true,json:async()=>({})};});
 await client.graphRun('session','workflow',3,'Task');await client.graphInput('root','answer.step','{"answer":1,"answer":2}',7);await client.graphChildHistory('root','child/opaque');
 assert.deepEqual(requests[0].body,{session_id:'session',graph_id:'workflow',graph_revision:3,prompt:'Task'});assert.deepEqual(requests[1].body,{input_json:'{"answer":1,"answer":2}',expected_checkpoint_revision:7});assert.ok(requests[2].url.endsWith('/v1/graph-runs/root/children/child%2Fopaque/history'));
});

test('session discovery, URL encoding, error handling and origin boundaries', async () => {
  const server = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json');
    if (req.headers.authorization !== `Bearer ${token}`) { res.statusCode = 401; res.end('{"detail":"Authentication required"}'); return; }
    if (req.url === '/v1/sessions') res.end(JSON.stringify([{ id: 'session-1', title: '<script>untrusted</script>' }]));
    else if (req.url === '/v1/runs/run%2F1/events?after=12') res.end(JSON.stringify([{ seq: 13, kind: 'run.completed' }]));
    else { res.statusCode = 409; res.end(JSON.stringify({ detail: 'Session already active' })); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const client = new BackendClient(`http://127.0.0.1:${server.address().port}`, () => token);
    assert.equal((await client.sessions())[0].title, '<script>untrusted</script>');
    assert.equal((await client.events('run/1', 12))[0].seq, 13);
    await assert.rejects(client.cancel('active'), /Session already active/);
    assert.throws(() => new BackendClient('https://remote.example'), /loopback/);
    assert.throws(() => new BackendClient('http://user:password@localhost'), /loopback/);
  } finally { await new Promise(resolve => server.close(resolve)); }
});

test('origin-scoped authentication and native request bodies', async () => {
  let current = token;
  const requests = [];
  const client = new BackendClient('http://localhost:8765', async () => current, async (url,options) => {
    requests.push({url,options});return {ok:true,json:async()=>({state:'queued'})};
  });
  await client.run('session','Real prompt');
  assert.equal(requests[0].url,'http://127.0.0.1:8765/v1/runs');
  assert.equal(requests[0].options.headers.Authorization,`Bearer ${token}`);
  assert.deepEqual(JSON.parse(requests[0].options.body),{session_id:'session',prompt:'Real prompt'});
  current = 'rotated-native-client-token-32-bytes';
  await client.cancel('run');
  assert.equal(requests[1].options.headers.Authorization,`Bearer ${current}`);
  assert.equal(requests[1].options.body,'{}');
  assert.equal(requests[1].options.redirect,'error');
  current = 'invalid';
  await assert.rejects(client.sessions(),/authentication token/);
  assert.equal(requests.length,2,'Invalid token must not leave the extension host');
  for (const url of ['http://localhost/path','http://localhost?query=1','http://localhost/#fragment','http://[::1]:8765']) {
    assert.throws(()=>new BackendClient(url,()=>token),/origin/);
  }
});

test('Local/Nexus connection catalog validates metadata and uses revision-checked public writes',async()=>{
 const {validateConnectionProfiles}=require('../client'),requests=[];let state={revision:1,active_id:'local',profiles:[{id:'local',name:'Local',kind:'local',origin:'',revision:1,status:'connected',command_routing:true,authenticated:true}]};
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{
  const body=options.body?JSON.parse(options.body):undefined;requests.push({url,options,body});
  if(!body)return{ok:true,json:async()=>structuredClone(state)};
  assert.equal(body.expected_revision,state.revision);assert.ok(!Object.hasOwn(body,'access_token'));
  const old=new Map(state.profiles.map(value=>[value.id,value]));state={revision:state.revision+1,active_id:state.active_id,profiles:body.profiles.map(value=>value.id==='local'?{...old.get('local'),name:value.name}:{...value,revision:old.get(value.id)?.revision||state.revision+1,status:'not_connected',command_routing:false,authenticated:false})};
  return{ok:true,json:async()=>structuredClone(state)};
 });
 assert.deepEqual(await client.connectionProfiles(),state);
 const saved=await client.saveConnectionProfile('engineering','CantorAI Nexus','https://nexus.example.com/xmind');assert.equal(saved.revision,2);assert.equal(saved.active_id,'local');assert.equal(saved.profiles[1].status,'not_connected');
 assert.deepEqual(requests.map(value=>[new URL(value.url).pathname,value.options.method]),[['/v1/connections','GET'],['/v1/connections','GET'],['/v1/connections','POST']]);assert.deepEqual(requests[2].body,{profiles:[{id:'local',name:'Local',kind:'local',origin:''},{id:'engineering',name:'CantorAI Nexus',kind:'nexus',origin:'https://nexus.example.com/xmind'}],expected_revision:1});
 const removed=await client.removeConnectionProfile('engineering');assert.equal(removed.profiles.length,1);assert.equal(removed.revision,3);
 assert.throws(()=>validateConnectionProfiles({...state,profiles:[...state.profiles,{id:'not-ready',name:'Not ready',kind:'nexus',origin:'https://nexus.example.com',revision:1,status:'connected',command_routing:true,authenticated:true}]}),/Nexus connection state/);
 await assert.rejects(client.saveConnectionProfile('bad','Bad','http://nexus.example.com'),/HTTPS server URL/);await assert.rejects(client.removeConnectionProfile('local'),/cannot be removed/);
});


test('graph stream validates pinned invocation ancestry before delivering a deep child event',async()=>{
 const leafSpec={nodes:[{id:'worker',type:'agent'}]},root={run:streamRoot({graph_root:true}),spec:{nodes:[{id:'call',type:'graph',graph_id:'child-flow',graph_revision:1,graph_spec:leafSpec}]}},child=streamRoot({id:'graph-child',parent_id:'root',node_id:'call',graph_root:true}),nested={run:child,graph_id:'child-flow',graph_revision:1,spec:leafSpec},leaf=streamRoot({id:'leaf',parent_id:'graph-child',node_id:'worker'}),events=[streamEvent(1,'leaf')],observed=[];
 const client=new BackendClient('http://localhost:8765',()=>token,async url=>{const path=new URL(url).pathname;if(path.endsWith('/events/stream'))return streamResponse(streamWire(events,{scope:'graph',run:streamRoot({graph_root:true})}));return new Response(JSON.stringify(path.endsWith('/root/children')?[child]:path.endsWith('/graph-child/children')?[leaf]:path.endsWith('/root')?root:nested));});
 const result=await client.eventStream('root',{scope:'graph',session_id:'session',onEvent:event=>observed.push(event)});assert.deepEqual(observed,events);assert.equal(result.cursor,1);
});

test('nested graph input keeps owner identity both revisions and raw JSON on its versioned route',async()=>{
 const calls=[],client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-client-token-32-bytes-long',async(url,options)=>{calls.push({url,options});return {ok:true,json:async()=>({})};});await client.graphTreeInput('root','child','answer','{"x":1,"x":2}',4,99);assert.equal(new URL(calls[0].url).pathname,'/v1/graph-runs/root/invocations/child/human/answer');assert.deepEqual(JSON.parse(calls[0].options.body),{input_json:'{"x":1,"x":2}',expected_checkpoint_revision:4,expected_owner_checkpoint_revision:99});
});
function capturedAdmissionFixture({supported=true,failRun=false,mismatch=false}={}){
 const identity={connection_id:'local',profile_revision:1,server_id:'local-'+'a'.repeat(64),backend_generation:'fixture-generation',protocol_version:1,kind:'local'},handle='1'.repeat(32),requests=[];
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-captured-client-token-32-bytes',async(url,options)=>{
  const path=new URL(url).pathname,input=options.body?JSON.parse(options.body):undefined;requests.push({path,method:options.method,input});let data,status=200;
  if(path==='/v1/health')data=supported?{status:'ok',local_connection_admission:1}:{status:'ok'};
  else if(path==='/v1/connections')data={revision:1,active_id:'local',profiles:[{id:'local',name:'Local',kind:'local',origin:'',revision:1,status:'connected',command_routing:true,authenticated:true}]};
  else if(path==='/v1/connection-bindings')data={binding_id:handle,identity};
  else if(path.endsWith('/sessions')&&path.includes(handle))data={binding:identity,session:{id:input.id,title:input.title,parent_id:''}};
  else if(path.endsWith('/runs')&&path.includes(handle)){if(failRun){status=409;data={detail:'Captured connection profile revision changed'};}else data={binding:mismatch?{...identity,server_id:'local-'+'b'.repeat(64)}:identity,run:{id:input.id,session_id:input.session_id,state:'queued',parent_id:'',node_id:'',graph_root:false}};}
  else if(path==='/v1/connection-bindings/'+handle&&options.method==='DELETE')data={closed:true};
  else if(path==='/v1/sessions')data={id:'legacy-session',title:input.title};
  else throw new Error('Unexpected fixture route '+path);
  return {ok:status<400,status,json:async()=>structuredClone(data)};
 });return {client,requests,identity,handle};
}
test('captured Local admission is lazy, preserves full prompt options and carries workspace guard fields',async()=>{
 const {client,requests,identity,handle}=capturedAdmissionFixture();await client.health();assert.deepEqual(requests.map(r=>r.path),['/v1/health']);
 client.bindWorkspace({prepare:async()=>({origin:client.baseUrl,fields:{expected_workspace_id:'fixture-workspace',expected_workspace_authority_id:'a'.repeat(32),expected_workspace_revision:9}}),assert:()=>{}});
 const session=await client.createSession('Captured conversation');assert.match(session.id,/^[a-f0-9]{32}$/);assert.equal(session.title,'Captured conversation');
 client.workspace=async()=>({configured:true,workspace_id:'fixture-workspace',authority_id:'a'.repeat(32)});client.workspaceMembership=async()=>({revision:9});
 const files=[{path:'input.txt',folder_id:'primary',name:'input.txt',description:'Observed attachment',mention:'@input'}];const run=await client.run(session.id,'Observed prompt','selected-model',{provider_profile_id:'selected-provider',expected_provider_revision:7},'queued-input',files,9);assert.match(run.id,/^[a-f0-9]{32}$/);
 const sent=requests.find(r=>r.path==='/v1/connection-bindings/'+handle+'/runs').input;assert.equal(sent.session_id,session.id);assert.equal(sent.model_id,'selected-model');assert.equal(sent.provider_profile_id,'selected-provider');assert.equal(sent.expected_provider_revision,7);assert.equal(sent.inbox_id,'queued-input');assert.equal(sent.expected_workspace_revision,9);assert.equal(sent.expected_workspace_authority_id,'a'.repeat(32));assert.deepEqual(sent.files,files);assert.equal(requests.filter(r=>r.path==='/v1/connection-bindings').length,1);assert.equal(requests.some(r=>r.path==='/v1/runs'),false);
 await Promise.all([client.closeConnection(),client.closeConnection()]);assert.equal(requests.filter(r=>r.method==='DELETE').length,1);assert.equal(identity.connection_id,'local');
});
test('stale or mismatched captured admission never retries through a new or legacy context',async()=>{
 for(const settings of [{failRun:true},{mismatch:true}]){
  const {client,requests}=capturedAdmissionFixture(settings);await client.health();let failure;try{await client.run('session','Observed prompt');}catch(error){failure=error;}assert(failure);assert.match(failure.admissionId,/^[a-f0-9]{32}$/);assert.equal(failure.connectionBinding.connection_id,'local');
  assert.equal(requests.filter(r=>r.path==='/v1/connection-bindings').length,1);assert.equal(requests.filter(r=>r.path.endsWith('/runs')).length,1);assert.equal(requests.some(r=>r.path==='/v1/runs'),false);await client.closeConnection();const before=requests.length;await assert.rejects(client.run('session','Must reconnect'),/closed/);assert.equal(requests.length,before);
 }
});
test('older Local backend health keeps the existing session path without a binding request',async()=>{
 const {client,requests}=capturedAdmissionFixture({supported:false});await client.health();assert.deepEqual(await client.createSession('Existing Local flow'),{id:'legacy-session',title:'Existing Local flow'});assert.deepEqual(requests.map(r=>r.path),['/v1/health','/v1/sessions']);
});

test('captured operations require explicit scope and reject mismatched receipts without legacy retry',async()=>{
 const {client,requests,identity,handle}=capturedAdmissionFixture();const baseFetch=client.fetch;let mismatch=false,missing=false;
 client.fetch=async(url,options)=>{const path=new URL(url).pathname;if(path==='/v1/health')return {ok:true,status:200,json:async()=>missing?{local_connection_admission:1}:{local_connection_admission:1,local_connection_operations:1}};
 if(path.includes('/operations')){requests.push({path,method:options.method,input:options.body?JSON.parse(options.body):undefined});const operation={id:'operation',run_id:mismatch?'foreign':'run',arguments_json:'{"x": 1}',state:options.body?'ready':'awaiting_approval'};return {ok:true,status:200,json:async()=>path.endsWith('/operations')?{binding:identity,session_id:'session',run_id:'run',operations:[operation]}:{binding:identity,operation}};}return baseFetch(url,options);};
 await client.health();assert.equal((await client.operations('run','session')).length,1);assert.equal((await client.operation('operation',{session_id:'session',run_id:'run'})).arguments_json,'{"x": 1}');assert.equal((await client.decide('operation','allow',{session_id:'session',run_id:'run'})).state,'ready');
 const decision=requests.find(r=>r.path.endsWith('/decision'));assert.equal(decision.path,'/v1/connection-bindings/'+handle+'/sessions/session/runs/run/operations/operation/decision');assert.deepEqual(decision.input,{decision:'allow'});
 const before=requests.length;await assert.rejects(client.operation('operation'),/identity/i);assert.equal(requests.length,before);
 mismatch=true;await assert.rejects(client.operations('run','session'),/another run/);await assert.rejects(client.decide('operation','deny',{session_id:'session',run_id:'run'}),/another run/);assert.equal(requests.some(r=>r.path==='/v1/operations/operation/decision'),false);
 missing=true;await assert.rejects(client.health(),/protocol changed/);await client.closeConnection();await assert.rejects(client.operations('run','session'),/closed/);
});
