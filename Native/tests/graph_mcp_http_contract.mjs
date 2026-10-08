// Actual administrator/server/CLI/view adapter, xlang3 SQLite and MCP file
// effects. The independent MCP protocol peer and its metadata are synthetic.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {execFile,spawn} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomBytes} from 'node:crypto';
const {BackendClient}=createRequire(import.meta.url)('../../extensions/vscode/client.js');
const [serverExe,cliExe,adminExe,modules,stdlib]=process.argv.slice(2),execute=promisify(execFile);
const root=await mkdtemp(join(tmpdir(),'xmind-graph-mcp-http-')),workspace=join(root,'work'),database=join(root,'state.sqlite');
const effect=join(workspace,'effect.txt'),marker=join(root,'effect.marker'),config=join(root,'mcp.json'),graphs=join(root,'graphs.json');
const body='Actual approved native graph MCP bytes\n',token=randomBytes(32).toString('hex'),env={...process.env,XMIND_AUTH_TOKEN:token};
const peer=fileURLToPath(new URL('./mcp_effect_peer.mjs',import.meta.url));
let child,client,port;
const setting={id:'graph-external',transport:'stdio',enabled:true,executable:process.execPath,working_directory:workspace,arguments:[peer,'modern',effect,marker],credentials:[]};
async function admin(...args){const output=await execute(adminExe,['--db',database,'--modules',modules,'--stdlib',stdlib,...args],{env,windowsHide:true,timeout:35000});return JSON.parse(output.stdout);}
async function cli(...args){const output=await execute(cliExe,[String(port),...args],{env,windowsHide:true,timeout:12000});return JSON.parse(output.stdout);}
async function start(){
 child=spawn(serverExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'--port','0','--workspace',workspace,'--workers','1'],{env,windowsHide:true});
 let out='',err='';child.stderr.on('data',bytes=>err+=bytes);
 port=await new Promise((yes,no)=>{const timer=setTimeout(()=>no(new Error('Graph MCP server readiness timed out')),10000);child.once('error',error=>{clearTimeout(timer);no(error);});child.once('exit',code=>{clearTimeout(timer);no(new Error(`Graph MCP server exited ${code}: ${err}`));});child.stdout.on('data',bytes=>{out+=bytes;const match=/listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(out);if(match){clearTimeout(timer);yes(Number(match[1]));}});});
 client=new BackendClient(`http://127.0.0.1:${port}`,()=>token);
}
async function stop(){if(!child||child.exitCode!==null)return;const finished=new Promise(yes=>child.once('exit',yes));child.kill();await finished;child=undefined;}
async function wait(action,predicate,label){const end=Date.now()+10000;for(;;){const value=await action();if(predicate(value))return value;if(Date.now()>=end)throw new Error(label+' timed out');await new Promise(yes=>setTimeout(yes,10));}}
async function state(id,wanted){return wait(()=>client.graph(id),value=>value.run.state===wanted,'Graph state '+wanted);}
async function pending(id){return wait(async()=>{for(const branch of await client.graphChildren(id)){const operation=(await client.operations(branch.id)).find(value=>value.state==='awaiting_approval');if(operation)return operation;}},Boolean,'Actual graph MCP proposal');}
async function absent(path){await assert.rejects(readFile(path),error=>error.code==='ENOENT');}
async function submit(){const session=await client.createSession('Actual native MCP graph contract');return {session,run:await client.graphRun(session.id,'mcp.read',1,'Run the registered external tool graph')};}
async function human(id){const paused=await state(id,'paused');return client.graphInput(id,'human','{"accepted":true}',paused.checkpoint_revision);}
try {
 await mkdir(workspace);await writeFile(config,JSON.stringify({servers:[setting]}));
 const imported=await admin('import-mcp',config);assert.deepEqual(imported.servers,[{id:setting.id,revision:1,enabled:true}]);
 await assert.rejects(admin('discover-mcp','missing-server',workspace));
 const catalogue=await admin('discover-mcp',setting.id,workspace);assert.equal(catalogue.server_id,setting.id);assert.equal(catalogue.config_revision,1);assert.equal(catalogue.protocol_version,'2026-07-28');assert.equal(catalogue.tool_dispatch_performed,false);assert.equal(catalogue.tools.length,1);
 const tool=catalogue.tools[0];assert.match(tool.alias,/^mcp_[a-f0-9]{48}$/);assert.ok(JSON.parse(tool.input_schema_json).required.includes('decimal'));assert.ok(tool.description.includes('fixture.write'));
 assert.ok(!JSON.stringify(catalogue).includes(process.execPath));await absent(effect);await absent(marker);
 // Configured private credentials must not leak through public peer metadata.
 const sourceEnv='GRAPH_MCP_PRIVATE_FIXTURE_SOURCE',privateKey='synthetic-mcp-credential-fixture';env[sourceEnv]=privateKey;
 const reflected=['reflect-credential','reflect-schema-credential'].map((mode,index)=>({...setting,id:'reflection-'+index,arguments:[peer,mode,effect,marker,'credential-fixture'],credentials:[{name:'MCP_TEST_KEY',scope:'server',id:'reflection-key-'+index}]}));
 await writeFile(config,JSON.stringify({servers:[setting,...reflected]}));await admin('import-mcp',config);
 for(const server of reflected){await admin('put-mcp-credential',server.id,'MCP_TEST_KEY',sourceEnv);await assert.rejects(admin('discover-mcp',server.id,workspace),error=>{assert.ok(!String(error.stdout||'').includes(privateKey));assert.ok(!String(error.stderr||'').includes(privateKey));assert.match(String(error.stderr||''),/MCP tool discovery did not complete/);return true;});}
 delete env[sourceEnv];await writeFile(config,JSON.stringify({servers:[setting]}));await admin('import-mcp',config);await absent(effect);await absent(marker);
 const spec={nodes:[{id:'human',type:'human',prompt:'Authorize continuation of the registered graph'},{id:'external',type:'tool',tool:tool.alias,mcp:{server_id:setting.id,config_revision:1},depends_on:['human'],arguments_json:'{"body":'+JSON.stringify(body)+',"decimal":1.00000000000000000001}'},{id:'read',type:'tool',tool:'read_file',depends_on:['external'],arguments:{path:'effect.txt'}}]};
 await writeFile(graphs,JSON.stringify({graphs:[{id:'mcp.read',spec}]}));assert.equal((await admin('import-graphs',graphs)).graphs[0].revision,1);
 for(const declaration of [[],['--model-tools','unknown'],['--model-tools','unsupported']])await assert.rejects(execute(serverExe,['--db',database,'--modules',modules,'--stdlib',stdlib,'--port','0','--workspace',workspace,'--model','synthetic-unavailable-model','--model-endpoint','http://127.0.0.1:1/chat',...declaration],{env,windowsHide:true,timeout:10000}),error=>{assert.match(String(error.stderr||''),/Workspace model execution requires --model-tools supported/);return true;});
 await start();assert.deepEqual((await client.models()).models,[]);assert.ok((await client.graphs()).graphs.find(value=>value.id==='mcp.read').executable);
 // An offline inspection must not acquire a competing database/backend owner.
 await assert.rejects(admin('discover-mcp',setting.id,workspace));
 const allowed=await submit();await state(allowed.run.id,'paused');await absent(effect);
 await stop();await start();await state(allowed.run.id,'paused');await human(allowed.run.id);
 const proposal=await pending(allowed.run.id),args=JSON.parse(proposal.arguments_json);
 assert.equal(proposal.tool,'mcp_tool');assert.equal(args.server_config_id,setting.id);assert.equal(args.config_revision,1);assert.equal(args.alias,tool.alias);assert.equal(args.peer_tool,'fixture.write');assert.ok(args.arguments_json.includes('1.00000000000000000001'));assert.equal(proposal.state,'awaiting_approval');await absent(effect);
 await cli('decide',proposal.id,'allow');const completed=await state(allowed.run.id,'completed');assert.equal(await readFile(effect,'utf8'),body);
 assert.equal(completed.checkpoint.nodes.find(value=>value.id==='read').output.content,body);
 assert.equal((await client.operation(proposal.id)).state,'succeeded');
 const branches=await cli('graph-children',allowed.run.id);assert.equal(branches.length,2);
 const external=branches.find(value=>value.node_id==='external');const history=await client.graphChildHistory(allowed.run.id,external.id);const answer=history.at(-1).data;
 assert.equal(answer.source,'graph_tool');assert.equal(answer.acknowledged_by_peer,true);assert.equal(answer.independently_verified,false);assert.equal(answer.operation_id,proposal.id);assert.equal(JSON.parse(answer.response_json).result.structuredContent.bytes,Buffer.byteLength(body));
 const events=await client.graphEvents(allowed.run.id,0);assert.ok(events.some(value=>value.run_id===external.id&&value.kind==='mcp.discovered'));assert.ok(events.some(value=>value.kind==='operation.succeeded'));
 const cursor=events.find(value=>value.run_id===external.id&&value.kind==='mcp.discovered').seq;assert.deepEqual(await client.graphEvents(allowed.run.id,cursor),events.filter(value=>value.seq>cursor));
 assert.equal((await client.history(allowed.session.id)).at(-1).data.source,'graph_join');
 const denied=await submit();await human(denied.run.id);const deniedProposal=await pending(denied.run.id);await cli('decide',deniedProposal.id,'deny');await state(denied.run.id,'failed');assert.equal((await client.operation(deniedProposal.id)).state,'denied');assert.equal(await readFile(effect,'utf8'),body);
 const cancelled=await submit();await human(cancelled.run.id);const cancelledProposal=await pending(cancelled.run.id);await cli('cancel',cancelled.run.id);await state(cancelled.run.id,'cancelled');assert.equal((await client.operation(cancelledProposal.id)).state,'cancelled');assert.equal(await readFile(effect,'utf8'),body);
 // A stale paused graph survives server restart for inspection/cancellation;
 // human input must fail before changing its durable checkpoint or scheduling.
 const stale=await submit(),paused=await state(stale.run.id,'paused');await stop();
 await writeFile(config,JSON.stringify({servers:[{...setting,enabled:false}]}));assert.equal((await admin('import-mcp',config)).servers[0].revision,2);await assert.rejects(admin('discover-mcp',setting.id,workspace));
 await start();assert.equal((await client.graphs()).graphs.find(value=>value.id==='mcp.read').executable,false);
 const restored=await client.graph(stale.run.id);assert.equal(restored.run.state,'paused');assert.equal(restored.checkpoint_revision,paused.checkpoint_revision);
 await assert.rejects(client.graphInput(stale.run.id,'human','{"accepted":true}',restored.checkpoint_revision),error=>error.status===503);
 assert.equal((await client.graph(stale.run.id)).checkpoint_revision,paused.checkpoint_revision);assert.deepEqual(await client.graphChildren(stale.run.id),[]);
 await cli('cancel',stale.run.id);await state(stale.run.id,'cancelled');
 assert.equal((await client.graph(allowed.run.id)).run.state,'completed');assert.equal((await client.operation(proposal.id)).state,'succeeded');assert.equal(await readFile(effect,'utf8'),body);
 console.log('Native graph MCP HTTP/CLI passed offline alias discovery, no-model graph admission, paused SQLite reopen, exact decimal proposal/wire, controller-approved actual external write/dependent read, view history/events/cursor replay, denial/cancellation without tool dispatch, and stale connector inspection/input rejection/cancellation. MCP protocol replies are synthetic; no live model or rendered IDE acceptance.');
} finally {await stop();assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-graph-mcp-http-'));await rm(root,{recursive:true,force:true});}
