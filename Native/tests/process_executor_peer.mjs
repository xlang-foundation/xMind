import {mkdtempSync,mkdirSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {resolve,join,sep} from 'node:path';
import {spawnSync} from 'node:child_process';
const [binary,node,fixture,modules,stdlib]=process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'xmind-approved-process-')),workspace=join(root,'workspace');
try {
  mkdirSync(workspace);
  const result=spawnSync(binary,[node,fixture,workspace,modules,stdlib],{encoding:'utf8',windowsHide:true,timeout:45000});
  process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
  if(result.error)throw result.error;if(result.status!==0)throw new Error('Actual approved native process contract exit '+result.status);
} finally {
  const safe=resolve(root),base=resolve(tmpdir())+sep;
  if(!safe.startsWith(base)||!safe.slice(base.length).startsWith('xmind-approved-process-'))throw new Error('Unsafe fixture cleanup target');
  rmSync(safe,{recursive:true,force:true});
}
