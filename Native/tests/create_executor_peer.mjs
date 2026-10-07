// Real Windows junction boundary for the native creation contract. No model.
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,symlink,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {spawnSync} from 'node:child_process';
const [executable,modules,stdlib]=process.argv.slice(2),folder=await mkdtemp(join(tmpdir(),'xmind-create-boundary-'));
try{
  const root=join(folder,'root'),outside=join(folder,'outside');await mkdir(root);await mkdir(outside);await symlink(outside,join(root,'outside-link'),'junction');
  const result=spawnSync(executable,[modules,stdlib,root,outside],{encoding:'utf8',windowsHide:true,timeout:30000});assert.equal(result.status,0,result.stdout+result.stderr);process.stdout.write(result.stdout);
}finally{assert.equal(dirname(resolve(folder)),resolve(tmpdir()));assert.ok(folder.includes('xmind-create-boundary-'));await rm(folder,{recursive:true,force:true});}
