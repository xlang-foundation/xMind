// Actual managed xmind.exe main-process ASGI host and SDK workspace routing.
// A fixed read-only graph exercises persistence with zero model requests.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {randomBytes} from 'node:crypto';
import {createRequire} from 'node:module';
const execute=promisify(execFile),require=createRequire(import.meta.url);
const {BackendClient}=require('../../extensions/vscode/client.js');
const {verifyNativeWebRuntime}=require('../../extensions/vscode/native-runtime.js');
const [runtimeArg,assetsArg]=process.argv.slice(2),runtime=path.resolve(runtimeArg),sourceAssets=path.resolve(assetsArg);
const verified=await verifyNativeWebRuntime(runtime),exe=path.join(runtime,'xmind.exe');
const repository=path.resolve(import.meta.dirname,'../..');
const root=path.join(repository,'build/skill-state-native','ewc-'+randomBytes(4).toString('hex'));
const first=path.join(root,'a'),second=path.join(root,'b'),profiles=path.join(root,'p'),assets=path.join(root,'ui'),replacementAssets=path.join(root,'ui2'),graphs=path.join(root,'config/graphs.json');
await fs.mkdir(first,{recursive:true});await fs.mkdir(second);await fs.cp(sourceAssets,assets,{recursive:true});
const content='Native ASGI document: café 雪\n';for(const folder of [first,second])await fs.writeFile(path.join(folder,'note.txt'),content);
await fs.mkdir(path.dirname(graphs));await fs.writeFile(graphs,JSON.stringify({graphs:[{id:'fixture.read',spec:{nodes:[{id:'read',type:'tool',tool:'read_file',arguments:{path:'note.txt'}}]}}]}));
const env={...process.env};for(const key of Object.keys(env))if(key.startsWith('XMIND_UI_')||['XMIND_API_KEY','XMIND_AUTH_TOKEN','XMIND_VIEW_TOKEN'].includes(key))delete env[key];
const owners=new Map();let passed=false;
async function cli(args){const result=await execute(exe,args,{cwd:first,env,windowsHide:true,maxBuffer:65536});return JSON.parse(result.stdout);}
const launch=directory=>cli(['web','--workspace',first,'--profile-root',profiles,'--assets',directory,'--graphs-config',graphs,'--upgrade-backend']);
async function inspect(pid){return cli(['admin','inspect-owner-process',String(pid)]);}
async function remember(metadata){owners.set(metadata.process_id,await inspect(metadata.process_id));}
async function until(read,label){const end=Date.now()+15000;for(;;){try{const value=await read();if(value)return value;}catch(error){if(Date.now()>=end)throw error;}if(Date.now()>=end)throw new Error(label);await new Promise(resolve=>setTimeout(resolve,20));}}
async function retireOwned(record){
  // Hold the Windows process handle before checking birth/image and stopping.
  const ps="$ErrorActionPreference='Stop'; $owned=Get-Process -Id "+record.process_id+"; $held=$owned.SafeHandle; if($owned.StartTime.ToFileTimeUtc().ToString() -ne '"+record.process_birth+"'){throw 'Process birth differs'}; $image=$owned.Path; if(-not $image.StartsWith('"+profiles.replaceAll("'","''")+"',[System.StringComparison]::OrdinalIgnoreCase)){throw 'Image escaped fixture'}; $owned.Kill(); if(-not $owned.WaitForExit(10000)){throw 'Exit unresolved'}";
  await execute('powershell.exe',['-NoProfile','-NonInteractive','-Command',ps],{env,windowsHide:true});
  owners.delete(record.process_id);
}
try{
  const original=await launch(assets);await remember(original);const origin=new URL(original.url).origin;
  await until(async()=>{const response=await fetch(original.url);return response.status===200;},'ASGI readiness');
  const processObservation=JSON.parse((await execute('powershell.exe',['-NoProfile','-NonInteractive','-Command',`Get-CimInstance Win32_Process -Filter 'ProcessId=${original.process_id}' | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress`],{env,windowsHide:true})).stdout);
  assert.match(processObservation.CommandLine,/"serve"/);assert.match(processObservation.CommandLine,/--web-assets/);assert.match(processObservation.CommandLine,/--db/);
  let cookie='';const request=async(route,body,method)=>{
    const response=await fetch(origin+route,{method:method||(body===undefined?'GET':'POST'),headers:{'Sec-Fetch-Site':'same-origin',...(cookie?{Cookie:cookie}:{}),...(body!==undefined?{Origin:origin,'Content-Type':'application/json'}:{})},...(body!==undefined?{body:JSON.stringify(body)}:{})});
    const replacement=response.headers.get('set-cookie');if(replacement)cookie=replacement.split(';')[0];return response;
  };
  assert.equal((await request('/ui/session',{})).status,200);assert(cookie);
  const fetchImpl=async(url,options={})=>request(new URL(url).pathname+new URL(url).search,options.body===undefined?undefined:JSON.parse(options.body),options.method);
  const makeClient=()=>new BackendClient(origin,()=> 'fixture-only-unused-owner-token'.padEnd(64,'x'),fetchImpl);let client=makeClient();
  const session=await client.createSession('Native main-process restoration');const catalogue=await client.graphs(),definition=catalogue.graphs.find(item=>item.id==='fixture.read');assert(definition?.executable);
  const run=await client.graphRun(session.id,definition.id,definition.revision,'fixture.read');
  const graph=await until(async()=>{const value=await client.graph(run.id);return value.run.state==='completed'&&value;},'Read-only native graph completion');
  assert.equal(graph.checkpoint.nodes[0].output.content,content);
  const events=await client.graphEvents(run.id,0);assert(!JSON.stringify(events).includes('model_call.started'));
  const created=await request('/ui/workspaces/create',{root:second});assert.equal(created.status,200);
  const peer=await cli(['--workspace',second,'--profile-root',profiles,'profile-info']);await remember(peer);client=makeClient();
  assert.notEqual(peer.process_id,original.process_id);assert.equal((await client.workspace()).root,await fs.realpath(second));
  const catalogueBefore=await (await request('/ui/workspaces')).json();assert.equal(catalogueBefore.workspaces.length,2);
  const firstId=catalogueBefore.workspaces.find(item=>path.resolve(item.root)===path.resolve(first)).workspace_id;
  const secondId=catalogueBefore.workspaces.find(item=>path.resolve(item.root)===path.resolve(second)).workspace_id,cookieBeforeCatalogue=cookie;
  const [firstCatalogue,secondCatalogue]=await Promise.all([request('/ui/workspaces/sessions?workspace_id='+encodeURIComponent(firstId)),request('/ui/workspaces/sessions?workspace_id='+encodeURIComponent(secondId))]);
  assert.equal(firstCatalogue.status,200);assert.equal(secondCatalogue.status,200);assert.equal(cookie,cookieBeforeCatalogue,'Expanding workspaces must not replace the browser cookie');
  assert.equal((await firstCatalogue.json()).sessions[0].id,session.id);assert.deepEqual((await secondCatalogue.json()).sessions,[]);assert.equal((await (await request('/ui/workspaces')).json()).selected_workspace_id,secondId);
  assert.equal((await request('/ui/workspaces/sessions?workspace_id=windows-local-file-v1:missing')).status,404);
  assert.equal((await request('/ui/workspaces/select',{workspace_id:firstId})).status,200);
  client=makeClient();
  assert.deepEqual(await client.graph(run.id),graph);
  // Replacing the prebuilt asset directory is an explicit managed web update.
  await fs.cp(assets,replacementAssets,{recursive:true});const replacement=await launch(replacementAssets);await remember(replacement);
  assert.equal(replacement.url,original.url);assert.notEqual(replacement.process_id,original.process_id);
  const exited=await cli(['admin','observe-owner-exit',String(original.process_id),original.process_birth,'10000']);assert.equal(exited.exited,true);owners.delete(original.process_id);
  await until(async()=>{try{return (await request('/ui/session',{})).status===200;}catch{return false;}},'Automatic cookie recovery');
  client=makeClient();
  assert((await client.sessions()).some(item=>item.id===session.id&&item.title==='Native main-process restoration'));
  assert.deepEqual(await client.graph(run.id),graph);assert.equal((await (await request('/ui/workspaces')).json()).workspaces.length,2);
  assert.equal((await inspect(peer.process_id)).process_birth,owners.get(peer.process_id).process_birth,'The other workspace owner must survive the main-host handoff');
  const after=await verifyNativeWebRuntime(runtime);assert.equal(after.manifestSha256,verified.manifestSha256);
  console.log(JSON.stringify({passed:true,scope:'Actual main-process FastAPI, standard SDK workspace routing and managed handoff; zero-model graph; no rendered UI claim',url:replacement.url,initialOwner:original.process_id,replacementOwner:replacement.process_id,peerOwner:peer.process_id,session:session.id,root:run.id,paidInference:false,neverReplay:true}));passed=true;
}finally{
  if(passed){for(const record of [...owners.values()])await retireOwned(record);
    const resolved=await fs.realpath(root),parent=await fs.realpath(path.join(repository,'build/skill-state-native'));assert.equal(path.dirname(resolved),parent);assert(path.basename(resolved).startsWith('ewc-'));
    await execute('powershell.exe',['-NoProfile','-NonInteractive','-Command',"Remove-Item -LiteralPath '"+resolved.replaceAll("'","''")+"' -Recurse -Force"],{env,windowsHide:true});
  }else console.error('Native web fixture and exact owned identities preserved:',JSON.stringify({root,owners:[...owners.values()]}));
}
