import {mkdtempSync,mkdirSync,symlinkSync,rmSync} from 'node:fs';
import {join,resolve,sep} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
const [binary,node,fixture]=process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'xmind-native-process-')),workspace=join(root,'workspace'),outside=join(root,'outside');
try {
  mkdirSync(workspace);mkdirSync(outside);symlinkSync(outside,join(workspace,'junction'),'junction');
  const result=spawnSync(binary,[node,fixture,workspace],{encoding:'utf8',windowsHide:true,timeout:30000});
  process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
  if(result.error)throw result.error;if(result.status!==0)throw new Error('Native process adapter contract exit '+result.status);
} finally {
  const safe=resolve(root),base=resolve(tmpdir())+sep;
  if(!safe.startsWith(base)||!safe.slice(base.length).startsWith('xmind-native-process-'))throw new Error('Unsafe fixture cleanup target');
  rmSync(safe,{recursive:true,force:true});
}
