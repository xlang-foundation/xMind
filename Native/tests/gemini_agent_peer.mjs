// Independent synthetic GenerateContent socket peer. The native child owns
// agent state, real file reads, credentials and embedded-xlang3 SQLite storage.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,basename,resolve} from 'node:path';
const [executable,modules,stdlib]=process.argv.slice(2);assert.ok(executable&&modules&&stdlib,'Pass native agent contract executable and runtime module/source roots');
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-gemini-agent-')),workspace=join(root,'workspace');
const key='synthetic-gemini-agent-key-not-live',firstFile='Actual native Gemini file bytes: "quoted" and Unicode 雪\n',secondFile='Second actual native Gemini file bytes\n';
const finalAnswer='Synthetic provider checked the native file reads 🌍.',reopenedAnswer='Synthetic provider accepted the SQLite-restored signed conversation.';
const suffix='/v1beta/models/fixture-gemini:streamGenerateContent?alt=sse',preciseMetadata='{"precision":1.2345678901234567890123456789,"large":18446744073709551615}',firstArguments='{"pa\\u0074h":"README.md"}';
const parts=`[{"text":"Synthetic hidden thought","thought":true,"thoughtSignature":"aGlkZGVuLXNpZ25hdHVyZQ=="},{"functionCall":{"name":"read_file","id":"provider-identified-call","args":${firstArguments}},"thoughtSignature":"Y2FsbC1zaWduYXR1cmU=","partMetadata":${preciseMetadata}},{"thoughtSignature":"c2lnbmF0dXJlLW9ubHk=","thought":false},{"functionCall":{"name":"read_file","args":{"path":"second.txt"}},"thought":false}]`;
const finalParts=[{text:finalAnswer},{thoughtSignature:'ZmluYWwtc2lnbmF0dXJl'}];let failure;const routes=[];let mainRequests=0;
function reply(response,rawParts,finish,usage,responseId){
 const candidate=`{"index":0,"content":{"role":"model","parts":${rawParts}}${finish?`,"finishReason":"${finish}"`:''}}`;
 const wire=Buffer.from(`data: {"candidates":[${candidate}],"usageMetadata":${JSON.stringify(usage)},"modelVersion":"fixture-gemini","responseId":"${responseId}"}\n\n`);
 response.writeHead(200,{'Content-Type':'text/event-stream'});for(let offset=0;offset<wire.length;offset+=7)response.write(wire.subarray(offset,offset+7));response.end();
}
function observedResults(content){
 assert.equal(content.role,'user');assert.equal(content.parts.length,2);
 for(const [index,path,text] of [[0,'README.md',firstFile],[1,'second.txt',secondFile]]){
  const result=content.parts[index].functionResponse;assert.equal(result.name,'read_file');if(index===0)assert.equal(result.id,'provider-identified-call');else assert.equal(Object.hasOwn(result,'id'),false,'Absent provider IDs must remain absent in results');assert.deepEqual(Object.keys(result.response),['output']);assert.equal(typeof result.response.output,'string');assert.deepEqual(JSON.parse(result.response.output),{path,content:text});
 }
}
const peer=createServer((request,response)=>{const buffers=[];let bytes=0;request.on('data',chunk=>{bytes+=chunk.length;if(bytes>512*1024){request.destroy();return;}buffers.push(chunk);});request.on('end',()=>{try{
 routes.push(request.url);assert.equal(request.method,'POST');assert.ok(request.url.endsWith(suffix));assert.equal(request.headers['x-goog-api-key'],key);assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers.accept,'text/event-stream');assert.equal(request.headers['content-type'],'application/json');
 const source=Buffer.concat(buffers).toString('utf8');assert.ok(!source.includes(key));assert.ok(!source.includes('provider_context'));assert.ok(!source.includes('provider_items'));assert.ok(!source.includes('gm_'),'Native call IDs must not become provider IDs');const body=JSON.parse(source),route=request.url.slice(0,-suffix.length);
 assert.deepEqual(Object.keys(body).sort(),['contents','generationConfig','systemInstruction','tools']);assert.deepEqual(body.generationConfig,{maxOutputTokens:64});assert.equal(body.systemInstruction.parts.length,1);assert.ok(body.systemInstruction.parts[0].text.startsWith('Synthetic socket contract:'));assert.deepEqual(body.tools[0].functionDeclarations.map(tool=>tool.name),['read_repository_instructions','read_file','list_files','search_files','list_skills','load_skill']);
 if(route==='/main'){
  ++mainRequests;
  if(mainRequests===1){assert.deepEqual(body.contents,[{role:'user',parts:[{text:'Read the actual fixture files'}]}]);reply(response,parts,'STOP',{promptTokenCount:11,candidatesTokenCount:4,totalTokenCount:47,cachedContentTokenCount:3,thoughtsTokenCount:2},'synthetic-native-tool-turn');return;}
  assert.ok(source.includes(preciseMetadata),'Raw signed metadata numeric tokens must survive native continuation and SQLite reload');assert.ok(source.includes(firstArguments),'Escaped original argument keys must survive native receipt replay');assert.deepEqual(body.contents[0],{role:'user',parts:[{text:'Read the actual fixture files'}]});assert.deepEqual(body.contents[1],{role:'model',parts:JSON.parse(parts)});observedResults(body.contents[2]);
  if(mainRequests===2){assert.equal(body.contents.length,3);reply(response,JSON.stringify(finalParts),'STOP',{promptTokenCount:19,candidatesTokenCount:6,cachedContentTokenCount:4,thoughtsTokenCount:5},'synthetic-native-final-turn');return;}
  assert.equal(mainRequests,3);assert.equal(body.contents.length,5);assert.deepEqual(body.contents[3],{role:'model',parts:finalParts});assert.deepEqual(body.contents[4],{role:'user',parts:[{text:'Continue after SQLite reopen'}]});reply(response,JSON.stringify([{text:reopenedAnswer}]),'STOP',{promptTokenCount:23,candidatesTokenCount:2},'synthetic-native-reopened-turn');return;
 }
 assert.deepEqual(body.contents,[{role:'user',parts:[{text:'Reject '+route.slice(1)}]}]);
 if(route==='/malformed'){reply(response,'[{"functionCall":{"name":"read_file","args":{"path":"README.md","path":"second.txt"}}}]','STOP',{promptTokenCount:1},'synthetic-malformed');return;}
 if(route==='/incomplete'){reply(response,'[{"functionCall":{"name":"read_file","args":{"path":"README.md"}}}]',undefined,{promptTokenCount:1},'synthetic-incomplete');return;}
 if(route==='/truncated'){reply(response,'[{"functionCall":{"name":"read_file","args":{"path":"README.md"}}}]','MAX_TOKENS',{promptTokenCount:1},'synthetic-truncated');return;}
 assert.equal(route,'/unknown');reply(response,'[{"functionCall":{"name":"unadvertised_function","args":{}}}]','STOP',{promptTokenCount:1},'synthetic-unoffered');
 }catch(error){failure=error;response.destroy();}});});
try{
 await mkdir(workspace);await writeFile(join(workspace,'README.md'),firstFile);await writeFile(join(workspace,'second.txt'),secondFile);await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));let result;
 try{result=await execute(executable,[join(root,'state.sqlite'),modules,stdlib,workspace,`http://127.0.0.1:${peer.address().port}`],{windowsHide:true,timeout:30000});}catch(error){if(failure)throw failure;throw error;}
 if(failure)throw failure;assert.equal(mainRequests,3);assert.deepEqual(routes,['/main','/main','/main','/malformed','/incomplete','/truncated','/unknown'].map(route=>route+suffix));assert.equal(await readFile(join(workspace,'README.md'),'utf8'),firstFile);assert.equal(await readFile(join(workspace,'second.txt'),'utf8'),secondFile);process.stdout.write(result.stdout);
}finally{
 peer.closeAllConnections();if(peer.listening)await new Promise(resolve=>peer.close(resolve));const resolvedRoot=resolve(root);assert.equal(dirname(resolvedRoot),resolve(tmpdir()));assert.ok(basename(resolvedRoot).startsWith('xmind-gemini-agent-'));await rm(resolvedRoot,{recursive:true,force:true});
}
