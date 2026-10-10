// Actual native domain/storage/admin/owner-lease fixtures; no graph inference.
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {spawn,spawnSync} from 'node:child_process';
import {randomBytes} from 'node:crypto';
const [binary,modules,stdlib,admin,server]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-graph-'));
const env={...process.env,XMIND_AUTH_TOKEN:randomBytes(32).toString('hex')};delete env.XMIND_API_KEY;
let child;
try {
  const actual=spawnSync(binary,[root,modules,stdlib],{encoding:'utf8',windowsHide:true,timeout:30000});assert.ifError(actual.error);assert.equal(actual.status,0,actual.stderr);process.stdout.write(actual.stdout);
  const agents=join(root,'agents.yaml'),config=join(root,'catalog.yaml'),db=join(root,'admin.sqlite');
  await writeFile(agents,'agents:\n  - id: reviewer\n    model_id: unconfigured-fixture-model\n    instructions: Review the fixture only.\n');
  const importedAgents=spawnSync(admin,['admin','--db',db,'--modules',modules,'--stdlib',stdlib,'import-agents',agents],{env,encoding:'utf8',windowsHide:true,timeout:10000});assert.ifError(importedAgents.error);assert.equal(importedAgents.status,0,importedAgents.stderr);assert.deepEqual(JSON.parse(importedAgents.stdout),{catalog_revision:1,agents:[{id:'reviewer',revision:1,model_id:'unconfigured-fixture-model'}],execution_available:false});
  await writeFile(config,'graphs:\n  - id: fixture\n    spec:\n      nodes:\n        - id: work\n          type: agent\n          agent_id: reviewer\n          prompt: Synthetic configuration, not graph execution\n');
  const imported=spawnSync(admin,['admin','--db',db,'--modules',modules,'--stdlib',stdlib,'import-graphs',config],{env,encoding:'utf8',windowsHide:true,timeout:10000});assert.ifError(imported.error);assert.equal(imported.status,0,imported.stderr);assert.deepEqual(JSON.parse(imported.stdout),{catalog_revision:1,graphs:[{id:'fixture',revision:1,node_count:1}],execution_available:false});
  child=spawn(server,['--db',db,'--modules',modules,'--stdlib',stdlib,'--port','0'],{env,windowsHide:true});let errors='';child.stderr.on('data',data=>errors+=data);
  await new Promise((resolve,reject)=>{let output='';const deadline=setTimeout(()=>reject(new Error('Native readiness deadline: '+errors)),10000);child.once('error',error=>{clearTimeout(deadline);reject(error);});child.once('exit',code=>{clearTimeout(deadline);reject(new Error('Native server exited '+code+': '+errors));});child.stdout.on('data',data=>{output+=data;if(/listening on http:\/\/127\.0\.0\.1:\d+/.test(output)){clearTimeout(deadline);resolve();}});});
  const busy=spawnSync(admin,['admin','--db',db,'--modules',modules,'--stdlib',stdlib,'import-graphs',config],{env,encoding:'utf8',windowsHide:true,timeout:5000});assert.ifError(busy.error);assert.equal(busy.status,1,'A running backend must prevent concurrent offline graph import');
  process.stdout.write('Actual native named-agent and graph catalog admin imports plus server ownership passed; execution_available remains false\n');
} finally {
  if(child && child.exitCode===null){const ended=new Promise(resolve=>child.once('exit',resolve));child.kill();await ended;}
  assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-graph-'));await rm(root,{recursive:true,force:true});
}
