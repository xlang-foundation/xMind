// Synthetic inference protocol only. The tested native runner executes actual
// workspace reads. This fixture is not live-model validation or a product path.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [executable,modules,stdlib,...extra]=process.argv.slice(2),rustAnalyzer=extra.length===2?extra[0]:undefined,workerExecutable=extra.at(-1),execute=promisify(execFile);
const folder=await mkdtemp(join(tmpdir(),'xmind-agent-')),workspace=join(folder,'workspace');
let failure,requests=0,skillRequests=0,skillCapacityRequests=0,skillFollowRequests=0,manualRequests=0,clearRequests=0,pythonRequests=0,pythonWorkerRequests=0,pythonFilteredRequests=0;
const server=createServer((request,response)=>{
  let source='';request.on('data',data=>{source+=data;});request.on('end',()=>{
    try {
      requests++;assert.equal(request.headers.authorization,'Bearer engine-protocol-test-not-a-real-key');
      assert.ok(!source.includes('engine-protocol-test-not-a-real-key'));
      const body=JSON.parse(source);assert.equal(body.model,'fixture-deployment');assert.equal(body.stream,true);
      const expectedTools=request.url==='/python-tool-filtered'?['read_file']:['read_repository_instructions','read_file','list_files','search_files','find_files','glob_files','list_skills','load_skill'];if(request.url==='/python-tool'||request.url==='/python-tool-worker')expectedTools.push('python_uppercase');if(request.url.startsWith('/multi-folder-process'))expectedTools.push('run_process','wait_process','stop_process');if(request.url.startsWith('/multi-folder-patch')||request.url.startsWith('/multi-root-patch')||request.url==='/blocked-effect')expectedTools.push('edit_file','create_file','apply_patch');if(request.url.startsWith('/multi-folder-lsp'))expectedTools.push('language_server');
      assert.deepEqual(body.tools.map(item=>item.function.name),expectedTools);
      if(request.url==='/error') {response.writeHead(429,{'Content-Type':'application/json'});response.end('{"error":"do-not-log-provider-body"}');return;}
      response.writeHead(200,{'Content-Type':'text/event-stream'});
      if(request.url==='/blocked-effect'){
        const previous=body.messages.at(-1);let delta,finish;
        if(previous.role==='user'){
          delta={tool_calls:[{index:0,id:'blocked-edit',type:'function',function:{name:'edit_file',arguments:JSON.stringify({path:'README.md',old_text:'Actual file content written by fixture\n',new_text:'must remain unchanged\n'})}}]};finish='tool_calls';
        }else{
          assert.equal(previous.role,'tool');const result=JSON.parse(previous.content);assert.equal(result.error.code,'effect_resource_uncertain',JSON.stringify(result));assert.match(result.error.message,/uncertain operation blocker-operation/);
          delta={content:'Synthetic model reported that the prior process operation must be inspected before editing.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url.startsWith('/multi-folder-lsp-later')){
        assert.ok(rustAnalyzer,'The model-visible language-server contract requires the real Rust Analyzer executable');
        const folderId=new URL(request.url,'http://fixture.local').searchParams.get('folder_id');assert.ok(folderId,'The subsequent model turn must select the newly admitted folder');
        const systemMessage=body.messages.find(item=>['system','developer'].includes(item.role));assert.ok(systemMessage?.content.includes(`folder_id=${folderId}`),'The later-run system context must contain the newly admitted folder ID');
        const previous=body.messages.at(-1);let delta,finish;
        if(previous.role==='user'){
          delta={tool_calls:[{index:0,id:'newly-admitted-language-server',type:'function',function:{name:'language_server',arguments:JSON.stringify({server_id:'rust-analyzer',method:'documentSymbol',path:'src/lib.rs',folder_id:folderId})}}]};finish='tool_calls';
        }else{
          assert.equal(previous.role,'tool');const result=JSON.parse(previous.content);assert.equal(result.folder_id,folderId);assert.equal(result.result[0].name,'late_symbol',JSON.stringify(result));
          delta={content:'Synthetic model verified the new folder became available in the following admitted run.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url.startsWith('/multi-folder-lsp')){
        assert.ok(rustAnalyzer,'The model-visible language-server contract requires the real Rust Analyzer executable');
        const route=new URL(request.url,'http://fixture.local'),folderId=route.searchParams.get('folder_id'),lateFolderId=route.searchParams.get('late_folder_id');assert.ok(folderId&&lateFolderId,'Native LSP run must bind both admitted and subsequently attached folder identities into the provider route');
        const systemMessage=body.messages.find(item=>['system','developer'].includes(item.role));assert.ok(systemMessage?.content.includes(`folder_id=${folderId}`),'The admitted folder ID must be visible to the model');
        const tool=body.tools.find(item=>item.function.name==='language_server');assert.ok(tool?.function.parameters.properties.folder_id,'The model-visible LSP schema must allow an admitted folder ID');
        const previous=body.messages.at(-1);let delta,finish;
        const turn=body.messages.filter(item=>item.role==='assistant'&&item.tool_calls?.some(call=>call.function?.name==='language_server')).length;
        if(previous.role==='user'){
          delta={tool_calls:[{index:0,id:'attached-language-server',type:'function',function:{name:'language_server',arguments:JSON.stringify({server_id:'rust-analyzer',method:'documentSymbol',path:'src/lib.rs',folder_id:folderId})}}]};finish='tool_calls';
        }else if(turn===1){
          assert.equal(previous.role,'tool');const result=JSON.parse(previous.content);assert.equal(result.folder_id,folderId);assert.equal(result.result[0].name,'attached_symbol',JSON.stringify(result));
          delta={tool_calls:[{index:0,id:'late-language-server',type:'function',function:{name:'language_server',arguments:JSON.stringify({server_id:'rust-analyzer',method:'documentSymbol',path:'src/lib.rs',folder_id:lateFolderId})}}]};finish='tool_calls';
        }else{
          assert.equal(turn,2);assert.equal(previous.role,'tool');assert.equal(JSON.parse(previous.content).error.code,'access_denied','The active run must reject membership attached after admission');
          delta={content:'Synthetic model verified real Rust Analyzer symbols from the admitted attached folder.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url.startsWith('/multi-folder-patch')){
        const folderId=new URL(request.url,'http://fixture.local').searchParams.get('folder_id');assert.ok(folderId,'Native patch fixture must bind the secondary folder ID into the provider route');
        const systemMessage=body.messages.find(item=>['system','developer'].includes(item.role));assert.ok(systemMessage?.content.includes(`folder_id=${folderId}`),'Agent context must disclose the admitted folder identity to the coding agent');
        const patchTool=body.tools.find(item=>item.function.name==='apply_patch');assert.ok(patchTool?.function.parameters.properties.folder_id,'Agent patch schema must expose explicit folder selection');
        const previous=body.messages.at(-1);let delta,finish;
        if(previous.role==='user'){
          delta={tool_calls:[{index:0,id:'attached-patch',type:'function',function:{name:'apply_patch',arguments:JSON.stringify({patch_text:'*** Begin Patch\n*** Add File: agent-patches/secondary.txt\n+attached agent patch\n*** End Patch\n',create_parents:true,folder_id:folderId})}}]};finish='tool_calls';
        }else{
          assert.equal(previous.role,'tool');const result=JSON.parse(previous.content);assert.equal(result.state,'succeeded',JSON.stringify(result));assert.equal(result.succeeded_files,1,JSON.stringify(result));
          delta={content:'Synthetic model observed the completed approved patch in the attached folder.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url.startsWith('/multi-root-patch')){
        const route=new URL(request.url,'http://fixture.local'),primaryId=route.searchParams.get('primary_id'),attachedId=route.searchParams.get('attached_id');assert.ok(primaryId&&attachedId&&primaryId!==attachedId,'Cross-folder patch fixture must bind two distinct admitted roots');
        const patchTool=body.tools.find(item=>item.function.name==='apply_patch');assert.ok(patchTool?.function.parameters.properties.patches?.items?.properties?.folder_id,'Coding tool schema must expose per-entry folder identity');const denied=request.url.startsWith('/multi-root-patch-deny');
        const previous=body.messages.at(-1);let delta,finish;
        if(previous.role==='user'){
          const prefix=denied?'partial-':'';const textFor=(name,content)=>'*** Begin Patch\n*** Add File: agent-patches/'+prefix+name+'.txt\n+'+content+'\n*** End Patch\n';
          const patches=[{folder_id:primaryId,patch_text:textFor('primary','primary-root patch'),create_parents:true},{folder_id:attachedId,patch_text:textFor('attached','attached-root patch'),create_parents:true}];
          delta={tool_calls:[{index:0,id:'cross-root-patch',type:'function',function:{name:'apply_patch',arguments:JSON.stringify({patches})}}]};finish='tool_calls';
        }else{
          assert.equal(previous.role,'tool');const report=JSON.parse(previous.content);assert.equal(report.requested_folders,2);assert.deepEqual(report.folder_results.map(value=>value.folder_id),[primaryId,attachedId]);if(denied){assert.equal(report.state,'partial',JSON.stringify(report));assert.equal(report.stop_reason,'permission_denied');assert.equal(report.succeeded_files,1);}else{assert.equal(report.state,'succeeded',JSON.stringify(report));assert.equal(report.succeeded_files,2);}
          delta={content:'Synthetic model verified the reviewed patch outcomes in both admitted workspace folders.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url.startsWith('/multi-folder-process')){
        const folderId=new URL(request.url,'http://fixture.local').searchParams.get('folder_id');assert.ok(folderId,'Native process fixture must bind the secondary folder ID into the provider route');
        const systemMessage=body.messages.find(item=>['system','developer'].includes(item.role));assert.ok(systemMessage?.content.includes(`folder_id=${folderId}`),'Agent context must disclose the admitted folder identity to the coding agent');
        const processTool=body.tools.find(item=>item.function.name==='run_process');assert.ok(processTool?.function.parameters.properties.folder_id,'Agent must receive explicit folder selection in the process tool schema');
        const previous=body.messages.at(-1);let delta,finish;
        if(previous.role==='user'){
          delta={tool_calls:[{index:0,id:'attached-process',type:'function',function:{name:'run_process',arguments:JSON.stringify({profile:'powershell',arguments:["[IO.File]::WriteAllText('agent-process-folder.txt',(Get-Location).Path)"],workdir:'.',folder_id:folderId})}}]};finish='tool_calls';
        }else{
          assert.equal(previous.role,'tool');const result=JSON.parse(previous.content);assert.equal(result.exit_code,0);assert.ok(result.operation_id&&result.process_tree_retired);
          delta={content:'Synthetic model observed the successful process in the captured attached folder.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url.startsWith('/multi-folder')){
        const folderId=new URL(request.url,'http://fixture.local').searchParams.get('folder_id');assert.ok(folderId,'Native fixture must bind the secondary folder ID into the provider route');
        const systemMessage=body.messages.find(item=>['system','developer'].includes(item.role));assert.ok(systemMessage?.content.includes(`folder_id=${folderId}`),'Agent context must disclose the exact admitted folder IDs so it can select an attached folder: '+String(systemMessage?.content));
        assert.ok(body.tools.find(item=>item.function.name==='read_file').function.parameters.properties.folder_id,'Agent tool schema must expose explicit folder selection');
        const previous=body.messages.at(-1);let delta,finish;
        if(previous.role==='user'){
          delta={tool_calls:[{index:0,id:'attached-read',type:'function',function:{name:'read_file',arguments:JSON.stringify({path:'README.md',folder_id:folderId})}}]};finish='tool_calls';
        }else{
          assert.equal(previous.role,'tool');const result=JSON.parse(previous.content);assert.equal(result.content,'Attached folder content\n');assert.equal(result.folder_id,folderId);assert.equal(result.workspace_revision,2);
          delta={content:'Synthetic model verified the registered secondary folder read.'};finish='stop';
        }
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/manual-attached'){
        const turn=++manualRequests;assert.ok(body.messages[0].content.includes('Synthetic user-only active body'));assert.ok(body.messages[0].content.includes('"activation_origin":"user"'));let delta,finish;
        if(turn===1){assert.equal(body.messages.at(-1).role,'user');delta={tool_calls:[{index:0,id:'manual-companion-read',type:'function',function:{name:'read_file',arguments:'{"path":".agents/skills/manual/helper.txt"}'}}]};finish='tool_calls';}
        else{assert.equal(turn,2);assert.equal(JSON.parse(body.messages.at(-1).content).content,'Actual user-only companion bytes\n');delta={content:'Synthetic actual manual companion conclusion.'};finish='stop';}
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/manual-cleared'){
        assert.equal(++clearRequests,1);assert.ok(!body.messages[0].content.includes('Synthetic user-only active body'));assert.ok(!body.messages[0].content.includes('"activation_origin":"user"'));assert.ok(body.messages[0].content.includes('"active_skills":[]'));response.write('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic removal boundary checked.'},finish_reason:'stop'}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/skill-capacity'){
        const turn=++skillCapacityRequests;assert.ok(body.messages[0].content.length<=65536);assert.ok(!body.messages[0].content.includes('Synthetic oversized active skill body'));
        let delta,finish;if(turn===1){delta={tool_calls:[{index:0,id:'reject-large-skill',type:'function',function:{name:'load_skill',arguments:'{"id":"large"}'}}]};finish='tool_calls';}
        else {assert.equal(turn,2);assert.equal(JSON.parse(body.messages.at(-1).content).error.code,'file_unavailable');delta={content:'Synthetic model continued after the rejected guide load.'};finish='stop';}
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/skill'){
        const turn=++skillRequests;let delta,finish;
        if(turn===1){assert.ok(body.messages[0].content.includes('Synthetic skill catalogue description'));assert.ok(!body.messages[0].content.includes('Synthetic active skill body'));
          delta={tool_calls:[{index:0,id:'load-native-skill',type:'function',function:{name:'load_skill',arguments:'{"id":"inspect"}'}},{index:1,id:'read-before-skill-delivery',type:'function',function:{name:'read_file',arguments:'{"path":".agents/skills/inspect/helper.txt"}'}}]};finish='tool_calls';
        }else if(turn===2){assert.ok(body.messages[0].content.includes('Synthetic active skill body'));const results=body.messages.filter(message=>message.role==='tool').map(message=>JSON.parse(message.content));assert.equal(results[0].activation,'requested_for_next_model_request');assert.equal(results[0].effect_permission,false);assert.equal(results[1].error.code,'repository_instructions_required');
          delta={tool_calls:[{index:0,id:'read-after-skill-delivery',type:'function',function:{name:'read_file',arguments:'{"path":".agents/skills/inspect/helper.txt"}'}}]};finish='tool_calls';
        }else {assert.equal(turn,3);assert.equal(JSON.parse(body.messages.at(-1).content).content,'Actual native skill companion bytes\n');delta={content:'Synthetic skill conclusion after the actual native companion read.'};finish='stop';}
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.write('data: '+JSON.stringify({choices:[],usage:{prompt_tokens:8,completion_tokens:5,total_tokens:13}})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/skill-follow-up'){
        const turn=++skillFollowRequests;assert.ok(body.messages[0].content.includes('Synthetic active skill body'));let delta,finish;
        if(turn===1){assert.equal(body.messages.at(-1).role,'user');delta={tool_calls:[{index:0,id:'continued-skill-read',type:'function',function:{name:'read_file',arguments:'{"path":".agents/skills/inspect/helper.txt"}'}}]};finish='tool_calls';}
        else{assert.equal(turn,2);assert.equal(JSON.parse(body.messages.at(-1).content).content,'Actual native skill companion bytes\n');delta={content:'Synthetic continued skill conclusion after actual companion read.'};finish='stop';}
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/python-tool'||request.url==='/python-tool-worker'){
        const turn=request.url==='/python-tool'?++pythonRequests:++pythonWorkerRequests;let delta,finish;
        if(turn===1){delta={tool_calls:[{index:0,id:'actual-xlang-call',type:'function',function:{name:'python_uppercase',arguments:'{"text":"xMind"}'}}]};finish='tool_calls';}
        else{assert.equal(turn,2);assert.equal(body.messages.at(-1).role,'tool');assert.deepEqual(JSON.parse(body.messages.at(-1).content),{value:'XMIND'});delta={content:'Synthetic model observed actual embedded xlang3 tool result.'};finish='stop';}
        response.write('data: '+JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/python-tool-filtered'){
        assert.equal(++pythonFilteredRequests,1);response.write('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic filtered tool remained unavailable.'},finish_reason:'stop'}]})+'\n\n');response.end('data: [DONE]\n\n');return;
      }
      if(request.url==='/claim'||request.url==='/delay') {response.write('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic pending stream'}}]})+'\n\n');return;}
      const last=body.messages.at(-1);
      if(last.role==='tool') {
        const result=JSON.parse(last.content);
        if(request.url==='/denied') assert.equal(result.error.code,'access_denied');
        else if(request.url==='/agent-permission-deny') assert.equal(result.error.code,'agent_permission_denied');
        else assert.equal(result.content,'Actual file content written by fixture\n');
        response.write('data: '+JSON.stringify({choices:[{index:0,delta:{content:'Synthetic inference peer accepted the actual tool result.'},finish_reason:'stop'}]})+'\n\n');
      } else {
        const path=request.url==='/denied'?'../outside.txt':'README.md';
        response.write('data: '+JSON.stringify({choices:[{index:0,delta:{tool_calls:[{index:0,id:`call-${requests}`,type:'function',function:{name:'read_file',arguments:JSON.stringify({path})}}]},finish_reason:'tool_calls'}]})+'\n\n');
      }
      response.write('data: '+JSON.stringify({choices:[],usage:{prompt_tokens:8,completion_tokens:5,total_tokens:13}})+'\n\n');response.end('data: [DONE]\n\n');
    } catch(error) {failure=error;if(!response.headersSent)response.writeHead(400);response.end();}
  });
});
try {
  await mkdir(workspace);await writeFile(join(workspace,'README.md'),'Actual file content written by fixture\n');await mkdir(join(folder,'attached'));await writeFile(join(folder,'attached','README.md'),'Attached folder content\n');await mkdir(join(folder,'late-attached'));
  await writeFile(join(workspace,'fixture_tools.py'),
    "def configure(xmind):\n"
    +"    xmind.tool({'name': 'python_uppercase', 'description': 'Convert the supplied text to uppercase', 'input_schema': {'type': 'object', 'properties': {'text': {'type': 'string'}}, 'required': ['text'], 'additionalProperties': False}, 'module': 'fixture_tools', 'function': 'uppercase'})\n"
    +"def uppercase(args):\n    return {'value': args['text'].upper()}\n");
  await mkdir(join(workspace,'.agents','skills','inspect'),{recursive:true});await writeFile(join(workspace,'.agents','skills','inspect','SKILL.md'),'---\nname: inspect\ndescription: Synthetic skill catalogue description\n---\nSynthetic active skill body: inspect the actual companion bytes.\n');await writeFile(join(workspace,'.agents','skills','inspect','helper.txt'),'Actual native skill companion bytes\n');
  await mkdir(join(workspace,'.agents','skills','large'));await writeFile(join(workspace,'.agents','skills','large','SKILL.md'),'---\nname: large\ndescription: Synthetic instruction budget fixture\n---\nSynthetic oversized active skill body '+ 'x'.repeat(14500));
  await mkdir(join(workspace,'.agents','skills','manual'));await writeFile(join(workspace,'.agents','skills','manual','SKILL.md'),'---\nname: User-only guide\ndisable-model-invocation: true\n---\nSynthetic user-only active body: inspect actual companion bytes.\n');await writeFile(join(workspace,'.agents','skills','manual','helper.txt'),'Actual user-only companion bytes\n');
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let result;try{result=await execute(executable,[join(folder,'state.sqlite'),modules,stdlib,workspace,`http://127.0.0.1:${server.address().port}`,...(rustAnalyzer?[rustAnalyzer]:[]),workerExecutable],{windowsHide:true,timeout:60000});}catch(error){if(failure)throw failure;throw error;}
  if(failure) throw failure;assert.equal(requests,rustAnalyzer?43:38);assert.equal(skillRequests,3);assert.equal(skillCapacityRequests,2);assert.equal(skillFollowRequests,2);assert.equal(manualRequests,2);assert.equal(clearRequests,1);assert.equal(pythonRequests,2);assert.equal(pythonWorkerRequests,2);assert.equal(pythonFilteredRequests,1);process.stdout.write(result.stdout);
} finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));await rm(folder,{recursive:true,force:true});}
