// Actual native C++/embedded-xlang3 catalogue, CLI and authenticated browser
// routing. Guide content/endpoint/adapter assets are synthetic test fixtures;
// no inference, script execution or rendered view acceptance is claimed.
import assert from 'node:assert/strict';
import {spawn,execFile} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {randomBytes} from 'node:crypto';
import {request as rawRequest} from 'node:http';
import {createRequire} from 'node:module';
import {createBrowserServer} from '../../views/browser/server.mjs';
const require=createRequire(import.meta.url),{BackendClient}=require('../../extensions/vscode/client.js'),execute=promisify(execFile);
const [server,cli,modules,stdlib]=process.argv.slice(2),parent=await fs.realpath(tmpdir()),root=await fs.mkdtemp(path.join(parent,'xmind-skill-catalogue-')),workspace=path.join(root,'workspace'),skills=path.join(workspace,'.agents/skills'),assets=path.join(root,'assets');
await fs.mkdir(skills,{recursive:true});await fs.mkdir(assets);
for(const name of ['index.html','browser.js','browser.css','chat.js','chat.css','client.js','marked.js','purify.js'])await fs.writeFile(path.join(assets,name),'Synthetic routing fixture; not rendered UI.\n',{flag:'wx'});
const explicit='---\nname: Explicit guide\ndescription: Synthetic catalogue metadata\nautoinvoke: false\n---\nSynthetic body must not appear in a catalogue.\n',disabled='---\nname: Disabled guide\ndisable-model-invocation: true\n---\nSynthetic disabled body must not be invoked.\n';
await fs.mkdir(path.join(skills,'explicit'));await fs.writeFile(path.join(skills,'explicit/SKILL.md'),explicit,{flag:'wx'});await fs.writeFile(path.join(skills,'disabled.md'),disabled,{flag:'wx'});
const token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};delete env.XMIND_API_KEY;
let child,exitPromise,exited,view,activeCookie;
async function stop(){if(view){await view.close();view=undefined;}if(child&&!exited){child.kill();let timer;try{await Promise.race([exitPromise,new Promise((_,no)=>{timer=setTimeout(()=>no(Error('Owned native catalogue fixture still running')),10000);})]);}finally{clearTimeout(timer);}assert.ok(exited);}}
async function start(mode,extra){exited=false;const log=await fs.open(path.join(root,mode+'-server.log'),'wx');child=spawn(server,['--db',path.join(root,mode+'.sqlite'),'--modules',modules,'--stdlib',stdlib,'--workspace',workspace,'--port','0',...extra],{env,windowsHide:true,stdio:['ignore','pipe',log.fd]});await log.close();exitPromise=new Promise((yes,no)=>{child.once('error',no);child.once('exit',(code,signal)=>{exited=true;yes({code,signal});});});
 return new Promise((yes,no)=>{let output='';const timer=setTimeout(()=>no(Error('Native catalogue fixture startup deadline')),15000);child.stdout.on('data',bytes=>{output+=bytes;if(output.length>65536){clearTimeout(timer);no(Error('Unexpected native fixture output'));return;}const match=/listening on (http:\/\/127\.0\.0\.1:\d+)/.exec(output);if(match){clearTimeout(timer);yes(match[1]);}});child.once('error',e=>{clearTimeout(timer);no(e);});child.once('exit',()=>{clearTimeout(timer);no(Error('Native catalogue fixture exited before readiness'));});});
}
try{
 for(const [mode,extra]of [['profile',[]],['platform',['--model','synthetic-catalogue-model','--model-endpoint','http://127.0.0.1:1/chat/completions','--model-tools','supported']]]){
  const origin=await start(mode,extra),headers={Authorization:'Bearer '+token},client=new BackendClient(origin,()=>token),binding=await client.workspace();assert.equal(binding.configured,true);
  assert.equal((await fetch(origin+'/v1/workspace/skills')).status,401);
  const catalogue=await client.skills();assert.equal(catalogue.workspace_id,binding.workspace_id);assert.equal(catalogue.authority_id,binding.authority_id);assert.equal(catalogue.skills.length,2);
  assert.equal(catalogue.skills.find(s=>s.id==='explicit').autoinvoke,false);assert.equal(catalogue.skills.find(s=>s.id==='disabled').model_invocable,false);assert.ok(!JSON.stringify(catalogue).includes('Synthetic body'));assert.ok(!JSON.stringify(catalogue).includes('Synthetic disabled body'));assert.ok(catalogue.skills.every(s=>!Object.hasOwn(s,'content')&&!Object.hasOwn(s,'body')));
  // fetch normalizes away an empty trailing query. Send this exact request
  // target through raw HTTP so the backend, not the URL serializer, is tested.
  const emptyQueryStatus=await new Promise((yes,no)=>{const request=rawRequest({hostname:'127.0.0.1',port:new URL(origin).port,path:'/v1/workspace/skills?',headers},response=>{response.resume();response.once('end',()=>yes(response.statusCode));});request.once('error',no);request.end();});assert.equal(emptyQueryStatus,400);
  for(const suffix of ['?id=disabled','?path=../.config/providers.yaml'])assert.equal((await fetch(origin+'/v1/workspace/skills'+suffix,{headers})).status,400);
  const nativeCli=await execute(cli,[new URL(origin).port,'skills'],{env,windowsHide:true,timeout:15000});assert.deepEqual(JSON.parse(nativeCli.stdout),catalogue);
  const interactive=await new Promise((yes,no)=>{const process=execFile(cli,[new URL(origin).port,'chat'],{env,windowsHide:true,timeout:15000},(error,stdout)=>error?no(error):yes(stdout));process.stdin.end('/skills\n/exit\n');});
  const chatRecords=interactive.trim().split(/\r?\n/).map(line=>JSON.parse(line));assert.equal(chatRecords.length,1);assert.equal(chatRecords[0].type,'workspace_skills');assert.deepEqual(chatRecords[0].catalogue,catalogue);
  assert.deepEqual(await client.sessions(),[],'Catalogue inspection and console /skills must not admit a session');
  view=await createBrowserServer({backend:origin,assetRoot:assets});const viewOrigin=await view.listen(),viewHeaders={Origin:viewOrigin,'Content-Type':'application/json','Sec-Fetch-Site':'same-origin'};
  const enrolled=await fetch(viewOrigin+'/ui/session',{method:'POST',headers:{...viewHeaders,Authorization:'Bearer '+token},body:'{}'});assert.equal(enrolled.status,200);activeCookie=enrolled.headers.get('set-cookie').split(';')[0];
  const observed=await fetch(viewOrigin+'/v1/workspace/skills',{headers:{Cookie:activeCookie,'Sec-Fetch-Site':'same-origin'}});assert.equal(observed.status,200);assert.deepEqual(await observed.json(),catalogue);
  assert.equal((await fetch(viewOrigin+'/v1/workspace/skills',{method:'POST',headers:{...viewHeaders,Cookie:activeCookie},body:'{}'})).status,404);
  assert.equal((await fetch(viewOrigin+'/v1/workspace/skills?path=other',{headers:{Cookie:activeCookie,'Sec-Fetch-Site':'same-origin'}})).status,400);
  await fs.writeFile(path.join(skills,'explicit/SKILL.md'),explicit.replace('Explicit guide','Current guide'));
  assert.equal((await client.skills()).skills.find(s=>s.id==='explicit').name,'Current guide');
  await fs.writeFile(path.join(skills,'explicit.md'),explicit,{flag:'wx'});assert.equal((await fetch(origin+'/v1/workspace/skills',{headers})).status,409,'Ambiguous current sources cannot yield a partial catalogue');await fs.unlink(path.join(skills,'explicit.md'));
  await fs.writeFile(path.join(skills,'malformed.md'),'---\nname: [not, scalar]\n---\nSynthetic invalid guide',{flag:'wx'});assert.equal((await fetch(origin+'/v1/workspace/skills',{headers})).status,409);await fs.unlink(path.join(skills,'malformed.md'));
  assert.deepEqual(await client.sessions(),[],'Scoped catalogue inspection must leave sessions empty');
  await fs.writeFile(path.join(skills,'explicit/SKILL.md'),explicit);await stop();
 }
 // No configured root must fail explicitly rather than choosing a directory.
 exited=false;const noRoot=spawn(server,['--db',path.join(root,'unbound.sqlite'),'--modules',modules,'--stdlib',stdlib,'--port','0'],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});child=noRoot;exitPromise=new Promise((yes,no)=>{noRoot.once('error',no);noRoot.once('exit',()=>{exited=true;yes();});});
 const origin=await new Promise((yes,no)=>{let output='';const timer=setTimeout(()=>no(Error('Unbound native startup deadline')),15000);noRoot.stdout.on('data',b=>{output+=b;const m=/listening on (http:\/\/127\.0\.0\.1:\d+)/.exec(output);if(m){clearTimeout(timer);yes(m[1]);}});noRoot.once('error',e=>{clearTimeout(timer);no(e);});});assert.equal((await fetch(origin+'/v1/workspace/skills',{headers:{Authorization:'Bearer '+token}})).status,503);await stop();
 console.log('Native skill catalogue passed actual configured/unconfigured-provider roots, exact workspace authority, native CLI and real scoped browser cookies, fresh source discovery, value-free metadata, read-only routes, malformed/ambiguous rejection and absent-root failure. Guides/assets/endpoint are synthetic; no inference or rendered UI claimed.');
}finally{await stop();const actual=await fs.realpath(root);assert.equal(actual,root);assert.equal(path.dirname(actual),parent);assert.ok(path.basename(actual).startsWith('xmind-skill-catalogue-'));await fs.rm(actual,{recursive:true});}
