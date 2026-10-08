// Independent synthetic Gemini catalogue/model socket peer. The native child
// owns authenticated enrollment, execution, real reads and xlang3 SQLite.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,basename,resolve} from 'node:path';

const [executable,modules,stdlib]=process.argv.slice(2);
assert.ok(executable&&modules&&stdlib,'Pass native profile contract executable and embedded-xlang3 roots');
const execute=promisify(execFile),root=await mkdtemp(join(tmpdir(),'xmind-gemini-profile-')),workspace=join(root,'workspace');
const key='synthetic-gemini-profile-key-not-live',secondKey='synthetic-gemini-profile-second-key-not-live',raceKey='synthetic-gemini-profile-race-key-not-live';
const fileText='Actual enrolled native file bytes: "quoted" and Unicode 雪\n';
const finalAnswer='Synthetic Gemini peer checked the enrolled native file read.',reopenedAnswer='Synthetic Gemini peer accepted the reopened enrolled conversation.';
const cursor='opaque/cursor+?=&雪',encodedCursor=encodeURIComponent(cursor);
const callParts=[{functionCall:{id:'provider-profile-read',name:'read_file',args:{path:'README.md'}},thoughtSignature:'cHJvZmlsZS1jYWxsLXNpZ25hdHVyZQ=='}];
const finalParts=[{text:finalAnswer},{thoughtSignature:'cHJvZmlsZS1maW5hbC1zaWduYXR1cmU='}];
let child,failure,stalledDiscovery,discoveryReleased=false,heldRequestClosed=false;const catalogues=[],modelRequests=[];
function catalogue(response,next){
 response.writeHead(200,{'Content-Type':'application/json'});
 response.end(JSON.stringify(next?{models:[{name:'models/fixture-gemini',supportedGenerationMethods:['generateContent'],displayName:'Untrusted display label',baseModelId:'different-base-id'}]}:{models:[{name:'models/fixture-gemini-second',supportedGenerationMethods:['countTokens','generateContent']},{name:'models/fixture-unknown-tools',supportedGenerationMethods:['generateContent']},{name:'models/fixture-embedding',supportedGenerationMethods:['embedContent']}],nextPageToken:cursor}));
}
function reply(response,parts,input,output,responseId){
 const data={candidates:[{index:0,content:{role:'model',parts},finishReason:'STOP'}],usageMetadata:{promptTokenCount:input,candidatesTokenCount:output},modelVersion:'fixture-gemini',responseId};
 const wire=Buffer.from(`data: ${JSON.stringify(data)}\n\n`);response.writeHead(200,{'Content-Type':'text/event-stream'});
 for(let offset=0;offset<wire.length;offset+=11)response.write(wire.subarray(offset,offset+11));response.end();
}
function results(content){
 assert.equal(content.role,'user');assert.equal(content.parts.length,1);const result=content.parts[0].functionResponse;
 assert.equal(result.name,'read_file');assert.equal(result.id,'provider-profile-read');assert.deepEqual(Object.keys(result.response),['output']);
 assert.deepEqual(JSON.parse(result.response.output),{path:'README.md',content:fileText},'Only actual native file bytes may satisfy the synthetic peer');
}
const peer=createServer((request,response)=>{
 const chunks=[];let size=0;request.on('data',chunk=>{size+=chunk.length;if(size>512*1024){request.destroy();return;}chunks.push(chunk);});
 request.on('end',()=>{try{
  const source=Buffer.concat(chunks).toString('utf8'),url=new URL(request.url,'http://127.0.0.1');
  assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['anthropic-version'],undefined);
  assert.ok([key,secondKey,raceKey].includes(request.headers['x-goog-api-key']));
  assert.ok(!source.includes(key)&&!source.includes(secondKey)&&!source.includes(raceKey));
  if(request.method==='GET'){
   assert.equal(source,'');assert.equal(url.pathname,'/v1beta/models');assert.equal(url.searchParams.get('pageSize'),'1000');
   const next=url.searchParams.get('pageToken');assert.ok(next===null||next===cursor);assert.deepEqual([...url.searchParams.keys()],next===null?['pageSize']:['pageSize','pageToken']);
   if(next!==null)assert.equal(request.url,`/v1beta/models?pageSize=1000&pageToken=${encodedCursor}`,'Opaque cursor must be encoded, retained and scoped to the native endpoint');
   else assert.equal(request.url,'/v1beta/models?pageSize=1000');
   const usedKey=request.headers['x-goog-api-key'];catalogues.push({key:usedKey,next:next!==null});
   if(usedKey===raceKey&&next===null){assert.equal(stalledDiscovery,undefined);stalledDiscovery=response;child.stdin.write('fixture-discovery-started\n');return;}
   assert.equal(usedKey===raceKey?discoveryReleased:true,true);catalogue(response,next!==null);return;
  }
  assert.equal(request.method,'POST');assert.equal(request.headers['x-goog-api-key'],key);assert.equal(request.headers.accept,'text/event-stream');assert.equal(request.headers['content-type'],'application/json');
  assert.ok(!source.includes('provider_context')&&!source.includes('provider_items')&&!source.includes('gm_'),'Only wire-native conversation values may reach Gemini');
  const body=JSON.parse(source);assert.deepEqual(body.generationConfig,{maxOutputTokens:64});assert.equal(body.systemInstruction.parts.length,1);assert.ok(body.systemInstruction.parts[0].text.startsWith('Synthetic Gemini enrollment contract:'));
  modelRequests.push(request.url);const prompt=body.contents.at(-1).parts?.[0]?.text;
  if(url.pathname==='/v1beta/models/fixture-unknown-tools:streamGenerateContent'){
   assert.equal(request.url,'/v1beta/models/fixture-unknown-tools:streamGenerateContent?alt=sse');assert.equal(prompt,'Text-only enrolled model');
   assert.equal(Object.hasOwn(body,'tools'),false,'Unknown tool capability must produce a text-only request with no tool declarations');
   assert.deepEqual(body.contents,[{role:'user',parts:[{text:prompt}]}]);reply(response,[{text:'Synthetic text-only enrolled Gemini reply.'}],2,3,'synthetic-profile-text-only');return;
  }
  assert.equal(request.url,'/v1beta/models/fixture-gemini:streamGenerateContent?alt=sse');
  assert.deepEqual(Object.keys(body).sort(),['contents','generationConfig','systemInstruction','tools']);
  assert.deepEqual(body.tools[0].functionDeclarations.map(tool=>tool.name),['read_repository_instructions','read_file','list_files','search_files','plan_tasks','revise_plan','inspect_plan']);
  if(prompt==='Read the enrolled fixture file'){
   assert.deepEqual(body.contents,[{role:'user',parts:[{text:prompt}]}]);reply(response,callParts,7,2,'synthetic-profile-read');return;
  }
  if(prompt==='Cancel enrolled native run'){
   assert.deepEqual(body.contents,[{role:'user',parts:[{text:prompt}]}]);response.once('close',()=>{heldRequestClosed=true;});child.stdin.write('fixture-run-started\n');return;
  }
  assert.deepEqual(body.contents[0],{role:'user',parts:[{text:'Read the enrolled fixture file'}]});assert.deepEqual(body.contents[1],{role:'model',parts:callParts});results(body.contents[2]);
  if(body.contents.length===3){reply(response,finalParts,13,4,'synthetic-profile-completed');return;}
  assert.equal(body.contents.length,5);assert.deepEqual(body.contents[3],{role:'model',parts:finalParts});assert.deepEqual(body.contents[4],{role:'user',parts:[{text:'Continue enrolled run after reopen'}]});
  reply(response,[{text:reopenedAnswer}],17,3,'synthetic-profile-reopened');
 }catch(error){failure=error;response.destroy();}});
});
try{
 await mkdir(workspace);await writeFile(join(workspace,'README.md'),fileText);await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));
 const running=execute(executable,[root,modules,stdlib,workspace,`http://127.0.0.1:${peer.address().port}`],{windowsHide:true,timeout:40000,maxBuffer:1024*1024});child=running.child;
 let stdout='';child.stdout.on('data',chunk=>{
  stdout+=chunk;if(!discoveryReleased&&stdout.includes('fixture-discovery-committed')){
   try{assert.ok(stalledDiscovery,'Native revision acknowledgement must follow the independently observed socket request');discoveryReleased=true;catalogue(stalledDiscovery,false);}catch(error){failure=error;stalledDiscovery?.destroy();}
  }
 });
 let result;try{result=await running;}catch(error){if(failure)throw failure;throw error;}if(failure)throw failure;
 assert.equal(discoveryReleased,true);assert.equal(heldRequestClosed,true,'Actual native cancellation must close the held provider socket');
 assert.deepEqual(catalogues,[...Array.from({length:5},()=>[{key,next:false},{key,next:true}]).flat(),{key:raceKey,next:false},{key:raceKey,next:true}]);
 assert.deepEqual(modelRequests,[...Array(4).fill('/v1beta/models/fixture-gemini:streamGenerateContent?alt=sse'),'/v1beta/models/fixture-unknown-tools:streamGenerateContent?alt=sse']);
 assert.equal(await readFile(join(workspace,'README.md'),'utf8'),fileText,'The real read tool must retain the original file bytes');
 for(const entry of await readdir(root,{withFileTypes:true}))if(entry.isFile()&&/\.sqlite(?:-wal|-shm)?$/.test(entry.name)){
  // This hostile-input fixture deliberately writes a reflected credential into
  // the registry directly to verify that reopening rejects public metadata.
  // It is not evidence about the enrollment path's encrypted secret storage.
  if(entry.name.startsWith('invalid-saved-resource.sqlite'))continue;
  const stored=await readFile(join(root,entry.name));for(const secret of [key,secondKey,raceKey])assert.equal(stored.includes(Buffer.from(secret)),false,'Provider keys must not persist as plaintext SQLite bytes');
 }
 process.stdout.write(result.stdout);
}finally{
 peer.closeAllConnections();if(peer.listening)await new Promise(resolve=>peer.close(resolve));
 const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('xmind-gemini-profile-'));await rm(target,{recursive:true,force:true});
}
