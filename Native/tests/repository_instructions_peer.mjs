// Synthetic guidance; native reads operate on actual bounded filesystem fixtures.
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm,symlink,link} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const folder=await mkdtemp(join(tmpdir(),'xmind-guidance-')),root=join(folder,'workspace'),outside=join(folder,'outside');
function read(directory='.') {const result=spawnSync(process.argv[2],['--guidance',root,directory],{encoding:'utf8',windowsHide:true,timeout:5000});assert.ifError(result.error);return result;}
function snapshot(directory='.') {const result=read(directory);assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);}
try {
  await mkdir(join(root,'src','deep'),{recursive:true});await mkdir(join(root,'other'));await mkdir(outside);
  await writeFile(join(outside,'AGENTS.md'),'Outside fixture must never be read');
  assert.deepEqual(snapshot().sources,[],'Absent final guidance is distinct from an error');
  const rootText='Synthetic root guidance 🌍\n',nestedText='Synthetic src guidance\n';
  await writeFile(join(root,'AGENTS.md'),rootText);await writeFile(join(root,'src','AGENTS.md'),nestedText);await writeFile(join(root,'other','AGENTS.md'),'Sibling guidance');
  const actual=snapshot('src/deep');assert.deepEqual(actual.sources.map(x=>x.path),['AGENTS.md','src/AGENTS.md']);
  assert.deepEqual(actual.sources.map(x=>x.content),[rootText,nestedText]);assert.equal(actual.order,'parent_to_child');
  for(const source of actual.sources){assert.ok(source.file_id);assert.equal(source.workspace_id,actual.sources[0].workspace_id);assert.equal(source.content_sha256,createHash('sha256').update(source.content).digest('hex'));}
  assert.equal(snapshot('other').sources.at(-1).content,'Sibling guidance');
  await writeFile(join(root,'src','AGENTS.md'),'Changed fixture');assert.equal(snapshot('src').sources.at(-1).content,'Changed fixture','Each call must reread changed bytes');
  await rm(join(root,'src','AGENTS.md'));assert.deepEqual(snapshot('src').sources.map(x=>x.path),['AGENTS.md'],'Deletion must remove guidance');
  for(const path of ['../outside',outside,'src/../other','missing','src:stream'])assert.notEqual(read(path).status,0,'Unsafe/missing directory must fail: '+path);
  await symlink(outside,join(root,'linked'),'junction');assert.notEqual(read('linked').status,0,'Guidance must never traverse a junction');
  await link(join(outside,'AGENTS.md'),join(root,'src','AGENTS.md'));assert.notEqual(read('src').status,0,'Hard links must be rejected');await rm(join(root,'src','AGENTS.md'));
  for(const bytes of [Buffer.from([0,255]),Buffer.from([255]),'x'.repeat(16385)]){await writeFile(join(root,'src','AGENTS.md'),bytes);assert.notEqual(read('src').status,0,'Invalid/oversized guidance must fail');}
  await writeFile(join(root,'AGENTS.md'),'x'.repeat(16384));await writeFile(join(root,'src','AGENTS.md'),'y'.repeat(16384));assert.equal(snapshot('src').sources.length,2);
  await writeFile(join(root,'src','deep','AGENTS.md'),'z');assert.notEqual(read('src/deep').status,0,'Aggregate limit must be enforced');
  await rm(join(root,'src','deep','AGENTS.md'));await rm(join(root,'src','AGENTS.md'));await rm(join(root,'AGENTS.md'));await mkdir(join(root,'AGENTS.md'));assert.notEqual(read().status,0,'A directory named AGENTS.md is not absent guidance');
  process.stdout.write('Native repository guidance passed: actual scope, hashes, refresh, absence, boundaries and limits; no model called\n');
} finally {
  assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(basename(folder).startsWith('xmind-guidance-'));await rm(folder,{recursive:true,force:true});
}
