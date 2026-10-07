import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,symlink,link,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const execute=promisify(execFile),folder=await mkdtemp(join(tmpdir(),'xmind-workspace-'));
const root=join(folder,'workspace'),outside=join(folder,'workspace-other');
try {
  await Promise.all([mkdir(join(root,'sub'),{recursive:true}),mkdir(join(root,'.git'),{recursive:true}),mkdir(join(root,'many'),{recursive:true}),mkdir(join(outside,'sub'),{recursive:true})]);
  await Promise.all([
    writeFile(join(root,'README.txt'),'first\r\nalpha[.]needle 中\r\nlast\n'),
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
  const result=await execute(process.argv[2],[root,outside],{windowsHide:true,timeout:20000});
  assert.equal(await readFile(join(outside,'secret.txt'),'utf8'),'outside marker\n','Outside fixture must remain unchanged');
  process.stdout.write(result.stdout);
} finally {await rm(folder,{recursive:true,force:true});}
