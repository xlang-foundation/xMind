import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,symlink,link,rm,readFile,rename} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createHash} from 'node:crypto';
const execute=promisify(execFile),folder=await mkdtemp(join(tmpdir(),'xmind-workspace-'));
const root=join(folder,'workspace'),outside=join(folder,'workspace-other');
try {
  await Promise.all([mkdir(join(root,'sub'),{recursive:true}),mkdir(join(root,'.git'),{recursive:true}),mkdir(join(root,'many'),{recursive:true}),mkdir(join(outside,'sub'),{recursive:true})]);
  await Promise.all([
    writeFile(join(root,'README.txt'),'first\r\nalpha[.]needle 中\r\nlast\n'),
    writeFile(join(root,'edit.txt'),'original\n'),
    writeFile(join(root,'edit-copy.txt'),'original\n'),
    writeFile(join(root,'sub','inside.txt'),'alpha[.]needle nested\n'),
    writeFile(join(root,'.git','ignored.txt'),'alpha[.]needle excluded\n'),
    writeFile(join(root,'binary.bin'),Buffer.from([0,255,128])),
    writeFile(join(root,'too-large.txt'),'x'.repeat(1024*1024+1)),
    writeFile(join(root,'long.txt'),'alpha[.]needle '+'中'.repeat(3000)+'\n'),
    writeFile(join(outside,'secret.txt'),'outside marker\n')
  ]);
  // Windows junction creation does not need symlink privileges. Failure fails
  // the contract rather than silently skipping the boundary tests.
  await symlink(outside,join(root,'outside-link'),'junction');
  await link(join(outside,'secret.txt'),join(root,'hard-link.txt'));
  for(let i=0;i<1001;i++) await writeFile(join(root,'many',`${i}.txt`),'');
  const expectedHash=createHash('sha256').update(await readFile(join(root,'README.txt'))).digest('hex');
  const expectedAfterHash=createHash('sha256').update((await readFile(join(root,'README.txt'),'utf8')).replace('alpha[.]needle','beta-native')).digest('hex');
  const result=await execute(process.argv[2],[root,outside,expectedHash,expectedAfterHash],{windowsHide:true,timeout:20000});
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
