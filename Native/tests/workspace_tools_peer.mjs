import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,symlink,link,rm,readFile,rename,open} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createHash} from 'node:crypto';
const execute=promisify(execFile),folder=await mkdtemp(join(tmpdir(),'xmind-workspace-'));
const root=join(folder,'workspace'),outside=join(folder,'workspace-other');
try {
  await Promise.all([mkdir(join(root,'sub','.CoNfIg'),{recursive:true}),mkdir(join(root,'.config'),{recursive:true}),mkdir(join(root,'.configurable'),{recursive:true}),mkdir(join(root,'.agentflow'),{recursive:true}),mkdir(join(root,'sub','.AgEnTfLoW'),{recursive:true}),mkdir(join(root,'.agentflow-guide'),{recursive:true}),mkdir(join(root,'.git'),{recursive:true}),mkdir(join(root,'many'),{recursive:true}),mkdir(join(outside,'sub'),{recursive:true})]);
  // Only disposable synthetic secret/guidance markers. No user configuration
  // is read, copied or passed to the native fixture.
  const privateRoot='api_key: SyntheticPrivateConfigValue-workspace-only\nalpha[.]needle private root\n';
  const privateNested='api_key: SyntheticPrivateConfigValue-workspace-only\nalpha[.]needle private nested\n';
  const privateState='owner_token: SyntheticBackendStateValue-workspace-only\nalpha[.]needle private state\n';
  await mkdir(join(root,'glob-src','nested'),{recursive:true});await mkdir(join(root,'.glob-hidden'),{recursive:true});
  const globDepth=Array(33).fill('d');await mkdir(join(root,'glob-depth',...globDepth),{recursive:true});
  const ignored=join(root,'ignore-corpus');
  for(const dir of ['.git/info','build','nested','children','blocked','items'])await mkdir(join(ignored,dir),{recursive:true});
  await writeFile(join(ignored,'.gitignore'),'# owned Git-style corpus\n/build/\n/root-only.cpp\n*.tmp\n!keep.tmp\nnested/item?.cpp\ntrailing.cpp   \n\\#literal.cpp\n\\!literal.cpp\nliteral{x}.cpp\nitems/[[:digit:]].cpp\nchildren/**\n!children/keep.cpp\nblocked/\n!blocked/inside.cpp\n');
  await writeFile(join(ignored,'.ignore'),'!build/\n!root-only.cpp\n*.ignore.cpp\n');
  await writeFile(join(ignored,'.rgignore'),'priority.cpp\n');
  await writeFile(join(ignored,'.git','info','exclude'),'info-*.cpp\n');
  await writeFile(join(ignored,'nested','.gitignore'),'!item1.cpp\nlocal.hpp\n');
  const ignoreNames=['main.cpp','root-only.cpp','drop.tmp','keep.tmp','trailing.cpp','#literal.cpp','!literal.cpp','literal{x}.cpp','info-drop.cpp','priority.cpp','drop.ignore.cpp','build/built.cpp','nested/item1.cpp','nested/item2.cpp','nested/local.hpp','children/keep.cpp','children/drop.cpp','blocked/inside.cpp','items/1.cpp','items/a.cpp'];
  for(const file of ignoreNames)await writeFile(join(ignored,file),'OwnedIgnoreNeedle '+file+'\n');
  await mkdir(join(root,'unsafe-ignore'));await writeFile(join(root,'unsafe-ignore','visible.cpp'),'');
  await mkdir(join(root,'search-fixtures'));
  await mkdir(join(root,'search-byte-budget'));
  const budgetBlock=Buffer.alloc(33*1024*1024,0x61);
  budgetBlock[budgetBlock.length-1]=0xff;
  await writeFile(join(root,'search-byte-budget','a-invalid.txt'),budgetBlock);
  budgetBlock.fill(0x61);Buffer.from('ByteBudgetNeedle\n').copy(budgetBlock,budgetBlock.length-17);
  await writeFile(join(root,'search-byte-budget','b-large.txt'),budgetBlock);
  await writeFile(join(root,'search-byte-budget','c-small.txt'),'ByteBudgetNeedle\n');
  await Promise.all([
    writeFile(join(root,'search-fixtures','.ignore'),'ignored.txt\n'),
    writeFile(join(root,'search-fixtures','main.txt'),'NativeRegex item12\r\nNativeRegex itemXYZ\n\u00c5NGSTR\u00d6M\n'),
    writeFile(join(root,'search-fixtures','ignored.txt'),'NativeRegex ignored\n'),
    writeFile(join(root,'search-fixtures','.hidden.txt'),'NativeRegex hidden\n'),
    writeFile(join(root,'search-fixtures','budget.txt'),('NativeRegex\t'+'\t'.repeat(2000)+'\n').repeat(100)),
    writeFile(join(root,'search-fixtures','pathological.txt'),'a'.repeat(8*1024*1024)+'!\n'),
    writeFile(join(root,'search-fixtures','invalid-tail.txt'),Buffer.concat([Buffer.from('NativeRegex must not leak\n'),Buffer.from([0xff])])),
  ]);
  await Promise.all([
    writeFile(join(root,'README.txt'),'first\r\nalpha[.]needle 中\r\nlast\n'),
    writeFile(join(root,'edit.txt'),'original\n'),
    writeFile(join(root,'edit-copy.txt'),'original\n'),
    writeFile(join(root,'sub','inside.txt'),'alpha[.]needle nested\n'),
    writeFile(join(root,'.config','providers.yaml'),privateRoot),
    writeFile(join(root,'.config','AGENTS.md'),'SyntheticPrivateConfigValue-workspace-only private guidance\n'),
    writeFile(join(root,'sub','.CoNfIg','nested.yaml'),privateNested),
    writeFile(join(root,'sub','.CoNfIg','AGENTS.md'),'SyntheticPrivateConfigValue-workspace-only nested private guidance\n'),
    writeFile(join(root,'.configurable','visible.txt'),'Ordinary configuration documentation\n'),
    writeFile(join(root,'.agentflow','owner.token'),privateState),
    writeFile(join(root,'.agentflow','AGENTS.md'),'SyntheticBackendStateValue-workspace-only private state guidance\n'),
    writeFile(join(root,'sub','.AgEnTfLoW','owner.token'),privateState),
    writeFile(join(root,'sub','.AgEnTfLoW','AGENTS.md'),'SyntheticBackendStateValue-workspace-only nested state guidance\n'),
    writeFile(join(root,'.agentflow-guide','visible.txt'),'Ordinary agent documentation\n'),
    writeFile(join(root,'.git','ignored.txt'),'alpha[.]needle excluded\n'),
    writeFile(join(root,'binary.bin'),Buffer.from([0,255,128])),
    writeFile(join(root,'too-large.txt'),'x'.repeat(1024*1024+1)),
    writeFile(join(root,'long.txt'),'alpha[.]needle '+'中'.repeat(3000)+'\n'),
    writeFile(join(root,'range-large.txt'),Array.from({length:100000},(_,i)=>`line ${i+1} 中\r\n`).join('')),
    writeFile(join(root,'range-empty.txt'),''),
    writeFile(join(root,'range-ending.txt'),'\n\r\nlast'),
    writeFile(join(root,'range-boundary.txt'),'x'.repeat(8191)+'中\n😀end'),
    writeFile(join(root,'range-budget.txt'),('\t'.repeat(2000)+'\n').repeat(100)),
    writeFile(join(root,'range-astral.txt'),'😀'.repeat(2001)+'\n'),
    ...[[0xc0,0xaf],[0xed,0xa0,0x80],[0xf4,0x90,0x80,0x80],[0xe4,0xb8],[0x61,0x00,0x62]].map((bytes,i)=>writeFile(join(root,`range-invalid-${i}.txt`),Buffer.from(bytes))),
    writeFile(join(root,'range-invalid-skipped.txt'),Buffer.from([0xff,10,0x61,10])),
    writeFile(join(root,'range-invalid-clipped.txt'),Buffer.concat([Buffer.from('x'.repeat(3000)),Buffer.from([0xff,10])])),
    writeFile(join(root,'glob-src','main.cpp'),'Glob fixture: no native reader needs these contents\n'),
    writeFile(join(root,'glob-src','main.hpp'),''),
    writeFile(join(root,'glob-src','nested','item1.cpp'),''),
    writeFile(join(root,'glob-src','nested','item2.hpp'),''),
    writeFile(join(root,'glob-src','nested','中文😀.cpp'),''),
    writeFile(join(root,'.glob-hidden','hidden.cpp'),''),
    writeFile(join(root,'windows-hidden.cpp'),''),
    writeFile(join(root,'glob-depth',...globDepth.slice(0,32),'edge.deep'),''),
    writeFile(join(root,'glob-depth',...globDepth,'beyond.deep'),''),
    writeFile(join(outside,'secret.txt'),'outside marker\n')
  ]);
  const oversize=await open(join(root,'range-oversize.txt'),'wx');
  try{await oversize.truncate(64*1024*1024+1);}finally{await oversize.close();}
  // Windows junction creation does not need symlink privileges. Failure fails
  // the contract rather than silently skipping the boundary tests.
  await symlink(outside,join(root,'outside-link'),'junction');
  await symlink(join(root,'sub'),join(root,'inside-link'),'junction');
  await symlink(join(root,'.config'),join(root,'config-alias'),'junction');
  await symlink(join(root,'.agentflow'),join(root,'state-alias'),'junction');
  await link(join(outside,'secret.txt'),join(root,'hard-link.txt'));
  await link(join(outside,'secret.txt'),join(root,'glob-src','hard.cpp'));
  await link(join(outside,'secret.txt'),join(root,'unsafe-ignore','.ignore'));
  await symlink(outside,join(root,'glob-src','link'),'junction');
  for(let i=0;i<1001;i++) await writeFile(join(root,'many',`${i}.txt`),'');
  const expectedHash=createHash('sha256').update(await readFile(join(root,'README.txt'))).digest('hex');
  const expectedAfterHash=createHash('sha256').update((await readFile(join(root,'README.txt'),'utf8')).replace('alpha[.]needle','beta-native')).digest('hex');
  const result=await execute(process.argv[2],[root,outside,expectedHash,expectedAfterHash],{windowsHide:true,timeout:20000});
  assert.equal(await readFile(join(root,'.config','providers.yaml'),'utf8'),privateRoot,'Rejected native access/edit attempts must preserve synthetic private configuration');
  assert.equal(await readFile(join(root,'sub','.CoNfIg','nested.yaml'),'utf8'),privateNested,'Nested private configuration must remain unchanged');
  assert.equal(await readFile(join(root,'.agentflow','owner.token'),'utf8'),privateState,'Rejected native state reads/edits must preserve the synthetic owner-token fixture');
  assert.equal(await readFile(join(root,'sub','.AgEnTfLoW','owner.token'),'utf8'),privateState,'Nested private state must remain unchanged');
  await assert.rejects(readFile(join(root,'.agentflow','created.txt')),{code:'ENOENT'});
  const oldVersion=JSON.parse((await execute(process.argv[2],['--snapshot',root,'README.txt'],{windowsHide:true,timeout:5000})).stdout);
  await writeFile(join(root,'README.txt'),'Actual fixture changed after snapshot\n');
  const newVersion=JSON.parse((await execute(process.argv[2],['--snapshot',root,'README.txt'],{windowsHide:true,timeout:5000})).stdout);
  assert.equal(newVersion.file_id,oldVersion.file_id,'An in-place fixture update retains file identity');
  assert.equal(newVersion.workspace_id,oldVersion.workspace_id);
  assert.notEqual(newVersion.content_sha256,oldVersion.content_sha256,'Content modification must change the edit precondition');
  assert.equal(newVersion.content_sha256,createHash('sha256').update(await readFile(join(root,'README.txt'))).digest('hex'));
  const applied=JSON.parse((await execute(process.argv[2],['--apply',root],{windowsHide:true,timeout:5000})).stdout);
  assert.equal((await readFile(join(root,'edit.txt'))).length,0,'Native edit must really truncate the file');
  assert.equal(applied.edit_hash,createHash('sha256').update(await readFile(join(root,'edit.txt'))).digest('hex'));
  assert.equal(applied.raw_hash,createHash('sha256').update(await readFile(join(root,'binary.bin'))).digest('hex'));
  assert.equal(applied.raw_size,3);
  assert.equal(await readFile(join(outside,'secret.txt'),'utf8'),'outside marker\n','Outside fixture must remain unchanged');
  const before=await execute(process.argv[2],['--identity',root],{windowsHide:true,timeout:5000});
  const patchRoot=join(folder,'patch-workspace');await mkdir(join(patchRoot,'.config'),{recursive:true});
  const patchSources={'update-source.txt':'before\n','move-source.txt':'old move\n','delete-source.txt':'remove me\n','.config/providers.yaml':'synthetic-private-config\n'};
  await Promise.all(Object.entries(patchSources).map(([name,text])=>writeFile(join(patchRoot,name),text)));
  await Promise.all(Array.from({length:5},(_,index)=>writeFile(join(patchRoot,'large'+index+'.txt'),'x'.repeat(900000))));
  await symlink(outside,join(patchRoot,'outside-link'),'junction');await symlink(patchRoot,join(patchRoot,'inside-link'),'junction');await link(join(outside,'secret.txt'),join(patchRoot,'hard-link.txt'));
  const patchPlan=JSON.parse((await execute(process.argv[2],['--patch-plan',patchRoot],{windowsHide:true,timeout:10000})).stdout);
  assert.equal(patchPlan.files,4);assert.equal(patchPlan.planning_only,true);
  for(const [name,text] of Object.entries(patchSources))assert.equal(await readFile(join(patchRoot,name),'utf8'),text,'Patch preparation/rejections must have no effects');
  for(const [field,name] of [['updated_before_sha256','update-source.txt'],['moved_before_sha256','move-source.txt'],['removed_before_sha256','delete-source.txt']])assert.equal(patchPlan[field],createHash('sha256').update(patchSources[name]).digest('hex'));
  await assert.rejects(readFile(join(patchRoot,'new-parent','deep','added.txt')),{code:'ENOENT'});await assert.rejects(readFile(join(patchRoot,'move-parent','moved.txt')),{code:'ENOENT'});
  const moved=join(folder,'moved-workspace');
  // Both directory-move targets are checked against this task's exact temp
  // fixture parent before moving the tree. No user workspace is moved.
  assert.equal(dirname(resolve(root)),resolve(folder));
  assert.equal(dirname(resolve(moved)),resolve(folder));
  await rename(root,moved);
  const after=await execute(process.argv[2],['--identity',moved],{windowsHide:true,timeout:5000});
  assert.equal(after.stdout,before.stdout,'A directory move/reopen must retain its local policy identity');
  process.stdout.write(result.stdout);
} finally {await rm(folder,{recursive:true,force:true});}
